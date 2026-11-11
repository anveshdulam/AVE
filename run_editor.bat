@echo off
setlocal
title Autonomous Video Editor (AVE)

set "REPO_ROOT=%~dp0"
set "APP_EXE="

if exist "%REPO_ROOT%build\x64-release\apps\desktop\ave_desktop.exe" (
    set "APP_EXE=%REPO_ROOT%build\x64-release\apps\desktop\ave_desktop.exe"
) else if exist "%REPO_ROOT%build\x64-debug\apps\desktop\ave_desktop.exe" (
    set "APP_EXE=%REPO_ROOT%build\x64-debug\apps\desktop\ave_desktop.exe"
) else if exist "%REPO_ROOT%build\apps\desktop\ave_desktop.exe" (
    set "APP_EXE=%REPO_ROOT%build\apps\desktop\ave_desktop.exe"
) else if exist "%REPO_ROOT%build\vs2022-x64-debug\apps\desktop\Debug\ave_desktop.exe" (
    set "APP_EXE=%REPO_ROOT%build\vs2022-x64-debug\apps\desktop\Debug\ave_desktop.exe"
)

if "%APP_EXE%"=="" (
    echo [ERROR] Native ave_desktop.exe not found in build output directories.
    echo         Build it first with:
    echo           cmake --preset x64-debug
    echo           cmake --build --preset x64-debug --target ave_desktop
    exit /b 1
)

for %%I in ("%APP_EXE%") do set "EXE_DIR=%%~dpI"
set "PATH=%EXE_DIR%;%PATH%"
if exist "D:\DevTools\ffmpeg\bin" set "PATH=D:\DevTools\ffmpeg\bin;%PATH%"
if exist "D:\Qt\6.8.3\msvc2022_64\bin" set "PATH=D:\Qt\6.8.3\msvc2022_64\bin;%PATH%"

echo ======================================================================
echo       AUTONOMOUS VIDEO EDITOR (AVE) - LAUNCHING NATIVE DESKTOP UI
echo ======================================================================
echo Launching: %APP_EXE%

"%APP_EXE%" %*
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
