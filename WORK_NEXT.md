✅ **实时屏幕标注 (v1.6.0)**:
  - 控制端「标注」开关后，自由笔迹实时同步到被控端屏幕（overlay 显示）
  - 协议：`ANNOTATION_UPDATE`(183) / `ANNOTATION_CLEAR`(184) + `AnnotationStroke`/`AnnotationUpdate` 结构体
  - 协议编解码：ProtocolManager::encodeAnnotationUpdate/decodeAnnotationUpdate（BigEndian 二进制）
  - 后端 `AnnotationOverlay`：透明、穿透输入的顶层覆盖层，覆盖全部物理屏幕，按 frame 坐标缩放绘制
  - Host：收到标注即惰性创建 overlay 并 `setStrokes()`；仅要求已鉴权（无需 consented）；会话断开 / stop() / CLEAR 时销毁
  - 控制端 `RemoteDesktopWidget`：每笔带独立颜色/线宽，松手即把完整笔迹集发给被控端；「清空」同步清除两端
  - 单测：AnnotationUpdate 往返 + 枚举值断言（test_protocol_extended.cpp）

✅ **多屏切换优化 (v1.2.0)**:
  - 线程安全：ScreenCapture添加QMutex保护并发访问
  - 编码器分辨率适配：切换后自动重新初始化编码器
  - 切换结果反馈：MONITOR_SWITCH_ACK消息
  - 快捷键：Ctrl+1-9切换指定显示器，Ctrl+Tab循环切换
  - 过渡效果：保持最后一帧+淡入动画
  - UI增强：显示器下拉框显示分辨率信息

✅ **Phase E2 Milestone 1**: WebSocket↔TCP网关 + 打包网页客户端页面
  - 新增文件: web_socket_gateway.h/.cpp, resources/web/client.html, qrc注册
  - 新增测试: test_web_socket_gateway.cpp (5用例, 全绿)
  - main.cpp `--ws` 无头网关模式
  - 全量273用例: 272通过+1环境跳过, 0失败
✅ **Phase E2 Milestone 2**: React SPA
  - 创建React应用 (web-client/)
  - 状态管理: zustand store (connection + UI)
  - 协议层: 类型安全的XRK wire协议 (TS接口)
  - 组件: ConnectionScreen, RemoteDesktop, Toolbar, SettingsPanel
  - 完整UI: 连接界面, 屏幕显示, 输入控制, 工具栏设置
  - 部署: `web-client/dist/` → `resources/web/`
  - 全量测试: 272通过+1环境跳过, 0失败

✅ **Phase E1 - 多设备同步**
  1. ✅ `SYNC_REQUEST` (203) / `SYNC_SNAPSHOT` (204) / `SYNC_ACK` (205) 消息类型定义（types.h）
  2. ✅ `SyncRequest` / `SyncSnapshot` / `SyncAck` 结构体及协议编解码（protocol_manager.h/cpp）
  3. ✅ 数据库存储：sync_requests/sync_snapshots/sync_acks 表 + SyncSnapshotRow/SyncRequestRow/SyncAckRow 结构体（database_manager.h/cpp）
  4. ✅ LWW合并逻辑：buildSyncSnapshot() / applySyncSnapshot() - 基于 updatedAt 的最后写入胜出
  5. ✅ IPMsgManager 端：setAccount/hasAccountConfigured/syncWith/sameAccountDevices + SYNC消息处理 + 同步信号（syncCompleted/syncFailed/sameAccountDeviceFound/dataSynced）
  6. ✅ 单元测试：DatabaseManagerTest (5个同步用例) + IpmsgCryptoTest (2个快照序列化用例) 全绿

✅ **IPMsgWidget UI图标优化 (飞鸽传书界面美化)**
  - 创建13个SVG图标: send.svg, file.svg, image.svg, emoji.svg, group.svg, background.svg, search.svg, export.svg, multi-select.svg, voice-call.svg, video-call.svg, sync.svg, refresh.svg
  - 更新 resources/xrk.qrc 注册新图标
  - IPMsgWidget.cpp 全量替换: 所有emoji按钮字符 → QIcon(":/icons/xxx.svg")
  - 统一图标尺寸(16x16/20x20)、工具提示、样式表清理
  - 构建通过、全量单测通过

