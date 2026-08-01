@echo off
setlocal enabledelayedexpansion

echo ========================================
echo XRK Deploy Script
echo ========================================

set QT_DIR=D:\Qt\6.10.0\msvc2022_64
set BUILD_DIR=build
set BUILD_TYPE=Release
set DEPLOY_DIR=%BUILD_DIR%\deploy
set EXE_PATH=%BUILD_DIR%\src\%BUILD_TYPE%\xrk.exe

if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [ERROR] Qt not found at %QT_DIR%
    pause
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo [ERROR] Executable not found: %EXE_PATH%
    echo [HINT] Run build.bat first
    pause
    exit /b 1
)

echo [INFO] Using Qt: %QT_DIR%
echo [INFO] Deploying: %EXE_PATH% -> %DEPLOY_DIR%

REM Stop any running instance so the deployed exe isn't locked. A locked
REM xrk.exe makes rmdir/windeployqt/copy silently fail to replace it on some
REM runs, which is why the exe sometimes wasn't copied. This is forgiving:
REM it's fine if no instance is running.
taskkill /f /im xrk.exe >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Stopped a running xrk.exe instance to free the deploy target
    timeout /t 1 >nul
)

REM Clean deploy directory
if exist "%DEPLOY_DIR%" (
    echo [INFO] Cleaning previous deploy...
    rmdir /s /q "%DEPLOY_DIR%" 2>nul
    timeout /t 1 >nul
    if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%" 2>nul
)
mkdir "%DEPLOY_DIR%" 2>nul

echo [INFO] Running windeployqt...
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime --no-opengl-sw --dir "%DEPLOY_DIR%" "%EXE_PATH%"

if errorlevel 1 (
    echo [WARN] windeployqt had issues, continuing with manual copy...
)

echo [INFO] Copying additional Qt plugins...
REM SVG icon engine (required for :/icons/*.svg)
if exist "%QT_DIR%\plugins\iconengines\qsvgicon.dll" (
    if not exist "%DEPLOY_DIR%\iconengines" mkdir "%DEPLOY_DIR%\iconengines"
    copy /y "%QT_DIR%\plugins\iconengines\qsvgicon.dll" "%DEPLOY_DIR%\iconengines\"
    echo [OK] qsvgicon.dll
)

REM SVG image format support
if exist "%QT_DIR%\plugins\imageformats\qsvg.dll" (
    if not exist "%DEPLOY_DIR%\imageformats" mkdir "%DEPLOY_DIR%\imageformats"
    copy /y "%QT_DIR%\plugins\imageformats\qsvg.dll" "%DEPLOY_DIR%\imageformats\"
    echo [OK] qsvg.dll
)

REM Common image formats
for %%f in (qico.dll qjpeg.dll qgif.dll qtga.dll qtiff.dll qwbmp.dll qwebp.dll) do (
    if exist "%QT_DIR%\plugins\imageformats\%%f" (
        copy /y "%QT_DIR%\plugins\imageformats\%%f" "%DEPLOY_DIR%\imageformats\"
        echo [OK] %%f
    )
)

REM Platform plugin
if not exist "%DEPLOY_DIR%\platforms" mkdir "%DEPLOY_DIR%\platforms"
if exist "%QT_DIR%\plugins\platforms\qwindows.dll" (
    copy /y "%QT_DIR%\plugins\platforms\qwindows.dll" "%DEPLOY_DIR%\platforms\"
    echo [OK] qwindows.dll
)

REM Style plugins
if not exist "%DEPLOY_DIR%\styles" mkdir "%DEPLOY_DIR%\styles"
for %%f in (qmodernwindowsstyle.dll qfusion.dll) do (
    if exist "%QT_DIR%\plugins\styles\%%f" (
        copy /y "%QT_DIR%\plugins\styles\%%f" "%DEPLOY_DIR%\styles\"
        echo [OK] %%f
    )
)

echo [INFO] Copying FFmpeg DLLs...
for %%f in (avcodec-61.dll avutil-59.dll swscale-8.dll swresample-5.dll avformat-61.dll) do (
    if exist "%QT_DIR%\bin\%%f" (
        copy /y "%QT_DIR%\bin\%%f" "%DEPLOY_DIR%\"
        echo [OK] %%f
    )
)

echo [INFO] Copying translations...
if exist "%BUILD_DIR%\translations" (
    if not exist "%DEPLOY_DIR%\translations" mkdir "%DEPLOY_DIR%\translations"
    copy /y "%BUILD_DIR%\translations\*.qm" "%DEPLOY_DIR%\translations\" 2>nul
    echo [OK] translations
)

echo [INFO] Copying main executable...
copy /y "%EXE_PATH%" "%DEPLOY_DIR%\xrk.exe"
if not exist "%DEPLOY_DIR%\xrk.exe" (
    echo [ERROR] Failed to copy xrk.exe to deploy directory
    pause
    exit /b 1
)
REM Safety check: binary-compare the deployed exe against the freshly built
REM one. A mismatch (e.g. a still-locked file from a previous run) is retried
REM once before failing, so the deploy never ships a stale/corrupt exe.
fc /b "%EXE_PATH%" "%DEPLOY_DIR%\xrk.exe" >nul
if errorlevel 1 (
    echo [WARN] Deployed xrk.exe differs from source; retrying copy...
    copy /y /b "%EXE_PATH%" "%DEPLOY_DIR%\xrk.exe" >nul
    fc /b "%EXE_PATH%" "%DEPLOY_DIR%\xrk.exe" >nul
    if errorlevel 1 (
        echo [ERROR] xrk.exe copy verification failed
        pause
        exit /b 1
    )
)
echo [OK] xrk.exe verified

echo.
echo ========================================
echo Deploy completed successfully!
echo Output directory: %DEPLOY_DIR%\
echo ========================================
pause