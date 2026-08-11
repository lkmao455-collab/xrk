# XRK 局域网远程控制软件 - 修改记录

## 版本历史

### v1.8.0 — 基于角色的访问控制（RBAC）🚧 开发中
- **权限模型（`src/core/permission_model.{h,cpp}`）**：不依赖 Qt/Host 的纯计算模块，便于单测
  - `PermLevel`（None/Viewer/Operator/Admin）+ `Capability` 15 个能力位（查看屏幕/控制输入/剪贴板/读写文件/进程查看与管理/终端/系统信息/电源/通话/聊天/标注/录制/用户管理）
  - `evaluate(level, hostToggles, deviceOverride)`：角色默认能力 ∩ 主机全局开关 ∩ 设备级覆盖；`UserManage` 位不受主机开关关闭影响，避免管理员把自己锁死
- **协议 v2（`types.h` + `ProtocolManager`）**
  - `AUTH_REQ`(13) 扩展为 `[0x02][u8 userLen][user][u16 pwdLen][pwd]`；无用户名时保持旧版「裸访问码」载荷，老客户端零改动可用
  - `AUTH_RESP`(14) 在 `"OK" + key(32) + iv(16)` 之后追加 `[u8 level][u32 caps]`（50→55 字节），长度守卫解码，旧客户端忽略尾部
  - 新增消息：`USER_LIST_REQ/RESP`(210/211)、`PERMISSION_TOGGLE_REQ/RESP`(212/213)、`DEVICE_PERM_SET_REQ`/`DEVICE_PERM_RESP`(214/215)、`USER_ADD/REMOVE/UPDATE`(216/217/218)、`PERMISSION_DENIED`(219)、`AUDIT_LOG_REQ/RESP`(220/221)、`TEMP_GRANT_REQ/RESP`(222/223)
- **持久化（DatabaseManager schema v5）**：用户表（用户名/口令散列/级别/启用位/最后登录）+ 设备权限表（deviceId/level/capMask/备注）及对应 CRUD API
- **Host 门禁改写**：会话持有 `permLevel` + `caps`；所有特权操作经 `requireCap()` 校验，拒绝时回 `PERMISSION_DENIED`(219) 并写审计 `<op>_denied`
- **权限管理控制台（`PermissionConsoleDialog`）**：管理员一站式入口
  - 用户页（增删改、改级别、启停）、主机能力开关页、设备权限页（按控制端 IP 覆盖级别/能力掩码）
  - 「审计日志」按钮：经 `AUDIT_LOG_REQ`(220) 远程拉取被控端审计条目
  - 「临时授权」按钮：给某设备下发限时授权（级别 + 分钟数，0 分钟＝撤销）
- **限时授权（Temporary Grant）**：`TemporaryGrant{deviceId, level, capMask, expiresAt}` 叠加在持久化设备覆盖之上；Host 端定时器到期自动回收并刷新在线会话权限，审计 `temp_grant_set` / `temp_grant_clear`
- **实时审批工作流**：主机菜单「连接需本机审批」开启后，认证通过的会话先挂起在 `PermLevel::None`（无能力、不推帧），经本机批准后才落到目标级别；拒绝则记审计并断开；受信 IP 直接放行
- **审计日志排序修复**：`AuditLogger::entriesSince()` 跨日志文件轮转时会整体反转列表，导致返回的是最旧而非最新条目；改为逐文件「新→旧」拼接并按 limit 截断
- **Web 客户端 RBAC 对齐**
  - `types/protocol.ts` 补齐 210–223 消息号 + `PermLevel` / `Capability` / `ALL_CAPABILITIES`（与 C++ 侧逐位对齐）
  - `services/protocol.ts` 新增 `encodeAuthRequest`（v2/legacy 双路）、`decodeAuthResponse`（55 字节新格式 + 50 字节旧格式兼容）、`decodePermissionDenied`、`hasCapability` / `capabilityLabel` / `permLevelLabel` / `requiredCapabilityFor`
  - 新增 `services/messageBus.ts` 消息订阅机制（`subscribe` / `publish` / `ANY_MESSAGE`），`useWebSocket` 收包后统一 `publish`，新消息类型不必再改硬编码 switch
  - `store/connection.ts` 增加 `permLevel` / `capabilities` / `capsKnown` / `lastDenied` 与 `hasCap()`；`capsKnown=false`（旧版被控端）时不做客户端置灰，仍由 Host 权威裁决
  - UI：连接页新增可选「用户名」（留空＝访问码模式）、工具栏角色徽章（悬停显示已授予能力）、无「控制输入」能力时鼠标/键盘/触控全部禁用并显示「只读模式」、`PERMISSION_DENIED` 提示条（6 秒自动消失）
