# Creates or pushes the two GitHub repos after: gh auth login
# YOUR_FIRMWARE_DIR and YOUR_VOICE_ASSISTANT_SERVER_DIR are the two clone folders. See SETUP.md.
$ErrorActionPreference = "Continue"

$gh = "C:\Program Files\GitHub CLI\gh.exe"
if (-not (Test-Path $gh)) {
    throw "Install GitHub CLI (winget install GitHub.cli) then run: gh auth login"
}

& $gh auth status
if ($LASTEXITCODE -ne 0) {
    throw "Run gh auth login first."
}
& $gh auth setup-git | Out-Null

$fw = "YOUR_FIRMWARE_DIR"
$shop = "YOUR_VOICE_ASSISTANT_SERVER_DIR"

function Ensure-Repo([string]$dir, [string]$name, [string]$desc) {
    Push-Location $dir
    try {
        if (-not (Test-Path .git)) { git init -b main }
        $view = & $gh repo view $name --json url -q .url 2>&1
        $exists = ($LASTEXITCODE -eq 0 -and "$view" -match '^https://')
        if (-not $exists) {
            Write-Host "Creating public repo $name ..."
            & $gh repo create $name --public --source $dir --remote origin --description $desc --push
            if ($LASTEXITCODE -ne 0) { throw "gh repo create $name failed" }
        } else {
            Write-Host "Repo exists: $view"
            git remote remove origin 2>$null
            git remote add origin $view.ToString().Trim()
            git push -u origin HEAD
            if ($LASTEXITCODE -ne 0) { throw "git push $name failed" }
        }
        $url = & $gh repo view $name --json url -q .url
        Write-Host "Pushed $name -> $url"
    } finally {
        Pop-Location
    }
}

Ensure-Repo $fw "watch-assistant-firmware" "ESP32-S3 AMOLED phoneshop voice watch (ESP-IDF)"
Ensure-Repo $shop "voice-assistant-server" "Flask+Waitress shop voice assistant for the ESP32 watch"

