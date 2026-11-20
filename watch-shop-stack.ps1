# Keep shop server, Pocket TTS, and Cloudflare tunnel running.
# Close this window to stop watching (the three services keep running).
$ErrorActionPreference = "Continue"

$ShopDir = "YOUR_VOICE_ASSISTANT_SERVER_DIR"
$TtsDir = "YOUR_FIRMWARE_DIR\tts"
$Cloudflared = "C:\Program Files (x86)\cloudflared\cloudflared.exe"
if (-not (Test-Path $Cloudflared)) {
    $Cloudflared = "C:\Program Files\cloudflared\cloudflared.exe"
}

$ShopHealth = "http://127.0.0.1:8080/health"
$TtsHealth = "http://127.0.0.1:8090/health"
$TunnelHealth = "https://your-server.example.com/health"
$PollSeconds = 15
$StartGraceSeconds = 25
$LogDir = "YOUR_FIRMWARE_DIR\logs"
$LogFile = Join-Path $LogDir "shop-stack-watchdog.log"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$mutex = New-Object System.Threading.Mutex($false, "Global\WatchShopStack")
if (-not $mutex.WaitOne(0)) {
    Write-Host "Watchdog already running. Leave that window open."
    exit 0
}

function Write-Log([string]$msg) {
    $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $msg"
    Write-Host $line
    Add-Content -Path $LogFile -Value $line -Encoding UTF8
}

function Test-Health([string]$url) {
    try {
        $r = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 6
        return ($r.StatusCode -ge 200 -and $r.StatusCode -lt 300)
    } catch {
        return $false
    }
}

function Stop-Listeners([int[]]$ports) {
    foreach ($p in $ports) {
        Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty OwningProcess -Unique |
            ForEach-Object {
                if ($_ -gt 0) {
                    Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue
                }
            }
    }
}

function Start-Shop {
    Write-Log "Starting shop server :8080"
    Stop-Listeners @(8080)
    Start-Process -FilePath (Join-Path $ShopDir "venv\Scripts\python.exe") `
        -ArgumentList "src\server.py" `
        -WorkingDirectory $ShopDir `
        -WindowStyle Minimized
}

function Start-Tts {
    Write-Log "Starting Pocket TTS :8090"
    Stop-Listeners @(8090)
    Start-Process -FilePath "cmd.exe" `
        -ArgumentList "/c start-tts.bat" `
        -WorkingDirectory $TtsDir `
        -WindowStyle Minimized
}

function Start-Tunnel {
    if (-not (Test-Path $Cloudflared)) {
        Write-Log "WARNING: cloudflared.exe not found"
        return
    }
    Write-Log "Starting Cloudflare tunnel YOUR_TUNNEL_NAME"
    Get-Process cloudflared -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    Start-Process -FilePath $Cloudflared `
        -ArgumentList @("tunnel", "run", "YOUR_TUNNEL_NAME") `
        -WindowStyle Minimized
}

function Wait-Health([string]$name, [string]$url, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Health $url) {
            Write-Log "$name is up"
            return $true
        }
        Start-Sleep -Seconds 2
    }
    Write-Log "$name did not come up in ${seconds}s"
    return $false
}

try {
    Write-Log "Watchdog started. Shop + TTS + tunnel. Poll every ${PollSeconds}s."
    Write-Host "Admin  http://localhost:8080"
    Write-Host "Public https://your-server.example.com"
    Write-Host "TTS    http://127.0.0.1:8090/health"
    Write-Host "Log    $LogFile"
    Write-Host "Close this window to stop watching (services keep running)."
    Write-Host ""

    $tunnelFails = 0

    while ($true) {
        if (-not (Test-Health $ShopHealth)) {
            Start-Shop
            Wait-Health "shop" $ShopHealth $StartGraceSeconds | Out-Null
        }

        if (-not (Test-Health $TtsHealth)) {
            Start-Tts
            Wait-Health "TTS" $TtsHealth 90 | Out-Null
        }

        $shopOk = Test-Health $ShopHealth
        $publicOk = Test-Health $TunnelHealth
        if ($shopOk -and -not $publicOk) {
            $tunnelFails++
            if ($tunnelFails -ge 2) {
                Start-Tunnel
                Wait-Health "tunnel" $TunnelHealth 40 | Out-Null
                $tunnelFails = 0
            } else {
                Write-Log "tunnel health miss ($tunnelFails) — will retry"
            }
        } else {
            $tunnelFails = 0
            if (-not (Get-Process cloudflared -ErrorAction SilentlyContinue) -and (Test-Path $Cloudflared)) {
                Start-Tunnel
            }
        }

        $s = if (Test-Health $ShopHealth) { "OK" } else { "DOWN" }
        $t = if (Test-Health $TtsHealth) { "OK" } else { "DOWN" }
        $u = if (Test-Health $TunnelHealth) { "OK" } else { "DOWN" }
        $status = "shop=$s  tts=$t  tunnel=$u"
        Write-Host ("`r" + (Get-Date -Format "HH:mm:ss") + "  $status   ") -NoNewline
        Start-Sleep -Seconds $PollSeconds
    }
} finally {
    $mutex.ReleaseMutex() | Out-Null
    $mutex.Dispose()
}