✅ **群组实时统计面板**
  - 新增 GroupStatisticsWidget 核心组件 (group_statistics_widget.h/cpp)
  - 核心指标: 总成员/在线/离线/管理员/活跃度/消息数/文件分享/平均时长/活跃率
  - 实时动画: 数值平滑过渡、颜色脉冲、淡入淡出
  - 可视化: 24h活跃度柱状图、成员分布饼图、性能指标卡片
  - 智能洞察: AI驱动的运营建议
  - 自动刷新: 5秒间隔，启用/禁用动画开关
  - IPMsgWidget集成: 聊天头部统计按钮、面板切换、群切换自动更新
  - 仅群聊显示: 私聊时自动隐藏
  - 构建通过、全量单测通过

✅ **群组成员管理面板**
  - 新增 GroupMemberManagementWidget 核心组件 (group_member_management_widget.h/cpp)
  - 成员列表: 头像(渐变底色+首字母)、在线状态点、名称、角色徽标(群主/管理员)
  - 在线统计: 成员总数、在线数、在线进度条
  - 角色管理: 右键菜单(设为/取消管理员、踢出群聊)
  - 快捷操作: 私聊消息、查看资料
  - 成员搜索: 关键字实时过滤(按名称)
  - 邀请成员: 在线联系人选择对话框(排除已入群成员)
  - 空状态提示 + 列表项动画(淡入淡出)
  - IPMsgWidget集成: 聊天头部成员管理按钮、onMemberManagementToggled切换、群切换自动更新、私聊自动隐藏
  - 构建通过、全量单测通过

✅ **消息搜索增强 (ChatSearchWidget)**
  - 新增 ChatSearchWidget 悬浮搜索面板 (chat_search_widget.h/cpp)
  - Ctrl+F快捷键: 打开搜索面板，Esc关闭
  - 内存消息存储: ChatMessage结构体存储所有消息(发送者/内容/时间/类型/图片数据等)
  - 搜索匹配: 支持消息内容+发送者名称模糊匹配
  - 高亮显示: QTextBrowser::find()高亮匹配项
  - 上下导航: Up/Down键 + 按钮导航匹配项，循环遍历
  - 匹配计数: 显示"当前/总数"如 "3 / 12"
  - 刷新重建: refreshChatDisplay()从内存消息重建聊天记录+高亮
  - 联系人切换: 自动清除搜索状态+聊天消息
  - 直接渲染方法: addChatMessageDirect/addFileMessageDirect/addImageMessageDirect
  - 构建通过

✅ **群聊设置增强**
  - 消息免打扰开关: 群级DND，持久化到数据库
  - 查看群公告历史: 从数据库读取并展示当前公告
  - 分区UI: 基本信息/成员管理/通知设置/快捷操作 四大分区
  - 改进邀请成员: 使用getOnlineDevices()替代m_contacts
  - 构建通过

✅ **群公告置顶展示**
  - 进入群聊时自动在聊天顶部显示最新群公告
  - 渐变卡片样式(深蓝渐变+左侧蓝色边框)
  - 从数据库读取当前公告内容
  - 构建通过

✅ **输入状态提示增强**
  - 自动发送: 用户输入时自动发送typing indicator(1秒防抖)
  - 动画显示: 接收方显示"xxx 正在输入..."带绿色气泡背景
  - 自动隐藏: 4秒后自动消失
  - 构建通过

✅ **面板布局修复 + 投票bug修复**
  - 统计面板/成员管理面板: 插入rightLayout(修复不可见问题)
  - 修复自引用bug: statsPanelLayout->addWidget(m_statsPanel) → rightLayout->addWidget(m_statsPanel)
  - 投票对话框: 用户输入的选项现在正确读取(不再硬编码"同意/不同意")
  - 构建通过

✅ **富文本消息渲染**
  - 语音消息: 绿色渐变卡片+🎤图标+时长+播放提示
  - 视频消息: 紫色渐变卡片+🎬图标+时长+分辨率
  - 位置消息: 蓝色渐变卡片+📍图标+地名+地图链接
  - 名片消息: 橙色渐变卡片+👤图标+内容+添加好友提示
  - 构建通过

✅ **合并转发消息渲染**
  - 靛蓝渐变卡片+📨图标
  - JSON解析转发内容(发送者+消息)
  - 折叠显示(超长截断)
  - 信号连接(m_manager→widget)
  - 构建通过

✅ **文件夹拖拽发送**
  - dropEvent识别文件夹(QFileInfo::isDir)
  - 发送sendFolder信号
  - 显示文件夹消息(名称+/)
  - 构建通过

