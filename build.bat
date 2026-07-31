@echo off
setlocal enabledelayedexpansion

echo ========================================
echo XRK Build Script
echo ========================================

set QT_DIR=D:\Qt\6.10.0\msvc2022_64
set BUILD_DIR=build
set BUILD_TYPE=Release

if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [ERROR] Qt not found at %QT_DIR%
    if not defined BUILD_NONINTERACTIVE pause
    exit /b 1
)

echo [INFO] Using Qt: %QT_DIR%
echo [INFO] Build type: %BUILD_TYPE%

REM Clean build directory to avoid CMake platform mismatch
if exist "%BUILD_DIR%" (
    echo [INFO] Cleaning previous build...
    rmdir /s /q "%BUILD_DIR%"
)

mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

echo [INFO] Configuring CMake...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%"

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    if not defined BUILD_NONINTERACTIVE pause
    exit /b 1
)

echo [INFO] Building project...
cmake --build . --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo [ERROR] Build failed!
    if not defined BUILD_NONINTERACTIVE pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo Output: %BUILD_DIR%\src\Release\xrk.exe
echo ========================================
if not defined BUILD_NONINTERACTIVE pause