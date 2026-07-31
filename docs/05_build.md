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

### 7.1 Windows部署
```powershell
# 使用windeployqt打包
windeployqt.exe bin\Release\xrk.exe

# 复制到目标机器
xcopy /E /I bin\Release\ deploy\
```

### 7.2 Linux部署
```bash
# 使用linuxdeployqt打包
linuxdeployqt bin/xrk -appimage
```
