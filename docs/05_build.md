# XRK 局域网远程控制软件 - 编译说明

## 1. 环境要求

### 1.1 操作系统
- Windows 10/11 (64位)
- Linux (后续支持)

### 1.2 开发工具
- **编译器**: MSVC 2019+ / GCC 9+ / Clang 10+
- **构建系统**: CMake 3.20+
- **Qt版本**: Qt 6.2+

### 1.3 依赖库
- Qt6: Core, Network, Widgets, Gui
- GTest (可选，用于单元测试)

## 2. Windows 编译步骤

### 2.1 安装依赖

```powershell
# 安装Qt6 (通过Qt Online Installer或vcpkg)
# 推荐使用vcpkg
vcpkg install qt6-base:x64-windows qt6-network:x64-windows

# 或使用Qt官方安装器
# 下载地址: https://www.qt.io/download
```

### 2.2 配置环境

```powershell
# 设置Qt路径 (根据实际安装位置)
set Qt6_DIR=C:\Qt\6.5.0\msvc2019_64
set CMAKE_PREFIX_PATH=%Qt6_DIR%

# 或使用Qt VS Tools / Qt Creator
```

### 2.3 CMake配置

```powershell
cd E:\xrk
mkdir build
cd build

# 配置项目
cmake .. -G "Visual Studio 17 2022" -A x64

# 或使用Ninja
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

### 2.4 编译项目

```powershell
# 使用Visual Studio
cmake --build . --config Release

# 或使用命令行
cmake --build . --target xrk --config Release
```

### 2.5 运行程序

```powershell
# 可执行文件位置
.\bin\Release\xrk.exe
```

## 3. Linux 编译步骤 (实验性)

```bash
# 安装依赖
sudo apt-get install -y \
    cmake \
    g++ \
    qt6-base-dev \
    qt6-network-dev

# 配置和编译
cd E:\xrk
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行
./bin/xrk
```

## 4. Qt Creator 编译

1. 打开Qt Creator
2. 打开 `E:\xrk\CMakeLists.txt`
3. 配置构建目录
4. 选择构建套件 (Kit)
5. 点击构建按钮

## 5. 测试编译

```powershell
# 启用测试
cmake .. -DENABLE_TESTS=ON

# 编译测试
cmake --build . --target xrk_tests

# 运行测试
ctest --output-on-failure
```

### 5.1 弱网分块传输（Tiled Transport）专项测试

分块传输相关的自动化测试分两类，均可在无 GUI 的 headless 环境运行（Qt offscreen）：

- **编码器/协议单元测试**（`tests/test_tile_encoder.cpp`，9 个）：RLE 无损往返、JPEG 分类、
  关键帧全网格、脏区限制、MD5 篡改检测等。
- **真实 socket 回环集成测试**（`tests/test_tiled_transport.cpp`，5 个）：用协议正确的桩 Host
  （真实 `TileEncoder` + `Encryption` + `ProtocolManager`）在 127.0.0.1 上连接真实
  `RemoteController`，覆盖能力握手、差分传输、丢块关键帧修复、**单 tile 精准 NACK 修复**、
  **光标优先级**。

运行（Qt 6.10 于 `D:\Qt\6.10.0\msvc2022_64`，路径按本机调整）：

```bash
export PATH="/d/Qt/6.10.0/msvc2022_64/bin:$PATH"
export QT_QPA_PLATFORM=offscreen
./build/tests/Release/xrk_tests.exe --gtest_filter='TileEncoder.*:TiledTransportTest.*'
# 预期：14 个测试全部 PASSED（9 编码器/协议 + 5 回环传输）
```

> 全量回归：`xrk_tests.exe`（无 filter）目前约 401 PASSED / 1 SKIPPED
> （`VideoRoundTrip.H264EncodeDecode` 因沙箱缺 libx264 跳过）。
> 弱网分块传输的**手动**验证清单见 `tests/TILED_TRANSPORT_CHECKLIST.md`，
> 设计说明见 `docs/02_module_design.md` 第 5 节与 `docs/03_protocol.md` 第 3.7 节。

## 6. 常见问题

### 6.1 Qt未找到
```
CMake Error: Could not find Qt6
```
**解决**: 设置 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR`

### 6.2 DXGI链接失败 (Windows)
```
undefined reference to IDXGIOutputDuplication
```
**解决**: 确保链接 `d3d11.lib` 和 `dxgi.lib`

### 6.3 MOC错误
```
Automoc error: moc not found
```
**解决**: 确保Qt的bin目录在PATH中