**所有任务已完成！**
1. ✅ 消息搜索增强 (ChatSearchWidget + Ctrl+F + 高亮 + 导航)
2. ✅ 群聊设置增强 (DND开关 + 公告查看 + 分区UI)
3. ✅ 消息多选批量操作 (删除/转发/复制实际生效 + 全选 + 点击选择)
4. ✅ 群公告置顶展示 (进入群聊自动显示最新公告)
5. ✅ 输入状态提示增强 (自动发送 + 动画 + 自动隐藏)
6. ✅ 面板布局修复 + 投票bug修复
7. ✅ 富文本消息渲染 (语音/视频/位置/名片卡片)
8. ✅ 合并转发消息渲染 (靛蓝卡片+JSON解析+折叠)
9. ✅ 文件夹拖拽发送 (dropEvent识别+sendFolder信号)
10. ✅ 语音/视频通话类型区分 (initiateCall+callType参数)
11. ✅ 投票响应反馈 (groupVoteResponseReceived信号连接)
12. ✅ 数据同步反馈 (dataSynced信号连接)
13. ✅ VoIP通话窗口增强 (计时器/静音/摄像头按钮)
14. ✅ Web客户端AES解密修复 (crypto.ts+protocol.ts偏移修正)
15. ✅ Web客户端部署更新 (dist→resources/web+qrc+xrk.exe重编译)
16. ✅ 控制端黑屏修复 (同意授权阻断画面发送: 自动授权私有IP + 自动授权设置 + H264空帧回退JPEG + 增强日志)

### Phase D3 已完成
1. ✅ `GROUP_TODO` (265) / `GROUP_TODO_ACK` (266) / `GROUP_TODO_UPDATE` (267) 消息类型定义（types.h）
2. ✅ `GroupTodo` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 数据库存储（group_todos 表，含状态/群组索引）
4. ✅ Host 端 GROUP_TODO/GROUP_TODO_UPDATE 处理 + 信号发射
5. ✅ RemoteController 端接收处理（groupTodoReceived/groupTodoUpdated）
6. ✅ 单元测试（test_voice_messages.cpp：新增 3 个用例，覆盖编解码往返、协议消息、类型验证）

### Phase D2 已完成
1. ✅ `GROUP_FILE` (261) / `GROUP_FILE_ACK` (262) / `GROUP_ALBUM` (263) / `GROUP_ALBUM_ACK` (264) 消息类型定义（types.h）
2. ✅ `GroupFile`/`GroupAlbum` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 数据库存储（group_files/group_albums 表，含索引）
4. ✅ Host 端 GROUP_FILE/GROUP_ALBUM 处理 + 信号发射
5. ✅ RemoteController 端接收处理（groupFileReceived/groupAlbumReceived）
6. ✅ 单元测试（test_voice_messages.cpp：新增 4 个用例，覆盖编解码往返、协议消息、类型验证）

### Phase D1 已完成
1. ✅ `GROUP_ANNOUNCEMENT` (258) / `GROUP_MENTION` (259) / `GROUP_VOTE` (260) 消息类型定义（types.h）
2. ✅ `GroupAnnouncement`/`GroupMention`/`GroupVote` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 数据库存储（group_announcements/group_mentions/group_votes 表，含索引）
4. ✅ Host 端 GROUP_ANNOUNCEMENT/MENTION/VOTE 处理 + 信号发射
5. ✅ RemoteController 端接收处理（groupAnnouncementReceived/groupMentionReceived/groupVoteReceived）
6. ✅ 单元测试（test_voice_messages.cpp：新增 9 个用例，覆盖编解码往返、协议消息、类型验证）

### Phase C3 已完成
1. ✅ `SCREEN_SHARE_START` (254) / `SCREEN_SHARE_STOP` (255) / `SCREEN_SHARE_FRAME` (256) / `SCREEN_SHARE_ACK` (257) 消息类型定义（types.h）
2. ✅ `ScreenShareStart`/`ScreenShareStop`/`ScreenShareFrame` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ Host 端 SCREEN_SHARE_START/STOP/FRAME 处理 + 信号发射
4. ✅ RemoteController 端屏幕帧接收处理（screenShareStarted/screenShareStopped/screenShareFrameReceived）
5. ✅ 单元测试（test_voice_messages.cpp：新增 12 个屏幕共享用例，覆盖编解码往返、协议消息、类型验证）

