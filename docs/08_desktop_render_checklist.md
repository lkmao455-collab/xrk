# 桌面渲染实测验证清单

本文档是**真实桌面环境下**验证「远程桌面画面能否被正确采集、编码、传输并在控制端/浏览器渲染」的手动 QA 清单。它覆盖两条渲染路径：

- **场景一：原生 GUI 控制器**（`RemoteDesktopWidget` 渲染）
- **场景二：Web 控制台**（浏览器 SPA + `WebSocketGateway` 桥接渲染，见 `WORK_NEXT.md`「XRK 集成」）

> 配套文档：输入正确性见 `tests/KEYBOARD_TESTS.md`、鼠标注入见 `tests/MOUSE_TESTS.md`、Web 控制台部署/集成见 `WORK_NEXT.md`、黑屏根因决策树见记忆 `xrk_black_screen.md`。

---

## 0. 适用范围与前提

本清单**必须在有显示器的交互式桌面会话中执行**。无头/CI 会话下 DXGI/GDI 桌面捕获不可稳定复现（见第 5 节「无头可自动化部分」与 `xrk_black_screen.md`），**不要**试图在无头环境里靠反复重试拿到渲染帧。

**最少环境**：
- 被控端（Host）：一台已登录、有显示器、处于交互式桌面会话的 Windows 机器。
- 控制端：可与被控端网络互通的另一台 Windows，或**同一台机器**（连 `127.0.0.1`，用于自测）。
- 浏览器（场景二需要）：Chrome / Edge，运行在与 Host 网络互通的环境（同机或局域网）。
- 构建产物：`build/src/Release/xrk.exe`（Release）。运行需 Qt 运行时在 PATH：
  ```bash
  export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"   # Linux/Git-Bash 下
  ```

---

## 1. 环境准备

- [ ] 被控端与控制端均使用同一份最新 `xrk.exe`（含 Web 控制台集成与跨布局键盘修复）。
- [ ] 被控端已登录并停留在桌面（非锁屏、非会话 0 服务态），确保 DXGI 桌面复制有内容可抓。
- [ ] 防火墙放行 Host 监听端口（默认 `9999`）与控制端连接。
- [ ] 场景二额外：确认被控端机器上浏览器可访问 `http://localhost:8081/` 与 `ws://localhost:8080`。

---

## 2. 场景一：原生 GUI 控制器渲染桌面

在被控端启动 Host，在控制端用原生 GUI 连接并验证画面渲染。

- [ ] 被控端：启动 `xrk.exe` → 点「启动服务」（或 `xrk.exe --host 9999`），状态栏显示「服务已启动 - 识别码: xxxxxxxx」。
- [ ] 控制端：启动 `xrk.exe` → 输入被控端 IP → 点「连接到 IP」。
- [ ] 连接建立：控制端状态栏显示「远程连接已建立」，且日志出现 `AUTH_RESP` 含会话密钥（加密密钥已协商）。
- [ ] **同意授权**：跨机首次连接时，被控端弹出同意请求；点「允许」或被自动同意（私有网段/已信任 IP 自动授权）。鼠标在同意后开始可动。
- [ ] **画面渲染**：`RemoteDesktopWidget` 出现被控桌面图像（非黑屏、非绿屏、非静止长图）。
- [ ] **画面刷新**：在被控端移动窗口/打开程序，控制端画面实时跟随变化（证明是连续帧而非单帧）。
- [ ] **鼠标注入**：在控制端移动/点击鼠标，被控端指针正确跟随（对应 `tests/MOUSE_TESTS.md`）。
- [ ] **键盘注入**：在控制端输入字符（含中文/非当前布局字符），被控端正确录入，不乱码（对应 `tests/KEYBOARD_TESTS.md`）。
- [ ] **质量/帧率**：调整画质或帧率后，分辨率与流畅度符合预期；长时间运行无内存暴涨、无卡死。
- [ ] **断开重连**：断开后再次连接，画面能恢复渲染。

---

## 3. 场景二：Web 控制台（浏览器）渲染桌面

验证本次「XRK 集成」交付的 Web 控制台端到端渲染。

- [ ] 被控端：启动 `xrk.exe` → 点「启动服务」（Host 监听 `9999`）。
- [ ] 主程序 → **工具菜单「打开 Web 控制台」**（或系统托盘右键菜单）：网关按需启动，`ws :8080` 桥接 `127.0.0.1:9999`，浏览器自动打开 `http://localhost:8081/`。
- [ ] **页面加载**：SPA 首页 `index.html` 与哈希资源（`/assets/index-*.js`、`/assets/index-*.css`、`/favicon.svg`）全部 200，无 404（即 `WORK_NEXT.md` 部署项已生效）。
- [ ] **WS 握手**：浏览器 `WebSocket` 连 `ws://localhost:8080`，AUTH 握手返回 `OK` + 会话密钥。
- [ ] **画布渲染**：浏览器 `<canvas>` 渲染被控桌面（SPA 路径：AES-256-CBC 解密 `AUTH_RESP` 下发的 key/iv → `decodeScreenFrame` 解析 `width/height/format/dataSize` → JPEG 解码 → canvas 绘制）。
- [ ] **输入生效**：在浏览器里移动鼠标/键入，被控端指针与录入正确响应。
- [ ] **关闭/停止**：关闭浏览器标签页、或停止 Host 服务，进程不崩溃、无残留监听端口泄漏。
- [ ] **端口冲突保护**：若 `8080` 已被占用（如另有独立 `--ws` 网关），点击「打开 Web 控制台」应弹窗提示而非崩溃（见 `main_window.cpp::onOpenWebConsole`）。

---

## 4. 关键观测点（什么叫「渲染正常」）

