# macOS 编译环境搭建指南

## 概述

本项目支持在 macOS 上编译，自动区分 **Apple Silicon (M 系列芯片, arm64)** 和 **Intel 芯片 (x86_64)**，也支持构建通用二进制。

## 系统要求

- macOS 11.0 (Big Sur) 或更高版本
- Xcode Command Line Tools
- Qt 6.5+ (推荐 Qt 6.8+)

---

## 1. 安装 Xcode Command Line Tools

```bash
xcode-select --install
```

验证安装：
```bash
xcode-select -p
# 应输出: /Library/Developer/CommandLineTools
```

---

## 2. 安装 Qt 6

### 方案 A: Homebrew (推荐)

#### Apple Silicon (M 系列 Mac)
```bash
brew install qt6
```

#### Intel Mac
```bash
brew install qt6
```

#### 跨架构安装 (在 Apple Silicon 上安装 Intel 版 Qt)
```bash
arch -x86_64 brew install qt6
```

> **注意**: Homebrew 会根据当前架构自动安装对应版本。Apple Silicon Mac 上默认安装 arm64 版，Intel Mac 上默认安装 x86_64 版。

### 方案 B: Qt 在线安装器

1. 下载: https://www.qt.io/download
2. 运行安装器，选择 `macOS` 组件
3. 安装路径默认: `~/Qt` 或 `/Applications/Qt`

---

## 3. 安装其他依赖 (可选)

```bash
# CMake (通常 Xcode 自带，但建议用 Homebrew 版本)
brew install cmake

# Ninja 构建系统 (更快)
brew install ninja

# Git
brew install git
```

---

## 4. 生成应用图标

项目需要 `.icns` 格式图标，运行生成脚本：

```bash
cd /path/to/xrk
chmod +x resources/generate_icns.sh
./resources/generate_icns.sh
```

这会从 `resources/icons/xrk.png` 生成 `resources/icons/xrk.icns`。

---

## 5. 编译项目

### 使用构建脚本 (推荐)

```bash
# 赋予执行权限
chmod +x build_mac.sh build_mac_apple.sh build_mac_intel.sh build_mac_universal.sh

# 1. 一键编译 (自动检测当前架构)
./build_mac.sh

# 2. 指定架构编译
./build_mac_apple.sh          # Apple Silicon (arm64)
./build_mac_intel.sh          # Intel (x86_64)
./build_mac_universal.sh      # 通用二进制 (arm64 + x86_64)

# 3. 手动指定参数
./build_mac.sh --arch arm64        # 仅编译 Apple Silicon
./build_mac.sh --arch x86_64       # 仅编译 Intel
./build_mac.sh --universal         # 通用二进制
./build_mac.sh --release           # Release 模式 (默认)
./build_mac.sh --debug             # Debug 模式
./build_mac.sh --clean --release   # 清理后重新编译
```

### 手动 CMake 编译

```bash
mkdir build_macos && cd build_macos

# 自动检测架构
cmake .. -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6" -DCMAKE_BUILD_TYPE=Release

# 或指定架构
cmake .. -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6" \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_OSX_ARCHITECTURES="arm64"

# 通用二进制
cmake .. -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6" \
         -DCMAKE_BUILD_TYPE=Release \
         -DXRK_BUILD_UNIVERSAL=ON

# 编译
cmake --build . --config Release --target xrk -j$(sysctl -n hw.ncpu)
```

---

## 6. 架构检测逻辑

### 自动检测优先级

1. **`--arch` 参数** - 显式指定架构 (`./build_mac.sh --arch arm64`)
2. **`--universal` 参数** - 构建通用二进制 (`./build_mac_universal.sh`)
3. **宿主机架构** - `uname -m` 检测 (`./build_mac.sh` 自动检测)
   - `arm64` → Apple Silicon
   - `x86_64` → Intel

### CMake 端检测

```cmake
# 在 CMakeLists.txt 中
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" 
        OR CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        # Apple Silicon
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        # Intel
        set(CMAKE_OSX_ARCHITECTURES "x86_64")
    endif()
endif()
```

---

## 7. 交叉编译说明

### Apple Silicon Mac 编译 Intel 版本

需要 Rosetta 2：
```bash
# 安装 Rosetta 2 (如果未安装)
softwareupdate --install-rosetta

# 安装 Intel 版 Qt
arch -x86_64 brew install qt6

# 编译
./build_mac_intel.sh
```

