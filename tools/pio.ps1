<#
.SYNOPSIS
    Build, upload or monitor a project in projects/ with PlatformIO.
.EXAMPLE
    .\tools\pio.ps1 01-hello-display-touch
    .\tools\pio.ps1 02-lvgl-hello all -Port COM5
.NOTES
    Run from PowerShell, not Git Bash: ESP's tool installer refuses MSYS shells.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Project,
    [Parameter(Position = 1)][ValidateSet('build', 'upload', 'monitor', 'all')][string]$Action = 'build',
    [string]$Port
)

$ErrorActionPreference = 'Continue'  # errors are handled via exit codes; 'Stop' breaks on native stderr in PS 5.1
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $repoRoot "projects\$Project"
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'

if (-not (Test-Path $pio)) {
    Write-Host ("ERROR: " + "PlatformIO not found at $pio. Install the PlatformIO IDE extension in VS Code.") -ForegroundColor Red
    exit 1
}
if (-not (Test-Path (Join-Path $projectDir 'platformio.ini'))) {
    $available = (Get-ChildItem (Join-Path $repoRoot 'projects') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'platformio.ini') }).Name -join ', '
    Write-Host ("ERROR: " + "No platformio.ini in $projectDir. Available: $available") -ForegroundColor Red
    exit 1
}

function Invoke-Pio([string[]]$PioArgs) {
    Write-Host "> pio $($PioArgs -join ' ')" -ForegroundColor Cyan
    & $pio @PioArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$uploadArgs = @('run', '-d', $projectDir, '-t', 'upload')
if ($Port) { $uploadArgs += @('--upload-port', $Port) }
$monitorArgs = @('device', 'monitor', '-d', $projectDir)
if ($Port) { $monitorArgs += @('-p', $Port) }

switch ($Action) {
    'build' { Invoke-Pio @('run', '-d', $projectDir) }
    'upload' { Invoke-Pio $uploadArgs }
    'monitor' { Invoke-Pio $monitorArgs }
    'all' { Invoke-Pio $uploadArgs; Invoke-Pio $monitorArgs }
}
exit 0
