# ph-common.ps1 -- shared by the numbered Phantom Hourglass scripts (dot-sourced; not run on its own).
# Expects the calling script to have a [switch] $DryRun parameter.

$ErrorActionPreference = 'Stop'
# The scripts live in the repo (ph-recomp\scripts); the workspace is the folder the repo is in: the ROM, the finished
# game folder, the logs and the APKs go there, outside the repo.
$Repo = Split-Path -Parent $PSScriptRoot
$Root = Split-Path -Parent $Repo
# The workspace's folders (its START-HERE.md): your ROMs and patches, the finished PC games, and what the build
# makes on the way. ROMs are also still found loose in the workspace itself.
$RomsDir = Join-Path $Root 'roms'
$GamesDir = Join-Path $Root 'games'
$WorkDir = Join-Path $Root 'work'
$RomDir = Join-Path $RomsDir 'The_Legend_of_Zelda_Phantom_Hourglass_USA_DPAD-Patch'
# Your latest progress is in the .ml1.sav (written after the plain .sav; the two differ).
$MelonSave = Join-Path $RomDir 'The_Legend_of_Zelda_Phantom_Hourglass_USA_DPAD-Patch.ml1.sav'
$BuildDir = Join-Path $Repo 'build\release'
$GameDir = Join-Path $GamesDir 'PhantomHourglass'      # the finished, ready-to-play folder (step 3)
$LogDir = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Stamp = Get-Date -Format 'yyyyMMdd-HHmm'
$Progress = Join-Path $LogDir 'progress.log'
# The calling script's name (this file is dot-sourced, so PSCommandPath is the caller); no .NET calls (constrained mode).
$ScriptName = 'ph'
if ($MyInvocation.PSCommandPath) { $ScriptName = (Split-Path -Leaf $MyInvocation.PSCommandPath) -replace '\.ps1$', '' }
$Log = Join-Path $LogDir ("$ScriptName-$Stamp" + $(if ($DryRun) { '-dryrun' } else { '' }) + '.log')

function Step([string] $text) {
    $prefix = if ($DryRun) { '[dry-run] ' } else { '' }
    $line = "$(Get-Date -Format 'HH:mm:ss') $ScriptName $prefix$text"
    Write-Host $line -ForegroundColor Cyan
    Add-Content -Path $Progress -Value $line
    Add-Content -Path $Log -Value "==== $line"
}
function Warn([string] $text) {
    Write-Host "WARNING: $text" -ForegroundColor Yellow
    Add-Content -Path $Log -Value "WARNING: $text"
}
function Fail([string] $text) {
    Step "FAIL: $text"
    Write-Host "Log: $Log" -ForegroundColor Yellow
    exit 1
}
# Runs a native command, appending its output to the log (and the screen); fails on a nonzero exit.
# In a dry run it only says what it would run.
function Run([string] $what, [scriptblock] $cmd) {
    if ($DryRun) { Step "would run: $what  ->  $($cmd.ToString().Trim())"; return }
    Step $what
    # Windows PowerShell turns a native tool's stderr into error records: with 'Stop' the first warning would abort.
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    # (Not Tee-Object: Windows PowerShell's writes UTF-16 into a log the other lines write as ASCII.)
    & $cmd 2>&1 | ForEach-Object { $s = "$_"; Write-Host $s; Add-Content -Path $Log -Value $s }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    if ($code -ne 0) { Fail "$what (exit $code)" }
}
# Like Run, for an optional step: returns whether it worked instead of failing.
function Run-Optional([string] $what, [scriptblock] $cmd) {
    if ($DryRun) { Step "would run: $what  ->  $($cmd.ToString().Trim())"; return $true }
    Step $what
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $cmd 2>&1 | ForEach-Object { $s = "$_"; Write-Host $s; Add-Content -Path $Log -Value $s }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    return ($code -eq 0)
}

