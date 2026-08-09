# XRK 局域网远程控制软件

XRK 是一款面向**局域网**环境的远程控制软件（类似向日葵 / TeamViewer 的局域网版），
基于 Qt6 + C++17 实现，内置飞鸽传书（IPMsg）风格的局域网文件传输与远程桌面控制。

## 主要功能

- **远程桌面控制**：实时查看并控制对端桌面，支持鼠标 / 键盘输入。
- **局域网文件传输**：基于 IPMsg 协议的发现与文件收发。
- **弱网分块屏幕传输（Tiled Transport）**：在丢包 / 高延迟链路上，把屏幕切成
  64×64 的 tile，只对变化区域编码发送，并对每个 tile 做 MD5 完整性校验。包含
  - 脏区检测（DXGI 脏矩形 + `hasFrameChanged` 整帧 hash 兜底）
  - 逐 tile 内容分类编码（纯色 → RLE 无损 / 照片 → JPEG）
  - 可靠传输（周期关键帧 IDR + AIMD tile 预算自适应）
  - **精准 NACK 逐 tile 重传**（单 tile 损坏只重传该 tile，而非整屏）
  - **光标优先级**（鼠标所在 tile 优先发送，弱网下操作区先重绘）
- **多种编码**：JPEG（默认）/ H.264（可选，游戏/低延迟档位）。
- **加密传输**：屏幕帧与消息均经 AES 加密，密钥在鉴权握手时下发。
- 双向语音、远程终端、屏幕截图、剪贴板同步、群聊等增强能力。
- **多屏切换**：热插拔检测、DXGI 热切换、自动轮巡、缩略图预览。
- **群聊增强**：公告发布、待办事项、群投票。
- **通话**：语音/视频通话（ICE 协商 + WebRTC 信令）。
- **表情 & 通讯录**：Emoji 选择器、联系人名片卡。
- **局域网扫描**：子网主动扫描 + 可配置超时。
- **远程进程管理器（v1.5.0）**：连接被控端后查看进程列表、结束进程、启动进程；结束/启动需被控端授权并写入审计日志。
- **实时屏幕标注（v1.6.0）**：远程协助时控制端在屏幕上自由画笔，标注实时同步并在被控端屏幕上以透明覆盖层显示（不捕获输入、不读取数据，仅已鉴权会话可用），「清空」同步清除两端。
- **安全与运维增强（v1.4.0）**：黑名单联系人、消息置顶、IP 黑名单 + 频率限制、审计日志查看器、连接质量仪表板、会话录像回放、聊天备份/恢复、快捷键管理器、自动更新、双因素认证（TOTP）、无人值守访问。


## 架构概览

分层架构：`UI → Application → Core → Hardware`，模块职责单一、接口驱动。

- **UI 层**：Qt Widgets（`MainWindow` / `RemoteDesktopWidget` / `EmojiPickerWidget` 等 24 个组件）
- **应用层**：`RemoteController`（控制端）、`Host` + `EncodeWorker`（被控端）、
  `DeviceManager` / `SessionManager` / `FileTransferManager` / `IPMsgManager` 等
- **核心层**：`NetworkManager` / `TcpConnection` / `ProtocolManager` / `MessageCodec`
- **硬件层**：`ScreenCapture`（DXGI Desktop Duplication）/ `InputControl` /
  `TileEncoder`（分块编码核心）/ `H264Encoder` / `JpegEncoder`

弱网分块传输的分层数据流与各阶段设计要点，见 [docs/02_module_design.md](docs/02_module_design.md)
第 5 节；协议格式（`SCREEN_TILE` / `SCREEN_KEYFRAME` / `SCREEN_TILE_REQUEST` /
`ScreenAck` 等）见 [docs/03_protocol.md](docs/03_protocol.md) 第 3.3.1 / 3.7 节。

## 快速开始

### 构建

环境要求：MSVC 2019+ / GCC 9+，CMake 3.20+，Qt 6.2+（含 Network / Widgets / Gui）。

```bash
# Windows（以 VS2022 + Qt 6.10.0 为例）
cd E:\xrk
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target xrk
# 可执行文件：build/src/Release/xrk.exe
```

详细步骤（含 Linux、Qt Creator、依赖库）见 [docs/05_build.md](docs/05_build.md)。

### 运行与部署

- Host（被控端）需在**交互式桌面会话**中启动（无头 / 服务器会话抓不到桌面，属环境限制）。
- 控制端能看桌面的关键运行时依赖是 `imageformats/qjpeg.dll`（JPEG 解码，分块模式下照片类
  tile 同样依赖它）。完整部署流程与"黑屏"排查清单见
  [docs/05_build.md](docs/05_build.md) 第 7 节，以及 `FIX_DESKTOP_NOT_VISIBLE.md`。

### 测试

项目使用 Google Test。分块传输相关自动化测试可在无 GUI（headless）环境运行：

