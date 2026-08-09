# XRK 局域网远程控制软件 - 架构设计文档

## 1. 系统概述

XRK 是一款局域网环境下的远程控制软件，类似向日葵、TeamViewer，但仅限于局域网内使用。

## 2. 设计原则

- 分层架构：UI → Application → Core → Hardware
- 模块独立：各层职责单一，禁止循环依赖
- 接口驱动：面向接口编程，便于扩展和测试
- 线程安全：多线程协作，避免竞态条件

## 3. 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        UI Layer (Qt Widgets)                    │
├─────────────────────────────────────────────────────────────────┤
│  MainWindow  │  DeviceListWidget  │  RemoteDesktopWidget        │
└───────────────────────┬─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│                     Application Layer                           │
├─────────────────────────────────────────────────────────────────┤
│  DeviceManager  │  SessionManager  │  RemoteController          │
└───────────────────────┬─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│                        Core Layer                               │
├─────────────────────────────────────────────────────────────────┤
│  NetworkManager  │  ProtocolManager  │  DeviceDiscovery         │
└───────────────────────┬─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│                      Hardware Layer                             │
├─────────────────────────────────────────────────────────────────┤
│  ScreenCapture(DXGI)  │  InputControl  │  AudioCapture          │
└─────────────────────────────────────────────────────────────────┘
```

## 4. 模块职责

| 模块 | 职责 | 依赖 |
|------|------|------|
| UI Layer | 用户界面、交互事件 | Application Layer |
| Application Layer | 业务逻辑、状态管理 | Core Layer |
| Core Layer | 网络通信、协议解析 | Hardware Layer |
| Hardware Layer | 硬件交互、设备控制 | 系统API |

## 5. 技术选型

- **语言**: C++17
- **UI框架**: Qt6 Widgets
- **构建系统**: CMake 3.20+
- **网络**: TCP/UDP (Qt Network)
- **屏幕采集**: DXGI Desktop Duplication API (Windows)
- **图像编码**: JPEG (Qt) / H.264 (FFmpeg可选)
- **弱网分块传输**: 64×64 tile 差分 + RLE/JPEG 内容分类编码 + 精准 NACK（与 H264 互斥）
- **协议**: 自定义二进制协议 + Protobuf可选

## 6. 线程模型

| 线程 | 职责 | 优先级 |
|------|------|--------|
| Main Thread | UI、事件循环 | Normal |
| Capture Thread | 屏幕采集 | High |
| Encode Thread | 图像编码（含 Host 侧 EncodeWorker 分块切片/发送） | Normal |
| Network Thread | 数据收发 | High |
| Decode Thread | 图像解码（客户端 tile 解码线程，应用 applyTile） | Normal |
| Render Thread | 画面渲染 | Normal |
