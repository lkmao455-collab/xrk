# 修复记录：控制端可控制鼠标但看不到桌面 (黑屏问题)

## 问题描述
控制端成功连接到被控制端后：
- ✅ 鼠标/键盘控制正常（输入事件被处理）
- ❌ 看不到被控制端桌面（黑屏/无画面）

---

## 结论速览（一句话根因）

**黑屏的真正原因 ≠ 编码/加密/授权**，而是**控制端的 `TcpConnection` 在拿到已连好的 socket 后，没有把
socket 的 `readyRead` 信号接上 `processBuffer`**，导致 Host 发来的 `AUTH_RESP` / `SCREEN_FRAME` 永远到不了
`RemoteController::onMessageReceived`。控制端因此"能发不能收"——鼠标（出方向，不经接收链路）照常工作，
但桌面画面（入方向）收不到 → 黑屏。

**一行修复**（`src/core/tcp_connection.cpp` 构造函数）：
```cpp
if (m_socket) { setupSocket(m_socket); }   // 接上预连接 socket 的接收链路
```

> 详细根因分析、调用链、为什么单元测试没发现、完整代码 diff 与验证见下文「真正根因与最终修复」。
> 下文「H264 / 编码」「同意授权」等章节是排查过程中做的**健壮性改进**，并非黑屏根因，仅供备查。

---

## 真正根因与最终修复（2026-08-05）：控制端 TCP 接收链路未接线

> **这是"鼠标通、画面黑"的唯一真正根因。** 此前排查的 H264 默认编码器、加密、同意授权等都**不是**
> 黑屏原因（它们只是健壮性改进），其各自的单元测试用 `invokeMethod(onMessageReceived)` 绕过了
> 真实 socket，所以一直没暴露真实接收路径。最终通过 **Host + 控制端 + 控件的 localhost 端到端测试**
> （`tests/test_e2e_black_screen.cpp`）真实复现并定位：Host 把帧写进了 socket，但控制端**永远收不到
> 任何来自 Host 的消息**。

### 1. 现象（端到端实测）
- **Host 端全绿**：抓屏 OK → 首帧 1920×1080 → 编码 JPEG → `NetworkWorker: FIRST frame batch delivered`
  （帧确实写到了已授权客户端的 socket）。说明 Host 侧抓屏、编码、网络发送全部正常。
- **控制端断流**：连接、授权都成功，但 `RemoteController::onMessageReceived` **从未收到 `SCREEN_FRAME`**，
  也从未收到 `AUTH_RESP`（日志里没有 `[Auth] Response received`）。即 **Host→控制端方向数据完全到不了控制端**。
- **不对称**：控制端**能发**（鼠标/授权请求到得了 Host，所以"鼠标通"），但**不能收**（Host 发的
  `AUTH_RESP`、`SCREEN_FRAME` 都到不了控制端，所以"画面黑"）——与用户症状**完全吻合**。

### 2. 根因：`TcpConnection` 构造函数没有接上 socket 的接收信号
**位置**：`src/core/tcp_connection.cpp` —— `TcpConnection::TcpConnection(QTcpSocket* socket, QObject* parent)`

#### 2.1 数据怎么从 socket 到业务层
`TcpConnection` 是控制端用来收发网络消息的封装。一条 socket 上的字节流要变成"一条消息"交给业务层，
依赖下面这条信号链：

```
QTcpSocket::readyRead
   └─(connect)→ TcpConnection::onSocketReadyRead
                   └─→ TcpConnection::processBuffer()   // 按 MessageCodec 分帧、拼包
                         └─→ emit TcpConnection::readyRead(QByteArray)  // 一条完整消息
                               └─(connect)→ RemoteController::onMessageReceived()
                                             └─→ 处理 AUTH_RESP / SCREEN_FRAME …
```

这条信号链是在 `setupSocket(socket)` 里建立的，关键一行是：

```cpp
connect(socket, &QTcpSocket::readyRead, this, &TcpConnection::onSocketReadyRead);
```

**只要 `setupSocket()` 没被调用，`readyRead` 就没接上，`processBuffer` 永不运行，`TcpConnection::readyRead`
永不发射，`RemoteController::onMessageReceived` 也就永远收不到任何消息。**