```bash
export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"
export QT_QPA_PLATFORM=offscreen
./build/tests/Release/xrk_tests.exe --gtest_filter='TileEncoder.*:TiledTransportTest.*'
# 预期：14 个测试全部 PASSED（9 编码器/协议 + 5 回环传输）
```

- 编码器/协议单元测试：`tests/test_tile_encoder.cpp`
- 真实 socket 回环集成测试：`tests/test_tiled_transport.cpp`（桩 Host + 真实
  `RemoteController`，覆盖能力握手 / 差分 / 丢块修复 / 精准 NACK / 光标优先级）
- 弱网分块传输**手动**验证清单：`tests/TILED_TRANSPORT_CHECKLIST.md`
- 完整测试方案：[docs/06_test_plan.md](docs/06_test_plan.md)

## UI 组件列表

| 组件 | 文件 | 说明 |
|------|------|------|
| EmojiPickerWidget | `emoji_picker_widget.h/cpp` | 表情选择器（7分类 + 搜索 + 最近） |
| ContactCardWidget | `contact_card_widget.h/cpp` | 联系人名片卡（头像/信息/操作） |
| CallWidget | `call_widget.h/cpp` | 语音/视频通话界面 |
| GroupAnnouncementWidget | `group_announcement_widget.h/cpp` | 群公告发布与查看 |
| GroupTodoWidget | `group_todo_widget.h/cpp` | 群待办事项（创建/状态切换/删除） |
| GroupVoteWidget | `group_vote_widget.h/cpp` | 群投票（创建/投票/查看结果） |
| RemoteDesktopWidget | `remote_desktop_widget.h/cpp` | 远程桌面控制（输入转发/缩略图/工具栏） |
| ChatWidget | `chat_widget.h/cpp` | 聊天窗口 |
| IpmsgWidget | `ipmsg_widget.h/cpp` | IPMsg 即时通讯 |
| FileTransferWidget | `file_transfer_widget.h/cpp` | 文件传输管理 |
| DeviceListWidget | `device_list_widget.h/cpp` | 设备列表（扫描/历史/地址簿） |
| SettingsWidget | `settings_widget.h/cpp` | 设置（5标签页: 基本/安全/视频/网络/外观） |
| TerminalWidget | `terminal_widget.h/cpp` | 远程终端 |
| SystemInfoWidget | `system_info_widget.h/cpp` | 系统信息 |
| RemoteProcessWidget | `remote_process_widget.h/cpp` | 远程进程管理器（列表/结束/启动） |
| ClipboardHistoryWidget | `clipboard_history_widget.h/cpp` | 剪贴板历史 |
| GroupStatisticsWidget | `group_statistics_widget.h/cpp` | 群统计图表 |
| GroupMemberManagementWidget | `group_member_management_widget.h/cpp` | 群成员管理 |
| SimpleHomeWidget | `simple_home_widget.h/cpp` | 首页（设备卡片/扫描） |
| MainWindow | `main_window.h/cpp` | 主窗口 |

## 文档索引

| 文档 | 内容 |
|------|------|
| [docs/01_architecture.md](docs/01_architecture.md) | 系统架构与线程模型 |
| [docs/02_module_design.md](docs/02_module_design.md) | 模块职责（含弱网分块传输设计第 5 节） |
| [docs/03_protocol.md](docs/03_protocol.md) | 通信协议（含分块传输消息与流程） |
| [docs/05_build.md](docs/05_build.md) | 编译与部署说明 |
| [docs/06_test_plan.md](docs/06_test_plan.md) | 测试方案 |
| [docs/07_change_log.md](docs/07_change_log.md) | 修改记录（v1.3.0） |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献指南 |

## 许可证

XRK 项目采用**双授权模式**，您可根据使用场景选择合适的许可：

| 场景                    | GPL v3 (免费) | 商业授权 (付费) |
|------------------------|:------------:|:--------------:|
| 个人学习/研究           | ✅ 可以       | ✅ 可以         |
| 开源项目                | ✅ 可以       | ✅ 可以         |
| 小团队 (<10人)          | ✅ 可以       | ✅ 可以         |
| 企业内部使用            | ✅ 可以       | ✅ 可以         |
| 商业闭源产品            | ❌ 不可以     | ✅ 可以         |
| 分发二进制(不含源码)     | ❌ 不可以     | ✅ 可以         |
| 大规模商业部署          | ❌ 不可以     | ✅ 可以         |

**选择指南：**
- 个人/开源/非商业 → **GPL v3** (见 [LICENSE](LICENSE))
- 商业/闭源/企业 → **商业授权** (见 [COMMERCIAL-LICENSE](COMMERCIAL-LICENSE))

详细对比与常见问题见 [LICENSE-INFO.md](LICENSE-INFO.md)。

**商业授权咨询：**
- Email: lkmao455-collab@users.noreply.github.com
- GitHub: https://github.com/lkmao455-collab/caipiao
