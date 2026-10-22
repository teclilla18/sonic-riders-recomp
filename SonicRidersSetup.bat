@echo off
rem Sonic Riders native port: setup (builds from your own disc image; see README.md)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\setup.ps1" %*
echo.
pause
