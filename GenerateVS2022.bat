@echo off
setlocal
cd /d "%~dp0"
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File Src\Tools\SetBuildPaths.ps1
exit /b %errorlevel%
