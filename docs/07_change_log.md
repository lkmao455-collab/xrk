# XRK 局域网远程控制软件 - 修改记录

## 版本历史

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
