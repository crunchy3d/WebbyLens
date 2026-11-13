# Builds installer\output\WebbyLens-Setup-<version>.exe
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# version from src\common\version.h
$h = Get-Content (Join-Path $root 'src\common\version.h') -Raw
$v = '{0}.{1}.{2}' -f ([regex]::Match($h, 'VERSION_MAJOR (\d+)').Groups[1].Value),
                      ([regex]::Match($h, 'VERSION_MINOR (\d+)').Groups[1].Value),
                      ([regex]::Match($h, 'VERSION_PATCH (\d+)').Groups[1].Value)
Write-Host "WebbyLens $v"

& (Join-Path $root 'scripts\fetch-deps.ps1')
cmd /c "`"$(Join-Path $root 'build.bat')`""
if ($LASTEXITCODE) { throw 'build failed' }

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 not found (winget install JRSoftware.InnoSetup)' }
& $iscc /Q "/DAppVersion=$v" (Join-Path $PSScriptRoot 'WebbyLens.iss')
if ($LASTEXITCODE) { throw 'ISCC failed' }
Get-Item (Join-Path $PSScriptRoot "output\WebbyLens-Setup-$v.exe")
