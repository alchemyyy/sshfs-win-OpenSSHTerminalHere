@echo off
setlocal EnableDelayedExpansion

:: ============================================================================
:: SSHFS-Win Context Menu Shell Extension Uninstaller
:: ============================================================================

goto :main

:: ----------------------------------------------------------------------------
:: Helper function: echo only if not in quiet mode
:: Usage: call :echoquiet message text here
::        call :echoquiet  (for blank line)
:: ----------------------------------------------------------------------------
:echoquiet
if "%QUIET_MODE%"=="quiet" goto :eof
if "%~1"=="" (echo.) else echo %*
goto :eof

:: ----------------------------------------------------------------------------
:: Main script
:: ----------------------------------------------------------------------------
:main

:: Check if called with parameters
set "INSTALL_DIR=%~1"
set "QUIET_MODE=%~2"

:: Check for admin privileges
net session >nul 2>&1
if errorlevel 1 (
    :: Not running as admin - self-elevate using VBScript
    echo Requesting administrator privileges...
    
    set "ELEVATE_VBS=%TEMP%\elevate_%RANDOM%.vbs"
    
    set "ARGS="
    if not "%INSTALL_DIR%"=="" set "ARGS=%ARGS% ""%INSTALL_DIR%"""
    if not "%QUIET_MODE%"=="" set "ARGS=%ARGS% ""%QUIET_MODE%"""
    
    echo Set UAC = CreateObject^("Shell.Application"^) > "!ELEVATE_VBS!"
    echo UAC.ShellExecute "cmd.exe", "/c """"%~f0""%ARGS%""", "%~dp0", "runas", 1 >> "!ELEVATE_VBS!"
    
    cscript //nologo "!ELEVATE_VBS!"
    del "!ELEVATE_VBS!" >nul 2>&1
    exit /b
)

:: If no install dir provided, use default or detect from registry
if "%INSTALL_DIR%"=="" (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\SSHFS-Win" /v InstallDir 2^>nul ^| findstr /i "InstallDir"') do set "INSTALL_DIR=%%b"
    if defined INSTALL_DIR (
        if "!INSTALL_DIR:~-1!"=="\" set "INSTALL_DIR=!INSTALL_DIR:~0,-1!"
    )
    if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%ProgramFiles%\SSHFS-Win"
)

set "TARGET_DIR=%INSTALL_DIR%\usr\bin"

:: Show header
call :echoquiet ============================================
call :echoquiet   SSHFS-Win Context Menu Uninstaller
call :echoquiet ============================================
call :echoquiet

:: Step 1: Unregister the shell extension
call :echoquiet [1/2] Unregistering shell extension...
if exist "%TARGET_DIR%\sshfs-ctx.dll" (
    regsvr32 /s /u "%TARGET_DIR%\sshfs-ctx.dll"
    call :echoquiet   Shell extension unregistered
) else (
    :: Manual cleanup if DLL doesn't exist (both old and new registry key names)
    reg delete "HKCR\Directory\Background\shellex\ContextMenuHandlers\SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Directory\shellex\ContextMenuHandlers\SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Drive\shellex\ContextMenuHandlers\SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Directory\Background\shellex\ContextMenuHandlers\000-SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Directory\shellex\ContextMenuHandlers\000-SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\Drive\shellex\ContextMenuHandlers\000-SSHFSWin" /f >nul 2>&1
    reg delete "HKCR\CLSID\{7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B}" /f >nul 2>&1
    reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" /v "{7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B}" /f >nul 2>&1
    call :echoquiet   Registry entries cleaned up
)

:: Step 2: Refresh Explorer
call :echoquiet [2/2] Refreshing Windows Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start explorer.exe
timeout /t 1 /nobreak >nul
call :echoquiet   Done

:: Show completion message
call :echoquiet
call :echoquiet ============================================
call :echoquiet   Context Menu Uninstallation Complete!
call :echoquiet ============================================
call :echoquiet
if not "%QUIET_MODE%"=="quiet" pause

endlocal
exit /b 0