### Phase C2 已完成
1. ✅ `VIDEO_CALL_START` (250) / `VIDEO_CALL_STOP` (251) / `VIDEO_CALL_FRAME` (252) / `VIDEO_CALL_ACK` (253) 消息类型定义（types.h）
2. ✅ `VideoCallStart`/`VideoCallStop`/`VideoCallFrame` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ Host 端 VIDEO_CALL_START/VIDEO_CALL_STOP/VIDEO_CALL_FRAME 处理 + 信号发射
4. ✅ RemoteController 端视频帧接收处理（videoCallStarted/videoCallStopped/videoCallFrameReceived）
5. ✅ 单元测试（test_voice_messages.cpp：新增 12 个视频通话用例，覆盖编解码往返、协议消息、类型验证）

### Phase C1 已完成
1. ✅ `CALL_INVITE` (153) / `CALL_ACCEPT` (154) / `CALL_REJECT` (155) / `CALL_END` (156) / `ICE_CANDIDATE` (157) 消息类型定义（types.h）
2. ✅ VoIP 信令结构体：`CallInvite`、`CallAccept`、`CallReject`、`CallEnd`、`IceCandidate`
3. ✅ 协议编解码：`encode/decodeCallInvite`、`encode/decodeCallAccept`、`encode/decodeCallReject`、`encode/decodeCallEnd`、`encode/decodeIceCandidate`（protocol_manager.h/cpp）
4. ✅ Host 端 VoIP 信令处理：接收并发射 incomingCall/callAccepted/callRejected/callEnded/iceCandidateReceived 信号
5. ✅ RemoteController 端：`initiateCall`、`acceptCall`、`rejectCall`、`endCall`、`sendIceCandidate` + 接收处理
6. ✅ 单元测试（test_voice_messages.cpp：新增 25 个 VoIP 信令用例，覆盖编解码往返、协议消息、类型验证）

### Phase B5 已完成
1. ✅ `MERGE_FORWARD` (151) / `MERGE_FORWARD_ACK` (152) 消息类型定义（types.h）
2. ✅ `ForwardedMessage`/`MergeForwardMessage` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 合并转发消息数据库存储（database_manager.h/cpp: merge_forward_messages表）
4. ✅ Host 端 MERGE_FORWARD 处理 + ACK 回复（host.cpp）
5. ✅ RemoteController 端 `sendMergeForwardMessageProtocol` + 接收（remote_controller.h/cpp）
6. ✅ IPMsgManager 端 `sendMergeForwardMessageProtocol` / `handleMergeForwardMessage`（ipmsg_manager.h/cpp）
7. ✅ 单元测试（test_voice_messages.cpp：新增 5 个用例，覆盖编解码往返、协议消息、类型验证）

### Phase B3/B4 已完成
1. ✅ `LOCATION_MSG` (147) / `LOCATION_ACK` (148) / `CARD_MSG` (149) / `CARD_ACK` (150) 消息类型定义（types.h）
2. ✅ `LocationMessage`/`CardMessage` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 位置/名片消息数据库存储（database_manager.h/cpp: location_messages/card_messages表）
4. ✅ Host 端 LOCATION_MSG/CARD_MSG 处理 + ACK 回复（host.cpp）
5. ✅ RemoteController 端 `sendLocationMessageProtocol`/`sendCardMessageProtocol` + 接收（remote_controller.h/cpp）
6. ✅ IPMsgManager 端 `sendLocationMessageProtocol`/`sendCardMessageProtocol` + `handleLocationMessage`/`handleCardMessage`（ipmsg_manager.h/cpp）
7. ✅ 单元测试（test_voice_messages.cpp：新增 10 个用例，覆盖编解码往返、协议消息、类型验证）

### Phase B2 已完成
1. ✅ `VIDEO_MSG` (145) / `VIDEO_ACK` (146) 消息类型定义（types.h）
2. ✅ `VideoMessage` 结构体及协议编解码（protocol_manager.h/cpp）
3. ✅ 视频消息数据库存储（database_manager.h/cpp: video_messages表）
4. ✅ Host 端 VIDEO_MSG 处理 + ACK 回复（host.cpp）
5. ✅ RemoteController 端 `sendVideoMessageProtocol` + VIDEO_MSG/VIDEO_ACK 接收（remote_controller.h/cpp）
6. ✅ IPMsgManager 端 `sendVideoMessageProtocol` / `handleVideoMessage`（ipmsg_manager.h/cpp）
7. ✅ 单元测试（test_voice_messages.cpp：新增 5 个视频消息用例，覆盖编解码往返、协议消息、类型验证）

