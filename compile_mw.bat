@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
set PATH=D:\QT\6.10.0\MSVC2022_64\bin;C:\Program Files\CMake\bin;%PATH%
cd /e/xrk
cl /nologo /c /TP /EHsc /std:c++17 /I src /I src/core /I src/app /I src/ui /I src/hw /I "D:\QT\6.10.0\MSVC2022_64\include" src/ui/main_window.cpp > e:/xrk/mw_compile.txt 2>&1
echo EXITCODE=%ERRORLEVEL% >> e:/xrk/mw_compile.txt
