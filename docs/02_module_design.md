# XRK 局域网远程控制软件 - 模块设计文档

## 1. Core Layer 模块

### 1.1 NetworkManager
- **职责**: 管理TCP/UDP网络连接
- **接口**: initialize(), broadcastDiscovery(), connectTo(), disconnectAll()
- **信号**: connectionEstablished, connectionLost, messageReceived

### 1.2 TcpConnection
- **职责**: 封装TCP连接，处理收发
- **接口**: send(), isConnected(), deviceId()
- **信号**: readyRead, disconnected, errorOccurred

### 1.3 ProtocolManager
- **职责**: 消息编解码
- **接口**: encode(), decode(), validate()
- **静态方法**: 无需实例化

### 1.4 DeviceDiscovery
- **职责**: UDP广播发现局域网设备
- **接口**: startDiscovery(), stopDiscovery(), getDevices()
- **信号**: deviceFound, deviceLost

### 1.5 MessageCodec
- **职责**: 消息头解析、校验
- **接口**: encode(), decode(), calculateChecksum()
- **静态方法**: 无需实例化

### 1.6 Logger
- **职责**: 日志记录
- **接口**: debug(), info(), warning(), error(), fatal()
- **单例模式**: 全局唯一实例

## 2. Application Layer 模块

### 2.1 DeviceManager
- **职责**: 设备列表管理
- **接口**: addDevice(), removeDevice(), getDevices()
- **信号**: deviceAdded, deviceRemoved, deviceListChanged

### 2.2 SessionManager
- **职责**: 会话生命周期管理
- **接口**: createSession(), closeSession(), isSessionValid()
- **信号**: sessionCreated, sessionClosed

### 2.3 RemoteController
- **职责**: 远程控制核心逻辑；同时作为分块传输的客户端
- **接口**: startRemote(), stopRemote(), sendMouseEvent(), sendKeyEvent(),
  requestKeyFrame(), requestTileResend(const QList<QPoint>&)
- **信号**: remoteStarted, remoteStopped, screenFrameReceived, screenImageReceived
- **分块相关**:
  - 鉴权成功后发送 `SCREEN_KEYFRAME` 声明支持分块（能力握手）
  - `handleScreenTile` 把 tile 投入解码线程队列，`applyTile` 解密/校验 MD5/拼接到画布
  - tile MD5 校验失败 → 收集坏 tile 到 `m_pendingNack`，每 ≤100ms 批量发 `SCREEN_TILE_REQUEST`（精准 NACK）
  - `sendScreenAck` 每 1s 回馈 RTT/丢包/缓冲，驱动主机自适应预算

### 2.4 Host 与 EncodeWorker（被控端，分块传输主机侧）

- **Host**
  - **职责**: 被控端业务；接收客户端消息并分发（含 `SCREEN_TILE_REQUEST`）
  - **接口**: start(), stop(), processClientMessage(), handleScreenTileRequest()
  - **分块相关**: 收到 `SCREEN_KEYFRAME` 后启用分块传输；`processMouseEvent` 经
    `EncodeWorker::setMousePosition` 喂入光标坐标

- **EncodeWorker**（Host 内的编码/发送线程）
  - **职责**: 屏幕帧 → tile 网格 → 编码 → 加密 → 发送；自适应预算与精准重传
  - **接口**: start(), stop(), resendTiles(const QList<QPoint>&),
    setMousePosition(const QPoint&)
  - **分块相关**:
    - `buildTiles`：脏区检测（DXGI 脏矩形，缺失走 `hasFrameChanged` hash 兜底），仅编码变化 tile；
      显式关键帧强制全网格重绘（IDR）
    - 逐 tile 内容分类编码：纯色 → RLE（无损）/ 照片 → JPEG
    - 发送前 `std::stable_partition` 把鼠标所在 tile 排到最前（光标优先级）
    - `resendTiles`：从 `m_lastRawFrame` 缓存重新编码并单发被请求的 tile（精准 NACK 响应）

### 2.5 SecurityManager
- **职责**: 身份认证、加密
- **接口**: generateDeviceId(), validateToken(), encrypt(), decrypt()
- **信号**: authenticationFailed

### 2.6 FileTransferManager
- **职责**: 文件传输管理
- **接口**: uploadFile(), downloadFile(), cancelTransfer()
- **信号**: transferStarted, transferProgress, transferCompleted

## 3. UI Layer 模块

### 3.1 MainWindow
- **职责**: 主窗口、菜单、工具栏
- **槽**: onDeviceSelected(), onRemoteStarted(), onSettingsClicked()

