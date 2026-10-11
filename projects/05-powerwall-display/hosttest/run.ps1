<#
.SYNOPSIS
    Builds and runs the project-05 host tests (test.cpp) with MSVC. Exit code = failed checks.
.NOTES
    Needs Visual Studio (any edition with the C++ workload) and a Windows 10/11 SDK. vcvars is not needed:
    INCLUDE/LIB are set from the newest MSVC toolset and SDK found. Build output goes to %TEMP%\hosttest-05.
#>
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msvc = (Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } | Select-Object -Last 1).FullName
$kit = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdk = (Get-ChildItem "$kit\Include" -Directory | Where-Object { Test-Path "$($_.FullName)\ucrt" } |
        Sort-Object { [version]$_.Name } | Select-Object -Last 1).Name
$env:INCLUDE = "$msvc\include;$kit\Include\$sdk\ucrt;$kit\Include\$sdk\um;$kit\Include\$sdk\shared"
$env:LIB = "$msvc\lib\x64;$kit\Lib\$sdk\ucrt\x64;$kit\Lib\$sdk\um\x64"

$out = Join-Path $env:TEMP 'hosttest-05'
New-Item -ItemType Directory -Force $out | Out-Null
& "$msvc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS `
    /I "$PSScriptRoot\stubs" "/Fo$out\" "/Fe$out\hosttest.exe" "$PSScriptRoot\test.cpp"
if ($LASTEXITCODE -ne 0) { exit 1 }
& "$out\hosttest.exe" "$PSScriptRoot\data"
exit $LASTEXITCODE
