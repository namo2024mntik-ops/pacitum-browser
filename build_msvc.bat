@echo off
REM Build Safari Browser C++ with MSVC (requires BuildTools with VCTools)
REM Run this from x64 Native Tools Command Prompt or it will auto-call vcvars

if not defined VSCMD_VER (
  echo Setting up MSVC environment...
  call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 2>nul
  if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
)

cd /d "%~dp0"
if exist build rmdir /s /q build
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64"
if errorlevel 1 (
  echo CMake configure failed - check Qt at C:/Qt/6.9.1/msvc2022_64 and that cl.exe is on PATH
  pause
  exit /b 1
)
cmake --build build --config Release
if errorlevel 1 (
  echo Build failed
  pause
  exit /b 1
)
echo Build succeeded: build\SafariBrowser.exe
pause
build\SafariBrowser.exe