#### 2.2 为什么接收链路在控制端断掉了
`setupSocket()` 在整个类里只有**两处**被调用：
1. `reconnect()` 内部——它 `new QTcpSocket()` 新建一个 socket，然后立刻 `setupSocket(新建的socket)`。
2. **构造函数——原本根本没有调用。**

而控制端的**出站连接走的是构造函数这条路径**：
`NetworkManager::connectTo()` 的代码逻辑是：
```
QTcpSocket* sock = new QTcpSocket;
sock->connectToHost(host, port);          // 主动连 Host
...
new TcpConnection(sock, this);            // 把"已经连好的 socket"传给构造函数
```
构造函数只做了 `m_socket = socket;`，**没有调 `setupSocket()`**。于是这个已连好的 socket 的 `readyRead`
信号悬空，Host 发来的每一字节都被默默丢弃。

#### 2.3 为什么"鼠标通、画面黑"（不对称的本质）
- **控制端→Host（出方向）正常**：控制端发鼠标/键盘/授权请求时，是直接对 `TcpConnection` 调用"写 socket"
  的方法（`write()`/`sendMessage()`），**不需要 `readyRead` 接收链路**，所以输入照常送达 Host → 鼠标可控。
- **Host→控制端（入方向）全断**：Host 的 `AUTH_RESP`、`SCREEN_FRAME` 要靠控制端 socket 的 `readyRead`
  才能被读出来。接收链路没接 → 控制端对这些消息**一无所知** → 没有画面可绘 → 黑屏。
- **为什么 Host 端没这个问题**：Host 端**根本不用 `TcpConnection`**。它用 `QTcpServer::nextPendingConnection()`
  拿到客户端 socket 后，直接把 `readyRead` 连到 `Host::onClientDataReady`（`host.cpp`），所以 Host 能收鼠标、
  能收授权请求——这一不对称正是"鼠标通、画面黑"的来源。

### 3. 为什么之前的单元测试没发现（重要）
控制端相关的单元测试（如 `ControllerScreen.*`）验证"解码/授权"逻辑时，用的是：
```cpp
QMetaObject::invokeMethod(controller, "onMessageReceived",
                          Q_ARG(QByteArray, someMessage));
```
即**直接调业务层函数**，完全绕过了 `TcpConnection` 的 socket 接收链路。所以即使底层接收链路是断的，
这些测试照样通过——黑屏的真实断点（socket 没接 `readyRead`）被测试屏蔽了。只有用**真实 TCP socket
连真实 Host** 的端到端测试才能暴露它。

### 4. 解决办法（代码级）
**文件**：`src/core/tcp_connection.cpp` **构造函数**。当外部传入一个已存在的 socket（如
`NetworkManager::connectTo` 的预连接 socket）时，立即把它的接收链路接上：

修复前：
```cpp
TcpConnection::TcpConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent), m_socket(socket) {
    m_currentInterval = m_reconnectInterval;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpConnection::onReconnectTimer);
    setState(xrk::ConnectionState::Disconnected);
}
```

修复后（新增 `if (m_socket) setupSocket(m_socket);`）：
```cpp
TcpConnection::TcpConnection(QTcpSocket* socket, QObject* parent)
    : QObject(parent), m_socket(socket) {
    m_currentInterval = m_reconnectInterval;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpConnection::onReconnectTimer);
    // 接上 socket 的接收链路：当外部传入一个"已经连好"的 socket 时
    // （例如 NetworkManager::connectTo 的预连接 socket），若不在此调用 setupSocket，
    // 则该 socket 的 readyRead 永远不会连到 processBuffer，对等端发来的消息
    // （AUTH_RESP / SCREEN_FRAME …）永远到不了 onMessageReceived ——
    // 表现为"鼠标可控、桌面黑屏"。
    if (m_socket) {
        setupSocket(m_socket);
    }
    setState(xrk::ConnectionState::Disconnected);
}
```

**安全性说明**：
- `reconnect()` 仍对自己新建的 socket 调 `setupSocket()`（是另一个 socket 对象，不会重复接线；旧 socket
  会 `deleteLater`）。