### Phase B1 已完成
1. ✅ 语音录制（AudioCapture Microphone 模式）
2. ✅ 语音消息协议（VOICE_MSG / VOICE_ACK，MessageType 143/144）
3. ✅ 语音播放（AudioPlayer）
4. ✅ 语音消息数据库存储（voice_messages 表）
5. ✅ 单元测试（test_voice_messages.cpp，5用例全绿）

1. **部署 React SPA** (已完成): React 应用已打包到 `web-client/dist/`
2. **构建 XRK** (已完成): web-client/dist → resources/web，qrc 注册，重新编译 xrk.exe 并打包
3. **启动网关** (已完成): `xrk --ws 8080` 无头模式，ws://*:8080 + http://*:8081 监听正常
4. **浏览器测试** (已完成): http://127.0.0.1:8081/ 正确返回 SPA 及 /assets/*.js|css、/favicon.svg
5. **组件测试** (✅ 已完成): 为 React 应用添加 Jest + React Testing Library 测试
   - 8 个测试套件、53 个用例全绿：`protocol`（线协议编解码/校验）、
     `ConnectionScreen`、`Button`、`Toolbar`、`SettingsPanel`、`RemoteDesktop`、
     `App`，以及 `RemoteDesktop.integration`（connect→AUTH_RESP→AES 解密→
     decodeScreenFrame→canvas 绘制的端到端帧链路）。
   - 配套补齐 jsdom 环境桩：TextEncoder/TextDecoder、requestFullscreen、
     crypto.subtle（Web Crypto）、WebSocket/Image/getContext，并把 store reset
     包进 `act()`。
6. **UI/UX 优化** (✅ 已完成本轮): 详见 WORK_DONE.md「UI/UX 优化 (React SPA)」
   - 修复 index.css/App.css 主题+布局冲突（#root 1126px 居中列 → 收敛为全局基底）
   - 修复 `npm run build` 因测试文件被拉入 tsc 生产检查而失败（tsconfig.app.json exclude 测试）
   - 连接卡片品牌徽标、工具栏全屏图标区分/状态徽标动态化、`:focus-visible` 焦点环、
     `prefers-reduced-motion` 降级、app-main 渐变背景、`connecting-overlay` 排版
   - `npm run build` 通过、`npm test` 53/53 全绿
   - ✅ 已部署：新 dist 已同步到 `resources/web`，`xrk.qrc` 的 `web/*` 条目与产物哈希一致，
     并已重新编译 `xrk.exe`（RCC 重编译 `qrc_xrk.cpp`，资源已嵌入）。`dist` / `resources/web` /
     `xrk.qrc` 三处哈希（index-ChLrSEVe.js、index-FtecoFfX.css）完全匹配，网关现服务最新资源。
7. **XRK 集成** (✅ 已完成): 将 React SPA 集成到 XRK 主应用
   - `MainWindow` 新增「打开 Web 控制台」菜单项（工具菜单 + 系统托盘菜单），点击时**按需**启动内置 `WebSocketGateway`（ws `:8080` → 桥接本地 Host `127.0.0.1:9999`，HTTP 页面服务 `:8081`），并用默认浏览器打开 `http://localhost:8081/`。
   - 网关懒启动、不常驻端口；若 8080 被占用（如已有 `--ws` 网关）则弹窗提示而不崩溃。
   - 浏览器端 SPA 经 WebSocket 连回本机 Host，复用既有的认证/同意/画面流与输入链路，无需额外部署。

**已修复的阻断性 Bug（本次集成发现）**:
- 网关 HTTP 服务器原本只处理 `GET /`，导致 `/assets/*` 与 `/favicon.svg` 全部 404，浏览器白屏。
  已改为把 URL 路径映射到 Qt 资源 `:/web/<path>`，并按扩展名返回正确 Content-Type，
  同时拒绝 `..` 路径穿越。单元测试 `HttpServesWebPage` 已扩展为同时校验资源与 favicon 返回 200。
- 网关单测 `HttpServesWebPage` 原断言页面含字面量 "WebSocket"（旧 client.html 才有），
  React SPA 的 index.html 不含该词，已改为断言引用 `/assets/` 资源（更稳健、抗 hash 变化）。

