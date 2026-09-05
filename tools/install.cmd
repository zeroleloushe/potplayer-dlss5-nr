@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if "%~1"=="" (
  echo Usage: install.cmd C:\Users\you\PotPlayer
  echo.
  echo From PowerShell:
  echo   cmd /c install.cmd C:\Users\you\PotPlayer
  exit /b 1
)

set "POT=%~1"
if not exist "%POT%\" (
  echo PotPlayerDir not found: %POT%
  exit /b 1
)

set "AVSDEST="
set "VSDEST="
set "AVSDIR="
set "VSDIR="

for /f "delims=" %%i in ('dir /s /b "%POT%\AviSynth.dll" 2^>nul') do (
  if not defined AVSDIR set "AVSDIR=%%~dpi"
)
for /f "delims=" %%i in ('dir /s /b "%POT%\vapoursynth.dll" 2^>nul') do (
  if not defined VSDIR set "VSDIR=%%~dpi"
)

if not defined AVSDIR (
  for /d %%D in ("%POT%\..\AviSynth*" "%POT%\..\SVP 4*" "%POT%\..\SVP4*" "%POT%\..\SVP*") do (
    if exist "%%~fD\AviSynth.dll" (
      if not defined AVSDIR set "AVSDIR=%%~fD\"
    )
  )
)
if not defined VSDIR (
  for /d %%D in ("%POT%\..\VapourSynth*" "%POT%\..\SVP 4*" "%POT%\..\SVP*") do (
    if exist "%%~fD\vapoursynth.dll" (
      if not defined VSDIR set "VSDIR=%%~fD\"
    )
  )
)

call :pick_plugin_dir AVSDEST "%AVSDIR%"
call :pick_plugin_dir VSDEST "%VSDIR%"

set "COPIED=0"
if defined AVSDEST (
  call :copy_one DLSS5NR.dll "%AVSDEST%"
  call :copy_one nvngx.dll_pot.dll "%AVSDEST%"
)
if defined VSDEST (
  call :copy_one vsdlss5nr.dll "%VSDEST%"
  call :copy_one nvngx.dll_pot.dll "%VSDEST%"
)

if "%COPIED%"=="0" (
  echo AviSynth / VapourSynth plugins folder not found.
  echo Copy DLSS5NR.dll and nvngx.dll_pot.dll into your AviSynth plugins64 folder.
  echo Searched under: %POT%
  exit /b 1
)

set "SCRIPTDEST=%POT%\dlss5-nr"
if not exist "%SCRIPTDEST%" mkdir "%SCRIPTDEST%"
if exist "%~dp0after_svp.avs" copy /Y "%~dp0after_svp.avs" "%SCRIPTDEST%\" >nul
if exist "%~dp0after_svp.vpy" copy /Y "%~dp0after_svp.vpy" "%SCRIPTDEST%\" >nul
echo copied scripts -^> %SCRIPTDEST%

set "RUNTIME=%LOCALAPPDATA%\potplayer-dlss5-nr\runtime"
if not exist "%RUNTIME%" mkdir "%RUNTIME%"
echo.
echo Runtime folder: %RUNTIME%
if exist "%RUNTIME%\nvngx_dlssnr.dll" (
  echo nvngx_dlssnr.dll is already there.
) else (
  echo PUT nvngx_dlssnr.dll HERE ^(copy from a game that ships DLSS 5 NR^).
  echo This repo never ships NVIDIA files.
)

echo.
echo Next:
echo   1. PotPlayer renderer = Built-in Direct3D 11 or madVR. Turn OFF D3D11 GPU Super Resolution.
echo   2. Keep your SVP AviSynth / VapourSynth Filter as it is.
echo   3. Append the two lines from after_svp.avs AFTER SVSmoothFps.
echo   4. Play a 1080p file.
exit /b 0

:pick_plugin_dir
set "OUTVAR=%~1"
set "BASE=%~2"
if "%BASE%"=="" goto :eof
if exist "%BASE%plugins64+\" (
  set "%OUTVAR%=%BASE%plugins64+"
  goto :eof
)
if exist "%BASE%plugins64\" (
  set "%OUTVAR%=%BASE%plugins64"
  goto :eof
)
if exist "%BASE%plugins+\" (
  set "%OUTVAR%=%BASE%plugins+"
  goto :eof
)
if exist "%BASE%plugins\" (
  set "%OUTVAR%=%BASE%plugins"
  goto :eof
)
if exist "%BASE%vapoursynth64\plugins\" (
  set "%OUTVAR%=%BASE%vapoursynth64\plugins"
  goto :eof
)
set "%OUTVAR%=%BASE%"
goto :eof

:copy_one
set "NAME=%~1"
set "DEST=%~2"
if not exist "%~dp0%NAME%" (
  echo missing %NAME%
  goto :eof
)
if not exist "%DEST%" mkdir "%DEST%"
copy /Y "%~dp0%NAME%" "%DEST%\" >nul
if errorlevel 1 (
  echo FAILED to copy %NAME% to %DEST%
  goto :eof
)
echo copied %NAME% -^> %DEST%
set "COPIED=1"
goto :eof