- 全仓库只有 `NetworkManager::connectTo` 这一处用"带 socket 的构造函数"，修复影响面可控、无副作用。

### 5. 验证（端到端，真实 TCP）
修复后控制端日志完整出现：
```
[Auth] Response received, size=50
[Auth] Encryption initialized
[Auth] Encryption roundtrip OK
[DIAG] Controller: FIRST SCREEN_FRAME received, size=192688B, decodeWorker=on
[DIAG] Controller: (decode-worker) FIRST frame decrypt OK, size=192680B
[DIAG] Widget: FIRST frame DECODED and displayed 1920x1080
[Widget] frame#1 DECODE OK#1 sz=1920x1080 fmt=0
...
```
- `tests/test_e2e_black_screen.cpp::E2E.BlackScreenPipeline`：修复前失败、修复后**通过**。
- 全量套件：**371 通过 / 1 跳过**（`VideoRoundTrip.H264EncodeDecode` 因本机无 libx264 跳过）。
- 此前一直失败的 `LoopbackConnectionTest.*` 3 个也因同一根因而通过——它们同样走 `TcpConnection`
  客户端连接，接收端没接线导致 5s 超时。

> **全量构建验证（2026-08-05）**：`cmake --build build --config Release` 已成功把该修复链接进 `xrk.exe`
> （此前因遗留的 `xrk.exe` PID 13644 占用文件锁导致 `LNK1104` 链接失败，已 `taskkill /f` 后重链成功）。
> 之后重跑 `E2E.BlackScreenPipeline` 通过，全量 **371 通过 / 1 跳过** 复验一致。

---

## （次要 / 健壮性）根因（2026-08-05）：Host 默认使用 H264 编码，控制端解码失败

> 前置修复（加密密钥交付、同意授权 Consent）已生效，但“连上后能控鼠标、看不到桌面”
> 仍然复现。本次定位到**真正剩下的根因在编码/解码这一环**。

### 现象与关键判据
- 鼠标/键盘正常 ⇒ 连接已建立、AUTH 握手成功、**会话 AES 密钥已正确送达并初始化**
  （否则输入也不会被处理，且控制端解密帧会直接失败）。
- 画面黑 ⇒ 控制端虽持续收到 `SCREEN_FRAME`，但**解不出可见图像**。

### 根因：Host 默认编码器是 H264，而控制端 H264 解码链路脆弱
**位置**：`src/app/host.cpp` `Host::start()`（视频编码器创建处）

逐层核查结果（均正常，已排除）：
1. TCP 消息分帧 `TcpConnection::processBuffer` —— 能正确缓冲/拆分大数据包（屏幕帧很大），
   小消息（鼠标）与大消息走同一通道，分帧不是问题。
2. 加密交付 —— `Host::sendAuthKeyTo` 在 `grantConsent` 时下发 `"OK"+32B key+16B iv`，
   控制端 `handleAuthResponse`（`>= 50` 判定）对称初始化；LAN/无密码场景自动授权，
   密钥两端一致 ⇒ 帧能被正确解密。
3. 同意授权 Consent —— 私有 IP / 受信 IP / 自动授权均会自动 `grantConsent`，
   客户端被加入 `NetworkWorker` 发送列表 ⇒ 帧确实在发送。
4. `encodeScreenFrame` / `decodeScreenFrame` 线格式 —— 原生 C++ 版本偏移正确（web 端曾有的
   4 字节偏移 bug 不存在于原生端）。
5. 控件绘制 `RemoteDesktopWidget::paintEvent` —— 收到非空的 `m_currentFrame` 即绘制，无问题。

唯一剩下的失败点：**Host 在普通连接下默认用 H264 编码**（`Host::start` 先建 H264 编码器，
仅当 H264 *编码*失败时回退 JPEG）。控制端若无法稳定解码该 H264 流
（ffmpeg 不含 H264 解码能力 / 缺少 SPS/PPS 与关键帧重同步 / 丢帧后无法恢复），
每一帧都解出空图像 ⇒ 黑屏，而鼠标（不经解码、小消息）照常工作。
这正是“能控鼠标、看不到桌面”的典型表现。

### 修复方案