- **测试**：C++ 侧 704 项通过 / 1 跳过（缺 libx264 的 H264 编解码用例）；Web 侧 Jest 94 项通过（协议编解码 / 消息总线 / 能力门禁 / 角色徽章）

### v1.7.0 — Web 触控输入 / 快照导出 / 远程文件管理（读写）✅ 已发布 (2026-08-09)
- **Web 客户端触控输入（touchInput.ts）**：React Web 客户端新增 `TouchGestureController`（DOM 无关，便于单测）
  - 单击 → PRESS+RELEASE+CLICK（左键）；拖拽 → PRESS+MOVE…+RELEASE（无 CLICK）；长按（不移动，定时器）→ 右键（RELEASE 左 + PRESS 右 + RELEASE 右）+ 抑制 CLICK
  - 双指捏合（距离差）→ SCROLL；双指平移（中点 Y 差）→ SCROLL；双指松开转单指平滑恢复为单击按压
  - 协议复用既有 `MouseAction`（MOVE/CLICK/DOUBLECLICK/PRESS/RELEASE/SCROLL）+ `button`（LEFT/RIGHT/MIDDLE）
  - 单测 9 项（单击/拖拽/捏合/缩放/空操作/取消/长按右键/长按被拖拽取消/双指平移）全部通过
- **触控增强（RemoteDesktop.tsx + App.css）**：letterbox 感知的 `mapPoint` 坐标映射；`dispatchMouse` 派发 `xrk-mouse` 事件；触控工具栏（右键 / ▲ / ▼）与 `user-select:none`、`overscroll-behavior:none` 样式
- **标注快照导出（RemoteDesktopWidget::onSnapshotClicked）**：将当前帧 + 本地标注笔迹 + 可选水印合成 PNG（`QFileDialog::getSaveFileName`，默认 `xrk-snapshot-<时间戳>.png`），原生分辨率保存
- **远程文件管理器（读写）**：在既有文件浏览/分块上传下载之上新增写操作（需被控端 `consented` 授权 + 审计日志）
  - 协议：`FILE_OP_REQ`(162) / `FILE_OP_RESP`(163) + `FileOp`(Rename/Delete/Mkdir) + `FileOpRequest` / `FileOpResponse`
  - 编解码：`ProtocolManager::encode/decodeFileOpRequest`（op + UTF-8 path + newPath）、`encode/decodeFileOpResponse`（op + path + success + error）
  - Host：`handleFileOpRequest`（rename→`QFile::rename`、delete→`QDir::removeRecursively`/`QFile::remove`、mkdir→`QDir::mkpath`）+ 审计 `file_op_ok`/`file_op_failed`
  - 控制端：`RemoteController::sendFileOp` + `fileOpCompleted` 信号；`FileTransferWidget` 重命名/删除/新建文件夹按钮（删除前确认），成功后自动刷新目录
  - Host 高危门禁补强：`POWER_COMMAND`(158) 现强制 `consented`（与进程结束/启动一致）
  - 单测：`FileOpMessageTypeValues` / `Rename` / `Delete` / `Mkdir` / `Response` 往返

