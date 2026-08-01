@echo off
setlocal enabledelayedexpansion

echo ========================================
echo XRK Build Script (Optimized)
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

REM Check if this is a clean build request
set CLEAN_BUILD=0
if "%1"=="--clean" set CLEAN_BUILD=1
if "%1"=="/clean" set CLEAN_BUILD=1
if "%CLEAN_BUILD%"=="1" (
    echo [INFO] Clean build requested...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%" 2>nul
    )
)

REM Check if build directory exists with CMakeCache
set NEED_CONFIGURE=0
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    set NEED_CONFIGURE=1
    echo [INFO] Build directory not found, will configure...
)

REM Create build directory if needed
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

REM Only run CMake configure if needed
if "%NEED_CONFIGURE%"=="1" (
    echo [INFO] Configuring CMake (first time or clean build)...
    cd "%BUILD_DIR%"
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
    
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed!
        if not defined BUILD_NONINTERACTIVE pause
        exit /b 1
    )
    cd ..
) else (
    echo [INFO] Using cached CMake configuration...
)

echo [INFO] Building project...
cd "%BUILD_DIR%"
cmake --build . --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo [ERROR] Build failed!
    if not defined BUILD_NONINTERACTIVE pause
    exit /b 1
)
cd ..

echo.
echo ========================================
echo Build completed successfully!
echo Output: %BUILD_DIR%\src\Release\xrk.exe
echo ========================================
if not defined BUILD_NONINTERACTIVE pause