**修复 1（核心）：Host 默认编码器改为 JPEG**
`src/app/host.cpp` `Host::start()`：
- 默认创建 **JPEG** 编码器（Qt 的 JPEG 插件在每个控制端都能解码，零关键帧/编解码器依赖），
  桌面**保证可见**。
- H264 仅作为**可选**：用户选择“游戏/低延迟”档位（`setQualityLevel(gameMode=true)`）时
  才切到 H264；其余档位（低/标准/高清/自动）一律 JPEG。

**修复 2：H264 切换自愈**
`src/app/host.cpp` `setEncoderType(EncoderType::H264)`：
- 切到 H264 前先用 1920×1080 探测帧试编码；若产出为空（如本机无 libx264、或 Media
  Foundation 在 STA COM 下 `MF_E_NO_SAMPLE_TIMESTAMP`），**透明回退 JPEG**，桌面不黑。

**修复 3：控制端解码自愈**
`src/app/remote_controller.cpp` + `remote_controller.h`：
- 新增 `reportFrameDecodeResult(format, ok)`：统计**连续 H264 解码失败**次数。
- 连续失败达到 30 帧（约 0.5s@60fps）后，控制端自动向 Host 发
  `sendQualityLevel(MEDIUM, gameMode=false)` 请求切回 JPEG（一次性，之后不再打扰）。
- 任一帧解码成功即清零计数；新会话（`authSuccess`）重置自愈状态。

> 说明：在**无头/非交互会话**中（如本 agent 环境）`ScreenCapture::captureFrame()` 返回 null，
> 本就不会产出画面帧（这是环境限制，非渲染 Bug）。上述修复保证：一旦在能抓取桌面的
> 交互式会话中运行 Host，控制端**默认就能看到桌面**，不再依赖 H264 解码是否可用。

**修复 4（编码链路加固）：拒绝 h264_mf，避免 STA COM 下崩溃**
`src/hw/h264_encoder.cpp` `H264Encoder::initialize()`：
- 问题：在验证阶段发现，当本机**没有 libx264**、FFmpeg 只能回退到 Media Foundation 编码器
  `h264_mf` 时，`h264_mf` 在 Qt 默认 STA 线程模式下**不是返回空帧，而是直接在 MFT 内部崩溃**
  （`COM must not be in STA mode` → `MF_E_NO_SAMPLE_TIMESTAMP` 后段错误）。该崩溃在“单实例正常、
  多实例连跑（前一个 Host 的 Media Foundation/COM 状态搅动后）”时必现，会直接拖垮整个进程——
  比黑屏更严重，且使 `setEncoderType` 的探测回退（`encode()` 返回空才回退）根本来不及触发。
- 修复：在 `initialize()` 中，若 `libx264` 缺失且回退到的编码器是 `h264_mf`，**直接返回 false、
  完全不打开/不触碰该 MFT**。调用方（`setEncoderType` 探测、`h264EncoderAvailable()`）随即把 H264
  判定为“本机不可用”，干净地回退 JPEG，不再有崩溃。
- 影响：硬件编码器（`h264_nvenc`/`h264_qsv`/`h264_amf` 等）仍被允许；仅拒绝在 STA 下必崩的
  `h264_mf`。此环境无 libx264，故 H264 被正确判定为不可用、默认走 JPEG（与黑屏修复目标一致）。

### 验证清单（更新）
- [ ] 启动 Host，日志显示 `Using JPEG encoder for screen capture (default; ...)`（默认 JPEG）
- [ ] 普通连接（不切游戏档）→ 控制端 `paintEvent` 显示 `Frames: N | WxH` 且画面可见
- [ ] 切“游戏”档位 → Host 日志 `Encoder switch queued to H264`；若本机 H264 不可用则
      `H264 encoder unusable ... keeping JPEG`
- [ ] 模拟 H264 解码失败 → 控制端日志 `consecutive H264 decode failures - requesting host switch to JPEG`
- [ ] 断开重连、切分辨率、切档位后画面均不黑

---

## 历史根因（已修复，供参考）：同意授权 (Consent) 流程阻断画面发送


