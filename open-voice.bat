@echo off
setlocal EnableExtensions
title TTS Voice
set "URL=http://127.0.0.1:8080/admin#settings"

echo Opening TTS voice selection...
echo   %URL%
echo.
echo If the page does not load, run "Shop stack" first.

powershell -NoProfile -Command "Start-Process '%URL%'"
endlocal
