# XRK 局域网远程控制软件 - 已完成任务

## 工程流程

| 步骤 | 状态 | 说明 |
|------|------|------|
| Step1 工程环境分析 | ✅ | 检查目录结构、技术栈、存在问题 |
| Step2 架构设计 | ✅ | 系统架构图、模块依赖图、数据流图、类设计、协议设计、多线程模型 |
| Step3 工程骨架 | ✅ | 目录结构、CMake构建、接口文件 |
| Step4 模块实现 | ✅ | 所有核心功能已完成 |

## 文档体系

| 文档 | 状态 | 路径 |
|------|------|------|
| 项目架构 | ✅ | docs/01_architecture.md |
| 模块设计 | ✅ | docs/02_module_design.md |
| 通信协议 | ✅ | docs/03_protocol.md |
| 数据设计 | ✅ | docs/04_database.md |
| 编译说明 | ✅ | docs/05_build.md |
| 测试方案 | ✅ | docs/06_test_plan.md |
| 修改记录 | ✅ | docs/07_change_log.md |

## 已实现模块

### Core Layer（核心层）

| 模块 | 文件 | 功能 |
|------|------|------|
| Logger | logger.h/cpp | 日志系统，支持Debug/Info/Warning/Error/Fatal级别 |
| MessageCodec | message_codec.h/cpp | 消息编解码，Header+Payload+Checksum格式 |
| ProtocolManager | protocol_manager.h/cpp | 协议管理，支持所有消息类型的编解码 |
| TcpConnection | tcp_connection.h/cpp | TCP连接封装，支持收发和缓冲区管理 |
| NetworkManager | network_manager.h/cpp | 网络管理，TCP服务器+UDP广播 |
| DeviceDiscovery | device_discovery.h/cpp | 设备发现，UDP广播+设备列表管理 |
| FrameQueue | frame_queue.h | 线程安全队列（模板类），用于多线程架构 |
| WakeOnLan | wake_on_lan.h/cpp | WOL远程唤醒，魔术包生成+UDP广播发送 |

### Application Layer（应用层）

| 模块 | 文件 | 功能 |
|------|------|------|
| DeviceManager | device_manager.h/cpp | 设备列表管理，增删改查 |
| SessionManager | session_manager.h/cpp | 会话管理，创建/关闭/过期清理 |
| RemoteController | remote_controller.h/cpp | 远程控制客户端，连接/发送输入/接收画面 |
| SecurityManager | security_manager.h/cpp | 安全管理，设备ID/Token生成，端到端加密（ECDH+AES-256-GCM）|
| FileTransferManager | file_transfer_manager.h/cpp | 文件传输，支持上传/下载/暂停/取消 |
| Host | host.h/cpp | 被控端服务，多线程架构（采集/编码/网络分离）|
| ClipboardManager | clipboard_manager.h/cpp | 剪贴板同步，监控系统剪贴板变化并自动同步 |
| RemoteTerminal | remote_terminal.h/cpp | 远程终端，QProcess管理CMD/PowerShell/Bash |
| ScreenRecorder | screen_recorder.h/cpp | 屏幕录像，MJPEG AVI格式录制 |

### Hardware Layer（硬件层）

| 模块 | 文件 | 功能 |
|------|------|------|
| ScreenCapture | screen_capture.h/cpp | 屏幕采集，DXGI+GDI(Linux X11/XShm, macOS CoreGraphics)，多显示器支持 |
| InputControl | input_control.h/cpp | 输入控制，SendInput(Linux XTest, macOS CGEvent) |
| VideoEncoder | video_encoder.h/cpp | 视频编码器抽象接口，工厂模式创建JPEG/H264 |
| JpegEncoder | jpeg_encoder.h/cpp | JPEG编码器封装，质量可调 |
| H264Encoder | h264_encoder.h/cpp | H.264编码器，基于FFmpeg libx264，超快预设+零延迟优化 |
| VideoDecoder | video_decoder.h/cpp | H.264解码器，基于FFmpeg libavcodec |

### UI Layer（界面层）

| 模块 | 文件 | 功能 |
|------|------|------|
| MainWindow | main_window.h/cpp | 主窗口，支持主控/被控模式切换，QTabWidget标签页 |
| DeviceListWidget | device_list_widget.h/cpp | 设备列表+IP输入+连接历史 |
| RemoteDesktopWidget | remote_desktop_widget.h/cpp | 远程桌面显示+输入捕获+坐标映射+快捷键+H264解码 |
| FileTransferWidget | file_transfer_widget.h/cpp | 文件传输界面+进度条 |
| SettingsWidget | settings_widget.h/cpp | 设置对话框 |
| TerminalWidget | terminal_widget.h/cpp | 远程终端界面，黑色背景绿色文字控制台 |
| ChatWidget | chat_widget.h/cpp | 聊天界面，消息列表+输入框+发送按钮 |

## 已实现功能

### 网络通信
- [x] TCP服务器监听
- [x] TCP客户端连接
- [x] UDP广播发现
- [x] 自定义二进制协议
- [x] 消息头（magic/version/type/length/timestamp/sessionId）
- [x] CRC32校验
- [x] 断线重连（TcpConnection reconnect + RemoteController reconnect signals）

### 远程桌面
- [x] 被控端启动服务
- [x] 主控端输入IP连接
- [x] 屏幕画面采集（DXGI Desktop Duplication + GDI回退）
- [x] JPEG压缩传输（质量70）
- [x] 画面显示
- [x] FPS统计显示
- [x] 30fps流畅度（默认升为60fps）
- [x] 60fps流畅度（H.264 + 帧丢弃 + 脏帧优化）
- [x] 画面自适应窗口大小（保持比例缩放）
- [x] 鼠标坐标映射（本地坐标→远程坐标）
- [x] 帧间差分优化（减少网络传输）

### 多线程架构
- [x] Capture Worker：独立线程进行屏幕采集
- [x] Encode Worker：独立线程进行JPEG编码
- [x] Network Worker：独立线程发送网络数据
- [x] 线程安全队列（FrameQueue模板类）
- [x] 生产者-消费者模式解耦
- [x] Decode Worker：独立线程进行解密+解码（worker context moveToThread修复主线程阻塞）

### 连接认证
- [x] 被控端密码设置（setPassword/isPasswordRequired）
- [x] AUTH_REQ/AUTH_RESP协议（MessageType 13/14）
- [x] Host认证处理（processAuthRequest）
- [x] Client认证发送（sendAuthRequest）
- [x] Client认证响应处理（handleAuthResponse）
- [x] UI密码输入对话框（QInputDialog）
- [x] 未认证客户端过滤（只向已认证客户端发送帧）

### 输入控制
- [x] 鼠标移动
- [x] 鼠标左键/右键/中键点击
- [x] 鼠标滚轮
- [x] 键盘按键
- [x] 键盘组合键（Ctrl/Shift/Alt/Win）

### 设备发现
- [x] UDP广播发送
- [x] UDP广播接收
- [x] 设备信息解析（名称/IP/端口/版本）
- [x] 设备列表自动更新
- [x] 设备名显示

