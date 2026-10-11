<#
.SYNOPSIS
    Static analysis for one project or all of them: cppcheck (via `pio check`) + clang-tidy (run directly).
    Shows only findings in our own code (the project's src/ plus common/lib/Board4848S040 and SdStore).
.EXAMPLE
    .\tools\lint.ps1 06-sd-card-test
    .\tools\lint.ps1            # every project
    .\tools\lint.ps1 05-powerwall-display -All   # include low-severity style findings
    .\tools\lint.ps1 05-powerwall-display -Refresh   # regenerate the cached compilation database
.NOTES
    Exit code 1 if any medium/high finding remains in our code. Run from PowerShell, not Git Bash.

    How clang-tidy gets real include paths without analysing the framework:
      1. `pio run -t compiledb` writes compile_commands.json (no compile needed, ~7 s) into an isolated build dir
         ($env:TEMP\pio-lint-<project>), so it never collides with a build/flash running in the project.
      2. The database is cut down to OUR translation units and the GCC command lines are translated for clang:
         only -D/-I/-isystem/-include/-std/-O/-fexceptions/-frtti flags are kept (-mlongcalls, -fstrict-volatile-bitfields,
         -mdisable-hardware-atomics ... are dropped), the target is i686 (32-bit int/pointers, like Xtensa, and it accepts the
         inline-asm constraints that the ESP-IDF headers use), the toolchain's libstdc++/newlib/GCC headers are added with -isystem,
         and __XTENSA__/__XTENSA_EL__ are defined (with __i386__ undefined) so the headers take their Xtensa branches.
      3. clang-tidy runs only on those files; checks and HeaderFilterRegex come from .clang-tidy, so only our files report.
    The translated database is cached in the temp dir and rebuilt when platformio.ini, the shared ini, this script or the
    list of source files changes (or with -Refresh).

    Severity (clang-tidy): compiler errors = high; clang-diagnostic/analyzer/bugprone warnings = medium; performance/readability/
    misc = low (only with -All). cppcheck keeps its own severities.
    Silence a deliberate cppcheck finding with // cppcheck-suppress <id>, clang-tidy with // NOLINT(<check>).
    secrets.h (git-ignored, real credentials) is never printed. common/lib/SD is a patched upstream copy and is not checked.
#>
param(
    [Parameter(Position = 0)][string]$Project,
    [switch]$All,
    [switch]$Refresh
)

$ErrorActionPreference = 'Continue'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pioHome = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $env:USERPROFILE '.platformio' }
$pio = Join-Path $pioHome 'penv\Scripts\pio.exe'
$clangTidy = Join-Path $pioHome 'packages\tool-clangtidy\clang-tidy.exe'
$projects = if ($Project) { @($Project) } else {
    (Get-ChildItem (Join-Path $repoRoot 'projects') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'platformio.ini') }).Name
}
$severity = if ($All) { 'low|medium|high' } else { 'medium|high' }
$failed = 0

# ---- helpers ----------------------------------------------------------------------------------------------------