### v1.6.0 — 实时屏幕标注（端到端）✅ 已发布 (2026-08-09)
- **实时屏幕标注同步**：控制端画完自由笔标注即同步给被控端，在被控端物理屏以穿透式透明 overlay 显示，引导远端用户
  - 协议：`ANNOTATION_UPDATE`(183) / `ANNOTATION_CLEAR`(184) + 结构体 `AnnotationStroke`（颜色/线宽/点序列）、`AnnotationUpdate`（frame 宽高 + 笔迹集）
  - 协议编解码：`ProtocolManager::encodeAnnotationUpdate` / `decodeAnnotationUpdate`（BigEndian 二进制，与核心协议一致）
  - 新增 `AnnotationOverlay`：穿透点击的透明置顶窗口，跨所有屏幕，按帧坐标等比映射绘制
  - Host 仅 `authenticated` 即可接收，并写审计日志 `annotation` / `annotation_clear`；会话断开 / `stop()` / 收到 `ANNOTATION_CLEAR` 时销毁
  - 控制端 `RemoteDesktopWidget`：每笔标注携带独立颜色/线宽，鼠标松开即把当前完整笔迹集发给被控端；「清空」两端同步
  - 复用既有本地标注层（Phase 4）
  - 单测：`AnnotationUpdate` 往返（多笔 / 多色 / 多宽）+ 枚举值断言（183/184）

### v1.5.0 — 远程进程管理器
- **远程进程管理器**：控制端连接后可查看/结束/启动被控端进程
  - 协议：`PROCESS_LIST_REQ/RESP`(172/173)、`PROCESS_KILL_REQ/RESP`(174/175)、`PROCESS_START_REQ/RESP`(176/177)
  - 后端 `ProcessCollector`：跨平台进程枚举（Win `CreateToolhelp32Snapshot` / Linux `/proc` / macOS `sysctl`）、结束（`TerminateProcess`/`kill`）、启动（`QProcess::startDetached`）
  - Host 高危操作（结束/启动）强制 `consented` 门禁 + 审计日志 `process_kill` / `process_start`
  - UI `RemoteProcessWidget`：进程表格（PID/名称/内存）+ 3 秒轮询刷新 + 结束/启动按钮，挂载为「进程」分页

### v1.4.0 (2026-08-09) — 12项功能增强

#### 新增功能
- **黑名单联系人**：阻止联系人发消息
  - `blockUser()` / `unblockUser()` / `isBlocked()` API
  - 数据库 `blocked_users` 表持久化
  - `processTcpCommand` 中静默丢弃被阻止用户的消息
- **消息置顶**：群聊中置顶重要消息
  - `pinMessage()` / `unpinMessage()` / `isMessagePinned()` API
  - 数据库 `is_pinned` 字段
  - `loadPinnedMessages()` 查询已置顶消息
- **语音消息播放控件**：`VoicePlaybackWidget`
  - 播放/暂停按钮
  - 进度条拖拽
  - 速度选择器（1x / 1.25x / 1.5x / 2x）
  - 播放时间显示
- **审计日志查看器**：`AuditLogViewer`
  - 表格展示审计日志条目
  - 按类型/时间范围/关键字过滤
  - 导出为 JSON 文件
  - 清除旧日志（保留 30 天）
- **IP 黑名单 + 频率限制**：
  - `SecurityManager` 新增 `addBlacklistedIp()` / `removeBlacklistedIp()` / `isIpBlacklisted()`
  - `checkRateLimit()` / `recordFailedAttempt()` / `isIpLockedOut()` 频率限制
  - 数据库 `blacklisted_ips` 表
- **连接质量仪表板**：
  - `RemoteDesktopWidget` 新增可切换的统计覆盖层
  - 实时显示 FPS、带宽、延迟、编码格式、分辨率
  - 信息按钮切换显示/隐藏
