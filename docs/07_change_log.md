# XRK 局域网远程控制软件 - 修改记录

## 版本历史

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
