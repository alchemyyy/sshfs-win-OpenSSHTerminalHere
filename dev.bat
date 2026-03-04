@echo off
setlocal

:: ============================================================================
:: SSHFS-Win Context Menu - Dev/Test Script
:: Builds and runs sshfs-ssh.exe directly, bypassing the shell extension DLL
:: Usage: dev.bat [path]
:: ============================================================================

set "SCRIPT_DIR=%~dp0"

:: Build first
call "%SCRIPT_DIR%build.bat"
if errorlevel 1 (
    echo.
    echo Build failed. Fix errors above and try again.
    pause
    exit /b 1
)

:: Get path from argument or prompt
set "TARGET=%~1"
if "%TARGET%"=="" (
    set /p "TARGET=Enter path (e.g. X:\folder): "
)
if "%TARGET%"=="" (
    echo No path specified.
    exit /b 1
)

:: Run sshfs-ssh.exe directly from the build output
echo.
echo Launching: %TARGET%
echo ============================================
"%SCRIPT_DIR%bin\sshfs-ssh.exe" "%TARGET%"

endlocal
exit /b %errorlevel%