- **会话录像回放**：`RecordingPlayer`
  - 解析 AVI 格式录像文件
  - 播放/暂停/停止控制
  - 进度条拖拽定位
  - 速度切换（0.5x / 1x / 1.5x / 2x）
- **聊天备份/恢复**：
  - `exportDatabase()` / `importDatabase()` API
  - 数据库完整导出/导入（所有表）
  - 一键恢复聊天记录
- **快捷键管理器**：`ShortcutManager` + `ShortcutManagerWidget`
  - 全局快捷键注册和自定义
  - 双击编辑快捷键
  - 恢复默认设置
  - QSettings 持久化
- **自动更新**：`Updater`
  - GitHub Releases API 版本检查
  - 当前版本对比（`QVersionNumber`）
  - 可配置自动检查间隔
  - 更新信息展示（版本号、变更日志、文件大小）
- **双因素认证 (2FA/TOTP)**：`TotpManager`
  - TOTP 密钥生成（Base32 编码）
  - 6 位动态验证码生成/验证
  - 30 秒时间窗口，支持 +/- 1 步容错
  - 备用恢复码（10 个 8 位数字码）
  - QR 码 URI 生成（`otpauth://` 协议）
- **无人值守访问**：
  - `Host` 新增 `setUnattendedAccessEnabled()` / `setUnattendedPassword()`
  - 持久化密码存储（SHA-256 哈希，QSettings）
  - 重启后保持密码，支持远程无人值守连接

#### 数据库变更
- 新增 `blocked_users` 表（device_id, reason, blocked_at）
- 新增 `blacklisted_ips` 表（ip, reason, blocked_at）
- `messages` 表新增 `is_pinned` 列（默认 0）
- 迁移版本升级至 4

#### 构建变更
- `xrk_app` 新增 `updater.cpp/h`、`totp_manager.cpp/h`
- `xrk_ui` 新增 `voice_playback_widget`、`audit_log_viewer`、`recording_player`、`shortcut_manager_widget`
- UI 链接新增 `Qt6::Multimedia`

### v1.3.0 (2026-08-08) — 6个新UI组件 + 代码质量修复

#### 新增 UI 组件
- **EmojiPickerWidget**：表情选择器
  - 7 个分类（Recent / Smileys / Gestures / Hearts / Animals / Food / Objects / Symbols）
  - 搜索栏过滤
  - 最近使用记录（最多 16 个）
  - 网格布局，每行 8 个表情
- **ContactCardWidget**：联系人名片卡
  - 头像（首字母自动填充）、姓名、IP、设备、备注
  - 在线状态显示、最后在线时间
  - 操作按钮：发送消息 / 语音通话 / 视频通话 / 编辑 / 删除
- **CallWidget**：语音/视频通话界面
  - 三态：来电 / 呼叫中 / 通话中
  - 通话计时器、静音 / 扬声器 / 视频切换
  - 脉冲动画（来电时）
- **GroupAnnouncementWidget**：群公告
  - 公告列表（按时间倒序）
  - 发布新公告、全部清除
- **GroupTodoWidget**：群待办事项
  - 创建待办（标题/描述/优先级）
  - 点击循环切换状态：Pending → In Progress → Completed
  - 颜色标识（灰色/绿色/深绿）
- **GroupVoteWidget**：群投票
  - 创建投票（标题 + 多选项）
  - 详情视图（投票数/百分比）
  - 提交投票

#### 类型定义
- 新增 `ContactInfo` 结构体（contactId, displayName, avatarPath, ipAddress, port, deviceName, note, online, lastSeen, groups）
- 新增 `EmojiReaction` 结构体（messageId, emoji, userId, userName, timestamp）