# ---- the ROM: the clean USA dump if present, else the D-pad patched one (both supported, one config) ----------------
$KnownRoms = @{
    '4c8f52dd719918bbcd46e73a8bae8628139c1b85' = 'USA (clean)'
    '7e0d26d143992f1eee182b24f1abb9e7f038eacf' = 'USA (D-pad patch)'
}
function Find-Rom {
    $found = ''
    $candidates = @(Get-ChildItem -Path $Root, $RomsDir, $RomDir -Filter '*.nds' -File -ErrorAction SilentlyContinue)
    foreach ($c in $candidates) {
        $h = (Get-FileHash -Algorithm SHA1 -LiteralPath $c.FullName).Hash.ToLower()
        if ($KnownRoms.ContainsKey($h)) {
            if (-not $found -or $KnownRoms[$h] -eq 'USA (clean)') { $found = $c.FullName }
        }
    }
    if (-not $found) { Fail "no supported Phantom Hourglass ROM found in $Root or $RomDir" }
    $h = (Get-FileHash -Algorithm SHA1 -LiteralPath $found).Hash.ToLower()
    Step "ROM: $found ($($KnownRoms[$h]))"
    return $found
}

# ---- tools ----------------------------------------------------------------------------------------------------------
function Add-Path([string] $dir) { if ((Test-Path $dir) -and ($env:PATH -notlike "*$dir*")) { $env:PATH = "$dir;$env:PATH" } }
function Refresh-Path {
    # PATH as the installers left it (machine + user), without .NET calls (constrained-language safe).
    $m = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -ErrorAction SilentlyContinue).Path
    $u = (Get-ItemProperty 'HKCU:\Environment' -ErrorAction SilentlyContinue).Path
    if ($m -or $u) { $env:PATH = "$m;$u" }
    Add-Path 'C:\Program Files\Git\cmd'
    Add-Path 'C:\Program Files\CMake\bin'
    Add-Path 'C:\Program Files\LLVM\bin'
    Add-Path (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links')
    # A tools folder shared with other projects may hold Python, Git and the GitHub CLI too (a build PC's).
    if ($ToolsCache) { foreach ($d in 'python', 'mingit\cmd', 'gh\bin') { Add-Path (Join-Path $ToolsCache $d) } }
}
$Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
function Find-VS {
    if (-not (Test-Path $Vswhere)) { return $null }
    $p = & $Vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($p) { return "$p".Trim() }
    return $null
}
# The Windows SDK (kernel32.lib and the other system libraries the linker needs). Build Tools' C++ workload does not
# always bring it (2026-10-14, a fresh laptop): checked on its own.
function Find-WinSdk {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
    $lib = @(Get-ChildItem -Path (Join-Path $kits '*\um\x64\kernel32.lib') -ErrorAction SilentlyContinue) | Select-Object -First 1
    if ($lib) { return $lib.FullName }
    return $null
}
function Get-CMakeVersionOk {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { return $false }
    $first = "$((& cmake --version) | Select-Object -First 1)"
    if ($first -match '(\d+)\.(\d+)') { return ([int]$matches[1] -gt 3) -or ([int]$matches[1] -eq 3 -and [int]$matches[2] -ge 28) }
    return $false
}
# The winget ids of the tools still missing.
function Get-MissingTools {
    Refresh-Path
    $missing = @()
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += 'Git.Git' }
    if (-not (Get-Command python -ErrorAction SilentlyContinue) -and -not (Get-Command py -ErrorAction SilentlyContinue)) { $missing += 'Python.Python.3.12' }
    if (-not (Get-CMakeVersionOk)) { $missing += 'Kitware.CMake' }
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { $missing += 'Ninja-build.Ninja' }
    if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) { $missing += 'LLVM.LLVM' }
    if (-not (Find-VS)) { $missing += 'Microsoft.VisualStudio.2022.BuildTools' }
    elseif (-not (Find-WinSdk)) { $missing += 'WindowsSDK' }
    return $missing
}
# Loads vcvars64 (MSVC headers, libraries, Windows SDK) into this session, keeping CMake 3.28+ and LLVM first.
function Use-MSVC {
    $vs = Find-VS
    if (-not $vs) {
        if ($DryRun) { Step 'no Visual Studio 2022 Build Tools yet: would load vcvars64.bat from it'; return }
        Fail 'Visual Studio 2022 Build Tools (C++) not found: run 1-INSTALL-TOOLS.ps1'
    }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    Step "MSVC environment: $vcvars"
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
    $env:PATH = "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:PATH"
    if (-not (Find-WinSdk)) {
        if ($DryRun) { Step 'no Windows SDK yet (1-INSTALL-TOOLS.ps1 adds it)' }
        else { Fail 'the Windows SDK is missing (kernel32.lib): run 1-INSTALL-TOOLS.ps1 (NDS Manager: Set up this PC)' }
    }
    foreach ($t in 'cmake', 'ninja', 'clang-cl', 'lld-link') {
        $c = Get-Command $t -ErrorAction SilentlyContinue
        if (-not $c) {
            if ($DryRun) { Step "$t not found yet (1-INSTALL-TOOLS.ps1 installs it)"; continue }
            Fail "$t not on PATH: run 1-INSTALL-TOOLS.ps1"
        }
        Add-Content -Path $Log -Value "$t = $($c.Source)"
    }
}

