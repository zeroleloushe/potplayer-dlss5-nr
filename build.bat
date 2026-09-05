@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>&1 || (echo Install CMake and Visual Studio 2022 C++ workload. & exit /b 1)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
echo.
echo Output:
dir /b build\Release\*.dll 2>nul
echo.
echo Then:
echo   powershell -ExecutionPolicy Bypass -File tools\install-portable.ps1 -PotPlayerDir "D:\path\to\PotPlayer"
