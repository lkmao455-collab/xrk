# 工具说明：Web 控制台无头冒烟测试 (web_console_smoke.py)

`tools/web_console_smoke.py` 是「打开 Web 控制台」功能的**无头自动化冒烟测试**。它验证 Web 控制台两条可稳定实测的链路（详见 `docs/08_desktop_render_checklist.md` 第 5 节）：

1. **HTTP 托管 SPA**：网关在 `wsPort+1` 提供 `index.html` 与哈希资源（js/css/svg）。
2. **WS↔Host 桥接 + AUTH 握手**：浏览器客户端经 `ws://localhost:wsPort` 连回本机 Host，发送 `AUTH_REQ` 应收到 `AUTH_RESP`。
3. **帧捕获探测（信息项）**：发 `SCREENSHOT_REQ`，若 Host 返回 JPEG 帧则记录分辨率并存盘；无头会话下 `captureFrame()` 可能返回 null（不回包），此时记为 `SKIP`（环境限制，非失败）。

> 本工具在实测中**发现并推动了网关分片缺陷的修复**（见第 6 节）：原先大报文（截图/实时帧）会被 TCP 分片成多个 WS 二进制消息中继，导致浏览器 SPA 解出的帧残缺。修复后网关按完整 XRK 报文重组后再发送。本工具客户端侧也做了重组，以应对任何残余分片。

---

## 1. 前置条件

- Python 3（仓库内 `web-client` 自带 Node，但本工具用 Python）。
- `websocket-client`：`pip install websocket-client`（WS 握手/帧探测需要；未安装时 WS 检查记为 `SKIP`）。
- 被测 `xrk.exe`（Release 构建产物 `build/src/Release/xrk.exe`）。
- 若用 `--xrk` 自动拉起 Host/网关，需保证 **Qt 运行时在 PATH**（否则 `xrk.exe` 因缺 DLL 启动失败）：
  ```bash
  export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"
  ```

---

## 2. 用法

### 2.1 Host 与网关已在运行（最常见）
```bash
python3 tools/web_console_smoke.py
# 默认 wsPort=8080, hostPort=9999（即网关 --ws 8080 桥接 127.0.0.1:9999）
```

### 2.2 由本工具自动拉起并清理
适合 CI / 一键验证。工具会 `Popen` 启动 `xrk.exe --host <port>` 与 `xrk.exe --ws <port>`，测试后 `terminate`/`kill` 清理：
```bash
python3 tools/web_console_smoke.py --xrk build/src/Release/xrk.exe
```

### 2.3 自定义端口 / 抓取一帧存档
```bash
python3 tools/web_console_smoke.py --ws 8080 --host 9999 --save-frame /tmp/cap.jpg
```
`--save-frame` 在捕获到帧时把 JPEG 写入该路径，可供人工目检「渲染出的桌面」。

### 2.4 全部参数
| 参数 | 默认 | 说明 |
|------|------|------|
| `--ws` | 8080 | 网关 WebSocket 端口（HTTP 页面在 `wsPort+1`） |
| `--host` | 9999 | Host TCP 端口 |
| `--xrk` | 无 | `xrk.exe` 路径；提供则自动拉起/清理 Host 与网关 |
| `--timeout` | 10 | 单步超时（秒） |
| `--save-frame` | 无 | 将捕获到的帧存为此路径的 JPEG |

---

## 3. 输出与判定

