@echo off
title Shop stack watchdog
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0watch-shop-stack.ps1"
echo.
echo Watchdog stopped. Shop / TTS / tunnel may still be running.
pause