### Intel Mac 编译 Apple Silicon 版本

需要 Xcode 13+ 和 macOS 12+ SDK：
```bash
./build_mac_apple.sh
```

> **限制**: Intel Mac 无法运行 arm64 二进制进行测试，仅能编译。

---

## 8. 常见问题

### Qt 未找到

```bash
# 检查 Qt 安装路径
ls /opt/homebrew/opt/qt6/bin/qmake      # Apple Silicon
ls /usr/local/opt/qt6/bin/qmake         # Intel

# 手动指定 Qt 路径
./build_mac.sh --release
# 如果脚本找不到，手动运行 cmake 指定路径
cmake .. -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6"
```

### 权限问题

```bash
# 确保脚本可执行
chmod +x build_mac.sh build_mac_apple.sh build_mac_intel.sh build_mac_universal.sh
chmod +x resources/generate_icns.sh
```

### 码签和公证 (分发时需要)

```bash
# 码签
codesign --deep --force --verify --verbose \
    --sign "Developer ID Application: Your Name (TEAM_ID)" \
    --options runtime \
    build_macos/src/xrk.app

# 公证
xcrun notarytool submit build_macos/src/xrk.app.zip \
    --apple-id "your@email.com" \
    --team-id "TEAM_ID" \
    --password "app-specific-password" \
    --wait

# 装订
xcrun stapler staple build_macos/src/xrk.app
```

### 部署 Qt 框架

构建脚本会自动运行 `macdeployqt`，但如果失败可手动运行：

```bash
/opt/homebrew/opt/qt6/bin/macdeployqt build_macos/src/xrk.app -verbose=2
```

---

## 9. 验证编译结果

```bash
# 查看二进制架构
lipo -archs build_macos/src/xrk.app/Contents/MacOS/xrk

# 单架构输出示例:
# arm64
# 或
# x86_64

# 通用二进制输出示例:
# arm64 x86_64

# 查看详细信息
file build_macos/src/xrk.app/Contents/MacOS/xrk
```

---

## 10. 环境变量参考

| 变量 | 说明 | 示例 |
|------|------|------|
| `CMAKE_PREFIX_PATH` | Qt 安装路径 | `/opt/homebrew/opt/qt6` |
| `CMAKE_OSX_ARCHITECTURES` | 目标架构 | `arm64`, `x86_64`, `arm64;x86_64` |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | 最低 macOS 版本 | `11.0` |
| `XRK_BUILD_UNIVERSAL` | 构建通用二进制 | `ON` / `OFF` |

---

## 11. 目录结构

```
xrk/
├── build_win_x64.bat             # Windows x64 构建脚本
├── build_mac.sh                  # macOS 构建脚本 (主脚本，自动检测架构)
├── build_mac_apple.sh            # macOS Apple Silicon 专用构建脚本
├── build_mac_intel.sh            # macOS Intel 专用构建脚本
├── build_mac_universal.sh        # macOS 通用二进制构建脚本
├── CMakeLists.txt                # 主 CMake 配置 (含 macOS 支持)
├── src/
│   └── CMakeLists.txt            # 可执行目标配置 (含 MACOSX_BUNDLE)
├── resources/
│   ├── Info.plist                # macOS 应用包信息
│   ├── xrk.qrc                   # Qt 资源文件
│   ├── generate_icns.sh          # 图标生成脚本
│   └── icons/
│       ├── xrk.png               # 源图标 (1024x1024 推荐)
│       └── xrk.icns              # 生成的 macOS 图标
└── docs/
    └── macos_build_guide.md      # 本文档
```

---

## 12. 快速开始检查清单

- [ ] 安装 Xcode Command Line Tools
- [ ] 安装 Qt 6 (Homebrew 或在线安装器)
- [ ] 运行 `./resources/generate_icns.sh` 生成图标
- [ ] 运行 `chmod +x build_mac.sh build_mac_apple.sh build_mac_intel.sh build_mac_universal.sh` 赋予执行权限
- [ ] 运行 `./build_mac_apple.sh` (Apple Silicon) 或 `./build_mac_intel.sh` (Intel) 编译 Release 版
- [ ] 验证 `build_macos/src/xrk.app` 可正常运行
- [ ] (可选) 运行 `./build_mac_universal.sh` 编译通用二进制