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
| SecurityManager | security_manager.h/cpp | 安全管理，设备ID/Token生成 |
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
| 文件传输 | ✅ 上传/下载 |
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
---

## 🎯 微信对标差距分析 (新增)

| 维度 | 微信 | IPMsg现状 | 差距等级 | 计划Phase |
|------|------|-----------|----------|-----------|
| **消息持久化** | 本地数据库+云端同步 | 仅内存存储 | 🔴 严重 | Phase A1 |
| **离线消息** | 服务器存储离线推送 | 必须在线 | 🔴 严重 | Phase A2 |
| **端到端加密** | 端到端加密 | 明文传输 | 🔴 严重 | Phase A3 |
| **文件断点续传** | 秒传/断点续传/大文件 | 基础TCP传输 | 🟠 高 | Phase A4 |
| **语音消息** | 录音/播放/转文字 | 无 | 🟠 高 | Phase B1 |
| **视频消息** | 视频压缩/预览/播放 | 无 | 🟠 高 | Phase B2 |
| **位置/名片** | 地图选点/联系人分享 | 无 | 🟡 中 | Phase B3/B4 |
| **合并转发** | 多条消息合并转发 | 单条转发 | 🟡 中 | Phase B5 |
| **语音通话** | 实时音视频通话 | 无 | 🔴 严重 | Phase C1 |
| **视频通话** | 实时音视频通话 | 无 | 🔴 严重 | Phase C2 |
| **屏幕共享** | 会议共享屏幕 | 远程桌面能力复用 | 🟡 中 | Phase C3 |
| **群@/公告** | @提醒/群公告置顶 | 无 | 🟡 中 | Phase D1 |
| **群文件/相册** | 群共享文件/图片墙 | 无 | 🟡 中 | Phase D2 |
| **群待办/投票** | 协作工具 | 无 | 🟢 低 | Phase D3 |
| **多设备同步** | 手机/电脑/网页/平板 | 仅单PC实例 | 🔴 严重 | Phase E1 |
| **网页/移动端** | Web/小程序/移动端 | 无 | 🟠 高 | Phase E2 |

---

## 下一步执行计划

**当前**: Phase A1 - SQLite持久化存储 (预估3天)
**目标**: 完成消息/联系人/群组/设置的本地持久化，支持启动加载历史
