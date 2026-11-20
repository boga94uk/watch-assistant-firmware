$ErrorActionPreference = "Stop"
$WatchDir = "YOUR_FIRMWARE_DIR\secrets"
$Py = "YOUR_VOICE_ASSISTANT_SERVER_DIR\venv\Scripts\python.exe"
$Importer = "YOUR_FIRMWARE_DIR\tools\import_secret.py"
New-Item -ItemType Directory -Force -Path $WatchDir | Out-Null
Write-Host "WATCHING $WatchDir for openai.key or xai.key"

while ($true) {
    foreach ($pair in @(
        @{ Name = "openai.key"; Kind = "openai" },
        @{ Name = "xai.key"; Kind = "xai" }
    )) {
        $path = Join-Path $WatchDir $pair.Name
        if (Test-Path -LiteralPath $path) {
            Start-Sleep -Milliseconds 500
            & $Py $Importer --key-file $path --kind $pair.Kind
        }
    }
    Start-Sleep -Seconds 1
}
