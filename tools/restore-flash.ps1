<#
.SYNOPSIS
    Write a full flash image (your backup or a vendor merged image) at offset 0x0.
.EXAMPLE
    .\tools\restore-flash.ps1 -File firmware\factory\aabbccddeeff-20261008-2300.bin
    .\tools\restore-flash.ps1 -File 86switch_onoff.bin -Port COM5 -Force
#>
param(
    [Parameter(Mandatory = $true)][string]$File,
    [string]$Port,
    [switch]$Force
)

$ErrorActionPreference = 'Continue'  # errors are handled via exit codes; 'Stop' breaks on native stderr in PS 5.1
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$esptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'
foreach ($f in @($python, $esptool)) {
    if (-not (Test-Path $f)) { Write-Host ("ERROR: " + "Missing $f. Build any project once so PlatformIO installs it.") -ForegroundColor Red; exit 1 }
}
if (-not (Test-Path $File)) { Write-Host ("ERROR: " + "File not found: $File") -ForegroundColor Red; exit 1 }
$File = (Resolve-Path $File).Path
$size = (Get-Item $File).Length

if (-not $Force) {
    $answer = Read-Host "Overwrite the board's flash from $File ($([math]::Round($size / 1MB, 2)) MB) at 0x0? [y/N]"
    if ($answer -notmatch '^[yY]') { Write-Host 'Cancelled.'; exit 1 }
}

$portArgs = @()
if ($Port) { $portArgs = @('--port', $Port) }

& $python $esptool @portArgs --baud 921600 write-flash 0x0 $File
if ($LASTEXITCODE -ne 0) { Write-Host ("ERROR: " + 'write-flash failed. Retry, or hold BOOT, tap RST, release BOOT.') -ForegroundColor Red; exit 1 }
Write-Host 'Done. Press RST on the board if it does not restart by itself.' -ForegroundColor Green
exit 0