### 3.2 DeviceListWidget
- **职责**: 设备列表显示
- **信号**: deviceDoubleClicked, connectClicked

### 3.3 RemoteDesktopWidget
- **职责**: 远程桌面显示、输入捕获
- **方法**: startRemote(), stopRemote()
- **信号**: mouseEventSent, keyEventSent

### 3.4 FileTransferWidget
- **职责**: 文件传输界面
- **槽**: onUploadClicked(), onDownloadClicked()

### 3.5 SettingsWidget
- **职责**: 设置对话框
- **方法**: loadSettings(), saveSettings()

## 4. Hardware Layer 模块

### 4.1 ScreenCapture
- **职责**: 屏幕采集
- **接口**: initialize(), captureFrame(), setCaptureRect()
- **实现**: DXGI Desktop Duplication (Windows)
- **分块相关**: `captureDxgiFrameEx` 在生成 CPU 位图后计算整帧 FNV-1a hash；当 DXGI 脏矩形
  不可用时以 `hasFrameChanged` 兜底（整帧无变化则不发，有变化才触发全屏），避免脏矩形
  丢失就每帧重发整屏

### 4.2 InputControl
- **职责**: 输入事件模拟
- **接口**: simulateMouseMove(), simulateMouseButton(), simulateKeyPress()
- **实现**: Windows SendInput API

### 4.3 TileEncoder
- **职责**: 单 tile 像素 ⇄ 字节的编码/解码，以及整帧 → tile 网格切片（弱网分块传输核心）
- **接口**: buildTiles(const QImage&, const QList<QRect>&),
  encodeTilePixels(const QImage&, int quality, TileEncoding*),
  decodeTilePixels(const QByteArray&, TileEncoding, int w, int h),
  tileSize(), requestKeyFrame()
- **编码选择**: 颜色数极少 → RLE（无损）；中等 → RAW（deflate）；照片 → JPEG（按质量）
- **设计要点**: `TILE_SIZE = 64`，与 RDP 位图缓存 tile 对齐；单 tile 体积小，单个丢失的
  tile 只需一次微小重传

## 5. 弱网分块传输设计方案（Tiled Transport, Phases A–F）

### 5.1 目标
在丢包/高延迟的局域网链路上，用"小 tile + 差分 + 精准重传"替代整帧重发，降低带宽并
加快残块收敛。与 H264 互斥：任一客户端非 tile-capable 或编码器为 H264 时回退整帧。

### 5.2 分层数据流
```
ScreenCapture(DXGI) ──脏区/整帧hash──▶ EncodeWorker(Host)
                                        │ buildTiles: 切片 + 内容分类编码 + 加密
                                        │ 光标优先级 stable_partition
                                        ▼
                                    SCREEN_TILE (TCP, 加密)
                                        │
                                        ▼
RemoteController ──decode线程──▶ applyTile: 解密→MD5校验→拼接到画布→screenImageReceived
                  │ 每1s SCREEN_FRAME_ACK (RTT/丢包/缓冲)
                  │ 坏tile≤100ms 批量 SCREEN_TILE_REQUEST (NACK)
```

### 5.3 阶段与设计要点
| 阶段 | 能力 | 关键实现 |
|------|------|----------|
| A 脏区检测 | 只编码变化区域 | DXGI dirty/move 矩形；缺失走 FNV-1a `hasFrameChanged` 兜底 |
| C 能力握手 | 启用分块 | 客户端鉴权后发 `SCREEN_KEYFRAME` |
| D 逐tile编码 | 内容自适应 | 纯色→RLE(无损) / 照片→JPEG |
| E 可靠传输 | 丢块修复 | 周期关键帧(10s, IDR 强制全绘) + AIMD tile 预算 |
| F 精准NACK | 单tile重传 | 坏tile收集→批量 `SCREEN_TILE_REQUEST`→`resendTiles` |
| 光标优先级 | 弱网体验 | 发送前把鼠标所在 tile 排到最前 |

### 5.4 线程与并发注意
- `EncodeWorker` 在独立线程切片/发送；`m_lastRawFrame` 用 `QMutex` 保护，供 `resendTiles` 重编码。
- 客户端 `applyTile` 运行在解码线程：NACK 定时器通过
  `QMetaObject::invokeMethod(m_nackTimer, "start", QueuedConnection)` 切回主线程启动，
  避免"跨线程启动定时器"；`m_pendingNack` 用 `QMutex` 保护。

