@echo off
setlocal

:: ============================================================================
:: SSHFS-Win Context Menu - Build Script
:: Automatically finds Visual Studio and sets up the build environment
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
set "SRC_DIR=%SCRIPT_DIR%src"
set "OUT_DIR=%SCRIPT_DIR%bin"

:: Skip setup if already in a Developer Command Prompt
:: (cl.exe alone isn't enough — INCLUDE/LIB must be set too)
where cl.exe >nul 2>&1 && if defined INCLUDE goto :build

:: Find Visual Studio using vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Build Tools not found.
    echo Install "Desktop development with C++" from:
    echo   https://visualstudio.microsoft.com/downloads/
    exit /b 1
)

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if "%VS_PATH%"=="" (
    echo ERROR: No Visual Studio installation with C++ tools found.
    echo Install "Desktop development with C++" workload.
    exit /b 1
)

:: Initialize MSVC environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment.
    exit /b 1
)
if not defined INCLUDE (
    echo ERROR: MSVC environment did not set INCLUDE. Check vcvarsall output above.
    exit /b 1
)

:build
echo ============================================
echo   SSHFS-Win Context Menu - Build
echo ============================================
echo.

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo [1/4] Compiling resources...
rc.exe /nologo /fo "%OUT_DIR%\sshfs-ctx.res" "%SRC_DIR%\sshfs-ctx.rc"
if errorlevel 1 (
    echo ERROR: Failed to compile resources
    exit /b 1
)
echo   OK

echo [2/4] Building sshfs-ctx.dll...
cl.exe /nologo /O2 /W3 /DUNICODE /D_UNICODE ^
    "%SRC_DIR%\sshfs-ctx.c" ^
    "%OUT_DIR%\sshfs-ctx.res" ^
    /Fe:"%OUT_DIR%\sshfs-ctx.dll" ^
    /link /DEF:"%SRC_DIR%\sshfs-ctx.def" ^
    ole32.lib shell32.lib shlwapi.lib advapi32.lib mpr.lib uuid.lib user32.lib gdi32.lib
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ctx.dll
    exit /b 1
)
echo   OK

echo [3/4] Building sshfs-ssh.exe...
cl.exe /nologo /O2 /W3 /DUNICODE /D_UNICODE ^
    "%SRC_DIR%\sshfs-ssh.c" ^
    /Fe:"%OUT_DIR%\sshfs-ssh.exe" ^
    /link advapi32.lib mpr.lib shell32.lib shlwapi.lib user32.lib credui.lib
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ssh.exe
    exit /b 1
)
echo   OK

echo [4/4] Building sshfs-ssh-askpass.exe...
cl.exe /nologo /O2 /W3 /DUNICODE /D_UNICODE ^
    "%SRC_DIR%\sshfs-ssh-askpass.c" ^
    /Fe:"%OUT_DIR%\sshfs-ssh-askpass.exe" ^
    /link /SUBSYSTEM:CONSOLE
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ssh-askpass.exe
    exit /b 1
)
echo   OK

del /q "%OUT_DIR%\*.obj" 2>nul
del /q "%OUT_DIR%\*.exp" 2>nul
del /q "%OUT_DIR%\*.lib" 2>nul
del /q "%OUT_DIR%\*.res" 2>nul

echo.
echo Build complete:
echo   bin\sshfs-ctx.dll
echo   bin\sshfs-ssh.exe
echo   bin\sshfs-ssh-askpass.exe
echo.
echo Next: install.bat (as admin)
echo.

endlocal
exit /b 0
