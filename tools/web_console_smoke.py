#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XRK Web 控制台无头冒烟测试 (headless smoke test)

验证「打开 Web 控制台」所依赖的两条可稳定实测的链路：
  1) HTTP 托管 SPA：网关在 wsPort+1 提供 index.html 与哈希资源（js/css/svg）。
  2) WS<->Host 桥接 + AUTH 握手：浏览器客户端经 ws://localhost:wsPort 连回
     本机 Host，发送 AUTH_REQ 应收到 AUTH_RESP。

另含可选「帧捕获探测」：发 SCREENSHOT_REQ，若 Host 返回 JPEG 帧则记录分辨率并存盘；
若无头会话下 captureFrame() 返回 null（不回包），则记为 SKIP（环境限制，非失败）。

用法:
  # 假设 Host(--host 9999) 与网关(--ws 8080) 已在运行:
  python3 tools/web_console_smoke.py

  # 由本脚本自动拉起/清理 Host 与网关 (需提供 xrk.exe 路径, 且 Qt 运行时在 PATH):
  python3 tools/web_console_smoke.py --xrk build/src/Release/xrk.exe

  # 自定义端口 / 抓取一帧存档:
  python3 tools/web_console_smoke.py --ws 8080 --host 9999 --save-frame /tmp/cap.jpg

退出码: 0 = HTTP 与 AUTH 握手均通过; 1 = 任一必需检查失败; 2 = 参数/环境错误。
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import time
import urllib.request
import urllib.error

# ---- XRK 线格式常量 (与 src/core/message_codec.cpp 一致) ----
MAGIC = 0x58524B00
VERSION = 1
HEADER_FIXED = 24
AUTH_REQ = 13
AUTH_RESP = 14
SCREENSHOT_REQ = 90
SCREENSHOT_RESP = 91


def checksum(data: bytes) -> bytes:
    c = 0
    for b in data:
        c = (c << 1) + b
        c &= 0xFFFFFFFF
    return struct.pack(">I", c)


def encode(msg_type: int, payload: bytes = b"", session_id: str = "") -> bytes:
    sid = session_id.encode("utf-8")
    body = b""
    body += struct.pack(">I", MAGIC)
    body += struct.pack(">I", VERSION)
    body += struct.pack(">I", msg_type)
    body += struct.pack(">I", len(payload))
    body += struct.pack(">Q", int(time.time() * 1000))
    body += struct.pack(">I", len(sid))
    body += sid
    body += payload
    body += checksum(body)
    return body


def parse_header(data: bytes):
    if len(data) < HEADER_FIXED + 4:
        return None
    magic, ver, mtype, length, ts = struct.unpack(">IIIIQ", data[:24])
    sidlen = struct.unpack(">I", data[24:28])[0]
    return magic, ver, mtype, length, sidlen


def message_size(header):
    """完整 XRK 报文字节数 = 固定头 + sessionId 长度字段(4) + sid + payload + 校验和(4)。"""
    _, _, _, length, sidlen = header
    return HEADER_FIXED + 4 + sidlen + length + 4  # +CHECKSUM_SIZE


def extract_messages(buf):
    """从可能含 TCP 分片的字节流中切出若干完整 XRK 报文，返回 (messages, 剩余字节)。"""
    msgs = []
    while len(buf) >= HEADER_FIXED + 4:
        header = parse_header(buf)
        if header is None:
            break
        total = message_size(header)
        if len(buf) < total:
            break  # 报文尚未到齐（分片），等待更多数据
        data = buf[:total]
        _, _, _, length, sidlen = header
        payload = data[HEADER_FIXED + 4 + sidlen: HEADER_FIXED + 4 + sidlen + length]
        msgs.append((header, payload))
        buf = buf[total:]
    return msgs, buf


def fetch(url: str, timeout: int):
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.headers.get("Content-Type", ""), resp.read()


def http_check(base: str, path: str, timeout: int):
    try:
        status, ctype, body = fetch(base + path, timeout)
    except Exception as e:  # noqa: BLE001
        return False, f"{e}"
    ok = status == 200 and len(body) > 0
    detail = f"{status} {ctype} ({len(body)} B)"
    return ok, detail


def run_http(base: str, timeout: int):
    print("=== [1] HTTP 托管 SPA (Web 控制台页面) ===")
    # 从 index.html 动态发现哈希资源, 避免硬编码版本漂移
    results = []
    try:
        _, _, index_html = fetch(base + "/", timeout)
        assets = re.findall(r'(?:src|href)="(/assets/[^"]+)"', index_html.decode("utf-8", "ignore"))
    except Exception as e:  # noqa: BLE001
        assets = []
        print(f"  [FAIL] / -> {e}")
        results.append(False)
        assets = []
    paths = ["/"] + assets + ["/favicon.svg"]
    for p in paths:
        ok, detail = http_check(base, p, timeout)
        results.append(ok)
        mark = "OK" if ok else "FAIL"
        print(f"  [{mark}] {p} -> {detail}")
    passed = all(results)
    return passed