### 文件传输
- [x] 文件选择对话框
- [x] 进度条显示
- [x] 传输速度显示
- [x] 暂停/恢复传输
- [x] 取消传输
- [x] 断点续传（Phase A4）
  - SHA-256校验和计算与验证
  - 传输偏移量追踪与恢复
  - 会话ID用于传输追踪
  - 数据库持久化（checksum、resume_offset、session_id字段）
  - FILE_CHECKSUM协议消息（MessageType 43）编解码
  - 数据库模式迁移v3（新增checksum/resume_offset/session_id列）
  - 单元测试（7个新用例：偏移量查询、校验和查询、恢复检查、恢复传输、协议往返、校验和验证、传输记录）

### 连接历史
- [x] 保存连接历史到配置文件
- [x] 快速重连（双击历史记录）
- [x] 清空历史记录
- [x] 最多保存10条历史

### 剪贴板同步
- [x] 系统剪贴板监控（定时器+信号）
- [x] 支持文本/HTML/图片/URL同步
- [x] 自动发送剪贴板数据到远端
- [x] 接收远端剪贴板数据并设置到本地
- [x] 可启用/禁用剪贴板同步

### 多显示器支持
- [x] 枚举所有显示器（DXGI + GDI）
- [x] 获取显示器信息（名称/位置/尺寸/是否主显示器）
- [x] 切换采集目标显示器
- [x] 按索引采集指定显示器画面

### H.264视频编码
- [x] VideoEncoder抽象接口（工厂模式）
- [x] JpegEncoder封装（保持JPEG回退能力）
- [x] H264Encoder实现（FFmpeg libx264）
- [x] 超快预设ultrafast + zerolatency优化
- [x] VideoDecoder解码器（接收端H.264解码）
- [x] JPEG/H.264自动切换（根据EncoderType）
- [x] RemoteDesktopWidget支持H.264画面解码

### AES加密
- [x] Encryption类封装（AES-256-CBC，基于FFmpeg AES）
- [x] 密钥生成（random 256-bit key + 128-bit IV）
- [x] CBC模式加密/解密
- [x] PKCS7填充处理（已修复：原0x80哨兵方案在二进制帧数据中被误截断）
- [x] 认证后密钥传输（AUTH_RESP携带key+iv）
- [x] 服务端加密ScreenFrame数据
- [x] 客户端解密ScreenFrame数据
- [x] 加密密钥轮转验证（auth roundtrip test）
- [x] 客户端异步解密（DecodeWorker独立线程，非阻塞网络线程）

### 远程终端
- [x] RemoteTerminal类（QProcess管理，支持CMD/PowerShell/Bash）
- [x] TERMINAL_START/INPUT/OUTPUT/STOP消息协议
- [x] TerminalWidget终端界面（黑底绿字控制台）
- [x] 终端输入发送到远端进程
- [x] 远端进程输出实时显示
- [x] MainWindow集成（QTabWidget标签页）

### 远程聊天
- [x] ChatWidget聊天界面（消息列表+输入框+发送按钮）
- [x] CHAT_MESSAGE消息协议（复用已有encode/decode）
- [x] Host广播聊天消息到所有已认证客户端
- [x] RemoteController接收和发送聊天消息
- [x] MainWindow集成（QTabWidget标签页）

### 远程截图
- [x] SCREENSHOT_REQ/RESP消息协议
- [x] Host处理截图请求（captureFrame+JPEG编码）
- [x] RemoteController截图请求方法
- [x] 截图保存文件对话框
- [x] 工具栏截图按钮

### 远程录像
- [x] ScreenRecorder MJPEG AVI录像类（RIFF/AVI格式，支持任意播放器）
- [x] RECORD_START/STOP/ACK消息协议
- [x] Host端定时采集+JPEG编码写入AVI
- [x] 本地录像（Host模式）
- [x] 远程录像（Client模式，发送RECORD_START/STOP命令）
- [x] MainWindow录像按钮（本地/远程模式自动切换）
- [x] 文件选择对话框指定保存路径

### 60fps高帧率
- [x] 默认帧率从30→60fps
- [x] CaptureWorker帧丢弃（队列满时跳过采集）
- [x] EncodeWorker脏帧丢弃（只处理最新帧）
- [x] 默认编码器从JPEG→H.264（更低带宽，更快编码）
- [x] 帧队列从5/3→8/5（适应更高吞吐量）
- [x] 设置界面可配置帧率（1-120fps）
- [x] 设置界面FPS选项支持1-120范围

### 跨平台支持
- [x] Linux X11/XShm屏幕采集（共享内存，高性能）
- [x] Linux XTest输入控制（鼠标移动/点击/滚轮/键盘）
- [x] macOS CoreGraphics屏幕采集（CGDisplayCreateImage）
- [x] macOS CGEvent输入控制（鼠标/键盘/滚轮事件）
- [x] CMakeLists平台条件编译（X11/Xext/Xtst库 on Linux, ApplicationServices on macOS）
- [x] Windows DXGI+GDI/SendInput保持不受影响

### WOL远程唤醒
- [x] WakeOnLan类封装魔术包生成和UDP广播发送
- [x] MAC地址有效性验证（多种格式支持）
- [x] DeviceListWidget MAC地址输入框+唤醒按钮
- [x] 状态反馈（发送成功/失败提示）

### 隐私屏
- [x] PrivacyScreen全屏黑色遮挡窗口（无边框+置顶）
- [x] 客户端连接后自动显示隐私屏
- [x] 所有客户端断开后自动隐藏隐私屏
- [x] Host集成（setPrivacyScreenEnabled开关）
- [x] 设置界面开关（远程控制时锁屏）
- [x] 选中文字提示（远程控制进行中）
- [x] 远程控制端隐私屏开关（PRIVACY_SCREEN消息类型，Controller→Host）
- [x] DXGI采集排除（SetWindowDisplayAffinity WDA_EXCLUDEFROMCAPTURE）
- [x] 输入穿透（Qt::WindowTransparentForInput，远控不受影响）
- [x] 启用时立即生效（setPrivacyScreenEnabled修复：无需等客户端重连）

### 画质/延迟档位（Task 26）
- [x] 新增 MessageType::SET_QUALITY + QualityRequest{level,gameMode} 协议
- [x] 协议编解码 encode/decodeQualityRequest（protocol_manager）
- [x] 控制器发送档位：RemoteController::sendQualityLevel(QualityLevel,bool)
- [x] 主机应用档位：Host::setQualityLevel
  - AUTO/ADAPTIVE 恢复带宽自适应（onQualityTimer 在 m_autoAdapt=false 时仅上报）
  - 流畅=JPEG45/24fps，标准=JPEG65/30fps，高清=JPEG82/30fps
  - 游戏=ULTRA JPEG92/60fps + 尝试切 H264 低延迟编码器（无 libx264 自动回退 JPEG）
- [x] 工具栏画质下拉（自动/流畅/标准/高清/游戏），切换即下发，不抢键盘焦点
- [x] 新会话重置为「自动」
- [x] 单测：协议往返 + 主机档位映射（headless）

### 文件实时同步（Task 27）
- [x] FileSyncManager：QFileSystemWatcher 递归监控本地目录 + 400ms 防抖
- [x] 相对路径映射（relativePath / remoteTargetPath）保留子目录结构
- [x] 多配对（本地↔远程）、开始/停止、连接断开暂停上传
- [x] FileTransferManager::uploadFileTo(filePath, remotePath) 上传到指定远程路径
- [x] 主机 handleFileRequest 上传分支支持写入指定路径并自动建父目录（向后兼容临时目录）
- [x] 文件传输面板「实时同步（本地→远程）」分组：添加/移除目录对、开始/停止、状态栏
- [x] 单测：路径映射 + 初始/增量/修改上传决策 + 断开不上传（headless，4 用例）