# ---- Android: a folder of tools (Java 17, the Android SDK with this NDK, Gradle's cache, adb) ----------------------
# PH_TOOLS_CACHE names it; else ..\recomp-backups\tools-cache beside the workspace (one shared with other projects), else
# tools-cache in the workspace.
$NdkVersion = '28.2.13676358'
$ToolsCache = $env:PH_TOOLS_CACHE
if (-not $ToolsCache) {
    $ToolsCache = Join-Path (Split-Path -Parent $Root) 'recomp-backups\tools-cache'
    if (-not (Test-Path $ToolsCache)) { $ToolsCache = Join-Path $Root 'tools-cache' }
}
$ApkDir = Join-Path $Root 'apks'
$AndroidDir = Join-Path $Repo 'android'
$Package = 'com.phrecomp.ph'
function Find-Java {
    $c = @()
    Get-Item (Join-Path $ToolsCache 'jdk-17*') -ErrorAction SilentlyContinue | ForEach-Object { $c += Join-Path $_.FullName 'bin\java.exe' }
    if ($env:JAVA_HOME) { $c += Join-Path $env:JAVA_HOME 'bin\java.exe' }
    foreach ($p in 'C:\Program Files\Microsoft\jdk-17*', 'C:\Program Files\Eclipse Adoptium\jdk-17*', 'C:\Program Files\Android\Android Studio\jbr') {
        Get-Item $p -ErrorAction SilentlyContinue | ForEach-Object { $c += Join-Path $_.FullName 'bin\java.exe' }
    }
    $c | Where-Object { Test-Path $_ } | Select-Object -First 1
}
function Find-Sdk {
    @((Join-Path $ToolsCache 'android-sdk'), $env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk')) |
        Where-Object { $_ -and (Test-Path (Join-Path $_ "ndk\$NdkVersion")) } | Select-Object -First 1
}
function Find-Adb {
    $sdk = @((Join-Path $ToolsCache 'android-sdk'), $env:ANDROID_HOME, (Join-Path $env:LOCALAPPDATA 'Android\Sdk')) |
        Where-Object { $_ -and (Test-Path (Join-Path $_ 'platform-tools\adb.exe')) } | Select-Object -First 1
    if ($sdk) { return Join-Path $sdk 'platform-tools\adb.exe' }
    $c = Get-Command adb -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    return $null
}

# ---- a build folder made at another path ----------------------------------------------------------------------------
# The drive has a different letter on each PC (F:, J:, D:): CMake refuses a build folder whose cache was made for another
# source path, and another PC's compilers would not fit it anyway. Such a folder is removed, so it is made again here.
function Clear-IfMoved([string] $dir) {
    if (-not (Test-Path $dir)) { return $false }
    $here = ($Repo -replace '\\', '/') -replace '/+$', ''
    $caches = @(Get-ChildItem -Path $dir -Filter 'CMakeCache.txt' -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 8)
    foreach ($c in $caches) {
        $m = Select-String -Path $c.FullName -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1
        if (-not $m) { continue }
        $made = ($m.Matches[0].Groups[1].Value -replace '\\', '/') -replace '/+$', ''
        if (-not ($made -like "$here*")) {
            if ($DryRun) { Step "would remove $dir (made at $made, now $here)"; return $true }
            Step "removing $($dir): it was made at $made (the drive had another letter there); it is made again here"
            Remove-Item -Recurse -Force $dir
            return $true
        }
    }
    return $false
}
