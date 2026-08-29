@echo off
setlocal

:: Find MSBuild / VS Dev Cmd
set "VSCMD="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSCMD=%%i\Common7\Tools\VsDevCmd.bat"

if not defined VSCMD (
    echo Visual Studio not found.
    exit /b 1
)

call "%VSCMD%" -arch=x64

echo Compiling main.cpp...
cl.exe /EHsc /MD /O2 /std:c++17 main.cpp /I.\webview2\build\native\include /link /SUBSYSTEM:CONSOLE /OUT:web_wallpaper.exe .\webview2\build\native\x64\WebView2LoaderStatic.lib user32.lib gdi32.lib advapi32.lib ole32.lib shell32.lib shlwapi.lib version.lib

if %errorlevel% neq 0 (
    echo Build failed.
    exit /b %errorlevel%
)

echo Build succeeded!
