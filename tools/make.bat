@echo off
rem Builds only the named targets in build\ (32-bit, Release), e.g. "tools\make.bat gunshape leverfit":
rem the tools can be built while the game runs with the mod's DLL loaded (and locked).
setlocal
set PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
set PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cmake -S "%~dp0.." -B "%~dp0..\build" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
cmake --build "%~dp0..\build" --target %* || exit /b 1