def run_ws_auth(ws_url: str, timeout: int):
    print("=== [2] WS<->Host 桥接 + AUTH 握手 ===")
    try:
        import websocket  # websocket-client
    except ImportError:
        print("  [SKIP] 未安装 websocket-client (pip install websocket-client)")
        return None
    try:
        ws = websocket.create_connection(ws_url, timeout=timeout)
        ws.sock.settimeout(timeout)
        ws.send(encode(AUTH_REQ, b""), opcode=websocket.ABNF.OPCODE_BINARY)
    except Exception as e:  # noqa: BLE001
        print(f"  [FAIL] 无法连接/发送 AUTH_REQ: {e}")
        return False
    buf = b""
    got = False
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = ws.recv()
        except Exception:  # noqa: BLE001
            break
        if not chunk:
            break
        buf += chunk
        msgs, buf = extract_messages(buf)
        for h, payload in msgs:
            magic, ver, mtype, length, sidlen = h
            valid = (magic == MAGIC) and (ver == VERSION)
            tag = {AUTH_RESP: "AUTH_RESP", SCREENSHOT_RESP: "SCREENSHOT_RESP"}.get(mtype, f"type={mtype}")
            print(f"  [{'OK' if valid else 'FAIL'}] recv {tag} magic=0x{magic:08X} ver={ver} len={length}")
            if mtype == AUTH_RESP:
                got = True
                break
        if got:
            break
    ws.close()
    return got


def run_capture_probe(ws_url: str, timeout: int, save_path: str):
    print("=== [3] 帧捕获探测 (信息项, 无头环境可能 SKIP) ===")
    try:
        import websocket
    except ImportError:
        print("  [SKIP] 未安装 websocket-client")
        return "SKIP"
    try:
        ws = websocket.create_connection(ws_url, timeout=timeout)
        ws.sock.settimeout(timeout)
        ws.send(encode(AUTH_REQ, b""), opcode=websocket.ABNF.OPCODE_BINARY)
        time.sleep(0.5)
        ws.send(encode(SCREENSHOT_REQ, b""), opcode=websocket.ABNF.OPCODE_BINARY)
    except Exception as e:  # noqa: BLE001
        print(f"  [SKIP] 无法连接/发送: {e}")
        return "SKIP"
    buf = b""
    result = "SKIP"
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = ws.recv()
        except Exception:  # noqa: BLE001
            break
        if not chunk:
            break
        buf += chunk
        msgs, buf = extract_messages(buf)
        for h, payload in msgs:
            magic, ver, mtype, length, sidlen = h
            if mtype != SCREENSHOT_RESP:
                continue
            if len(payload) < 24:
                print(f"  [WARN] SCREENSHOT_RESP 载荷过短 magic=0x{magic:08X} len={length} payload={len(payload)} B, 跳过")
                continue
            try:
                # ScreenFrame 线格式: width(4)+height(4)+format(4)+timestamp(8)+dataSize(4)=24
                w, hh, fmt, _ts, dsz = struct.unpack(">IIIQI", payload[:24])
            except struct.error as e:
                print(f"  [WARN] SCREENSHOT_RESP 解包失败 type={type(payload).__name__} "
                      f"len_field={length} payload_len={len(payload)} slice_len={len(payload[:24])}: {e}, 跳过")
                continue
            jpeg = payload[24:24 + dsz]
            if dsz > 0:
                if save_path:
                    with open(save_path, "wb") as f:
                        f.write(jpeg)
                    print(f"  [OK] 捕获到帧 {w}x{hh} fmt={fmt} ({len(jpeg)} B) -> {save_path}")
                else:
                    print(f"  [OK] 捕获到帧 {w}x{hh} fmt={fmt} ({len(jpeg)} B)")
                result = "PASS"
                break
        if result == "PASS":
            break
    ws.close()
    if result == "SKIP":
        print("  [SKIP] 未收到帧 (无头会话 captureFrame() 常返回 null, 真实桌面会话应稳定返回)")
    return result


def main():
    ap = argparse.ArgumentParser(description="XRK Web 控制台无头冒烟测试")
    ap.add_argument("--ws", type=int, default=8080, help="网关 WebSocket 端口 (默认 8080)")
    ap.add_argument("--host", type=int, default=9999, help="Host TCP 端口 (默认 9999)")
    ap.add_argument("--xrk", default=None, help="xrk.exe 路径; 提供则自动拉起/清理 Host 与网关")
    ap.add_argument("--timeout", type=int, default=10, help="单步超时(秒)")
    ap.add_argument("--save-frame", default=None, help="将捕获到的帧存为此路径的 JPEG")
    args = ap.parse_args()

    http_port = args.ws + 1
    base = f"http://127.0.0.1:{http_port}"
    ws_url = f"ws://127.0.0.1:{args.ws}"

    procs = []
    if args.xrk:
        if not os.path.exists(args.xrk):
            print(f"[ERROR] --xrk 指定的文件不存在: {args.xrk}")
            return 2
        print(f"(自动拉起 Host --host {args.host} 与网关 --ws {args.ws})")
        procs.append(subprocess.Popen([args.xrk, "--host", str(args.host)]))
        procs.append(subprocess.Popen([args.xrk, "--ws", str(args.ws)]))
        time.sleep(2.5)  # 等待监听就绪

    try:
        http_ok = run_http(base, args.timeout)
        auth = run_ws_auth(ws_url, args.timeout)
        capture = run_capture_probe(ws_url, args.timeout, args.save_frame)
    finally:
        for p in procs:
            try:
                p.terminate()
                time.sleep(0.5)
                if p.poll() is None:
                    p.kill()
            except Exception:  # noqa: BLE001
                pass

    # 判定: HTTP 与 AUTH 为必需项; 帧捕获为信息项
    auth_ok = (auth is True)
    required_ok = http_ok and auth_ok
    print("---")
    print(f"HTTP 托管 SPA : {'PASS' if http_ok else 'FAIL'}")
    print(f"WS AUTH 握手  : {'PASS' if auth_ok else ('FAIL' if auth is False else 'SKIP')}")
    print(f"帧捕获探测    : {capture}")
    if required_ok:
        print("RESULT: PASS (Web 控制台数据通道可用; 真实画面渲染需在桌面会话目检, 见 docs/08_desktop_render_checklist.md)")
        return 0
    print("RESULT: FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
