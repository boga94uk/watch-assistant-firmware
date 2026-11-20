@echo off
setlocal EnableExtensions
title Shop stack
cd /d "%~dp0"

set "SHOP=YOUR_VOICE_ASSISTANT_SERVER_DIR"
set "TTS=YOUR_FIRMWARE_DIR\tts"
set "CLOUDFLARED=C:\Program Files (x86)\cloudflared\cloudflared.exe"
if not exist "%CLOUDFLARED%" set "CLOUDFLARED=C:\Program Files\cloudflared\cloudflared.exe"

echo ========================================
echo   Shop stack: server, TTS, tunnel
echo ========================================
echo Stopping anything already on 8080 / 8090 and cloudflared...

powershell -NoProfile -Command ^
  "foreach ($p in 8080,8090) { Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue | Select-Object -ExpandProperty OwningProcess -Unique | ForEach-Object { if ($_ -gt 0) { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue } } }; Get-Process cloudflared -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue; Get-Process | Where-Object { $_.ProcessName -eq 'python' -and $_.MainWindowTitle -match 'Voice Assistant|Pocket TTS|Shop' } | Stop-Process -Force -ErrorAction SilentlyContinue"

ping -n 3 127.0.0.1 >nul

echo Starting Voice Assistant Server on :8080 ...
start "Voice Assistant Server" /min cmd /c "cd /d \"%SHOP%\" && venv\Scripts\python.exe src\server.py"

echo Starting Pocket TTS on :8090 ...
start "Pocket TTS" /min cmd /c "cd /d \"%TTS%\" && start-tts.bat"

echo Starting Cloudflare tunnel YOUR_TUNNEL_NAME ...
if exist "%CLOUDFLARED%" (
    start "Cloudflare Tunnel" /min "%CLOUDFLARED%" tunnel run YOUR_TUNNEL_NAME
) else (
    echo WARNING: cloudflared.exe not found. Tunnel not started.
)

echo.
echo Admin:  http://localhost:8080
echo Public: https://your-server.example.com
echo TTS:    http://127.0.0.1:8090/health
echo.
echo For keep-alive, use Desktop "Shop stack" (watch-shop-stack.bat).
echo Run this file once to start; the watchdog restarts anything that dies.
ping -n 5 127.0.0.1 >nul
endlocal
