@echo off
rem Builds bf3vr.dll and injector.exe into build\ (32-bit, Release).
rem Pass "x64" as the first argument for a 64-bit build into build64\.
setlocal
set ARCH=x86
set OUT=build
if /i "%~1"=="x64" (set ARCH=x64& set OUT=build64)

set PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul || exit /b 1
set PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

cmake -S "%~dp0." -B "%~dp0%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0%OUT%" || exit /b 1
echo.
echo Built: %~dp0%OUT%\bf3vr.dll and %~dp0%OUT%\injector.exe
