@echo off
setlocal
if "%~1"=="" (
  echo Usage: install.cmd "C:\Users\you\PotPlayer"
  echo.
  echo Optional: install.cmd "C:\Users\you\PotPlayer" -AviSynthPlugins "D:\AviSynth+\plugins64"
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-portable.ps1" -PotPlayerDir %*
if errorlevel 1 exit /b 1
endlocal
