@echo off
setlocal
cd /d "%~dp0"

if not exist "build\bin\xnminer.exe" (
  echo Building Windows CUDA miner first...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  if errorlevel 1 (
    echo Build failed. Install VS C++ Build Tools, CMake, Ninja, and CUDA Toolkit.
    pause
    exit /b 1
  )
)

echo Starting miner...
"build\bin\xnminer.exe" %*
set ERR=%ERRORLEVEL%
if %ERR% neq 0 (
  echo.
  echo Miner exited with code %ERR%.
  echo Last log: data\session.log
  pause
)
exit /b %ERR%