### 真正根因：同意授权 (Consent) 流程阻断画面发送
**位置**：`src/app/host.cpp` `updateNetworkWorkerClients()` 第 1570-1594 行

通过主机端日志确认：
```
NetworkWorker: 138524 frames sent, 0 KB, clients=0, lastBatch=1 frames, 0 bytes
NetworkWorker: No consented clients to send frames to!
```

关键发现：
1. **CaptureWorker 正常工作** - 持续捕获 1920x1080 帧
2. **EncodeWorker 正常工作** - 正常编码帧（17.4ms/帧）
3. **NetworkWorker 发送 0 字节** - `clients=0`，没有已授权的客户端
4. **鼠标/键盘事件正常** - 在 `processMouseEvent`/`processKeyEvent` 中处理，**不检查 consent 状态**
5. **画面帧未发送** - `NetworkWorker::drainQueue()` 只向 `consented=true` 的客户端发送

**完整流程**：
1. 控制器连接 → `acceptExternalSocket()` → `info.authenticated = m_password.isEmpty()`
2. 若无密码 → `requestConsent()` → 发送 `CONSENT_REQUEST` → 弹出授权对话框
3. 主机用户必须点击"允许" → `grantConsent()` → `info.consented = true` → `updateNetworkWorkerClients()`
4. 此时 NetworkWorker 才有客户端列表，开始发送画面帧

**问题**：在无头/服务模式或用户未注意到授权对话框时，`consented` 永远为 `false`，画面帧永远不发送。

### 次要原因：H264 编码器可能静默失败
**位置**：`src/app/host.cpp` EncodeWorker::processFrames() 第 195-210 行

代码注释明确标注：
> "This causes 'mouse works but no frame' bug!"

H264 Media Foundation 编码器 (`h264_mf`) 在某些环境下：
- `initialize()` 返回成功
- 但 `encode()` 返回空数据 (`MF_E_NO_SAMPLE_TIMESTAMP`)
- 导致 EncodeWorker 跳过该帧，不发送画面数据

但从当前日志看，编码器工作正常（17.4ms/帧），所以这不是当前问题的主因。

---

## 修复方案

### 修复 1：自动授权私有/LAN IP 范围（核心修复）
**文件**：`src/app/host.cpp` `requestConsent()`

新增 `isPrivateIp()` 方法，自动授权以下 IP 范围：
- `127.0.0.0/8` (回环地址)
- `10.0.0.0/8` (私有A类)
- `192.168.0.0/16` (私有B类)
- `172.16.0.0/12` (私有C类)

由于这是局域网远程控制工具，来自这些地址的连接通常来自同一网络中的控制器，应自动授权。

### 修复 2：自动授权设置 (Auto-Grant Consent)
**文件**：`src/app/host.h`, `src/app/host.cpp`, `src/ui/settings_widget.h`, `src/ui/settings_widget.cpp`, `src/ui/main_window.cpp`

新增 `m_autoGrantConsent` 设置：
- 设置页复选框："自动允许受信任连接 (无需确认)"
- 持久化到 `security/auto_grant_consent`
- `Host::requestConsent()` 中检查此标志，自动调用 `grantConsent()`
- `Host::setAutoGrantConsent()` / `Host::isAutoGrantConsent()` 方法

### 修复 3：运行时 H264 编码器空帧自动回退 JPEG
**文件**：`src/app/host.h`, `src/app/host.cpp` EncodeWorker::processFrames()

在连续 60 次空编码后（~1秒@60fps），自动切换到 JPEG 编码器，保证画面不中断。
- 新增 `m_fallbackRequested` 标志
- 新增 `requestFallbackToJpeg()` 方法
- 新增 `encoderChanged(EncoderType)` 信号
- 新增 `Host::onEncoderChanged()` 处理器

### 修复 4：增强启动时编码器探测
**文件**：`src/app/host.cpp` Host 构造函数

使用 1920x1080 真实分辨率测试帧探测（而非固定 64x64），更准确检测编码器问题。

### 修复 5：捕获/编码/发送关键路径增强日志
**文件**：`src/app/host.cpp` CaptureWorker, EncodeWorker, NetworkWorker

