$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvPython = Join-Path $Root ".venv\Scripts\python.exe"
$Script = Join-Path $Root "tts_server.py"

if (Test-Path $VenvPython) {
    $Py = $VenvPython
} else {
    throw "No Python venv with pocket-tts. Run install-tts.ps1 first."
}

$env:OMP_NUM_THREADS = "2"
$env:MKL_NUM_THREADS = "2"
$env:TOKENIZERS_PARALLELISM = "false"
$env:POCKET_TTS_HOST = "127.0.0.1"
$env:POCKET_TTS_PORT = "8090"
$env:POCKET_TTS_RATE = "16000"

Set-Location $Root
& $Py $Script
