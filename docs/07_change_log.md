# XRK 局域网远程控制软件 - 修改记录

## 版本历史

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

#### 协议变更
- 新增消息类型：`MONITOR_SWITCH_ACK (62)`、`MONITOR_REFRESH (63)`
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
- `src/ui/remote_desktop_widget.h/cpp`：过渡效果、快捷键、UI增强
- `src/core/types.h`：新增 MONITOR_SWITCH_ACK、MONITOR_REFRESH 消息类型
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
