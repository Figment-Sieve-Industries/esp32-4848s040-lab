<#
.SYNOPSIS
    Back up the full 16 MB flash of the board to firmware/factory/<MAC>-<date>.bin.
    Do this BEFORE the first upload: it is the only way back to the shipped firmware.
.EXAMPLE
    .\tools\backup-flash.ps1
    .\tools\backup-flash.ps1 -Port COM5
.NOTES
    Takes a few minutes at 921600 baud. Uses PlatformIO's bundled esptool.
#>
param([string]$Port)

$ErrorActionPreference = 'Continue'  # errors are handled via exit codes; 'Stop' breaks on native stderr in PS 5.1
$repoRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$esptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'
foreach ($f in @($python, $esptool)) {
    if (-not (Test-Path $f)) { Write-Host ("ERROR: " + "Missing $f. Build any project once so PlatformIO installs it.") -ForegroundColor Red; exit 1 }
}

$portArgs = @()
if ($Port) { $portArgs = @('--port', $Port) }

Write-Host 'Reading MAC address...' -ForegroundColor Cyan
$macOut = & $python $esptool @portArgs read-mac 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { Write-Host $macOut; Write-Host ("ERROR: " + 'Could not talk to the board. Check the cable/COM port, or hold BOOT, tap RST, release BOOT.') -ForegroundColor Red; exit 1 }
$m = [regex]::Match($macOut, 'MAC:\s*([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})')
$mac = if ($m.Success) { $m.Groups[1].Value.Replace(':', '').ToLower() } else { 'unknown-mac' }

$outDir = Join-Path $repoRoot 'firmware\factory'
New-Item -ItemType Directory -Force $outDir | Out-Null
$outFile = Join-Path $outDir ("{0}-{1}.bin" -f $mac, (Get-Date -Format 'yyyyMMdd-HHmm'))

Write-Host "Reading 16 MB flash to $outFile (takes a few minutes)..." -ForegroundColor Cyan
& $python $esptool @portArgs --baud 921600 read-flash 0 0x1000000 $outFile
if ($LASTEXITCODE -ne 0) { Write-Host ("ERROR: " + 'read-flash failed. Retry, or edit --baud to 460800.') -ForegroundColor Red; exit 1 }

$hash = (Get-FileHash $outFile -Algorithm SHA256).Hash
Write-Host ''
Write-Host "Backup:  $outFile" -ForegroundColor Green
Write-Host "SHA256:  $hash"
Write-Host 'Commit it: git add firmware/factory && git commit -m "Factory flash backup"'
exit 0