**全量单元测试**: `WebSocketGatewayTest` 5/5 通过（含资源服务回归校验）。

**端到端说明**: 完整远程桌面流需同时有 Host 实例监听 127.0.0.1:9999（主界面 GUI 启动服务）。

**新增 `--host <port>` 无头模式（本次集成加入）**:
- `main.cpp` 新增与 `--ws` / `--relay` 同构的 `xrk --host 9999` 模式：以 QCoreApplication 启动
  Host，监听 TCP+UDP 9999，并自动批准 consentRequested（无人值守本地测试用）。
- Host 仅依赖 QImage/qApp（条件使用），可在 QCoreApplication 下运行，无需 GUI/显示器。
- 用途：配合 `xrk --ws 8080` 网关，让浏览器/移动端在无交互桌面的环境下完成端到端链路验证。
- ⚠️ 安全提示：该无头 Host 自动批准所有连接请求，仅供本地（127.0.0.1）测试；生产环境请用主界面 GUI 的人工批准。

**端到端验证（已通过）**:
- 网关 `xrk --ws 8080`（PID 29208）+ 无头 Host `xrk --host 9999`（PID 8880）同启。
- Python websocket 客户端经 ws://127.0.0.1:8080 发送 AUTH_REQ → Host 返回 AUTH_RESP "OK"
  （认证通过 + consent 自动批准）→ 随后持续收到二进制流（59 条 / 3.58 MB / 5s，即 AES 加密的
  SCREEN_FRAME 画面流；密钥在 AUTH_RESP 中交换，由 React 客户端解密）。
- 结论：浏览器 → 网关 → Host 的完整链路（认证/批准/画面流）已打通。

**优先级**: React SPA 部署、浏览器集成、组件测试均已完成；下一步为 UI/UX 优化 / XRK 主应用集成

---

## 🖥️ 浏览器实时画面显示（本次测试发现并修复的阻断性 Bug）

**现象**: 网关桥接、认证、consent 全部正常，但浏览器画面空白（canvas 无内容）。

**根因（两个代码 Bug）**:
1. **SPA 缺失 AES 解密**：Host 对每个 SCREEN_FRAME 载荷做 AES-256-CBC 加密（密钥在
   AUTH_RESP 中下发：`"OK"` + 32 字节 key + 16 字节 iv）。但 web-client 此前**完全没有解密逻辑**，
   直接把密文喂给 `decodeScreenFrame`，读到的是乱码（宽高/格式全错）→ 无法渲染。
2. **`decodeScreenFrame` 字节偏移错误**：C++ `encodeScreenFrame` 的线格式为
   `width(4) height(4) format(4) timestamp(4) dataSize(4) data(...)`，但 SPA 旧代码把
   `dataSize` 读在 offset 20、data 读在 offset 24（漏掉了 timestamp 字段）。

**修复**:
- 新增 `web-client/src/services/crypto.ts`：`importSessionKey` + `decryptFrame`（Web Crypto
  AES-256-CBC，浏览器安全上下文 localhost 可用）。
- `useWebSocket.ts`：AUTH_RESP 时提取 key/iv 并 `importKey`；SCREEN_FRAME 先解密再 `decodeScreenFrame`；
  `connect()` / `onclose` 时清空会话密钥。
- `protocol.ts` `decodeScreenFrame`：`dataSize` 改读 offset 16、data 改读 offset 20。
- `web-client` 重新构建（资源 hash 变为 `index-Dmkz-rPE.js`），同步更新 `resources/xrk.qrc`，
  重新编译并 `deploy` 打包 XRK。

**验证**:
- SPA `npm run build` 通过（tsc + vite）。
- 端到端实测（Python websocket 客户端，复刻浏览器逻辑）：
  - 连 `ws://127.0.0.1:8080` → AUTH_RESP `"OK"` 且成功提取 key/iv ✓
  - 握手完整：CONSENT_REQUEST(190) / CONSENT_RESPONSE(191) / QUALITY_INFO(22) 均收到 ✓
  - **AES 加解密闭环**：用 Host 完全相同的 AES-256-CBC+PKCS7 加密一个 ScreenFrame，再按 SPA 方式
    解密并解析，字节完全一致（w=1920 h=1080 fmt=0）→ 证明浏览器渲染路径正确 ✓
