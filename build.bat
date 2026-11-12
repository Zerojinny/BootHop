@echo off
rem ============================================================
rem  BootHop - build.bat (Windows)
rem  Compiles src\*.cpp into BootHop.exe (x64, the window program) and
rem  BootHop.com (the same for the console: the command line) in this folder with
rem  the Visual C++ of Visual Studio (Community or Build Tools, the
rem  "Desktop development with C++" workload), found with vswhere.
rem  The C runtime is linked statically (/MT): the exe needs no
rem  runtime and runs on Windows 8.1 / 10 / 11 and in WinPE.
rem  Usage: build.bat [nopause]
rem ============================================================
setlocal
cd /d "%~dp0"
set "OUT_EXE=%~dp0BootHop.exe"
set "OUT_COM=%~dp0BootHop.com"
set "OBJ=%~dp0obj"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe was not found: install Visual Studio with C++.
    goto :fail
)
set "VS="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%I"
if not defined VS (
    echo [ERROR] No Visual Studio with the x64 C++ tools was found.
    goto :fail
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed.
    goto :fail
)

if not exist "%OBJ%" mkdir "%OBJ%"
for %%O in ("%OUT_EXE%" "%OUT_COM%") do if exist "%%~O" (
    del "%%~O" 2>nul
    if exist "%%~O" (
        echo [ERROR] %%~O is in use. Close it first.
        goto :fail
    )
)

echo [INFO] Visual Studio: %VS%
echo [INFO] Output       : %OUT_EXE%
rem the language files, embedded as resources (type LANG, name = code)
if exist "%OBJ%\lang.rc" del "%OBJ%\lang.rc"
for %%F in ("%~dp0lang\*.ini") do echo %%~nF LANG "lang/%%~nxF">>"%OBJ%\lang.rc"
if not exist "%OBJ%\lang.rc" (
    echo [ERROR] No language file in lang\.
    goto :fail
)
rc /nologo /fo "%OBJ%\app.res" app.rc
if errorlevel 1 goto :fail
if exist "%OBJ%\*.obj" del "%OBJ%\*.obj"
cl /nologo /c /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /DUNICODE /D_UNICODE /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 ^
    /Fo"%OBJ%\\" src\*.cpp
if errorlevel 1 goto :fail
set "LIBS=kernel32.lib user32.lib gdi32.lib shell32.lib comctl32.lib uxtheme.lib dwmapi.lib ole32.lib propsys.lib advapi32.lib mpr.lib shlwapi.lib"
rem the window program
link /nologo /OUT:"%OUT_EXE%" "%OBJ%\*.obj" "%OBJ%\app.res" /SUBSYSTEM:WINDOWS ^
    /MANIFEST:EMBED /MANIFESTINPUT:"%~dp0app.manifest" %LIBS%
if errorlevel 1 goto :fail
if not exist "%OUT_EXE%" goto :fail
rem the same for the console (BootHop /boot, /status, /cancel in a command
rem prompt: .com comes before .exe, so the prompt waits and gets the exit code)
link /nologo /OUT:"%OUT_COM%" "%OBJ%\*.obj" "%OBJ%\app.res" /SUBSYSTEM:CONSOLE /ENTRY:wWinMainCRTStartup ^
    /MANIFEST:EMBED /MANIFESTINPUT:"%~dp0app.manifest" %LIBS%
if errorlevel 1 goto :fail
if not exist "%OUT_COM%" goto :fail
echo.
echo [DONE] Built %OUT_EXE% and %OUT_COM%
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo.
echo [FAILED] BootHop.exe was not built.
if /i not "%~1"=="nopause" pause
exit /b 1
