<#
.SYNOPSIS
    Build every projects/*/platformio.ini and print a pass/fail table.
    Run before committing. Exits 1 if any project fails.
#>
$ErrorActionPreference = 'Continue'  # errors are handled via exit codes; 'Stop' breaks on native stderr in PS 5.1
$repoRoot = Split-Path -Parent $PSScriptRoot
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
if (-not (Test-Path $pio)) {
    Write-Host ("ERROR: " + "PlatformIO not found at $pio") -ForegroundColor Red
    exit 1
}

$results = @()
$projects = Get-ChildItem (Join-Path $repoRoot 'projects') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'platformio.ini') } |
    Sort-Object Name

foreach ($p in $projects) {
    Write-Host "=== $($p.Name)" -ForegroundColor Cyan
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $log = & $pio run -d $p.FullName 2>&1
    $code = $LASTEXITCODE
    $sw.Stop()
    if ($code -ne 0) { $log | Select-Object -Last 30 | Write-Host }
    $results += [pscustomobject]@{
        Project = $p.Name
        Result  = if ($code -eq 0) { 'PASS' } else { 'FAIL' }
        Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    }
}

$results | Format-Table -AutoSize | Out-String | Write-Host
if ($results | Where-Object Result -eq 'FAIL') { exit 1 }
exit 0