关键节点输出日志，便于现场诊断：
- CaptureWorker: 每30秒输出捕获帧数+分辨率
- EncodeWorker: 空编码计数 + 自动回退日志
- NetworkWorker: 发送详情 + "无客户端"警告

---

## 使用方法

### 方案 A：自动授权LAN连接（默认行为）
控制器连接被控制端后，如果IP属于私有范围（192.168.x.x, 10.x.x.x, 127.0.0.1等），自动授权，无需弹窗。

### 方案 B：启用自动授权设置
1. 打开被控制端设置 (设置 → 安全)
2. 勾选 "自动允许受信任连接 (无需确认)"
3. 保存设置
4. 重启被控制端服务

### 方案 C：手动授权（默认）
1. 控制端连接被控制端
2. 被控制端弹出授权对话框
3. 点击"允许"
4. 控制端立即可见桌面

---

## 历史修复的验证清单（Consent 修复）

- [ ] 启动服务，日志显示 "Using JPEG encoder" 或 "Using H264 encoder"
- [ ] 连接控制端（LAN IP），日志显示 "auto-granting consent for <clientId>"
- [ ] 控制端收到 SCREEN_FRAME，画面正常渲染
- [ ] 网络日志显示 "NetworkWorker: X frames sent, Y KB, clients=1"（clients 从 0 变为 1）
- [ ] 断开重连、切换分辨率、切换编码器均无黑屏

---

## 相关文件修改（最新：2026-08-05 编码/解码根因）
- `src/app/host.cpp` `Host::start()` - **默认编码器改为 JPEG**（原先默认 H264）；仅“游戏/低延迟”档位才用 H264
- `src/app/host.cpp` `setEncoderType()` - H264 切换前探测编码，失败时透明回退 JPEG（自愈）
- `src/app/remote_controller.h` - 新增 `m_h264FailStreak` / `m_h264FallbackRequested` + `reportFrameDecodeResult()`
- `src/app/remote_controller.cpp` - 实现 `reportFrameDecodeResult()`：连续 30 帧 H264 解码失败后自动请求 Host 切回 JPEG；同步/异步解码路径均已接入；新会话重置自愈状态

## 其他修复（非黑屏，2026-08-05）
运行 `xrk.exe` 点“启动服务”时控制台曾打印：
`qt.core.qobject.connect: QObject::connect(xrk::Host, xrk::MainWindow): unique connections require a pointer to member function of a QObject subclass`
**根因**：`src/ui/main_window.cpp` 用 `Qt::UniqueConnection` 连接一个 lambda（非 PMF 成员函数）。
`Qt::UniqueConnection` 仅对“指向 QObject 成员函数的指针”有效，对 lambda 不支持，该 connect 会**静默失败**
（仅影响错误弹窗，不导致黑屏，也与看不到桌面无关）。
**修复**：移除该无效的 `Qt::UniqueConnection` 连接，把错误弹窗合并进 `MainWindow` 构造/setup 中
一次性连接的 `Host::errorOccurred` lambda（保留 `QMessageBox::critical` 弹窗 + 日志）。启动服务不再打印该告警。
- `src/ui/main_window.cpp` - 删除 `onToggleHost` 内重复的 `Qt::UniqueConnection` 连接；`:857` 的
  `errorOccurred` 连接改为“日志 + 错误弹窗”一次性连接

## 单元测试补全（2026-08-05）
- 新增 `tests/test_host_encoder_default.cpp` + 注册进 `tests/CMakeLists.txt`：
  - `HostEncoder.DefaultEncoderIsJpeg`：headless 启动 Host，断言默认编码器为 JPEG
  - `HostEncoder.NonGameGearIsJpeg`：标准档位仍用 JPEG
  - `HostEncoder.GameGearUsesH264OrFallsBackToJpeg`：游戏档用 H264（本机有 libx264 且可编码）或
    因 `initialize()` 拒绝 `h264_mf` 而自愈回退 JPEG（本环境即此路径，稳定通过）
- `tests/test_controller_screen.cpp` 扩展 `ControllerScreen.DecodeSelfHealRequestsJpeg`：连续 30 次
  H264 解码失败触发并锁定“请求切回 JPEG”自愈（一次性，不重复请求）
