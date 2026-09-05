@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if "%~1"=="" (
  echo Usage: install.cmd "C:\Users\you\PotPlayer"
  echo.
  echo From PowerShell use:
  echo   .\install.cmd "C:\Users\you\PotPlayer"
  echo or:
  echo   .\install-portable.ps1 -PotPlayerDir "C:\Users\you\PotPlayer"
  exit /b 1
)

rem Relative -File path: the extract folder is often named "... (1)" and
rem parentheses break powershell.exe -File argument parsing.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\install-portable.ps1" -PotPlayerDir "%~1"
set ERR=%ERRORLEVEL%
if not "%~2"=="" (
  echo Extra arguments are not forwarded. If you need -AviSynthPlugins, run:
  echo   powershell -NoProfile -ExecutionPolicy Bypass -File .\install-portable.ps1 -PotPlayerDir "%~1" %2 %3 %4 %5
)
exit /b %ERR%