| 观测项 | 正常表现 | 异常信号 |
|--------|----------|----------|
| 首帧 | 连接后数秒内出现桌面图像 | 一直黑/绿/空白 |
| 连续性 | 被控端变化实时反映到控制端 | 静止不动 / 仅首帧 |
| 色彩/几何 | 颜色正常、比例正确、无撕裂 | 绿屏、花屏、拉伸变形 |
| 输入回声 | 鼠标/键盘操作在被控端即时生效 | 输入无反应、字符乱码 |
| 稳定性 | 长时间运行帧率平稳、内存稳定 | 卡顿、内存泄漏、崩溃 |
| 重连 | 断开重连后画面恢复 | 重连后持续黑屏 |

---

## 5. 无头/CI 环境下可自动化验证的部分

无显示器时**无法**验证真实画面渲染，但下列与渲染强相关的链路**可稳定实测**（已在沙箱验证通过）：

**(a) Web 控制台 HTTP 托管** —— 证明 SPA 页面与资源可被浏览器加载：
```bash
# 启动 Host + 网关（与「打开 Web 控制台」同源）
xrk.exe --host 9999 &  xrk.exe --ws 8080 &
curl -i http://localhost:8081/                # 期望 200 text/html
curl -i http://localhost:8081/assets/index-ChLrSEVe.js   # 期望 200 application/javascript
curl -i http://localhost:8081/favicon.svg
```

**(b) WS↔Host 桥接 + AUTH 握手** —— 证明浏览器 SPA 的数据通道通畅（用 Python `websocket-client`，按 `MAGIC=0x58524B00` BigEndian 头 + 滚动校验和发 `AUTH_REQ`=13，期望回 `AUTH_RESP`=14）：
```python
import struct, time, websocket
def checksum(d):
    c=0
    for b in d: c=(c<<1)+b & 0xFFFFFFFF
    return struct.pack('>I', c)
def encode(t, p=b""):
    s=b""; body=b"".join([struct.pack('>I',0x58524B00),struct.pack('>I',1),
        struct.pack('>I',t),struct.pack('>I',len(p)),struct.pack('>Q',int(time.time()*1000)),
        struct.pack('>I',len(s)),s,p,checksum(body)])
    return body
ws=websocket.create_connection("ws://127.0.0.1:8080",timeout=10)
ws.send(encode(13,b""),opcode=websocket.ABNF.OPCODE_BINARY)  # AUTH_REQ, 空密码
print(ws.recv()[:4])  # 期望前 4 字节为 0x58524B00（AUTH_RESP 头 magic）
ws.close()
```

**(c) 真机帧捕获（环境受限，仅供记录）** —— `SCREENSHOT_REQ`(90)→`SCREENSHOT_RESP`(91) 在无头会话下**多数返回 null 不回包**，仅在 DXGI 偶发有帧时成功；**真实桌面会话应稳定返回 JPEG**。脚本可对 `SCREENSHOT_RESP` 的 `ScreenFrame` 载荷（`width(4)+height(4)+format(4)+timestamp(8)+dataSize(4)+jpeg`）解出 JPEG 存档供人工查看。

> 判定：第 5 节 (a)(b) 通过只能说明「Web 控制台可加载、数据通道通」，**不能**替代第 2/3 节的真实渲染目检。

---

## 6. 判定标准

- **PASS**：第 2 节与第 3 节全部勾选项通过；画面实时、正确、可交互。
- **FAIL**：任一核心项未过（黑屏 / 无帧 / 画面不刷新 / 输入无回声 / 字符乱码）。按第 7 节决策树定位。

---

## 7. 黑屏 / 花屏排查决策树（对照已知根因）

**连上了但黑屏**（参考 `xrk_black_screen.md`）：
1. **加密密钥未交付**：`AUTH_RESP` 是否携带 key+iv？两端是否一致？Native 端曾因 off-by-one（`data.size() > 2+32+16`）漏初始化 `m_encryption` 导致从不解密 → 黑屏。
2. **编码器不匹配**：Host 默认 **JPEG**；若误走 H264 且控制端解码失败（如沙箱缺 `libx264`/MediaFoundation 报 `MF_E_NO_SAMPLE_TIMESTAMP`），会黑屏。控制端连续 30 帧 H264 失败会自动请求切回 JPEG。
3. **未授权 Consent**：未同意则 Host 不发包（此时鼠标也不会动）。确认被控端已「允许」。
4. **无头会话 captureFrame() 返回 null**：纯环境限制，非 Bug；换真实桌面会话。

**花屏 / 绿屏 / 变形**：
- 加密/解密不一致（密钥、IV、AES-CBC vs GCM）、`decodeScreenFrame` 字段偏移错误（`width/height/format/timestamp/dataSize` 顺序，SPA 曾因漏读 `timestamp` 把 `dataSize` 读偏）。
- 分辨率/纵横比换算错误导致拉伸。

**卡顿 / 延迟高**：帧率、画质、网络带宽；可在设置中降低画质或帧率验证改善。

---

## 8. 记录与归档（可选）

为留痕，可在**真实桌面会话**中用 `SCREENSHOT_REQ` 抓取一帧 JPEG 存档：
```python
# 复用第 5 节 encode()，认证后发 SCREENSHOT_REQ(90)，解析 SCREENSHOT_RESP(91) 的 ScreenFrame：
w,hh,fmt,ts,dsz = struct.unpack('>IIIII', payload[:24])
jpeg = payload[24:24+dsz]
open("desktop_capture.jpg","wb").write(jpeg)   # 人工打开确认画面
```
> 注意：此抓取在**真实桌面会话**稳定可用；在无头沙箱常因 `captureFrame()` 返回 null 而收不到包，属预期。
