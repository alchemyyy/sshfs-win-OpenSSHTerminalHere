@echo off
setlocal EnableDelayedExpansion

:: ============================================================================
:: SSHFS-Win Context Menu Components Build Script
:: Builds native Windows shell extension and SSH launcher
:: 
:: These components are 100%% native Windows - NO Cygwin dependencies at runtime
:: They extract connection info from the SSHFS UNC path via Windows APIs
:: ============================================================================

echo ============================================
echo   SSHFS-Win Context Menu Build
echo ============================================
echo.

set "SCRIPT_DIR=%~dp0"
set "SRC_DIR=%SCRIPT_DIR%src"
set "OUT_DIR=%SCRIPT_DIR%bin"

:: Create output directory
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: ============================================================================
:: Detect available compiler (in order of preference)
:: 1. MSVC (Visual Studio) - best optimization, native Windows
:: 2. Standalone MinGW-w64 - good alternative
:: 3. Cygwin MinGW cross-compiler - fallback (requires bash to run)
:: ============================================================================
set "MINGW_PATH="

echo Detecting compiler...

:: Priority 1: Visual Studio cl.exe
where cl.exe >nul 2>&1
if %errorlevel%==0 (
    echo   Found: MSVC ^(cl.exe^)
    goto :build_msvc
)

:: Priority 2: Standalone MinGW-w64 in common locations
if exist "C:\mingw64\bin\gcc.exe" (
    set "MINGW_PATH=C:\mingw64\bin"
    echo   Found: MinGW-w64 at C:\mingw64
    goto :build_mingw
)
if exist "C:\mingw-w64\mingw64\bin\gcc.exe" (
    set "MINGW_PATH=C:\mingw-w64\mingw64\bin"
    echo   Found: MinGW-w64 at C:\mingw-w64\mingw64
    goto :build_mingw
)
if exist "C:\msys64\mingw64\bin\gcc.exe" (
    set "MINGW_PATH=C:\msys64\mingw64\bin"
    echo   Found: MinGW-w64 at C:\msys64\mingw64
    goto :build_mingw
)
if exist "C:\TDM-GCC-64\bin\gcc.exe" (
    set "MINGW_PATH=C:\TDM-GCC-64\bin"
    echo   Found: TDM-GCC-64
    goto :build_mingw
)

:: Priority 3 (fallback): Cygwin's MinGW cross-compiler
if exist "C:\cygwin64\bin\x86_64-w64-mingw32-gcc.exe" (
    if exist "C:\cygwin64\bin\bash.exe" (
        echo   Found: Cygwin MinGW cross-compiler ^(fallback^)
        goto :build_cygwin_mingw
    )
)

:: No suitable compiler found
echo.
echo ERROR: No native Windows compiler found.
echo.
echo Install one of the following (in order of preference):
echo.
echo 1. Visual Studio Build Tools ^(recommended^)
echo    Download: https://visualstudio.microsoft.com/downloads/
echo    Install "Desktop development with C++"
echo    Run this script from "Developer Command Prompt"
echo.
echo 2. MinGW-w64 ^(standalone^)
echo    Download: https://github.com/niXman/mingw-builds-binaries/releases
echo    Extract to C:\mingw64
echo.
echo 3. Cygwin with mingw64-x86_64-gcc-core package ^(fallback^)
echo.
exit /b 1

:: ----------------------------------------------------------------------------
:: MSVC Build
:: ----------------------------------------------------------------------------
:build_msvc
echo.
echo Using: MSVC
echo.

echo [1/3] Compiling resources...
rc.exe /nologo /fo "%OUT_DIR%\sshfs-ctx.res" "%SRC_DIR%\sshfs-ctx.rc"
if errorlevel 1 (
    echo ERROR: Failed to compile resources
    exit /b 1
)
echo   OK: sshfs-ctx.res

echo [2/3] Building sshfs-ctx.dll...
cl.exe /nologo /O2 /LD /W3 /DUNICODE /D_UNICODE ^
    "%SRC_DIR%\sshfs-ctx.c" ^
    "%OUT_DIR%\sshfs-ctx.res" ^
    /Fe:"%OUT_DIR%\sshfs-ctx.dll" ^
    /link /DEF:"%SRC_DIR%\sshfs-ctx.def" ^
    ole32.lib shell32.lib shlwapi.lib advapi32.lib mpr.lib uuid.lib user32.lib gdi32.lib
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ctx.dll
    exit /b 1
)
echo   OK: sshfs-ctx.dll

echo [3/3] Building sshfs-ssh.exe...
cl.exe /nologo /O2 /W3 /DUNICODE /D_UNICODE ^
    "%SRC_DIR%\sshfs-ssh.c" ^
    /Fe:"%OUT_DIR%\sshfs-ssh.exe" ^
    /link advapi32.lib mpr.lib shell32.lib shlwapi.lib user32.lib credui.lib
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ssh.exe
    exit /b 1
)
echo   OK: sshfs-ssh.exe

:: Clean up temp files
del /q "%OUT_DIR%\*.obj" 2>nul
del /q "%OUT_DIR%\*.exp" 2>nul
del /q "%OUT_DIR%\*.lib" 2>nul
del /q "%OUT_DIR%\*.res" 2>nul
goto :done

:: ----------------------------------------------------------------------------
:: MinGW-w64 Build (standalone)
:: ----------------------------------------------------------------------------
:build_mingw
echo.
echo Using: MinGW-w64
echo.

