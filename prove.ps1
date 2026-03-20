#Requires -Version 5.1
<#
.SYNOPSIS
    Clone Wine 11.0, build unpatched + patched ntdll, run proof tests.

.DESCRIPTION
    Windows/Linux PowerShell version of prove.sh. Clones Wine 11.0 source,
    compiles the test tools, builds ntdll.so unpatched and patched, swaps
    into the system Wine installation, and runs locktest + lockstress to
    demonstrate the before/after difference.

.PARAMETER SkipClone
    Reuse existing wine-src/ directory instead of cloning.

.PARAMETER PatchOnly
    Only build and install the patched ntdll, skip unpatched comparison.

.EXAMPLE
    ./prove.ps1
    ./prove.ps1 -SkipClone
    ./prove.ps1 -PatchOnly
#>

param(
    [switch]$SkipClone,
    [switch]$PatchOnly
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WineTag = "wine-11.0"
$WineDir = Join-Path $ScriptDir "wine-src"
$BuildDir = Join-Path $WineDir "build64-x86"
$BuildOut = Join-Path $ScriptDir "build"

# ── Detect platform ────────────────────────────────────────────────

$IsMacOS = ($PSVersionTable.OS -match "Darwin") -or ($env:OS -ne "Windows_NT" -and (uname 2>$null) -eq "Darwin")
$IsLinux = ($PSVersionTable.OS -match "Linux") -or ($env:OS -ne "Windows_NT" -and (uname 2>$null) -eq "Linux")
$IsWindows = ($env:OS -eq "Windows_NT") -or $PSVersionTable.Platform -eq "Win32NT"

# ── Helpers ────────────────────────────────────────────────────────

function Write-Info($msg) { Write-Host "==> $msg" -ForegroundColor White -NoNewline; Write-Host "" }
function Write-Ok($msg) { Write-Host "  OK " -ForegroundColor Green -NoNewline; Write-Host $msg }
function Write-Fail($msg) { Write-Host "  FAIL " -ForegroundColor Red -NoNewline; Write-Host $msg }
function Die($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Find-Command($name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-NcpuCount {
    if ($IsMacOS) { return [int](sysctl -n hw.ncpu 2>$null) }
    if ($IsLinux) { return [int](nproc 2>$null) }
    return [Environment]::ProcessorCount
}

# ── Find system Wine ntdll.so ─────────────────────────────────────

function Find-WineNtdll {
    $candidates = @(
        "/Applications/Wine Stable.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so",
        "/Applications/Wine.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so",
        "/opt/homebrew/lib/wine/x86_64-unix/ntdll.so",
        "/usr/lib/wine/x86_64-unix/ntdll.so",
        "/usr/lib64/wine/x86_64-unix/ntdll.so",
        "/usr/lib/x86_64-linux-gnu/wine/x86_64-unix/ntdll.so"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

# ── Swap ntdll ─────────────────────────────────────────────────────

function Swap-Ntdll($src, $label) {
    Copy-Item -Force $src $script:WineNtdll
    if ($IsMacOS) {
        & codesign -f -s - $script:WineNtdll 2>$null
    }
    & pkill -f wineserver 2>$null
    Start-Sleep -Seconds 2
    Write-Info "Installed $label ntdll.so"
}

function Backup-Ntdll {
    $backup = "$($script:WineNtdll).space-wine-backup"
    if (-not (Test-Path $backup)) {
        Copy-Item $script:WineNtdll $backup
        Write-Ok "Backed up original ntdll.so"
    }
}

# ── Build ntdll.so ─────────────────────────────────────────────────

function Build-Ntdll($label) {
    Write-Info "Building $label ntdll.so"

    if (-not (Test-Path (Join-Path $BuildDir "Makefile"))) {
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
        Push-Location $BuildDir

        $bisonPath = "/opt/homebrew/opt/bison/bin"
        if (-not (Test-Path "$bisonPath/bison")) {
            $bisonPath = (Split-Path (Find-Command "bison"))
        }

        if ($IsMacOS) {
            $configCmd = "cd '$BuildDir' && PATH='${bisonPath}:`$PATH' CC='clang -arch x86_64' CXX='clang++ -arch x86_64' ../configure --enable-win64 --without-freetype --with-mingw=x86_64-w64-mingw32"
            & arch -x86_64 /bin/bash -c $configCmd 2>&1 | Select-Object -Last 3
        } else {
            $env:PATH = "${bisonPath}:$($env:PATH)"
            & bash -c "cd '$BuildDir' && ../configure --enable-win64 --without-freetype --with-mingw=x86_64-w64-mingw32" 2>&1 | Select-Object -Last 3
        }
        Pop-Location
        Write-Ok "configure"
    }

    $ncpu = Get-NcpuCount
    Push-Location $BuildDir
    if ($IsMacOS) {
        & arch -x86_64 make "-j$ncpu" dlls/ntdll/ntdll.so 2>&1 | Select-Object -Last 1
    } else {
        & make "-j$ncpu" dlls/ntdll/ntdll.so 2>&1 | Select-Object -Last 1
    }
    Pop-Location

    $ntdllPath = Join-Path $BuildDir "dlls/ntdll/ntdll.so"
    if (-not (Test-Path $ntdllPath)) { Die "ntdll.so build failed" }
    Write-Ok "built: $ntdllPath"
    Write-Host ""
}

# ── Run tests ──────────────────────────────────────────────────────

function Run-Tests($label) {
    $resultsDir = Join-Path $BuildOut "results-$label"
    New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

    Write-Info "Running tests ($label)"

    $wine = Find-Command "wine"

    # locktest
    $locktestOut = & $wine (Join-Path $BuildOut "locktest.exe") -v 2>$null |
        Where-Object { $_ -notmatch '^\[mvk-|^\t|^$' }
    $locktestOut | Out-File -Encoding utf8 (Join-Path $resultsDir "locktest.txt")
    $passLine = $locktestOut | Where-Object { $_ -match 'passed' } | Select-Object -Last 1
    Write-Host "  locktest:   $passLine"

    # lockstress
    $stressOut = & $wine (Join-Path $BuildOut "lockstress.exe") 2>$null |
        Where-Object { $_ -notmatch '^\[mvk-|^\t|^$' }
    $stressOut | Out-File -Encoding utf8 (Join-Path $resultsDir "lockstress.txt")
    $completedLine = $stressOut | Where-Object { $_ -match 'Completed:' }
    $hangsLine = $stressOut | Where-Object { $_ -match 'Hangs:' }
    Write-Host "  lockstress: $completedLine"
    Write-Host "              $hangsLine"

    & pkill -f wineserver 2>$null
    Start-Sleep -Seconds 1
    Write-Host ""
}

# ── Main ───────────────────────────────────────────────────────────

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  space-wine: NtLockFile FIXME Proof Test" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# Preflight
Write-Info "Preflight checks"

$wine = Find-Command "wine"
if (-not $wine) { Die "wine not found in PATH" }
Write-Ok "wine: $wine"

$mingw = Find-Command "x86_64-w64-mingw32-gcc"
if (-not $mingw) { Die "x86_64-w64-mingw32-gcc not found. Install mingw-w64." }
Write-Ok "mingw: $mingw"

$script:WineNtdll = Find-WineNtdll
if (-not $script:WineNtdll) { Die "Cannot find system Wine ntdll.so" }
Write-Ok "ntdll: $($script:WineNtdll)"

if ($IsMacOS -and (uname -m) -eq "arm64") {
    try { & arch -x86_64 /bin/bash -c "echo ok" 2>$null | Out-Null; Write-Ok "rosetta: available" }
    catch { Die "Rosetta 2 not available" }
}
Write-Host ""

# Clone
if (-not $SkipClone) {
    if (Test-Path $WineDir) {
        Write-Info "Removing existing wine-src/"
        Remove-Item -Recurse -Force $WineDir
    }
    Write-Info "Cloning Wine $WineTag (shallow)"
    & git clone --depth 1 --branch $WineTag https://gitlab.winehq.org/wine/wine.git $WineDir
    Write-Host ""
} else {
    if (-not (Test-Path $WineDir)) { Die "wine-src/ not found. Run without -SkipClone first." }
    Write-Info "Reusing existing wine-src/"
    Write-Host ""
}

# Compile test tools
Write-Info "Compiling test tools"
New-Item -ItemType Directory -Force -Path $BuildOut | Out-Null
& $mingw -o (Join-Path $BuildOut "locktest.exe") (Join-Path $ScriptDir "tests/locktest.c") -lntdll
Write-Ok "locktest.exe"
& $mingw -o (Join-Path $BuildOut "lockstress.exe") (Join-Path $ScriptDir "tests/lockstress.c")
Write-Ok "lockstress.exe"
Write-Host ""

# Backup
Backup-Ntdll

if (-not $PatchOnly) {
    # Build unpatched
    Build-Ntdll "UNPATCHED (Wine 11.0)"
    Swap-Ntdll (Join-Path $BuildDir "dlls/ntdll/ntdll.so") "UNPATCHED"
    Run-Tests "unpatched"

    # Patch
    Write-Info "Applying NtLockFile patch"
    Copy-Item -Force (Join-Path $ScriptDir "patches/file.c") (Join-Path $WineDir "dlls/ntdll/unix/file.c")
    Write-Ok "patched dlls/ntdll/unix/file.c"

    # Rebuild
    Remove-Item -Force (Join-Path $BuildDir "dlls/ntdll/unix/file.o") -ErrorAction SilentlyContinue
    Build-Ntdll "PATCHED"
    Swap-Ntdll (Join-Path $BuildDir "dlls/ntdll/ntdll.so") "PATCHED"
    Run-Tests "patched"
} else {
    Write-Info "Applying NtLockFile patch"
    Copy-Item -Force (Join-Path $ScriptDir "patches/file.c") (Join-Path $WineDir "dlls/ntdll/unix/file.c")
    Write-Ok "patched dlls/ntdll/unix/file.c"
    Remove-Item -Force (Join-Path $BuildDir "dlls/ntdll/unix/file.o") -ErrorAction SilentlyContinue
    Build-Ntdll "PATCHED"
    Swap-Ntdll (Join-Path $BuildDir "dlls/ntdll/ntdll.so") "PATCHED"
    Run-Tests "patched"
}

# Summary
Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Results Summary" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

if (-not $PatchOnly) {
    Write-Host "  UNPATCHED Wine 11.0:" -ForegroundColor Yellow
    $unpatched = Join-Path $BuildOut "results-unpatched"
    if (Test-Path (Join-Path $unpatched "locktest.txt")) {
        Get-Content (Join-Path $unpatched "locktest.txt") | Where-Object { $_ -match 'passed|Hangs:|Completed:' } | ForEach-Object { Write-Host "    $_" }
    }
    if (Test-Path (Join-Path $unpatched "lockstress.txt")) {
        Get-Content (Join-Path $unpatched "lockstress.txt") | Where-Object { $_ -match 'Completed:|Hangs:|Time:' } | ForEach-Object { Write-Host "    $_" }
    }
    Write-Host ""
}

Write-Host "  PATCHED:" -ForegroundColor Green
$patched = Join-Path $BuildOut "results-patched"
if (Test-Path (Join-Path $patched "locktest.txt")) {
    Get-Content (Join-Path $patched "locktest.txt") | Where-Object { $_ -match 'passed|Hangs:|Completed:' } | ForEach-Object { Write-Host "    $_" }
}
if (Test-Path (Join-Path $patched "lockstress.txt")) {
    Get-Content (Join-Path $patched "lockstress.txt") | Where-Object { $_ -match 'Completed:|Hangs:|Time:' } | ForEach-Object { Write-Host "    $_" }
}

Write-Host ""
Write-Host "  Full results in: build/results-*/" -ForegroundColor White
Write-Host "  Patched ntdll.so is now installed in system Wine."
Write-Host "  To restore: Copy-Item '$($script:WineNtdll).space-wine-backup' '$($script:WineNtdll)'" -ForegroundColor DarkGray
Write-Host ""
