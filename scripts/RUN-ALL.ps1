# RUN-ALL.ps1 -- runs the four Phantom Hourglass steps in order and stops at the first that fails (home PC):
#   1-INSTALL-TOOLS.ps1   tools (once; skips what is installed)
#   2-BUILD.ps1           recompile the ROM and build the game
#   3-MAKE-GAME-FOLDER.ps1  PhantomHourglass\ with PLAY.cmd: the game, ready to play
#   4-TEST.ps1            plays it for 40 s by itself, screenshots in logs\
#   powershell -NoProfile -ExecutionPolicy Bypass -File RUN-ALL.ps1 [-DryRun] [-NoTest]
[CmdletBinding()]
param([switch] $DryRun, [switch] $NoTest)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$steps = @('1-INSTALL-TOOLS.ps1', '2-BUILD.ps1', '3-MAKE-GAME-FOLDER.ps1')
if (-not $NoTest) { $steps += '4-TEST.ps1' }
foreach ($s in $steps) {
    Step "---- $s"
    $global:LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot $s) -DryRun:$DryRun
    if ($LASTEXITCODE -ne 0) { Fail "$s failed (see logs\progress.log)" }
}
if ($DryRun) { Step 'DRY RUN OK: every step checked, nothing installed or built' }
else { Step "ALL DONE: play with $(Join-Path $GameDir 'PLAY.cmd')" }
