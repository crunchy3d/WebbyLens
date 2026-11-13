# Downloads the third-party runtime files for development builds into third_party\
# (not stored in git):
#   scrcpy-server  - scrcpy v5.0.1 phone-side server (Apache 2.0), checksum verified
#   platform-tools - Google's Android SDK Platform-Tools (adb), under Google's terms
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$tp = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force $tp | Out-Null

$serverUrl = 'https://github.com/Genymobile/scrcpy/releases/download/v5.0.1/scrcpy-server-v5.0.1'
$serverSha = '764eb6f79811d5211fe9df341120882ba9994c7a61b897d7bf3fb662e53bc536'
$server = Join-Path $tp 'scrcpy-server'
if (-not (Test-Path $server) -or (Get-FileHash $server -Algorithm SHA256).Hash.ToLower() -ne $serverSha) {
    Write-Host 'Downloading scrcpy-server v5.0.1...'
    Invoke-WebRequest $serverUrl -OutFile $server -Headers @{ 'User-Agent' = 'webbylens-build' }
    $h = (Get-FileHash $server -Algorithm SHA256).Hash.ToLower()
    if ($h -ne $serverSha) { Remove-Item $server; throw "scrcpy-server checksum mismatch: $h" }
}
Write-Host 'scrcpy-server OK'

$pt = Join-Path $tp 'platform-tools'
if (-not (Test-Path (Join-Path $pt 'adb.exe'))) {
    Write-Host 'Downloading Android SDK Platform-Tools (adb) from Google...'
    $zip = Join-Path $tp 'platform-tools.zip'
    Invoke-WebRequest 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip' -OutFile $zip
    Expand-Archive $zip -DestinationPath $tp -Force
    Remove-Item $zip
}
Write-Host 'platform-tools OK'
