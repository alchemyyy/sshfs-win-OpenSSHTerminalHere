@echo off
setlocal

:: ============================================================================
:: SSHFS-Win Context Menu - Uninstaller
:: ============================================================================

:: Detect SSHFS-Win install directory from registry, fall back to default
set "INSTALL_DIR="
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\SSHFS-Win" /v InstallDir 2^>nul ^| findstr /i "InstallDir"') do set "INSTALL_DIR=%%b"
if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%ProgramFiles%\SSHFS-Win"
set "TARGET_DIR=%INSTALL_DIR%\bin"

:: Check for admin privileges
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo ============================================
echo   SSHFS-Win Context Menu - Uninstaller
echo ============================================
echo.

:: Step 1: Unregister the shell extension
echo [1/2] Unregistering shell extension...
if exist "%TARGET_DIR%\sshfs-ctx.dll" (
    regsvr32 /s /u "%TARGET_DIR%\sshfs-ctx.dll"
) else (
    :: Manual cleanup if DLL is gone
    reg delete "HKCR\Directory\Background\shellex\ContextMenuHandlers\000-SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Directory\shellex\ContextMenuHandlers\000-SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Drive\shellex\ContextMenuHandlers\000-SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\CLSID\{7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B}" /f >nul 2>&1
    reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" /v "{7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B}" /f >nul 2>&1
)
echo   OK

:: Step 2: Remove files
echo [2/2] Removing files...
del /f "%TARGET_DIR%\sshfs-ctx.dll" >nul 2>&1
del /f "%TARGET_DIR%\sshfs-ssh.exe" >nul 2>&1
del /f "%TARGET_DIR%\sshfs-ssh-launcher.exe" >nul 2>&1
echo   OK

echo.
echo ============================================
echo   Uninstallation Complete!
echo ============================================
echo.
pause
endlocal
exit /b 0