echo [1/3] Compiling resources...
"%MINGW_PATH%\windres.exe" -i "%SRC_DIR%\sshfs-ctx.rc" -o "%OUT_DIR%\sshfs-ctx.res.o"
if errorlevel 1 (
    echo ERROR: Failed to compile resources
    exit /b 1
)
echo   OK: sshfs-ctx.res.o

echo [2/3] Building sshfs-ctx.dll...
"%MINGW_PATH%\gcc.exe" -shared -O2 -Wall -DUNICODE -D_UNICODE ^
    -o "%OUT_DIR%\sshfs-ctx.dll" ^
    "%SRC_DIR%\sshfs-ctx.c" ^
    "%OUT_DIR%\sshfs-ctx.res.o" ^
    "%SRC_DIR%\sshfs-ctx.def" ^
    -lole32 -lshell32 -lshlwapi -ladvapi32 -lmpr -luuid -lgdi32 ^
    -Wl,--subsystem,windows -static
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ctx.dll
    exit /b 1
)
echo   OK: sshfs-ctx.dll

echo [3/3] Building sshfs-ssh.exe...
"%MINGW_PATH%\gcc.exe" -O2 -Wall -DUNICODE -D_UNICODE ^
    -o "%OUT_DIR%\sshfs-ssh.exe" ^
    "%SRC_DIR%\sshfs-ssh.c" ^
    -ladvapi32 -lmpr -lshell32 -lshlwapi -luser32 -lcredui ^
    -municode -Wl,--subsystem,windows -static
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ssh.exe
    exit /b 1
)
echo   OK: sshfs-ssh.exe

:: Clean up temp files
del /q "%OUT_DIR%\*.res.o" 2>nul
goto :done

:: ----------------------------------------------------------------------------
:: Cygwin MinGW Cross-Compiler Build (produces native Windows binaries)
:: ----------------------------------------------------------------------------
:build_cygwin_mingw
echo.
echo Using: Cygwin MinGW cross-compiler
echo   (Output binaries will be native Windows - no Cygwin deps)
echo.

:: Convert paths: C:\Users\... -> /cygdrive/c/Users/...
set "SRC_PATH_CYG=%SRC_DIR:C:\=/cygdrive/c/%"
set "SRC_PATH_CYG=%SRC_PATH_CYG:\=/%"
set "OUT_PATH_CYG=%OUT_DIR:C:\=/cygdrive/c/%"
set "OUT_PATH_CYG=%OUT_PATH_CYG:\=/%"

echo [1/3] Compiling resources...
C:\cygwin64\bin\bash.exe -l -c "x86_64-w64-mingw32-windres -i '%SRC_PATH_CYG%/sshfs-ctx.rc' -o '%OUT_PATH_CYG%/sshfs-ctx.res.o'"
if errorlevel 1 (
    echo ERROR: Failed to compile resources
    exit /b 1
)
if not exist "%OUT_DIR%\sshfs-ctx.res.o" (
    echo ERROR: sshfs-ctx.res.o was not created
    exit /b 1
)
echo   OK: sshfs-ctx.res.o

echo [2/3] Building sshfs-ctx.dll...
C:\cygwin64\bin\bash.exe -l -c "x86_64-w64-mingw32-gcc -shared -O2 -Wall -DUNICODE -D_UNICODE -o '%OUT_PATH_CYG%/sshfs-ctx.dll' '%SRC_PATH_CYG%/sshfs-ctx.c' '%OUT_PATH_CYG%/sshfs-ctx.res.o' '%SRC_PATH_CYG%/sshfs-ctx.def' -lole32 -lshell32 -lshlwapi -ladvapi32 -lmpr -luuid -lgdi32 -Wl,--subsystem,windows -static"
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ctx.dll
    exit /b 1
)
if not exist "%OUT_DIR%\sshfs-ctx.dll" (
    echo ERROR: sshfs-ctx.dll was not created
    exit /b 1
)
echo   OK: sshfs-ctx.dll

echo [3/3] Building sshfs-ssh.exe...
C:\cygwin64\bin\bash.exe -l -c "x86_64-w64-mingw32-gcc -O2 -Wall -DUNICODE -D_UNICODE -o '%OUT_PATH_CYG%/sshfs-ssh.exe' '%SRC_PATH_CYG%/sshfs-ssh.c' -ladvapi32 -lmpr -lshell32 -lshlwapi -luser32 -lcredui -municode -Wl,--subsystem,windows -static"
if errorlevel 1 (
    echo ERROR: Failed to build sshfs-ssh.exe
    exit /b 1
)
if not exist "%OUT_DIR%\sshfs-ssh.exe" (
    echo ERROR: sshfs-ssh.exe was not created
    exit /b 1
)
echo   OK: sshfs-ssh.exe

:: Clean up temp files
del /q "%OUT_DIR%\*.res.o" 2>nul
goto :done

:done
echo.
echo ============================================
echo   Build Complete
echo ============================================
echo.
echo Output:
echo   %OUT_DIR%\sshfs-ctx.dll  - Shell extension (with embedded icon)
echo   %OUT_DIR%\sshfs-ssh.exe  - SSH launcher
echo.
echo These are native Windows binaries (no Cygwin runtime deps).
echo Run install-ctx.bat as Administrator to install.
echo.
endlocal
exit /b 0
