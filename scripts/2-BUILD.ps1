# 2-BUILD.ps1 -- builds Phantom Hourglass from your ROM (home PC).
# The recompiler turns the ROM's code into C (ph-recomp\build\release\recomp_generated, never committed), then the
# game (ph_game.dll) and the program (PhantomHourglass.exe) are compiled. The first build also downloads SDL3.
# -GenerateOnly makes only the C (the recompiler and its output), what the Android build needs: no PC game or program
# (the build PC, scripts\ci-build.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File 2-BUILD.ps1 [-Clean] [-Jobs N] [-GenerateOnly] [-DryRun]
[CmdletBinding()]
param([switch] $Clean, [int] $Jobs = 0, [switch] $GenerateOnly, [switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$rom = Find-Rom

# The source tree: everything the configure step reads.
$need = @('CMakeLists.txt', 'CMakePresets.json', 'third_party\CMakeLists.txt', 'frontend\CMakeLists.txt',
          'frontend\config\azee\functions.txt', 'frontend\config\azee\hooks.txt', 'frontend\config\azee\regions.txt',
          'frontend\src\main.cpp', 'frontend\src\ph_hooks.cpp', 'frontend\res\app.ico', 'runtime\include\ntr\recomp.h',
          'recompiler\src\main.cpp')
foreach ($n in $need) { if (-not (Test-Path (Join-Path $Repo $n))) { Fail "missing $Repo\$n" } }
if (-not (Test-Path (Join-Path $Repo 'third_party\melonDS\src\NDS.cpp'))) {
    Run 'fetch the melonDS submodule' { git -C $Repo submodule update --init --depth 1 }
}
Step "source tree OK"

$missing = @(Get-MissingTools)
if ($missing.Count -gt 0) {
    if ($DryRun) { Step "tools missing (1-INSTALL-TOOLS.ps1 installs them): $($missing -join ', ')" }
    else { Fail "tools missing: $($missing -join ', '). Run 1-INSTALL-TOOLS.ps1 first" }
}
Use-MSVC

if ($Clean -and (Test-Path $BuildDir)) {
    if ($DryRun) { Step "would remove $BuildDir" } else { Step "clean: removing $BuildDir"; Remove-Item -Recurse -Force $BuildDir }
}
Push-Location $Repo
try {
    Run 'configure (cmake --preset release)' { cmake --preset release "-DPH_ROM=$rom" }
    $jobArgs = @()
    if ($Jobs -gt 0) { $jobArgs = @('-j', "$Jobs") }
    $t0 = Get-Date
    if ($GenerateOnly) {
        Run 'build: recompile the ROM to C only (for the Android build)' { cmake --build --preset release --target ph_generate @jobArgs }
        $func = Join-Path $BuildDir 'recomp_generated\func_table.c'
        if ($DryRun) { Step "would produce $func"; Step 'DRY RUN OK'; exit 0 }
        if (-not (Test-Path $func)) { Fail "the build finished but $func is missing" }
        Step ("C generated in {0:N1} min: $func" -f ((Get-Date) - $t0).TotalMinutes)
        exit 0
    }
    Run 'build: recompile the ROM, compile the game and the program (10-30+ min the first time)' { cmake --build --preset release @jobArgs }
    if (-not $DryRun) { Step ("build done in {0:N1} min" -f ((Get-Date) - $t0).TotalMinutes) }
    # The phone's GPU renderer on the PC: the program again with the phone's OpenGL ES shaders and shims (the
    # gles-test preset, program only, a few minutes), which 4-TEST.ps1 runs. A failure here does not stop the rest.
    $ok = Run-Optional 'gles-test: configure the program with the phone renderer''s shaders' { cmake --preset gles-test }
    if ($ok) { $ok = Run-Optional 'gles-test: build it (a few minutes)' { cmake --build --preset gles-test @jobArgs } }
    if (-not $ok) { Warn "the gles-test program did not build (see $Log); the game itself is fine" }
    elseif (-not $DryRun) { Step 'gles-test program built' }
} finally { Pop-Location }

$exe = Join-Path $BuildDir 'frontend\PhantomHourglass.exe'
$dll = Join-Path $BuildDir 'frontend\ph_game.dll'
if ($DryRun) { Step "would produce $exe and $dll"; Step 'DRY RUN OK'; exit 0 }
if (-not (Test-Path $exe) -or -not (Test-Path $dll)) { Fail "the build finished but $exe / $dll are missing" }
Step "BUILT: $exe"
exit 0