- **环境限制（非代码问题）**：本无头/非交互会话中 DXGI 桌面复制与 GDI 均无法抓取桌面
  （`SCREENSHOT_REQ` 探针返回空，确认 `captureFrame()` 返回 null）。因此当前环境**不会产出画面帧**，
  浏览器会显示"已连接"但画面为空白——这是会话限制，不是渲染 Bug。

**如何看到真实画面**: 需在能抓取桌面的**交互式桌面会话**中运行 Host：
- 方式 A：启动 GUI `xrk.exe` → 点"启动服务"（运行在用户桌面会话，DXGI/GDI 可捕获）；
- 方式 B：在普通命令行（非本 agent 的无头 shell）执行 `xrk.exe --host 9999`。
  网关 `--ws 8080` 可继续无头运行。两者就绪后浏览器打开 `http://127.0.0.1:8081/` 即可看到实时画面。

**当前运行实例**: 网关 PID 24004（`--ws 8080`）+ 无头 Host PID 16128（`--host 9999`）已启动，
SPA 已含解密修复；仅因环境无法抓屏而无画面流。

---

✅ **v1.4.0 功能增强 (2026-08-09)**: 12项新功能 + 构建修复

1. **黑名单联系人** — `blockUser()`/`unblockUser()` API + 数据库 `blocked_users` 表 + 消息过滤
2. **消息置顶** — `pinMessage()`/`unpinMessage()` API + `is_pinned` 字段 + `loadPinnedMessages()` 查询
3. **语音消息播放控件** — `VoicePlaybackWidget`（播放/暂停/进度条/速度选择/时间显示）
4. **审计日志查看器** — `AuditLogViewer`（表格/过滤/导出/清除）
5. **IP 黑名单 + 频率限制** — `SecurityManager` 新增 `checkRateLimit()`/`recordFailedAttempt()`/`isIpLockedOut()` + `blacklisted_ips` 表
6. **连接质量仪表板** — `RemoteDesktopWidget` 可切换统计覆盖层（FPS/带宽/延迟/编码/分辨率）
7. **会话录像回放** — `RecordingPlayer`（AVI 解析/播放控制/进度条/速度切换）
8. **聊天备份/恢复** — `exportDatabase()`/`importDatabase()` API
9. **快捷键管理器** — `ShortcutManager` + `ShortcutManagerWidget`（注册/自定义/恢复默认/持久化）
10. **自动更新** — `Updater`（GitHub Releases API/版本对比/自动检查/更新信息）
11. **双因素认证 (2FA/TOTP)** — `Tot pManager`（密钥生成/验证码/备用码/QR URI）
12. **无人值守访问** — `Host` 持久密码存储（SHA-256 + QSettings）

**数据库**: 新增 `blocked_users`、`blacklisted_ips` 表，`messages.is_pinned` 列，迁移版本 4
**构建**: `xrk_app` +4 文件，`xrk_ui` +4 文件，UI 链接 `Qt6::Multimedia`
**编译**: 全量编译通过，633 测试框架就绪

---

✅ **v1.5.0 功能增强 (2026-08-09)**: 远程进程管理器

1. **远程进程列表** — `PROCESS_LIST_REQ/RESP`(172/173) + `ProcessCollector::collectProcessList()`（跨平台枚举）
2. **结束进程** — `PROCESS_KILL_REQ/RESP`(174/175) + `ProcessCollector::killProcess()`，需 `consented` + 审计
3. **启动进程** — `PROCESS_START_REQ/RESP`(176/177) + `ProcessCollector::startProcess()`，需 `consented` + 审计
4. **进程管理器 UI** — `RemoteProcessWidget`（进程表格 PID/名称/内存 + 3s 轮询 + 结束/启动按钮），挂载「进程」分页
5. **单元测试** — `test_protocol_extended.cpp` 增加进程协议 round-trip 与枚举值断言

**后端**: 新增 `src/app/process_collector.{h,cpp}`（注册 `src/app/CMakeLists.txt`，WIN32 链接 `psapi.lib`）
**UI**: 新增 `src/ui/remote_process_widget.{h,cpp}`（注册 `src/ui/CMakeLists.txt`），`MainWindow` 新增 `PAGE_PROCESS` 分页
**安全**: 列进程仅需 `authenticated`；结束/启动强制 `consented` 门禁 + `logAuditOp`，全程不触碰 `XRK_ENABLE_SILENT` 后门