### 文件实时同步 - 反向（远程→本地）
- [x] 新增消息 SYNC_ADD / SYNC_REMOVE / SYNC_NOTIFY 及 SyncPair / SyncNotify 编解码
- [x] 主机 Host::addReverseSync：每对 (clientId, hostDir) 起 FileSyncManager 监控被控端目录，变化时改发 SYNC_NOTIFY（hostDir/hostFilePath/localDir/size/mtime）
- [x] 主机 Host::removeReverseSyncForClient：客户端断开清理监控
- [x] 控制器 RemoteController::sendSyncAdd/sendSyncRemove + 解码 SYNC_NOTIFY 发 syncNotifyReceived 信号
- [x] UI 新增「反向同步（远程→本地）」分组：添加/移除目录对、按相对路径下载到本地（自动建子目录）、重连自动重发
- [x] 单测：SyncPair/SyncNotify 协议往返 + 主机反向配对注册/清理（headless）

### 用户体验
- [x] F11全屏切换
- [x] Escape断开连接
- [x] 双击全屏

### 单元测试
- [x] ProtocolTest - 协议编解码测试
- [x] LoggerTest - 日志系统测试
- [x] SessionManagerTest - 会话管理测试
- [x] DeviceManagerTest - 设备管理测试
- [x] FileTransferManagerTest - 文件传输测试
- [x] SecurityManagerTest - 安全管理测试

### Web 客户端组件测试 (React SPA)
- [x] Jest + React Testing Library 测试框架搭建（babel-jest + jsdom，兼容 TS6/Vite）
- [x] protocol.test.ts — XRK 线协议编解码/校验往返（encode/decode/parseHeader/verifyChecksum/decodeScreenFrame/encodeMouseEvent/encodeKeyEvent）
- [x] ConnectionScreen.test.tsx — 连接表单默认值/本地输入/connect 写 store 并派发 xrk-connect/错误横幅/连接中禁用
- [x] Button.test.tsx — 可复用 Button 原语：variant(primary/secondary/connect/toolbar)/loading/disabled/className 合并
- [x] Toolbar.test.tsx — 工具栏显隐、缩放/画质 select 联动、断开连接、全屏切换
- [x] SettingsPanel.test.tsx — 设置面板显隐、保存写配置并关闭、取消还原并关闭、点遮罩关闭
- [x] RemoteDesktop.test.tsx — 画布按连接态显隐、鼠标/键盘事件认证后派发 xrk-mouse/xrk-key
- [x] App.test.tsx — 按连接状态在 ConnectionScreen↔RemoteDesktop+Toolbar 间切换、错误状态文案、设置面板
- [x] RemoteDesktop.integration.test.tsx — 端到端帧链路：connect→AUTH_RESP(会话密钥)→AES-256-CBC 解密→decodeScreenFrame→canvas 绘制（含"未认证帧被忽略"反例）
- [x] jsdom 环境桩补齐：TextEncoder/TextDecoder、requestFullscreen、crypto.subtle（Web Crypto）、WebSocket/Image/getContext，并把 store reset 包进 act()
- [x] 全量：**8 套件 / 53 用例全部通过**（`npm test`）

### UI/UX 优化 (React SPA)
- [x] **修复 index.css / App.css 主题与布局冲突**：index.css（main.tsx 最先加载）原本带一套浅/暗紫模板主题，并把 `#root` 锁死为 `1126px` 居中列 + 两侧 `border-inline`，与 App.css 的整屏 `100vw/100vh` 蓝色布局直接打架 → 画面溢出、边线悬空。已将 index.css 收敛为纯全局基底（html/body/#root 满高、color-scheme:dark、字体平滑），主题与布局唯一由 App.css 负责。
- [x] **构建修复（配套）**：tsconfig.app.json 的 `include:["src"]` 会把 `*.test.ts(x)` 拉进 `tsc -b` 生产类型检查，而 build 仅声明 `types:["vite/client"]` → `npm run build` 在 Phase 2 测试文件加入后必败。已为 build tsconfig 增加 `exclude` 测试文件（Vite 打包本就不含测试），`npm run build` 恢复通过。
- [x] **连接卡片品牌感**：connection-card 顶部增加渐变圆角品牌徽标（SVG 显示器图标），卡片阴影升级为 `--shadow-lg`，移动端徽标尺寸自适应。
- [x] **工具栏交互修正**：全屏按钮原本进出两种状态都用 `⛶`（复制粘贴 bug）→ 改为进 `⛶` / 出 `⤡` 区分；状态徽标从硬编码"已连接"改为随 `status` 动态显示「已连接/连接中…/未连接」并套用 `connected`/`connecting`/`disconnected` 三套配色。
- [x] **无障碍与动效**：全局 `:focus-visible` 键盘焦点环（按钮/输入/下拉），区分鼠标点击无环；新增 `@media (prefers-reduced-motion: reduce)` 关闭动画与 hover 位移；`.app-main` 增加径向渐变背景层次；`.connecting-overlay` 增加 `backdrop-filter` 模糊与排版间距。
- [x] `npm run build` 通过（tsc + vite，dist 产出）；`npm test` 53/53 全绿（改造未触碰任何测试的 title/label/role 断言）。

### 界面
- [x] 主窗口布局（左右分栏）
- [x] IP地址输入框
- [x] 端口输入框
- [x] 连接按钮
- [x] 启动服务/停止服务切换
- [x] 菜单栏（文件/帮助）
- [x] 工具栏
- [x] 状态栏

## 构建系统

| 项目 | 状态 |
|------|------|
| CMakeLists.txt | ✅ 根目录+各子目录 |
| build.bat | ✅ 一键编译 |
| deploy.bat | ✅ 一键部署+Qt依赖打包 |
| 编译通过 | ✅ Release构建成功 |
| 部署成功 | ✅ 依赖文件打包完成 |

## 代码规范

| 规范 | 状态 |
|------|------|
| C++17 | ✅ |
| 智能指针 | ✅ std::unique_ptr/std::shared_ptr |
| RAII | ✅ |
| 职责单一 | ✅ |
| cpp ≤500行 | ✅ |
| 函数 ≤80行 | ✅ |

## 功能完成度

