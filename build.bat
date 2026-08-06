@echo off
setlocal enabledelayedexpansion

:: XRK Build Script - Optimized for Windows
:: Usage: build.bat [--clean] [--release|--debug]

set "QT_DIR=D:\Qt\6.10.0\msvc2022_64"
set "BUILD_DIR=build"
set "BUILD_TYPE=Release"
set "TARGET=xrk"
set "CLEAN=0"

:: Parse arguments
for %%a in (%*) do (
    if /i "%%a"=="--clean" set "CLEAN=1"
    if /i "%%a"=="--release" set "BUILD_TYPE=Release"
    if /i "%%a"=="--debug" set "BUILD_TYPE=Debug"
)

echo ========================================
echo XRK Build Script
echo ========================================
echo [INFO] Qt: %QT_DIR%
echo [INFO] Build type: %BUILD_TYPE%
echo [INFO] Target: %TARGET%
echo.

:: Verify Qt
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [ERROR] Qt not found at %QT_DIR%
    exit /b 1
)

:: Clean build directory if requested
if "%CLEAN%"=="1" (
    echo [INFO] Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%" 2>nul
)

:: Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: Configure CMake if needed
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] Configuring CMake...
    cd /d "%BUILD_DIR%"
    cmake .. -G "Visual Studio 17 2022" -A x64 ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% 2>nul
    if errorlevel 1 (
        echo [ERROR] CMake configure failed
        cd ..
        exit /b 1
    )
    cd ..
) else (
    echo [INFO] Using cached CMake config...
)

:: Build
echo [INFO] Building %TARGET%...
cd /d "%BUILD_DIR%"
cmake --build . --config %BUILD_TYPE% --target %TARGET% -- /verbosity:quiet /nologo 2>nul
if errorlevel 1 (
    echo [ERROR] Build failed
    cd ..
    exit /b 1
)
cd ..

:: Success
echo.
echo ========================================
echo Build succeeded!
echo Output: %BUILD_DIR%\src\%BUILD_TYPE%\%TARGET%.exe
echo ========================================