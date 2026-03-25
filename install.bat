@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: SSHFS-Win Context Menu - Installer
:: Copies files to SSHFS-Win\bin and registers the shell extension
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
set "SRC_DIR=%SCRIPT_DIR%bin"

:: Detect SSHFS-Win install directory from registry, fall back to default
set "INSTALL_DIR="
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\SSHFS-Win" /v InstallDir 2^>nul ^| findstr /i "InstallDir"') do set "INSTALL_DIR=%%b"
if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%ProgramFiles%\SSHFS-Win"
set "TARGET_DIR=%INSTALL_DIR%\bin"

:: Check for admin privileges
net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: This script requires administrator privileges.
    echo Please right-click and select "Run as administrator".
    echo.
    pause
    exit /b 1
)

echo ============================================
echo   SSHFS-Win Context Menu - Installer
echo ============================================
echo.

if not exist "!SRC_DIR!\sshfs-ctx.dll" (
    echo ERROR: Build output not found.
    echo Please run build.bat first.
    echo Expected: !SRC_DIR!\sshfs-ctx.dll
    echo.
    pause
    exit /b 1
)

if not exist "!TARGET_DIR!" (
    echo ERROR: SSHFS-Win not found at !INSTALL_DIR!
    echo Please install SSHFS-Win first.
    echo.
    pause
    exit /b 1
)

:: Step 1: Unregister old extension if present
echo [1/3] Unregistering old shell extension...
if exist "!TARGET_DIR!\sshfs-ctx.dll" (
    regsvr32 /s /u "!TARGET_DIR!\sshfs-ctx.dll" >nul 2>&1
)
echo   OK

:: Step 2: Copy files
echo [2/3] Copying files to !TARGET_DIR!...
copy /Y "!SRC_DIR!\sshfs-ctx.dll" "!TARGET_DIR!\" >nul
copy /Y "!SRC_DIR!\sshfs-ssh.exe" "!TARGET_DIR!\" >nul
copy /Y "!SRC_DIR!\sshfs-ssh-askpass.exe" "!TARGET_DIR!\" >nul
echo   OK

:: Step 3: Register the shell extension
echo [3/3] Registering shell extension...
regsvr32 /s "!TARGET_DIR!\sshfs-ctx.dll"
if errorlevel 1 (
    echo ERROR: Shell extension registration failed
    pause
    exit /b 1
)
echo   OK

echo.
echo ============================================
echo   Installation Complete!
echo ============================================
echo.
echo Right-click in SSHFS mounted folders to see
echo "Open SSH Terminal Here" in the context menu.
echo.
pause
endlocal
exit /b 0
