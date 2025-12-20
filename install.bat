@echo off
setlocal EnableDelayedExpansion

:: ============================================================================
:: SSHFS-Win Context Menu Shell Extension Installer
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

set "SCRIPT_DIR=%~dp0"
set "TARGET_DIR=%INSTALL_DIR%\bin"

:: Determine source directory (bin\ subdirectory or current directory)
set "SRC_DIR="
if exist "%SCRIPT_DIR%bin\sshfs-ctx.dll" (
    set "SRC_DIR=%SCRIPT_DIR%bin"
) else if exist "%SCRIPT_DIR%sshfs-ctx.dll" (
    set "SRC_DIR=%SCRIPT_DIR%"
)

:: Show header
call :echoquiet ============================================
call :echoquiet   SSHFS-Win Context Menu Installer
call :echoquiet ============================================
call :echoquiet

:: Step 1: Stop any running processes that might lock files
call :echoquiet [1/4] Stopping processes that may lock files...
taskkill /F /IM sshfs-ssh.exe >nul 2>&1
taskkill /F /IM explorer.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start explorer.exe
timeout /t 1 /nobreak >nul
call :echoquiet   Done

:: Step 2: Unregister old extension if exists
call :echoquiet [2/4] Unregistering old shell extension...
if exist "%TARGET_DIR%\sshfs-ctx.dll" (
    regsvr32 /s /u "%TARGET_DIR%\sshfs-ctx.dll" >nul 2>&1
)
call :echoquiet   Done

:: Step 3: Copy files if we have source files and target dir exists
call :echoquiet [3/4] Copying files...
if defined SRC_DIR (
    if exist "%TARGET_DIR%" (
        copy /Y "%SRC_DIR%\sshfs-ctx.dll" "%TARGET_DIR%\" >nul 2>&1
        copy /Y "%SRC_DIR%\sshfs-ssh.exe" "%TARGET_DIR%\" >nul 2>&1
        copy /Y "%SRC_DIR%\sshfs-ssh-launcher.exe" "%TARGET_DIR%\" >nul 2>&1
        call :echoquiet   Files copied
    ) else (
        call :echoquiet   Target directory does not exist: %TARGET_DIR%
        call :echoquiet   Please install SSHFS-Win first, or specify install directory
        exit /b 1
    )
) else (
    call :echoquiet   No source files to copy ^(using existing^)
)

:: Step 4: Register the shell extension
call :echoquiet [4/4] Registering shell extension...
if exist "%TARGET_DIR%\sshfs-ctx.dll" (
    regsvr32 /s "%TARGET_DIR%\sshfs-ctx.dll"
    if errorlevel 1 (
        echo   ERROR: Shell extension registration failed
        exit /b 1
    )
    call :echoquiet   Shell extension registered
) else (
    echo   ERROR: Shell extension DLL not found at %TARGET_DIR%\sshfs-ctx.dll
    echo   Please build the context menu components first:
    echo     build.bat
    exit /b 1
)

:: Show completion message
call :echoquiet
call :echoquiet ============================================
call :echoquiet   Context Menu Installation Complete!
call :echoquiet ============================================
call :echoquiet
call :echoquiet The "Open SSH Terminal Here" option will now appear
call :echoquiet when you right-click in SSHFS mounted folders.
call :echoquiet
if not "%QUIET_MODE%"=="quiet" pause

endlocal
exit /b 0