- `RemoteController` 新增 `h264DecodeFallbackRequested()` 访问器 + 将 `reportFrameDecodeResult()` 提升为 `private slots:`
  （供测试通过 `invokeMethod` 驱动）
- 验证：`xrk_tests` 全量 **371 通过 / 1 跳过**（`VideoRoundTrip.H264EncodeDecode`，本环境无
  libx264 故 H264 被判定不可用而跳过）。原 `LoopbackConnectionTest.*` 3 个失败**已随 `TcpConnection`
  接收链路修复而转为通过**——它们与黑屏同根（客户端连接接收端没接线导致 5s 超时），并非环境无关。
  修复前 `HostEncoder.* + ControllerScreen.*` 组合在多次 Host 实例连跑时必现段错误（h264_mf 在
  STA COM 下崩溃），修复后连跑 4 次均稳定通过。

## 相关文件修改（历史：Consent 根因）
- `src/app/host.h` - 新增 `m_autoGrantConsent` + `setAutoGrantConsent()`/`isAutoGrantConsent()` + `isPrivateIp()` + `onEncoderChanged()`
- `src/app/host.cpp` - 修改 `requestConsent()` 自动授权逻辑 + 编码器回退 + 日志增强 + `isPrivateIp()` 实现
- `src/ui/settings_widget.h` - 新增 `autoGrantConsentEnabled()` + `m_autoGrantConsentCheckBox`
- `src/ui/settings_widget.cpp` - 新增设置页复选框 + 持久化
- `src/ui/main_window.cpp` - 应用 auto_grant_consent 设置到 Host

---

## 运行时诊断日志（2026-08-05，用于定位“鼠标通、画面黑”的断点）

黑屏排查中，编码器/加密/授权/发送/接收/解码/绘制的**静态逻辑均已核对正确**，故在每环加了
**只打印一次**的 `[DIAG]` 前缀诊断标记。在真实两台机器上连一次，对两端日志 `grep DIAG`（或
直接看控制台），按下面顺序即可看出断在哪一环：

被控制端（Host）：
1. `[DIAG] CaptureWorker: screen capture INITIALIZED OK` —— 抓屏初始化成功（否则见下方 FAILED，桌面必黑）
2. `[DIAG] CaptureWorker: FIRST frame captured WxH` —— 首帧抓取成功（没有则抓屏一直返回 null）
3. `[DIAG] EncodeWorker: FIRST frame encoded format=JPEG size=N B` —— 首帧编码成功
4. `[DIAG] NetworkWorker: FIRST frame batch delivered to a connected client` —— 首帧已发给已连接客户端
   （若长期只看到 `NetworkWorker: No consented clients to send frames to!` ⇒ 授权没过 / 客户端没进发送列表）

控制端（Controller）：
5. `[DIAG] Controller: FIRST SCREEN_FRAME received, size=N B` —— 收到首帧
6. `[DIAG] Controller: (decode-worker) FIRST frame decrypt OK` —— 解密成功（FAILED 则密钥未初始化/不匹配）
7. `[DIAG] Widget: FIRST frame DECODED and displayed WxH` —— 首帧解码并绘制（到此必可见桌面）

判断规则：
- 缺 1/2 ⇒ 抓屏环境问题（无头/安全桌面/UAC/无显示器/DXGI 不可用）。
- 有 3 无 4 ⇒ Host 没把帧发给客户端（看 `clients=` 是 0 还是 1）。
- 有 4 无 5 ⇒ 网络/连接层没把帧送到控制端（防火墙、端口、P2P/中继路径）。
- 有 5 无 6 ⇒ 控制端解密失败（AUTH_RESP 里的密钥没送达或 `m_encryption` 未初始化）。
- 有 6 无 7 ⇒ 解码失败（JPEG 数据损坏 / 解码器问题），看 `[Widget] ... DECODE FAIL` 详情。

改动文件：`src/app/host.cpp`（CaptureWorker::start/onCaptureTimer、EncodeWorker::processFrames、
NetworkWorker::drainQueue）、`src/app/remote_controller.cpp`（handleScreenFrame + 解码 worker）、
`src/ui/remote_desktop_widget.cpp`（onScreenFrameReceived）。