## 7. 部署说明

> 实际部署请以仓库根目录的 `deploy.bat` 为准（它已封装 windeployqt + 手工补全插件/FFmpeg/翻译/资源）。
> 下面 7.1 是脚本行为的最小化等价说明；7.3 列出运行所需的运行时依赖；7.4 给出“黑屏/看不到桌面”
> 相关的部署注意点。

### 7.1 Windows 部署（deploy.bat 流程）

前置条件：
- 已用 `build.bat`（或 `cmake --build build --config Release --target xrk`）编译出
  `build/src/Release/xrk.exe`。
- 本机安装 Qt 6.10.0 (msvc2022_64)，路径在脚本中硬编码为 `D:\Qt\6.10.0\msvc2022_64`
  （按需修改 `deploy.bat` 顶部的 `QT_DIR`）。

执行：
```powershell
deploy.bat
```
脚本依次完成：
1. 校验 Qt 与 `xrk.exe` 存在；若有运行中的 `xrk.exe` 先 `taskkill` 释放占用。
2. 清空并重建 `build/deploy\`。
3. `windeployqt --release --no-translations --no-compiler-runtime --no-opengl-sw`
   拷贝 Qt 运行时到 `build/deploy\`。
4. 手工补全 Qt 插件（windeployqt 可能漏掉）：
   - `iconengines/qsvgicon.dll`（`:/icons/*.svg` 图标引擎，必需）
   - `imageformats/qsvg.dll`（SVG 图像格式）
   - `imageformats/qico/qjpeg/qgif/qtga/qtiff/qwbmp/qwebp.dll`（含 **qjpeg** —— 见 7.4）
   - `platforms/qwindows.dll`、`styles/qmodernwindowsstyle.dll`、`styles/qfusion.dll`
5. 拷贝 FFmpeg 运行时：`avcodec-61 / avutil-59 / swscale-8 / swresample-5 / avformat-61`
   （来自 `QT_DIR\bin\`；缺失则跳过，但会影响屏幕编码/H264 能力，见 7.4）。
6. 拷贝 `build/translations\*.qm`。
7. 拷贝并 **二进制校验** `xrk.exe`（与源比对，失败重试一次，避免发布陈旧/被锁文件）。

输出目录：`build/deploy\`（含 `xrk.exe` + Qt/FFmpeg 依赖 + plugins + translations），
整目录复制到目标机即可运行。

### 7.2 Linux 部署
```bash
# 使用linuxdeployqt打包（Qt5 工具链；Qt6 可改用对应的 linuxdeployqt 或手动打包）
linuxdeployqt bin/xrk -appimage
```

### 7.3 运行时依赖清单（Windows）

| 类别 | 文件 | 用途 | 缺失影响 |
|------|------|------|----------|
| Qt 核心 | Qt6Core/Widgets/Gui/Network/...dll | 框架运行时 | 无法启动 |
| 平台插件 | platforms/qwindows.dll | 窗口系统 | 无法启动 |
| 图像格式 | imageformats/**qjpeg.dll** | **JPEG 屏幕帧解码（控制端必备）** | 控制端看不到桌面（黑屏） |
| 图像格式 | imageformats/qsvg.dll (+ iconengines/qsvgicon.dll) | SVG 图标/资源 | 图标缺失，不影响连接 |
| 编解码 | avcodec-61 / avutil-59 / swscale-8 / swresample-5 / avformat-61 | 屏幕编码、H264、音视频 | 无 FFmpeg 时：Host 屏幕共享/H264 不可用（JPEG 编码需 FFmpeg mjpeg） |

> 关键：**`qjpeg.dll` 必须随包发布**。控制端默认通过 JPEG 解码远端桌面画面
> （见 7.4），缺少该插件会导致“连上了、鼠标能动、但看不到桌面”的典型黑屏。

### 7.4 远程桌面“黑屏”相关的部署注意点

控制端“能控鼠标、看不到桌面”的根因链（详见 `FIX_DESKTOP_NOT_VISIBLE.md`）与部署强相关：

1. **JPEG 是默认且必备的编解码路径**：`Host::start()` 默认用 JPEG 编码，`RemoteController`
   用 Qt 的 JPEG 插件解码。因此目标机**必须带有 `imageformats/qjpeg.dll`**。
   - 若仅 `windeployqt` 默认未带，请在 `deploy.bat` 中确认已显式拷贝（当前脚本已含）。
2. **H264 为可选**：仅当用户选择“游戏/低延迟”档位时 Host 才切到 H264；此时两端都依赖
   FFmpeg（`avcodec` 含 H264 编码/解码）。若目标机 FFmpeg 不完整，Host 会自动回退 JPEG，
   控制端连续解码失败也会自动请求切回 JPEG——**不会黑屏**。
3. **加密**：屏幕帧始终 AES 加密，密钥在 AUTH 握手时下发，无需额外部署项。
4. **抓取限制**：在**无头/非交互会话**（如服务、无桌面的服务器）中 `ScreenCapture` 抓不到帧，
   属环境限制，非部署缺失；需在能抓取桌面的交互式会话运行 Host。
5. **弱网分块传输（Tiled Transport）为默认屏幕路径**：LAN 上客户端一旦鉴权即进入分块模式
   （Host 日志 `tiled screen transport enabled`）。每张 tile 按内容分类编码——纯色/文字走
   **RLE（自研解码，无需插件）**，照片走 **JPEG（依赖 `qjpeg.dll`）**。因此 7.3 中的
   `qjpeg.dll` 要求对分块模式同样适用：缺它会导致照片类 tile 解码失败、画面残块。
   分块模式与 H264 **互斥**——任一客户端非 tile-capable 或选“游戏/低延迟”H264 档位时，
   Host 回退整帧 `SCREEN_FRAME`。

### 7.5 运行验证清单

部署到目标机后，按以下清单逐项验证（重点确认“能控鼠标、看得到桌面”）。

**A. 目标机部署完整性（控制端必备）**
- [ ] `build/deploy\imageformats\qjpeg.dll` 存在 —— 缺此项会导致控制端“连上、鼠标能动、但看不到桌面”。
- [ ] `build/deploy\platforms\qwindows.dll` 存在 —— 缺此项 `xrk.exe` 无法启动。
- [ ] `build/deploy\iconengines\qsvgicon.dll` + `imageformats\qsvg.dll` 存在 —— 图标/资源正常。
- [ ] `build/deploy\` 根目录含 `Qt6*.dll` 与 `avcodec-61/avutil-59/swscale-8/swresample-5/avformat-61.dll`
      （FFmpeg 不全时 Host 仍可运行，但仅 JPEG 屏幕共享、无 H264）。

**B. 被控制端（Host）启动**
- [ ] 在**交互式桌面会话**中启动 `xrk.exe` → 点“启动服务”（无头/服务器会话抓不到桌面，属环境限制）。
- [ ] 控制台/日志**不再**出现
      `QObject::connect ... unique connections require a pointer to member function of a QObject subclass`
      （该告警已在 2026-08-05 修复；出现则说明部署的是旧版本）。
- [ ] 日志显示 `Using JPEG encoder for screen capture (default; switch to H264 via game/low-latency gear)`
      —— 确认默认编码器为 JPEG（看到桌面的关键）。
- [ ] 客户端连接后，Host 日志出现 `tiled screen transport enabled` —— 确认进入弱网分块模式
      （默认；旧版客户端或不选 H264 档位时生效）。控制端相应发出 `SCREEN_KEYFRAME` 能力握手。
- [ ] 若选“游戏/低延迟”档位：日志出现 `Encoder switch queued to H264`；本机无 libx264 时
      出现 `H264 encoder unusable ... keeping JPEG`（自愈，仍可见）。

**C. 控制端连接与画面**
- [ ] 控制端连接后，Host 日志出现 `auto-granting consent for <clientId>`（LAN/私有 IP 自动授权）
      或用户在被控制端点击“允许” → `Consent granted ... frames will now be sent`。
- [ ] Host 日志出现 `NetworkWorker: X frames sent, Y KB, clients=1`（clients 从 0 变为 1）。
- [ ] 控制端画面区域显示 `Frames: N | WxH` 且 `N` 持续增长、`WxH` 非 0，画面**不黑**
      （鼠标/键盘可正常控制）。
- [ ] 断开重连、切换分辨率、切换画质档位后画面均不黑。

**D. 异常自检**
- [ ] 若控制端仍黑屏：先确认 A 中 `qjpeg.dll` 已随包；再确认 B 中 Host 在交互式会话运行；
      最后查看 Host 日志 `clients=` 是否为 1（为 0 表示未授权，画面不发送，但鼠标也应不动）。
- [ ] 若 Host 日志报 `H264 encoder produced no output` / `MF_E_NO_SAMPLE_TIMESTAMP`：属 H264 不可用，
      应自动回退 JPEG（见 7.4），不影响可见性。

