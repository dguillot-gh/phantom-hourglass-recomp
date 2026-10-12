# 4-TEST.ps1 -- plays the finished game for about 50 seconds by itself and takes screenshots (home PC). Along the way it
# saves a state at the file select screen, moves on, loads the state back and checks the game carries on from it.
# Runs PhantomHourglass\PhantomHourglass.exe on a scratch COPY of the save and a scratch settings folder: the save in
# PhantomHourglass\data and your melonDS save are never touched. Results: logs\test-<time>\ (PNG screenshots,
# stderr.log). A window opens while it runs.
#   powershell -NoProfile -ExecutionPolicy Bypass -File 4-TEST.ps1 [-DryRun]
[CmdletBinding()]
param([switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$exe = Join-Path $GameDir 'PhantomHourglass.exe'
$rom = Join-Path $GameDir 'rom\azee.nds'
if (-not (Test-Path $exe) -or -not (Test-Path $rom)) {
    if ($DryRun) { Step "no game folder yet ($GameDir): 3-MAKE-GAME-FOLDER.ps1 makes it" }
    else { Fail "no game folder yet: run 3-MAKE-GAME-FOLDER.ps1 first ($GameDir)" }
}

$out = Join-Path $LogDir ("test-$Stamp" + $(if ($DryRun) { '-dryrun' } else { '' }))
$data = Join-Path $out 'data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
# The save to start from: the game folder's, else the melonDS one -- always a copy.
$saveArgs = @()
$from = Join-Path $GameDir 'data\saves\azee.sav'
if (-not (Test-Path $from)) { $from = $MelonSave }
if (Test-Path -LiteralPath $from) {
    Copy-Item -LiteralPath $from -Destination (Join-Path $out 'azee.sav')
    $saveArgs = @('--save-file', "`"$(Join-Path $out 'azee.sav')`"")
}
# About 60 frames a second: logos, title, a few presses and taps on the touch screen, screenshots along the way.
$script = Join-Path $out 'input.txt'
Set-Content -Path $script -Encoding ASCII -Value @(
    "300 shot $out\01-logos.png",
    "900 shot $out\02-title.png",
    "960 touch 128,96 6",
    "1000 press Start 6",
    "1200 shot $out\03-after-start.png",
    "1260 press A 6",
    "1500 shot $out\04.png",
    "1560 touch 128,120 6",
    "1800 shot $out\05.png",
    "2400 shot $out\06.png",
    "2410 savestate 1",
    "2440 touch 128,67 6",
    "2700 shot $out\07-moved-on.png",
    "2710 loadstate 1",
    "2760 shot $out\08-state-loaded.png",
    "2900 shot $out\09-after-load.png",
    "2960 quit"
)
$runArgs = @("`"$rom`"", '--data-dir', "`"$data`"", '--no-settings', '--input-script', "`"$script`"") + $saveArgs
if ($DryRun) {
    Step "would start: $exe $($runArgs -join ' ')"
    Step "test files written to $out (input.txt$(if ($saveArgs.Count) { ', save copy' }))"
    Step 'DRY RUN OK'
    exit 0
}
Step "running the game for about 50 s (results in $out)"
$p = Start-Process -FilePath $exe -ArgumentList $runArgs -WorkingDirectory $GameDir -PassThru `
    -RedirectStandardError (Join-Path $out 'stderr.log') -RedirectStandardOutput (Join-Path $out 'stdout.log')
$null = $p.Handle  # (without holding the handle, Windows PowerShell reports no exit code once the process is gone)
if (-not $p.WaitForExit(180000)) { Stop-Process -Id $p.Id -Force; Fail "the game hung (stopped after 3 min); see $out\stderr.log" }
$p.WaitForExit()
$shots = @(Get-ChildItem -Path $out -Filter '*.png' -ErrorAction SilentlyContinue)
Step "game exited with $($p.ExitCode); $($shots.Count) of 9 screenshots in $out"
if ($shots.Count -lt 9) { Fail "only $($shots.Count) of 9 screenshots: the game stopped early; see $out\stderr.log" }
if ($null -ne $p.ExitCode -and $p.ExitCode -ne 0) { Fail "the game exited with $($p.ExitCode); see $out\stderr.log" }
# Save states: slot 1 written at the file select screen, then loaded back, and the game ran on after it (shot 09).
# (Not $log: PowerShell names are case-blind, and $Log is this script's log file.)
$gameLines = Get-Content -Path (Join-Path $out 'stderr.log') -ErrorAction SilentlyContinue
$saved = @($gameLines | Where-Object { $_ -like 'state: Saved to slot 1*' }).Count -gt 0
$loaded = @($gameLines | Where-Object { $_ -like 'state: Loaded slot 1*' }).Count -gt 0
if ((Test-Path (Join-Path $data 'states\slot-01.state')) -and $saved -and $loaded) {
    Step 'save state: saved to slot 1 and loaded back, the game ran on'
} else {
    Warn "save state test: saved=$saved loaded=$loaded; see $out\stderr.log (the lines starting with state:)"
}

# The GPU renderer at 4x: a short second run (a fresh copy of the save), two screenshots at the higher resolution.
# A problem here is reported, not fatal: the software renderer above is the one that must work.
$gpu = Join-Path $out 'gpu'
New-Item -ItemType Directory -Force -Path (Join-Path $gpu 'data') | Out-Null
$gpuSave = @()
if ($saveArgs.Count) {
    Copy-Item -LiteralPath $from -Destination (Join-Path $gpu 'azee.sav')
    $gpuSave = @('--save-file', "`"$(Join-Path $gpu 'azee.sav')`"")
}
Set-Content -Path (Join-Path $gpu 'input.txt') -Encoding ASCII -Value @(
    "600 shot $gpu\gpu-title-4x.png",
    "960 touch 128,96 6",
    "1200 shot $gpu\gpu-4x.png",
    "1260 quit"
)
$gpuArgs = @("`"$rom`"", '--data-dir', "`"$(Join-Path $gpu 'data')`"", '--input-script', "`"$(Join-Path $gpu 'input.txt')`"",
             '--renderer', 'gpu', '--scale', '4') + $gpuSave
Step 'GPU renderer at 4x: about 20 s'
$g = Start-Process -FilePath $exe -ArgumentList $gpuArgs -WorkingDirectory $GameDir -PassThru `
    -RedirectStandardError (Join-Path $gpu 'stderr.log') -RedirectStandardOutput (Join-Path $gpu 'stdout.log')
$null = $g.Handle
if (-not $g.WaitForExit(120000)) { Stop-Process -Id $g.Id -Force; Warn 'the GPU run hung (stopped after 2 min)' }
$gshots = @(Get-ChildItem -Path $gpu -Filter '*.png' -ErrorAction SilentlyContinue)
Step "GPU run: exit $($g.ExitCode), $($gshots.Count) of 2 screenshots in $gpu"

# The phone's GPU renderer, on this PC: the gles-test program (2-BUILD.ps1) runs melonDS's renderer with the phone's
# OpenGL ES shaders and shims, at 4x, with this game folder's ph_game.dll. Reported, not fatal.
$glesExe = Join-Path $Repo 'build\gles-test\frontend\PhantomHourglass.exe'
if (Test-Path $glesExe) {
    $es = Join-Path $out 'gles'
    New-Item -ItemType Directory -Force -Path (Join-Path $es 'data') | Out-Null
    $esSave = @()
    if ($saveArgs.Count) {
        Copy-Item -LiteralPath $from -Destination (Join-Path $es 'azee.sav')
        $esSave = @('--save-file', "`"$(Join-Path $es 'azee.sav')`"")
    }
    Set-Content -Path (Join-Path $es 'input.txt') -Encoding ASCII -Value @(
        "600 shot $es\gles-title-4x.png",
        "960 touch 128,96 6",
        "1500 shot $es\gles-4x.png",
        "1560 quit"
    )
    $esArgs = @("`"$rom`"", '--data-dir', "`"$(Join-Path $es 'data')`"", '--input-script', "`"$(Join-Path $es 'input.txt')`"",
                '--renderer', 'gpu', '--scale', '4', '--module', "`"$(Join-Path $GameDir 'ph_game.dll')`"") + $esSave
    Step "the phone's GPU renderer (OpenGL ES shaders) on this PC at 4x: about 25 s"
    $e = Start-Process -FilePath $glesExe -ArgumentList $esArgs -WorkingDirectory $GameDir -PassThru `
        -RedirectStandardError (Join-Path $es 'stderr.log') -RedirectStandardOutput (Join-Path $es 'stdout.log')
    $null = $e.Handle
    if (-not $e.WaitForExit(120000)) { Stop-Process -Id $e.Id -Force; Warn 'the phone-renderer run hung (stopped after 2 min)' }
    $eshots = @(Get-ChildItem -Path $es -Filter '*.png' -ErrorAction SilentlyContinue)
    $esLog = Get-Content -Path (Join-Path $es 'stderr.log') -ErrorAction SilentlyContinue
    $esGpu = @($esLog | Where-Object { $_ -like 'video: OpenGL renderer*' }).Count -gt 0
    Step "phone-renderer run: exit $($e.ExitCode), $($eshots.Count) of 2 screenshots, GPU renderer started: $esGpu ($es)"
} else {
    Warn "no gles-test program ($glesExe): 2-BUILD.ps1 builds it; the phone renderer was not tested on the PC"
}
Step 'TEST DONE: tell Claude "PH test finished" and it will look at the screenshots and logs'
exit 0
