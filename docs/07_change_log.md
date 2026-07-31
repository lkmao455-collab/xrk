# XRK 局域网远程控制软件 - 修改记录

## 版本历史

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