#### 代码质量修复
- 修复 `TcpConnection` 构造函数：pre-connected socket 现在正确设置 `ConnectionState::Connected`
- 修复 `ScreenCaptureTest.MonitorInfoDefaultValues`：移除 tautology 断言
- 修复 `RemoteControllerMonitorTest.DeviceInfoDefaultValues`：期望 `DEFAULT_PORT` 而非 0
- 修复 `SubnetScanner` 测试：添加 `setConnectTimeout(50)` 加速执行
- 修复 `TcpConnection` 测试：正确创建 client/server socket 对
- 修复 `ScreenCapture` 测试：headless 环境下跳过无显示器测试
- 添加 `SettingsWidget` 测试（24 个用例）

#### 测试结果
- 555 测试运行，554 通过，1 跳过（H264 编码器缺失）
- 新增 SettingsWidget 24 个测试用例

#### 修改文件
- `src/core/types.h`：新增 ContactInfo、EmojiReaction 结构体
- `src/ui/CMakeLists.txt`：注册 6 个新组件
- `src/ui/emoji_picker_widget.h/cpp`：新文件
- `src/ui/contact_card_widget.h/cpp`：新文件
- `src/ui/call_widget.h/cpp`：新文件
- `src/ui/group_announcement_widget.h/cpp`：新文件
- `src/ui/group_todo_widget.h/cpp`：新文件
- `src/ui/group_vote_widget.h/cpp`：新文件
- `src/core/tcp_connection.cpp`：修复构造函数状态初始化
- `tests/test_screen_capture.cpp`：修复 headless 测试
- `tests/test_tcp_connection.cpp`：修复 socket 对测试
- `tests/test_remote_controller_monitor.cpp`：修复默认值断言
- `tests/test_subnet_scanner.cpp`：加速测试执行
- `tests/test_settings_widget.cpp`：新文件（24 个测试）

### v1.2.0 (2026-08-08) — 多屏切换优化 + 开源协议

#### 多屏切换优化
- **线程安全**：`ScreenCapture` 添加 `QMutex` 保护，修复采集线程与主线程并发访问冲突
- **编码器分辨率适配**：切换显示器后自动重新初始化编码器，支持不同分辨率显示器
- **切换结果反馈**：新增 `MONITOR_SWITCH_ACK` 消息，控制器知道切换是否成功
- **快捷键支持**：
  - `Ctrl+1` ~ `Ctrl+9`：切换到指定显示器
  - `Ctrl+Tab`：循环切换到下一个显示器
  - `Ctrl+Shift+Tab`：循环切换到上一个显示器
- **切换过渡效果**：切换时保持最后一帧显示，收到新帧后淡入动画
- **UI增强**：显示器下拉框显示分辨率信息（如 `0 \\.\DISPLAY1 1920x1080 (主屏)`）
- **新增 `switchMonitorSafe` 方法**：线程安全的显示器切换，返回切换结果
- **热切换 DXGI Duplication**：保持 D3D 设备存活，仅替换 IDXGIOutputDuplication，消除黑屏间隙
- **显示器热插拔检测**：Host 每 5 秒检测显示器变化，自动广播 `MONITOR_LIST` 通知所有控制器
- **多屏自动轮巡**：控制器可启动自动轮巡，在多个显示器间定时切换
  - 可配置切换间隔（1-60秒）
  - 支持暂停/恢复：暂停后停留在当前屏幕，方便观察和干预
  - 实时状态显示：显示当前轮巡状态、所在屏幕、间隔时间
- **多屏缩略图预览**：点击"多屏预览"按钮，侧边栏显示其他屏幕的实时缩略图
  - 缩略图每秒自动更新
  - 点击缩略图即可切换到对应屏幕
  - 缩略图显示屏幕编号和分辨率

#### 设备发现增强
- **局域网主动扫描**：新增子网扫描器（SubnetScanner），主动探测局域网内所有XRK设备
  - 点击"扫描局域网"按钮启动扫描
  - 扫描进度条实时显示
  - 扫描结果自动添加到设备列表
  - 适用于UDP广播无法跨子网/防火墙的场景

