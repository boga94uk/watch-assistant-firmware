$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvPython = Join-Path $Root ".venv\Scripts\python.exe"

Set-Location $Root
if (-not (Test-Path $VenvPython)) {
    py -3.10 -m venv .venv
}
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install -r (Join-Path $Root "requirements.txt")
Write-Host "Pocket TTS venv ready at $VenvPython"
