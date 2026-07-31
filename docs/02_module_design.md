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
- **职责**: 远程控制核心逻辑
- **接口**: startRemote(), stopRemote(), sendMouseEvent(), sendKeyEvent()
- **信号**: remoteStarted, remoteStopped, screenFrameReceived

### 2.4 SecurityManager
- **职责**: 身份认证、加密
- **接口**: generateDeviceId(), validateToken(), encrypt(), decrypt()
- **信号**: authenticationFailed

### 2.5 FileTransferManager
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

### 4.2 InputControl
- **职责**: 输入事件模拟
- **接口**: simulateMouseMove(), simulateMouseButton(), simulateKeyPress()
- **实现**: Windows SendInput API