#### UI体验增强
- **全屏显示优化**：F11切换全屏，全屏时工具栏自动隐藏
- **工具栏自动隐藏**：
  - 全屏模式下工具栏3秒后自动隐藏
  - 鼠标移到屏幕顶部时工具栏自动显示
  - 鼠标移开工具栏区域后重新开始计时隐藏
  - 带平滑动画过渡效果
- **工具栏半透明主题**：
  - 深色半透明背景（rgba(30, 30, 40, 200)）
  - 按钮悬停高亮效果
  - 选中状态蓝色高亮
  - 圆角设计（6px border-radius）
- **现代深色UI主题**：
  - 主背景深蓝色（#1a1a2e）
  - 缩略图面板半透明深色
  - 滚动条自定义样式
  - 切换overlay毛玻璃效果

#### 协议变更
- 新增消息类型：`MONITOR_SWITCH_ACK (62)`、`MONITOR_REFRESH (63)`
- 新增自动轮巡消息：`MONITOR_AUTO_SWITCH_START (64)`、`MONITOR_AUTO_SWITCH_STOP (65)`、`MONITOR_AUTO_SWITCH_PAUSE (66)`、`MONITOR_AUTO_SWITCH_RESUME (67)`、`MONITOR_AUTO_SWITCH_CONFIG (68)`、`MONITOR_AUTO_SWITCH_STATUS (69)`
- 新增缩略图消息：`MONITOR_THUMBNAIL_REQUEST (70)`、`MONITOR_THUMBNAIL_FRAME (71)`
- `MONITOR_SWITCH` 处理增强：发送 ACK + 重新初始化编码器
- `MONITOR_REFRESH` 处理：控制器可主动请求刷新显示器列表

#### 开源协议
- 完善 GPL v3 LICENSE 文件（完整版本）
- 更新 COMMERCIAL-LICENSE 商业授权协议（含 Starter/Professional/Enterprise 三级授权）
- 更新 LICENSE-INFO.md 双协议说明文档
- README.md 许可证章节更新为双授权对比表

#### 修改文件
- `src/hw/screen_capture.h/cpp`：添加 QMutex、switchMonitorSafe、switchDxgiOutput 方法
- `src/app/host.cpp`：MONITOR_SWITCH 处理增强、编码器重新初始化、显示器热插拔检测
- `src/app/host.h`：新增 m_monitorRefreshTimer、checkMonitorChanges、broadcastMonitorList
- `src/app/remote_controller.h/cpp`：新增 monitorSwitchCompleted 信号、MONITOR_SWITCH_ACK 处理、requestMonitorRefresh
- `src/ui/remote_desktop_widget.h/cpp`：过渡效果、快捷键、UI增强、工具栏自动隐藏、深色主题
- `src/core/types.h`：新增 MONITOR_SWITCH_ACK、MONITOR_REFRESH 消息类型
- `src/core/subnet_scanner.h/cpp`：新增子网扫描器
- `src/ui/device_list_widget.h/cpp`：扫描按钮、进度条
- `LICENSE`、`COMMERCIAL-LICENSE`、`LICENSE-INFO.md`：协议文档更新

### v1.1.0 (2026-08-07) — 弱网分块传输（Tiled Transport, Phases A–F）

#### 新增功能
- **分块屏幕传输**：整屏切 64×64 tile，仅对变化区域编码发送，弱网/高丢包下替代整帧重发
- **能力握手（Phase C）**：客户端鉴权后发 `SCREEN_KEYFRAME` 声明支持分块，主机切换分块模式
- **脏区检测（Phase A）**：DXGI Desktop Duplication 脏矩形 + 整帧 FNV-1a hash `hasFrameChanged` 兜底
- **逐 tile 内容分类编码（Phase D）**：纯色→RLE（无损）/ 照片→JPEG
- **可靠传输（Phase E）**：周期关键帧（IDR 强制全绘，10s）+ AIMD tile 预算自适应
- **精准 NACK 逐 tile 重传（Phase F）**：单 tile MD5 校验失败 → ≤100ms 批量 `SCREEN_TILE_REQUEST`
  → 主机 `EncodeWorker::resendTiles` 从 `m_lastRawFrame` 缓存精准重传
