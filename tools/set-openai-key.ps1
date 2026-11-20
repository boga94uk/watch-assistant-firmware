# Import an OpenAI or xAI key from a local file. Never prints the key.
# Usage:
#   1. Put ONLY the key in a text file (one line).
#   2. powershell -File YOUR_FIRMWARE_DIR\tools\set-openai-key.ps1 -KeyFile C:\path\to\that.txt
#   3. Delete the text file.
# Then tell the assistant "key is set" — do not paste the key in chat.

param(
    [Parameter(Mandatory = $true)]
    [string]$KeyFile,
    [ValidateSet("openai", "xai")]
    [string]$Kind = "openai"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $KeyFile)) {
    throw "Key file not found: $KeyFile"
}

$key = (Get-Content -LiteralPath $KeyFile -Raw).Trim()
$key = $key -replace '^\uFEFF', ''
if ($key.Length -lt 20) {
    throw "Key file looks empty or too short."
}

if ($Kind -eq "xai") {
    $var = "XAI_API_KEY"
    if ($key -notmatch '^xai-') {
        Write-Warning "xAI keys usually start with xai-. Importing anyway."
    }
} else {
    $var = "OPENAI_API_KEY"
    if ($key -notmatch '^sk-') {
        Write-Warning "OpenAI keys usually start with sk-. Importing anyway."
    }
}

$envFiles = @(
    "YOUR_VOICE_ASSISTANT_SERVER_DIR\config\.env"
)
$prefix = "$var="
$written = 0
foreach ($envPath in $envFiles) {
    $dir = Split-Path -Parent $envPath
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    $lines = @()
    if (Test-Path $envPath) {
        $lines = Get-Content -LiteralPath $envPath
    }
    $found = $false
    $out = foreach ($line in $lines) {
        if ($line -match "^[\s]*$var=") {
            $found = $true
            "$prefix$key"
        } else {
            $line
        }
    }
    if (-not $found) {
        $out = @($out) + "$prefix$key"
    }
    Set-Content -LiteralPath $envPath -Value $out -Encoding ascii
    $written++
}

Write-Host "Wrote $var ($($key.Length) chars) into $written .env file(s)."
Write-Host "Delete $KeyFile yourself. Do not paste the key in chat."
Write-Host "Tell the assistant: key is set"