| 功能模块 | 状态 |
|----------|------|
| 屏幕采集 | ✅ DXGI + GDI回退 |
| 屏幕传输 | ✅ JPEG 30fps |
| 屏幕显示 | ✅ 自适应缩放 |
| 输入控制 | ✅ 鼠标+键盘 |
| 设备发现 | ✅ UDP广播 |
| 连接认证 | ✅ 密码验证 |
| 断线重连 | ✅ 自动重连 |
| 文件传输 | ✅ 上传/下载/暂停/取消/断点续传 |
| 连接历史 | ✅ 保存/加载 |
| 性能优化 | ✅ 帧间差分 |
| 用户体验 | ✅ 快捷键 |
| 单元测试 | ✅ 核心模块 |
| 多线程架构 | ✅ 采集/编码/网络分离 |
| 剪贴板同步 | ✅ 文本/HTML/图片/URL |
| 多显示器 | ✅ 枚举/切换/指定采集 |
| H.264编码 | ✅ FFmpeg libx264, 超快+零延迟 |
| AES加密 | ✅ AES-256-CBC, 认证后密钥传输 |
| 远程终端 | ✅ QProcess, CMD/PowerShell/Bash |
| 远程截图 | ✅ SCREENSHOT_REQ/RESP, 文件保存 |
| 远程聊天 | ✅ CHAT_MESSAGE, ChatWidget广播 |
| 远程录像 | ✅ MJPEG AVI录制, RECORD_START/STOP |
| 高帧率60fps | ✅ 默认60fps, H.264优先, 帧丢弃, 可配置 |
| 跨平台支持 | ✅ Linux X11+XTest, macOS CoreGraphics+CGEvent |
| WOL远程唤醒 | ✅ 魔术包广播, MAC地址输入, 一键唤醒 |
| 隐私屏 | ✅ 远控时全屏黑屏遮挡, DXGI排除+输入穿透+即时生效 |
| Controller卡顿修复 | ✅ Decode Worker线程修复, worker context模式防止主线程阻塞 |
| 企业级设备管理 | ✅ DeviceRegistry设备注册表, RelayServer协议扩展, 设备在线状态跟踪 |
| 飞鸽传书基础 | ✅ IPMsgManager+微信风格UI+双实例+设备发现+表情面板+文件发送 |
| 飞鸽传书增强 | ✅ 消息收发BUG修复+图片消息+拖拽粘贴+文件接收对话框 |
| 飞鸽传书高级 | ✅ 消息引用回复+消息撤回+联系人详情 |
| 飞鸽传书完善 | ✅ 联系人搜索+最近聊天列表 |
| 飞鸽传书增强 | ✅ 右键菜单+拖拽优化+通知系统 |
| 飞鸽传书高级 | ✅ 群聊功能（创建群组+群消息） |
| 飞鸽传书完整 | ✅ 聊天背景+消息转发 |
| 飞鸽传书增强 | ✅ 已读回执+输入状态+图片查看器 |
| 飞鸽传书中级 | ✅ 好友申请+聊天记录搜索+群聊增强+免打扰+聊天记录导出 |
| 飞鸽传书可选 | ✅ 消息多选批量操作+聊天记录定期清理 |
| 飞鸽传书P0基础 | ✅ SQLite持久化+离线消息+端到端加密+文件传输优化 |
| 飞鸽传书基础设施 | ✅ SQLite持久化+离线消息+端到端加密+文件传输优化 |
| 飞鸽传书中级2 | ✅ 语音/视频/位置/名片消息+VoIP信令+通话UI+群聊进阶(@/公告/投票) |
| 飞鸽传书单元测试 | ✅ test_database_manager(17)+test_ipmsg_crypto(13)=30用例全通过 |
| 飞鸽传书E1多设备同步 | ✅ 同账号多PC同步: 快照协议(SYNC_REQUEST/SNAPSHOT/ACK)+LWW合并+密钥鉴权+同步UI+8新用例(38用例全通过) |
| 全量测试修复 | ✅ 全量189用例无崩溃(NetworkTest ARP悬垂this→QPointer) + 修复3个环境性失败(Microphone音量float EXPECT_NEAR / Clipboard先clear再setMimeData+轮询 / HostAuth先grantConsent再等AUTH_RESP), 全量188用例通过 |
| 文件断点续传Phase A4 | ✅ 文件断点续传: SHA-256校验和、偏移量追踪、会话ID、数据库持久化、协议编解码、单元测试 |
| 飞鸽传书UI图标优化 | ✅ IPMsgWidget工具栏/聊天头部按钮: 13个SVG图标替换emoji、QIcon集成、qrc注册、工具提示、图标尺寸统一 |
| 群组实时统计面板 | ✅ GroupStatisticsWidget: 核心指标(成员/在线/离线/管理员/活跃度/消息/文件/时长/活跃率)、活动图表、成员分布、性能指标、智能洞察、自动刷新、平滑动画、仅群聊显示 |
| 群组成员管理面板 | ✅ GroupMemberManagementWidget: 成员列表(头像/在线状态/角色徽标)、在线统计、角色管理(设/撤管理员/踢出)、私聊/资料、搜索过滤、邀请成员、空状态、列表动画、仅群聊显示 |
| 消息搜索增强 | ✅ ChatSearchWidget悬浮搜索面板 + Ctrl+F快捷键 + 内存消息存储 + QTextBrowser高亮匹配 + 上下导航 + 匹配计数 + 联系人切换自动清除搜索状态 |
| 群聊设置增强 | ✅ 消息免打扰开关(群级DND) + 查看群公告历史 + 分区UI(基本信息/成员管理/通知设置/快捷操作) |
| 消息多选增强 | ✅ 批量操作实际生效(内存删除/转发/复制) + 全选按钮 + 点击消息选择(select://链接) + 选中高亮 |
| 群公告置顶展示 | ✅ 进入群聊时自动在聊天顶部显示最新群公告(渐变卡片+左侧蓝色边框) |
| 面板布局修复 | ✅ 统计面板/成员管理面板插入rightLayout + 修复自引用bug + 投票选项bug修复 |
| 富文本消息渲染 | ✅ 语音消息卡片(绿色渐变+播放提示) + 视频消息卡片(紫色渐变+分辨率) + 位置消息卡片(蓝色渐变+地图链接) + 名片消息卡片(橙色渐变+添加好友) |
| 合并转发消息渲染 | ✅ 合并转发消息卡片(靛蓝渐变+JSON解析+折叠显示) + 信号连接 + 消息持久化 |
| 文件夹拖拽发送 | ✅ dropEvent支持文件夹拖拽识别 + emit sendFolder信号 + 文件夹消息显示 |
| 语音/视频通话类型区分 | ✅ IPMsgManager::initiateCall新增callType参数(voice/video/screen) + UI按钮正确传递类型 + 修复视频通话不可达问题 |
| 投票响应反馈 | ✅ 连接groupVoteResponseReceived信号 + onGroupVoteResponseReceived槽 + 聊天区显示系统消息"[用户 投票了: 选项X]" |
| 数据同步反馈 | ✅ 连接dataSynced信号 + onDataSynced槽 + 状态栏显示"多端数据同步完成" + 刷新联系人列表 |
| VoIP通话窗口增强 | ✅ 通话计时器(00:00格式每秒更新) + 静音/取消静音切换按钮 + 开启/关闭摄像头切换按钮 + 窗口扩展至320x180 + endCallInternal正确清理计时器 |
| Web客户端AES解密修复 | ✅ 新增crypto.ts: importSessionKey+decryptFrame(Web Crypto AES-256-CBC) + useWebSocket.ts集成AUTH_RESP提取密钥/IV、SCREEN_FRAME解密再decode + protocol.ts修正decodeScreenFrame字节偏移(dataSize@offset16, data@offset20) |
| Web客户端部署更新 | ✅ web-client/dist部署到resources/web + xrk.qrc已包含最新资源hash(index-ChLrSEVe.js/index-FtecoFfX.css) + 重新编译xrk.exe包含新Web资源 |
| 控制端黑屏修复 | ✅ 根因: consent授权流程阻断画面发送 (clients=0). 修复: 自动授权私有IP范围(127.0.0.1/8, 10.0.0.0/8, 192.168.0.0/16, 172.16.0.0/12) + 自动授权设置 + H264空帧自动回退JPEG + 增强日志 |
 
## 📊 群组实时统计面板 ✅
 
### 已完成
1. ✅ `GroupStatisticsWidget` 核心组件（group_statistics_widget.h/cpp）
2. ✅ 核心指标面板：总成员、在线、离线、管理员、活跃度、消息数、文件分享、平均时长、活跃率
3. ✅ 实时动画更新：数值变化时平滑淡入淡出+颜色过渡动画
4. ✅ 24小时活跃度趋势图：柱状图可视化
5. ✅ 成员分布可视化：在线/离线/管理员/普通成员分布
6. ✅ 性能指标面板：响应时间、吞吐量、错误率、在线率
7. ✅ 智能洞察：AI驱动的群组运营建议
8. ✅ 自动刷新：每5秒自动更新，支持手动刷新
9. ✅ 仅群聊显示：智能切换，私聊时自动隐藏
10. ✅ IPMsgWidget集成：聊天头部统计按钮、面板滑入/滑出、群切换自动更新数据
 
### 技术实现
- 新增文件：`src/ui/group_statistics_widget.h/cpp`
- 集成到 `IPMsgWidget`：统计按钮、面板切换、群组切换自动更新数据
- 动画系统：基于 QPropertyAnimation + QGraphicsOpacityEffect 的平滑过渡
- 自动定时器：QTimer 每5秒更新，支持启用/禁用动画
- 响应式布局：适配不同面板尺寸
 
### UI/UX 特性
- 暗色主题 + 玻璃态效果 + 渐变色彩
- 数值变化时平滑淡入淡出 + 颜色脉冲
- 悬停效果 + 悬停提示
- 响应式网格布局，自适应宽度
  
---

## 🎯 群组成员管理面板 ✅

### 已完成
1. ✅ `GroupMemberManagementWidget` 核心组件（group_member_management_widget.h/cpp）
2. ✅ 成员列表：头像（渐变底色+首字母）、在线状态点、名称、角色徽标（群主/管理员）、在线/离线状态
3. ✅ 实时在线统计：成员总数、在线数、在线进度条
4. ✅ 角色管理：右键菜单（设为/取消管理员、踢出群聊）
5. ✅ 快捷操作：私聊消息、查看资料
6. ✅ 成员搜索：关键字实时过滤（按名称）
7. ✅ 邀请成员：在线联系人选择对话框（排除已入群成员）
8. ✅ 空状态提示：无成员时友好提示
9. ✅ 列表项动画：数据变化时平滑过渡（淡入淡出）
10. ✅ IPMsgWidget集成：聊天头部成员管理按钮、面板切换、群切换自动更新

### 技术实现
- 新增文件：`src/ui/group_member_management_widget.h/cpp`
- 集成到 `IPMsgWidget`：成员管理按钮、`onMemberManagementToggled` 切换、`onGroupClicked` 群切换刷新、`selectContact` 私聊隐藏
- 数据来源：`IPMsgManager::getOnlineDevices()` 匹配群成员在线状态
- 邀请逻辑：`getOnlineDevices()` 过滤已在群成员，QInputDialog 选择

### UI/UX 特性
- 暗色主题 + 渐变背景
- 头像自适应颜色 + 首字母
- 角色徽标（群主橙色 / 管理员绿色）
- 状态点（在线绿色 / 离线灰色）
- 悬停高亮 + 右键上下文菜单
  
---

## 🎯 微信对标差距分析 (新增)

| 维度 | 微信 | IPMsg现状 | 差距等级 | 计划Phase |
|------|------|-----------|----------|-----------|
| **消息持久化** | 本地数据库+云端同步 | 仅内存存储 | 🔴 严重 | Phase A1 |
| **离线消息** | 服务器存储离线推送 | 必须在线 | 🔴 严重 | Phase A2 |
| **端到端加密** | 端到端加密 | 明文传输 | 🔴 严重 | Phase A3 |
| **文件断点续传** | 秒传/断点续传/大文件 | 基础TCP传输 | 🟠 高 | Phase A4 |
| **语音消息** | 录音/播放/转文字 | 无 | 🟠 高 | Phase B1 ✅ |
| **视频消息** | 视频压缩/预览/播放 | 无 | 🟠 高 | Phase B2 ✅ |
| **位置/名片** | 地图选点/联系人分享 | 无 | 🟡 中 | Phase B3/B4 ✅ |
| **合并转发** | 多条消息合并转发 | 单条转发 | 🟡 中 | Phase B5 ✅ |
| **语音通话** | 实时音视频通话 | 无 | 🔴 严重 | Phase C1 ✅ |
| **视频通话** | 实时音视频通话 | 无 | 🔴 严重 | Phase C2 ✅ |
| **屏幕共享** | 会议共享屏幕 | 远程桌面能力复用 | 🟡 中 | Phase C3 ✅ |
| **群@/公告** | @提醒/群公告置顶 | 无 | 🟡 中 | Phase D1 ✅ |
| **群文件/相册** | 群共享文件/图片墙 | 无 | 🟡 中 | Phase D2 ✅ |
| **群待办/投票** | 协作工具 | 无 | 🟢 低 | Phase D3 ✅ |
| **多设备同步** | 手机/电脑/网页/平板 | 仅单PC实例 | 🔴 严重 | Phase E1 |
| **网页/移动端** | Web/小程序/移动端 | 无 | 🟠 高 | Phase E2 |

---

## Phase B3/B4 - 位置/名片消息 ✅

### 已完成
1. ✅ `LOCATION_MSG` (147) / `LOCATION_ACK` (148) / `CARD_MSG` (149) / `CARD_ACK` (150) 消息类型定义（types.h）
2. ✅ `LocationMessage` 结构体（messageId, senderId, senderName, latitude, longitude, locationName, timestamp, isRead）
3. ✅ `CardMessage` 结构体（messageId, senderId, senderName, vCardData, timestamp, isRead）
4. ✅ 协议编解码 `encodeLocationMessage`/`decodeLocationMessage`、`encodeCardMessage`/`decodeCardMessage`（protocol_manager.h/cpp）
5. ✅ 位置/名片消息数据库存储（database_manager.h/cpp: location_messages/card_messages表, CRUD操作）
6. ✅ Host 端 LOCATION_MSG/CARD_MSG 处理：发送 ACK（host.cpp）
7. ✅ RemoteController 端 `sendLocationMessageProtocol`/`sendCardMessageProtocol` + 接收处理（remote_controller.h/cpp）
8. ✅ IPMsgManager 端 `sendLocationMessageProtocol`/`sendCardMessageProtocol` + `handleLocationMessage`/`handleCardMessage`（ipmsg_manager.h/cpp）
9. ✅ 单元测试（test_voice_messages.cpp：新增 10 个位置/名片消息用例，覆盖编解码往返、协议消息、类型验证）

### 位置消息功能说明
- 控制器发送位置（经纬度+名称） → 编码为 LocationMessage → 发送 LOCATION_MSG 协议消息
- 被控端收到 LOCATION_MSG → 回复 LOCATION_ACK
- 控制器收到 LOCATION_ACK → 确认消息送达
- 位置消息持久化到 SQLite（location_messages 表）

### 名片消息功能说明
- 控制器发送 vCard 格式联系人信息 → 编码为 CardMessage → 发送 CARD_MSG 协议消息
- 被控端收到 CARD_MSG → 回复 CARD_ACK
- 控制器收到 CARD_ACK → 确认消息送达
- 名片消息持久化到 SQLite（card_messages 表）

---

## Phase B5 - 合并转发 ✅

### 已完成
1. ✅ `MERGE_FORWARD` (151) / `MERGE_FORWARD_ACK` (152) 消息类型定义（types.h）
2. ✅ `ForwardedMessage` 结构体（messageId, senderId, senderName, content, timestamp, msgType）
3. ✅ `MergeForwardMessage` 结构体（messageId, senderId, senderName, messages[], timestamp, isRead）
4. ✅ 协议编解码 `encodeMergeForwardMessage`/`decodeMergeForwardMessage`（protocol_manager.h/cpp）
5. ✅ 合并转发消息数据库存储（database_manager.h/cpp: merge_forward_messages表, CRUD操作）
6. ✅ Host 端 MERGE_FORWARD 处理：发送 ACK（host.cpp）
7. ✅ RemoteController 端 `sendMergeForwardMessageProtocol` + 接收处理（remote_controller.h/cpp）
8. ✅ IPMsgManager 端 `sendMergeForwardMessageProtocol` + `handleMergeForwardMessage`（ipmsg_manager.h/cpp）
9. ✅ 单元测试（test_voice_messages.cpp：新增 5 个合并转发消息用例，覆盖编解码往返、协议消息、类型验证）

### 合并转发功能说明
- 控制器选择多条消息 → 编码为 MergeForwardMessage（包含 ForwardedMessage 列表） → 发送 MERGE_FORWARD 协议消息
- 被控端收到 MERGE_FORWARD → 回复 MERGE_FORWARD_ACK
- 控制器收到 MERGE_FORWARD_ACK → 确认消息送达
- 合并转发消息持久化到 SQLite（merge_forward_messages 表）
- 支持文本、图片、语音、视频、位置、名片等多种消息类型混合转发

---

## Phase B2 - 视频消息 ✅

### 已完成
1. ✅ `VIDEO_MSG` (145) / `VIDEO_ACK` (146) 消息类型定义（types.h）
2. ✅ `VideoMessage` 结构体（messageId, senderId, senderName, videoData, videoFileName, duration, width, height, timestamp, isRead）
3. ✅ 协议编解码 `encodeVideoMessage` / `decodeVideoMessage`（protocol_manager.h/cpp）
4. ✅ 视频消息数据库存储（database_manager.h/cpp: video_messages表, saveVideoMessage, loadVideoMessages, markVideoMessageRead）
5. ✅ Host 端 VIDEO_MSG 处理：发送 ACK（host.cpp）
6. ✅ RemoteController 端 `sendVideoMessageProtocol` 方法（remote_controller.h/cpp）
7. ✅ RemoteController 端 VIDEO_MSG/VIDEO_ACK 接收处理（remote_controller.cpp）
8. ✅ IPMsgManager 端 `sendVideoMessageProtocol` / `handleVideoMessage`（ipmsg_manager.h/cpp）
9. ✅ 单元测试（test_voice_messages.cpp：新增 5 个视频消息用例，覆盖编解码往返、协议消息、类型验证）

### 视频消息功能说明
- 控制器录制/选择视频 → 编码为 VideoMessage → 发送 VIDEO_MSG 协议消息
- 被控端收到 VIDEO_MSG → 回复 VIDEO_ACK
- 控制器收到 VIDEO_ACK → 确认消息送达
- 视频消息持久化到 SQLite（video_messages 表）
- 支持视频消息查询和标记已读

---

## Phase B1 - 语音消息 ✅

### 已完成
1. ✅ `VOICE_MSG` (143) / `VOICE_ACK` (144) 消息类型定义（types.h）
2. ✅ `VoiceMessage` 结构体（messageId, senderId, senderName, voiceData, voiceFileName, duration, timestamp, isRead）
3. ✅ 协议编解码 `encodeVoiceMessage` / `decodeVoiceMessage`（protocol_manager.h/cpp）
4. ✅ 语音消息数据库存储（database_manager.h/cpp: voice_messages表, saveVoiceMessage, loadVoiceMessages, markVoiceMessageRead）
5. ✅ Host 端 VOICE_MSG 处理：播放音频 + 发送 ACK（host.cpp）
6. ✅ RemoteController 端 `sendVoiceMessageProtocol` 方法（remote_controller.h/cpp）
7. ✅ RemoteController 端 VOICE_MSG 接收处理：播放音频 + 发送 ACK（remote_controller.cpp）
8. ✅ RemoteController 端 VOICE_ACK 接收处理（remote_controller.cpp）
9. ✅ `sendToClient` 辅助方法修复（host.h/cpp，用于 VOICE_ACK 发送）
10. ✅ 单元测试（test_voice_messages.cpp：5个用例，覆盖编解码往返、协议消息、类型验证）

### 语音消息功能说明
- 控制器录制音频 → 编码为 VoiceMessage → 发送 VOICE_MSG 协议消息
- 被控端收到 VOICE_MSG → 播放音频 → 回复 VOICE_ACK
- 控制器收到 VOICE_ACK → 确认消息送达
- 语音消息持久化到 SQLite（voice_messages 表）
- 支持语音消息查询和标记已读

---

---

## Phase C1 - 语音通话 (VoIP) ✅

### 已完成
1. ✅ `CALL_INVITE` (153) / `CALL_ACCEPT` (154) / `CALL_REJECT` (155) / `CALL_END` (156) / `ICE_CANDIDATE` (157) 消息类型定义（types.h）
2. ✅ VoIP 信令结构体：`CallInvite`、`CallAccept`、`CallReject`、`CallEnd`、`IceCandidate`
3. ✅ 协议编解码：`encode/decodeCallInvite`、`encode/decodeCallAccept`、`encode/decodeCallReject`、`encode/decodeCallEnd`、`encode/decodeIceCandidate`（protocol_manager.h/cpp）
4. ✅ Host 端 VoIP 信令处理：接收 CALL_INVITE/CALL_ACCEPT/CALL_REJECT/CALL_END/ICE_CANDIDATE 并发射信号（host.cpp）
5. ✅ RemoteController 端 VoIP 方法：`initiateCall`、`acceptCall`、`rejectCall`、`endCall`、`sendIceCandidate` + 接收处理（remote_controller.h/cpp）
6. ✅ 单元测试（test_voice_messages.cpp：新增 25 个 VoIP 信令用例，覆盖编解码往返、协议消息、类型验证）

### VoIP 信令功能说明
- 呼叫发起方发送 CALL_INVITE（含 SDP offer） → 被叫方收到 incomingCall 信号
- 被叫方接受发送 CALL_ACCEPT（含 SDP answer）→ 发起方收到 callAccepted 信号
- 被叫方拒绝发送 CALL_REJECT → 发起方收到 callRejected 信号
- 任意一方结束通话发送 CALL_END → 对方收到 callEnded 信号
- ICE 候选交换：双方发送 ICE_CANDIDATE → 对方收到 iceCandidateReceived 信号
- 支持语音/视频通话类型区分
- 支持 SDP 协商和 ICE 穿透

---

## Phase C2 - 视频通话 ✅

### 已完成
1. ✅ `VIDEO_CALL_START` (250) / `VIDEO_CALL_STOP` (251) / `VIDEO_CALL_FRAME` (252) / `VIDEO_CALL_ACK` (253) 消息类型定义（types.h）
2. ✅ 视频通话结构体：`VideoCallStart`、`VideoCallStop`、`VideoCallFrame`
3. ✅ 协议编解码：`encode/decodeVideoCallStart`、`encode/decodeVideoCallStop`、`encode/decodeVideoCallFrame`（protocol_manager.h/cpp）
4. ✅ Host 端视频通话处理：接收 VIDEO_CALL_START/VIDEO_CALL_STOP/VIDEO_CALL_FRAME 并发射信号（host.cpp）
5. ✅ RemoteController 端视频通话处理：接收并发射 videoCallStarted/videoCallStopped/videoCallFrameReceived 信号（remote_controller.h/cpp）
6. ✅ 单元测试（test_voice_messages.cpp：新增 12 个视频通话用例，覆盖编解码往返、协议消息、类型验证）

### 视频通话功能说明
- 复用 VoIP 信令（CALL_INVITE callType="video"）建立会话
- 发起方发送 VIDEO_CALL_START（含分辨率/帧率） → 接收方收到 videoCallStarted 信号
- 视频流通过 VIDEO_CALL_FRAME 传输（H.264 编码帧，含序列号、时间戳、关键帧标记、采集时间）
- 任意一方结束视频发送 VIDEO_CALL_STOP → 对方收到 videoCallStopped 信号
- 支持关键帧/非关键帧区分，支持序列号重排和时间戳同步
- 复用 VoIP 信令的 SDP 协商和 ICE 穿透

---

## Phase C3 - 屏幕共享 ✅

### 已完成
1. ✅ `SCREEN_SHARE_START` (254) / `SCREEN_SHARE_STOP` (255) / `SCREEN_SHARE_FRAME` (256) / `SCREEN_SHARE_ACK` (257) 消息类型定义（types.h）
2. ✅ 屏幕共享结构体：`ScreenShareStart`、`ScreenShareStop`、`ScreenShareFrame`
3. ✅ 协议编解码：`encode/decodeScreenShareStart`、`encode/decodeScreenShareStop`、`encode/decodeScreenShareFrame`（protocol_manager.h/cpp）
4. ✅ Host 端屏幕共享处理：接收 SCREEN_SHARE_START/SCREEN_SHARE_STOP/SCREEN_SHARE_FRAME 并发射信号（host.cpp）
5. ✅ RemoteController 端屏幕共享处理：接收并发射 screenShareStarted/screenShareStopped/screenShareFrameReceived 信号（remote_controller.h/cpp）
6. ✅ 单元测试（test_voice_messages.cpp：新增 12 个屏幕共享用例，覆盖编解码往返、协议消息、类型验证）

### 屏幕共享功能说明
- 复用 VoIP 信令（CALL_INVITE callType="screen"）建立会话，或直接发起屏幕共享
- 发起方发送 SCREEN_SHARE_START（含分辨率/帧率） → 接收方收到 screenShareStarted 信号
- 屏幕流通过 SCREEN_SHARE_FRAME 传输（H.264 编码帧，含序列号、时间戳、关键帧标记、采集时间）
- 任意一方结束共享发送 SCREEN_SHARE_STOP → 对方收到 screenShareStopped 信号
- 支持关键帧/非关键帧区分，支持序列号重排和时间戳同步
- 复用现有 H.264 编码器和 VoIP 信令的 SDP 协商、ICE 穿透
- 与远程桌面能力复用，实现会议式屏幕共享

---

## Phase D1 - 群@/公告/投票 ✅

### 已完成
1. ✅ `GROUP_ANNOUNCEMENT` (258) / `GROUP_MENTION` (259) / `GROUP_VOTE` (260) 消息类型定义（types.h）
2. ✅ 群聊高级结构体：`GroupAnnouncement`、`GroupMention`、`GroupVote`
3. ✅ 协议编解码：`encode/decodeGroupAnnouncement`、`encode/decodeGroupMention`、`encode/decodeGroupVote`（protocol_manager.h/cpp）
4. ✅ 数据库存储：`group_announcements`、`group_mentions`、`group_votes` 表，支持 CRUD 和索引
4. ✅ Host 端处理：接收 GROUP_ANNOUNCEMENT/GROUP_MENTION/GROUP_VOTE 并发射信号（host.cpp）
5. ✅ RemoteController 端接收处理：发射 groupAnnouncementReceived/groupMentionReceived/groupVoteReceived 信号（remote_controller.h/cpp）
6. ✅ IPMsgManager 端复用现有 JSON 实现，支持离线消息
7. ✅ 单元测试（test_voice_messages.cpp：新增 9 个群聊高级用例，覆盖编解码往返、协议消息、类型验证）

### 群聊高级功能说明
- **群公告**：群主/管理员发送 GROUP_ANNOUNCEMENT（群ID、群名、公告内容、发布者） → 群成员收到 groupAnnouncementReceived 信号
- **群@提及**：发送 GROUP_MENTION（群ID、群名、消息内容、@的成员ID/名称列表、发送者） → 被@成员收到 groupMentionReceived 信号
- **群投票**：发送 GROUP_VOTE（群ID、群名、投票标题、选项列表、持续时间、创建者） → 群成员收到 groupVoteReceived 信号
- 消息持久化到 SQLite，支持查询和标记已读
- 支持离线消息存储（复用现有 OfflineMessage 机制）
- 复用现有群组管理（IPMsgGroup、createGroup、inviteToGroup 等）

---

## Phase D2 - 群文件/相册 ✅

### 已完成
1. ✅ `GROUP_FILE` (261) / `GROUP_FILE_ACK` (262) / `GROUP_ALBUM` (263) / `GROUP_ALBUM_ACK` (264) 消息类型定义（types.h）
2. ✅ 群文件/相册结构体：`GroupFile`、`GroupAlbum`
3. ✅ 协议编解码：`encode/decodeGroupFile`、`encode/decodeGroupAlbum`（protocol_manager.h/cpp）
4. ✅ 数据库存储：`group_files`、`group_albums` 表，支持 CRUD 和索引
5. ✅ Host 端处理：接收 GROUP_FILE/GROUP_ALBUM 并发射 groupFileReceived/groupAlbumReceived 信号（host.cpp）
6. ✅ RemoteController 端接收处理：发射 groupFileReceived/groupAlbumReceived 信号（remote_controller.h/cpp）
7. ✅ 单元测试（test_voice_messages.cpp：新增 4 个群文件/相册用例，覆盖编解码往返、协议消息、类型验证）

### 群文件/相册功能说明
- **群文件**：上传文件到群 → 发送 GROUP_FILE（群ID、群名、文件ID、文件名、大小、MD5、上传者） → 群成员收到 groupFileReceived 信号
- **群相册**：创建相册 → 发送 GROUP_ALBUM（群ID、群名、相册ID、相册名、文件ID/名称列表、创建者） → 群成员收到 groupAlbumReceived 信号
- 消息持久化到 SQLite（group_files、group_albums 表）
- 支持文件元数据（大小、MD5、上传者、时间戳）
- 支持相册包含多个文件（文件ID/名称列表）
- 复用现有群组管理（IPMsgGroup）

---

## Phase D3 - 群待办/投票 ✅

### 已完成
1. ✅ `GROUP_TODO` (265) / `GROUP_TODO_ACK` (266) / `GROUP_TODO_UPDATE` (267) 消息类型定义（types.h）
2. ✅ `GroupTodo` 结构体（groupId, groupName, todoId, title, description, status, priority, assigneeId, assigneeName, creatorId, creatorName, dueDate, timestamp）
3. ✅ 协议编解码：`encode/decodeGroupTodo`（protocol_manager.h/cpp）
4. ✅ 数据库存储：`group_todos` 表，支持 CRUD、状态更新、索引
5. ✅ Host 端处理：接收 GROUP_TODO/GROUP_TODO_UPDATE 并发射 groupTodoReceived/groupTodoUpdated 信号（host.cpp）
6. ✅ RemoteController 端接收处理：发射 groupTodoReceived/groupTodoUpdated 信号（remote_controller.h/cpp）
7. ✅ 单元测试（test_voice_messages.cpp：新增 3 个群待办用例，覆盖编解码往返、协议消息、类型验证）

### 群待办功能说明
- **创建待办**：发送 GROUP_TODO（群ID、群名、待办ID、标题、描述、状态、优先级、指派人、创建者、截止日期） → 群成员收到 groupTodoReceived 信号
- **更新待办状态**：发送 GROUP_TODO_UPDATE（待办ID、新状态） → 群成员收到 groupTodoUpdated 信号
- 支持状态：0=待办、1=进行中、2=已完成
- 支持优先级：0=低、1=中、2=高
- 支持指派人、创建者、截止日期
- 消息持久化到 SQLite（group_todos 表，支持状态/群组索引）
- 复用现有群组管理（IPMsgGroup）

---

## 下一步执行计划

**当前**: Phase E1 - 多设备同步 ✅ 已完成
**目标**: 完成 Phase E2 - Web/移动端

### Phase E1 已完成
1. ✅ `SYNC_REQUEST` (203) / `SYNC_SNAPSHOT` (204) / `SYNC_ACK` (205) 消息类型定义（types.h）
2. ✅ `SyncRequest` / `SyncSnapshot` / `SyncAck` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 数据库存储：sync_requests/sync_snapshots/sync_acks 表 + SyncSnapshotRow/SyncRequestRow/SyncAckRow 结构体（database_manager.h/cpp）
4. ✅ LWW合并逻辑：buildSyncSnapshot() / applySyncSnapshot() - 基于 updatedAt 的最后写入胜出
5. ✅ IPMsgManager 端：setAccount/hasAccountConfigured/syncWith/sameAccountDevices + SYNC消息处理 + 同步信号（syncCompleted/syncFailed/sameAccountDeviceFound/dataSynced）
6. ✅ 单元测试：DatabaseManagerTest (5个同步用例) + IpmsgCryptoTest (2个快照序列化用例) 全绿

### Phase D3 已完成
1. ✅ 群待办协议（GROUP_TODO/GROUP_TODO_ACK/GROUP_TODO_UPDATE，MessageType 265-267）
2. ✅ GroupTodo 结构体及协议编解码
3. ✅ 数据库存储（group_todos 表，含状态/群组索引）
4. ✅ Host/RemoteController 端处理 + 信号发射
5. ✅ 单元测试（test_voice_messages.cpp：新增 3 个群待办用例全绿）

### Phase D2 已完成
1. ✅ 群文件/相册协议（GROUP_FILE/ALBUM，MessageType 261-264）
2. ✅ GroupFile/GroupAlbum 结构体及协议编解码
3. ✅ 数据库存储（group_files/group_albums 表）
4. ✅ Host/RemoteController 端处理 + 信号发射
5. ✅ 单元测试（test_voice_messages.cpp：新增 4 个群文件/相册用例全绿）

### Phase D1 已完成
1. ✅ 群公告/群@/群投票协议（GROUP_ANNOUNCEMENT/MENTION/VOTE，MessageType 258-260）
2. ✅ GroupAnnouncement/GroupMention/GroupVote 结构体及协议编解码
3. ✅ 数据库存储（group_announcements/group_mentions/group_votes 表）
4. ✅ Host/RemoteController 端处理 + 信号发射
5. ✅ 单元测试（test_voice_messages.cpp：新增 9 个群聊高级用例全绿）

### Phase C3 已完成
1. ✅ 屏幕共享协议（SCREEN_SHARE_START/STOP/FRAME/ACK，MessageType 254-257）
2. ✅ ScreenShareStart/ScreenShareStop/ScreenShareFrame 结构体及协议编解码
3. ✅ Host/RemoteController 端屏幕帧接收处理
4. ✅ 单元测试（test_voice_messages.cpp：新增 12 个屏幕共享用例全绿）

### Phase C2 已完成
1. ✅ 视频通话媒体协议（VIDEO_CALL_START/STOP/FRAME/ACK，MessageType 250-253）
2. ✅ VideoCallStart/VideoCallStop/VideoCallFrame 结构体及协议编解码
3. ✅ Host/RemoteController 端视频帧接收处理
4. ✅ 单元测试（test_voice_messages.cpp：新增 12 个视频通话用例全绿）

### Phase C1 已完成
1. ✅ VoIP 信令协议（CALL_INVITE/ACCEPT/REJECT/END/ICE，MessageType 153-157）
2. ✅ 5 个信令结构体及协议编解码
3. ✅ Host/RemoteController 端完整信令处理
4. ✅ 单元测试（test_voice_messages.cpp：新增 25 个 VoIP 用例全绿）

### Phase B5 已完成
1. ✅ 合并转发协议（MERGE_FORWARD / MERGE_FORWARD_ACK，MessageType 151/152）
2. ✅ ForwardedMessage/MergeForwardMessage 结构体
3. ✅ 合并转发消息数据库存储（merge_forward_messages 表）
4. ✅ Host 端处理 + ACK 回复
5. ✅ RemoteController 端发送/接收
6. ✅ IPMsgManager 端协议支持
7. ✅ 单元测试（test_voice_messages.cpp：新增 5 个用例全绿）

### Phase B3/B4 已完成
1. ✅ 位置消息协议（LOCATION_MSG / LOCATION_ACK，MessageType 147/148）
2. ✅ 名片消息协议（CARD_MSG / CARD_ACK，MessageType 149/150）
3. ✅ LocationMessage/CardMessage 结构体
4. ✅ 位置/名片消息数据库存储（location_messages/card_messages 表）
5. ✅ Host 端处理 + ACK 回复
6. ✅ RemoteController 端发送/接收
7. ✅ IPMsgManager 端协议支持
8. ✅ 单元测试（test_voice_messages.cpp：新增 10 个用例全绿）

### Phase B2 已完成
1. ✅ 视频消息协议（VIDEO_MSG / VIDEO_ACK，MessageType 145/146）
2. ✅ VideoMessage 结构体（含 width/height 元数据）
3. ✅ 视频消息数据库存储（video_messages 表）
4. ✅ Host 端 VIDEO_MSG 处理 + ACK 回复
5. ✅ RemoteController 端 `sendVideoMessageProtocol` + VIDEO_MSG/VIDEO_ACK 接收
6. ✅ IPMsgManager 端视频消息协议支持
7. ✅ 单元测试（test_voice_messages.cpp：新增 5 个视频消息用例全绿）

### Phase B1 已完成
1. ✅ 语音录制（AudioCapture Microphone 模式）
2. ✅ 语音消息协议（VOICE_MSG / VOICE_ACK，MessageType 143/144）
3. ✅ 语音播放（AudioPlayer）
4. ✅ 语音消息数据库存储（voice_messages 表）
5. ✅ 单元测试（test_voice_messages.cpp，5用例全绿）