- **光标优先级**：发送前 `std::stable_partition` 把鼠标所在 tile 排到最前，弱网下操作区优先重绘

#### 协议变更
- 新增消息类型：`SCREEN_TILE (23)`、`SCREEN_KEYFRAME (24)`、`SCREEN_TILE_REQUEST (25)`
- `SCREEN_FRAME_ACK (21)` 负载由 Frame ID 扩展为 `ScreenAck`（RTT/丢包/缓冲）
- 新增结构体：`ScreenTile`、`ScreenTileRequest`、`ScreenAck`（`src/core/types.h`）
- 新增编解码：`encodeScreenTile/decodeScreenTile`、`encodeScreenAck/decodeScreenAck`、
  `encodeScreenTileRequest/decodeScreenTileRequest`（`src/core/protocol_manager.cpp`）

#### 测试
- `tests/test_tile_encoder.cpp`：9 个编码器/协议单元测试
- `tests/test_tiled_transport.cpp`：5 个真实 socket 回环集成测试（含丢包/NACK/光标优先级）
- 手动验证清单：`tests/TILED_TRANSPORT_CHECKLIST.md`
- 全量回归：401 PASSED，1 SKIPPED（H264 回环，沙箱缺 libx264）

### v1.0.0 (2026-01-01)

#### 初始版本

**新增功能**:
- 项目初始化
- 分层架构设计
- 目录结构创建
- CMake构建系统
- 头文件接口定义
- 文档体系建立

**模块清单**:

| 模块 | 状态 | 说明 |
|------|------|------|
| Core Layer | 接口定义 | NetworkManager, ProtocolManager等 |
| App Layer | 接口定义 | DeviceManager, SessionManager等 |
| UI Layer | 接口定义 | MainWindow, RemoteDesktopWidget等 |
| HW Layer | 接口定义 | ScreenCapture, InputControl等 |

**文件清单**:

```
E:\xrk\
├── CMakeLists.txt
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── types.h
│   │   ├── network_manager.h
│   │   ├── tcp_connection.h
│   │   ├── protocol_manager.h
│   │   ├── device_discovery.h
│   │   ├── message_codec.h
│   │   └── logger.h
│   ├── app/
│   │   ├── CMakeLists.txt
│   │   ├── device_manager.h
│   │   ├── session_manager.h
│   │   ├── remote_controller.h
│   │   ├── security_manager.h
│   │   └── file_transfer_manager.h
│   ├── ui/
│   │   ├── CMakeLists.txt
│   │   ├── main_window.h
│   │   ├── device_list_widget.h
│   │   ├── remote_desktop_widget.h
│   │   ├── file_transfer_widget.h
│   │   └── settings_widget.h
│   └── hw/
│       ├── CMakeLists.txt
│       ├── screen_capture.h
│       └── input_control.h
├── tests/
│   └── CMakeLists.txt
├── proto/
├── third_party/
└── docs/
    ├── 01_architecture.md
    ├── 02_module_design.md
    ├── 03_protocol.md
    ├── 04_database.md
    ├── 05_build.md
    ├── 06_test_plan.md
    └── 07_change_log.md
```

**待完成**:
- [ ] 各模块.cpp实现文件
- [ ] 单元测试用例
- [ ] 集成测试
- [ ] 性能优化

---

## 修改规范

### 提交信息格式
```
类型(范围): 简短描述

详细说明 (可选)
```

### 类型
- **feat**: 新功能
- **fix**: 修复bug
- **docs**: 文档更新
- **style**: 代码格式调整
- **refactor**: 重构
- **test**: 测试相关
- **chore**: 构建/工具相关