# Split a Windows-style command line (backslash is literal unless it precedes a double quote), as stored by `pio -t compiledb`.
function Split-WinCommand([string]$s) {
    $tokens = New-Object 'System.Collections.Generic.List[string]'
    $sb = New-Object System.Text.StringBuilder
    $inQuote = $false; $has = $false
    for ($i = 0; $i -lt $s.Length; $i++) {
        $c = $s[$i]
        if ($c -eq '\' -and ($i + 1) -lt $s.Length -and $s[$i + 1] -eq '"') { [void]$sb.Append('"'); $i++; $has = $true }
        elseif ($c -eq '"') { $inQuote = -not $inQuote; $has = $true }
        elseif (-not $inQuote -and [char]::IsWhiteSpace($c)) {
            if ($has) { $tokens.Add($sb.ToString()); [void]$sb.Clear(); $has = $false }
        } else { [void]$sb.Append($c); $has = $true }
    }
    if ($has) { $tokens.Add($sb.ToString()) }
    return , $tokens.ToArray()
}

# Our translation units: the project's src/ and every common/lib/* except the patched upstream SD copy.
function Test-OurFile([string]$path, [string]$projDir) {
    $full = [IO.Path]::GetFullPath($path)
    if ($full -match '[\\/]\.pio[\\/]') { return $false }
    if ($full -notmatch '\.(c|cc|cpp)$') { return $false }
    if ($full.StartsWith((Join-Path $projDir 'src') + '\', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    $libs = Join-Path $repoRoot 'common\lib'
    if ($full.StartsWith($libs + '\', [StringComparison]::OrdinalIgnoreCase) -and $full -notmatch '[\\/]common[\\/]lib[\\/]SD[\\/]') { return $true }
    return $false
}

# pio's GCC compile database -> a clang-tidy database for our files only.
function ConvertTo-ClangDatabase([string]$rawPath, [string]$outPath, [string]$projDir) {
    $entries = Get-Content $rawPath -Raw | ConvertFrom-Json
    $res = New-Object 'System.Collections.Generic.List[object]'
    foreach ($e in $entries) {
        $abs = [IO.Path]::GetFullPath([IO.Path]::Combine($e.directory, $e.file))
        if (-not (Test-OurFile $abs $projDir)) { continue }
        $tok = Split-WinCommand $e.command
        # <toolchain>\bin\xtensa-esp32s3-elf-g++.exe -> <toolchain>
        $tc = Split-Path -Parent (Split-Path -Parent $tok[0])
        $gccVer = (Get-ChildItem (Join-Path $tc 'lib\gcc\xtensa-esp-elf') -Directory | Select-Object -First 1).Name
        $isC = $abs -match '\.c$'
        $a = New-Object 'System.Collections.Generic.List[string]'
        $a.Add($(if ($isC) { 'clang' } else { 'clang++' }))
        $a.Add('--target=i686-none-elf')
        $a.Add('-U__i386__'); $a.Add('-U__i386'); $a.Add('-Ui386')
        $a.Add('-D__XTENSA__=1'); $a.Add('-D__xtensa__=1'); $a.Add('-D__XTENSA_EL__=1')
        $prefix = ''; $std = $null
        for ($i = 1; $i -lt $tok.Count - 1; $i++) {
            $t = $tok[$i]
            switch -regex ($t) {
                '^-(o|MF|MT)$' { $i++; continue }
                '^-iprefix$' { $prefix = $tok[++$i]; continue }
                '^-iwithprefix(.+)$' { $a.Add('-isystem' + $prefix + $Matches[1]); continue }
                '^-(isystem|include|imacros)$' { $a.Add($t); $a.Add($tok[++$i]); continue }
                '^-std=' { $std = $t; continue }
                '^-[DI]' { $a.Add($t); continue }
                '^-f(no-)?(exceptions|rtti)$' { $a.Add($t); continue }
                '^-O[0-3s]$' { $a.Add($t); continue }
                default { continue }   # every other GCC/xtensa flag (-m*, -W*, -g*, -ffunction-sections, ...) is dropped
            }
        }
        if ($std) { $a.Insert(2, $std) }
        # Headers the GCC driver would add implicitly (clang has no resource dir here and no xtensa sysroot).
        $cxx = Join-Path $tc 'xtensa-esp-elf\include\c++'
        if (-not $isC) {
            $cv = (Get-ChildItem $cxx -Directory | Select-Object -First 1).Name
            $a.Add('-isystem'); $a.Add((Join-Path $cxx $cv))
            $a.Add('-isystem'); $a.Add((Join-Path $cxx "$cv\xtensa-esp-elf"))
        }
        $a.Add('-isystem'); $a.Add((Join-Path $tc "lib\gcc\xtensa-esp-elf\$gccVer\include"))
        $a.Add('-isystem'); $a.Add((Join-Path $tc "lib\gcc\xtensa-esp-elf\$gccVer\include-fixed"))
        $a.Add('-isystem'); $a.Add((Join-Path $tc 'xtensa-esp-elf\include'))
        $a.Add($abs)
        $res.Add([pscustomobject]@{ directory = $e.directory; file = $abs; arguments = $a.ToArray() })
    }
    $res | ConvertTo-Json -Depth 4 | Set-Content $outPath -Encoding utf8
    return $res.Count
}

# Run clang-tidy over $files in parallel (portable to Windows PowerShell 5.1); returns the combined stdout lines.
function Invoke-ClangTidyParallel([string[]]$files, [string]$dbDir, [string]$workDir) {
    $max = [Math]::Max(1, [Math]::Min([int]$env:NUMBER_OF_PROCESSORS, 8))
    $running = @{}; $queue = New-Object System.Collections.Queue; $files | ForEach-Object { $queue.Enqueue($_) }
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $n = 0
    $reap = {
        foreach ($k in @($running.Keys)) {
            $r = $running[$k]
            $timedOut = ((Get-Date) - $r.Start).TotalSeconds -gt 600
            if ($r.Proc.HasExited -or $timedOut) {
                if ($timedOut -and -not $r.Proc.HasExited) { Stop-Process -Id $r.Proc.Id -Force }   # our own child only
                if (Test-Path $r.Out) { Get-Content $r.Out | ForEach-Object { $lines.Add("$_") } }
                Remove-Item $r.Out, $r.Err -ErrorAction SilentlyContinue
                $running.Remove($k)
            }
        }
    }
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($queue.Count -gt 0 -and $running.Count -lt $max) {
            $f = $queue.Dequeue(); $n++
            $out = Join-Path $workDir "ct$n.out"; $err = Join-Path $workDir "ct$n.err"
            $argList = @('-p', "`"$dbDir`"", "--config-file=`"$(Join-Path $repoRoot '.clang-tidy')`"", '--quiet', "`"$f`"")
            $proc = Start-Process -FilePath $clangTidy -ArgumentList $argList -NoNewWindow -PassThru `
                -RedirectStandardOutput $out -RedirectStandardError $err
            $running[$n] = @{ Proc = $proc; Out = $out; Err = $err; Start = Get-Date }
        }
        & $reap
        if ($running.Count -ge $max -or $queue.Count -eq 0) { Start-Sleep -Milliseconds 200 }
    }
    return , $lines
}

function Format-Path([string]$full, [string]$projDir) {
    if ($full.StartsWith($projDir + '\', [StringComparison]::OrdinalIgnoreCase)) { return $full.Substring($projDir.Length + 1) }
    if ($full.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { return $full.Substring($repoRoot.Length + 1) }
    return $full
}

# ---- main loop --------------------------------------------------------------------------------------------------

foreach ($p in $projects) {
    Write-Host "== $p" -ForegroundColor Cyan
    $dir = Join-Path $repoRoot "projects\$p"
    $work = Join-Path $env:TEMP "pio-lint-$p"
    New-Item -ItemType Directory -Force $work | Out-Null
    $prevBuildDir = $env:PLATFORMIO_BUILD_DIR
    $env:PLATFORMIO_BUILD_DIR = Join-Path $work 'build'   # isolated: never collides with a build/flash of the project
    $mine = New-Object 'System.Collections.Generic.List[string]'
    $notes = New-Object 'System.Collections.Generic.List[string]'
    try {
        # -- clang-tidy ------------------------------------------------------------------------------------------
        $dbDir = Join-Path $work 'clangdb'
        $dbFile = Join-Path $dbDir 'compile_commands.json'
        $keyFile = Join-Path $work 'db.key'
        $keyText = (@(
            (Get-FileHash (Join-Path $dir 'platformio.ini')).Hash,
            (Get-FileHash (Join-Path $repoRoot 'common\platformio-base.ini')).Hash,
            (Get-FileHash $PSCommandPath).Hash
        ) + @(Get-ChildItem (Join-Path $dir 'src'), (Join-Path $repoRoot 'common\lib') -Recurse -File -Include *.c, *.cc, *.cpp, *.h, *.hpp, library.json |
            ForEach-Object { $_.FullName.Substring($repoRoot.Length) } | Sort-Object)) -join "`n"
        $key = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($keyText)))
        $cached = (-not $Refresh) -and (Test-Path $dbFile) -and (Test-Path $keyFile) -and ((Get-Content $keyFile -Raw).Trim() -eq $key)
        if (-not $cached) {
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $log = & $pio run -d $dir -t compiledb 2>&1 | ForEach-Object { "$_" }
            $rawProj = Join-Path $dir 'compile_commands.json'
            if (-not (Test-Path $rawProj)) {
                $log | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }
                throw "pio run -t compiledb produced no compile_commands.json for $p"
            }
            $raw = Join-Path $work 'compile_commands.raw.json'
            Move-Item $rawProj $raw -Force   # keep the project dir clean
            New-Item -ItemType Directory -Force $dbDir | Out-Null
            $cnt = ConvertTo-ClangDatabase $raw $dbFile $dir
            Set-Content $keyFile $key
            Write-Host ("   compile database: {0} translation units ({1:N1}s)" -f $cnt, $sw.Elapsed.TotalSeconds) -ForegroundColor DarkGray
        }
        $files = @((Get-Content $dbFile -Raw | ConvertFrom-Json) | ForEach-Object { $_.file })
        $outside = 0
        $tidy = Invoke-ClangTidyParallel $files $dbDir $work
        # "path:line:col: warning|error: message [check]" (notes and code excerpts are ignored)
        $rx = '^(?<f>(?:[A-Za-z]:)?[^:]+):(?<l>\d+):(?<c>\d+): (?<s>warning|error): (?<m>.*?)(?: \[(?<k>[^\]]+)\])?$'
        foreach ($ln in $tidy) {
            if ($ln -notmatch $rx) { continue }
            # copy the groups first: the -match/-notmatch tests below overwrite $Matches
            $f = $Matches.f; $check = $Matches.k; $lsev = $Matches.s; $lnum = $Matches.l; $lmsg = $Matches.m
            $full = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($f)) { $f } else { Join-Path $dir $f }))
            $ours = $full.StartsWith($repoRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and $full -notmatch '[\\/]\.pio[\\/]' -and
                $full -notmatch '[\\/]common[\\/]lib[\\/]SD[\\/]' -and $full -notmatch '[\\/]secrets\.h$'
            if (-not $ours) {
                if ($lsev -eq 'error' -and $full -notmatch '[\\/]secrets\.h$') { $outside++ }
                continue
            }
            $sev = if ($lsev -eq 'error') { 'high' }
                   elseif ($check -match '^(performance|readability|misc)-') { 'low' }
                   else { 'medium' }
            $mine.Add(("{0}:{1}: [{2}:clang-tidy] {3} [{4}]" -f (Format-Path $full $dir), $lnum, $sev, $lmsg, $check))
        }
        if ($outside) { $notes.Add("clang-tidy: $outside compiler error(s) inside framework/library headers (hidden); analysis of those files may be incomplete. Run clang-tidy by hand to see them.") }

        # -- cppcheck --------------------------------------------------------------------------------------------
        $out = & $pio check -d $dir 2>&1 | ForEach-Object { "$_" }
        # Findings look like "path:line: [severity:kind] message"; keep ours, drop .pio/libdeps and framework headers.
        foreach ($ln in $out) {
            if ($ln -match "^\s*(src|include|.*common[\\/]lib)[\\/].*:\d+: \[($severity):" -and $ln -notmatch '\.pio[\\/]' -and
                $ln -notmatch '(^|[\\/])secrets\.h:' -and $ln -notmatch 'common[\\/]lib[\\/]SD[\\/]') { $mine.Add($ln.Trim().Replace($repoRoot + '\', '')) }
        }
    } finally {
        $env:PLATFORMIO_BUILD_DIR = $prevBuildDir
        if (-not $prevBuildDir) { Remove-Item Env:\PLATFORMIO_BUILD_DIR -ErrorAction SilentlyContinue }
    }

    $shown = $mine | Where-Object { $_ -match "\[($severity):" } | Sort-Object -Unique
    $notes | ForEach-Object { Write-Host $_ -ForegroundColor DarkYellow }
    if ($shown) {
        $shown | ForEach-Object { Write-Host $_ }
        $bad = @($shown | Where-Object { $_ -match '\[(medium|high):' }).Count
        if ($bad) { $failed++ }
        Write-Host "$p : $bad medium/high finding(s)" -ForegroundColor $(if ($bad) { 'Yellow' } else { 'Green' })
    } else {
        Write-Host "$p : clean" -ForegroundColor Green
    }
}
exit $(if ($failed) { 1 } else { 0 })