示例（真实运行，含一次成功捕获）：
```
=== [1] HTTP 托管 SPA (Web 控制台页面) ===
  [OK] / -> 200 text/html; charset=utf-8 (505 B)
  [OK] /assets/index-ChLrSEVe.js -> 200 application/javascript (208662 B)
  [OK] /assets/index-FtecoFfX.css -> 200 text/css (8973 B)
  [OK] /favicon.svg -> 200 image/svg+xml (9522 B)
=== [2] WS<->Host 桥接 + AUTH 握手 ===
  [OK] recv AUTH_RESP magic=0x58524B00 ver=1 len=50
=== [3] 帧捕获探测 (信息项, 无头环境可能 SKIP) ===
  [OK] 捕获到帧 1920x1080 fmt=0 (250433 B) -> /tmp/xrk_smoke_cap.jpg
---
HTTP 托管 SPA : PASS
WS AUTH 握手  : PASS
帧捕获探测    : PASS
RESULT: PASS (Web 控制台数据通道可用; 真实画面渲染需在桌面会话目检, 见 docs/08_desktop_render_checklist.md)
```

- **HTTP 与 WS AUTH** 为必需项：`PASS` 表示 Web 控制台数据通道可用。
- **帧捕获** 为信息项：`PASS` = 收到并保存了一帧；`SKIP` = 无头会话未产出帧（换真实桌面会话即可稳定 `PASS`）；二者都不影响 `RESULT`（除非前两项失败）。
- **退出码**：`0`=HTTP 与 AUTH 均通过；`1`=任一必需项失败；`2`=参数/环境错误（如 `--xrk` 路径不存在）。

---

## 4. 线格式说明（与 `src/core/message_codec.cpp` 一致）

客户端按以下格式构造/解析 XRK 报文（BigEndian）：
```
[ magic(4)=0x58524B00 ][ version(4)=1 ][ type(4) ][ length(4) ][ timestamp(8) ]
[ sessionIdLen(4) ][ sessionId ][ payload ][ checksum(4) ]
```
- `AUTH_REQ`=13，`AUTH_RESP`=14，`SCREENSHOT_REQ`=90，`SCREENSHOT_RESP`=91。
- `SCREENSHOT_RESP` 的 payload 为 `ScreenFrame`：`width(4)+height(4)+format(4)+timestamp(8)+dataSize(4)+jpeg`，共 `24 + jpeg` 字节。
- 校验和：`checksum = (c<<1)+byte` 滚动累加，BigEndian uint32 附于末尾（与 `MessageCodec::calculateChecksum` 一致）。

工具对 WS 收到的字节做**报文重组**（`extract_messages`）：按头中的 `length` 切出完整报文，未到齐则缓存等待下一帧，避免大报文被 TCP 分片导致的误解析。

---

## 5. 排错

- `[FAIL] / -> ...`：网关未启动、端口不对，或 SPA 未部署到 `resources/web`（见 `WORK_NEXT.md` 部署项）。
- `[FAIL] 无法连接/发送 AUTH_REQ`：WS 端口被占用或 Host 未监听 `hostPort`。
- `[SKIP] 未安装 websocket-client`：`pip install websocket-client`。
- `[SKIP] 未收到帧`：无头会话 `captureFrame()` 返回 null，属预期；在真实桌面会话运行即可稳定拿到帧。

---

## 6. 关联修复：网关 TCP→WS 报文分片

本工具实测中暴露并修复了 `src/app/web_socket_gateway.cpp` 的 `onTcpReadyRead` 缺陷：
**原实现**每收到一次 `readyRead` 就把 `tcp->readAll()` 作为**一个** WS 二进制消息发出。Host 的大报文（截图 / 实时帧，常数百 KB）会被 TCP 拆成多个 chunk，于是被中继成多个残缺的 WS 消息，浏览器 SPA 解出的是不完整的帧 → 画面渲染失败。

**修复**：为每会话增加 `tcpBuffer`，按 XRK 报文头中的 `length` 重组出**完整报文**后再 `sendBinaryMessage`，并在头非法/损坏时按 `MAGIC` 重新同步。修复后，截图/帧以整报文送达，本工具得以稳定保存 1920×1080 的 JPEG 帧。

> 复测请见上方示例 `[3] 捕获到帧 1920x1080 ...`。该修复对「Web 控制台真实渲染桌面」是直接前置条件。
