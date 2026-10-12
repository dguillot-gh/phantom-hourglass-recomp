# 3-MAKE-GAME-FOLDER.ps1 -- puts the built game in PhantomHourglass\ next to this script, ready to play:
#   PhantomHourglass\
#     PLAY.cmd                          double-click to play
#     PhantomHourglass.exe, ph_game.dll the program and the recompiled game
#     rom\azee.nds                      a copy of your ROM (the original is untouched)
#     portable.txt                      keeps settings and saves in data\ here instead of %APPDATA%
#     data\saves\azee.sav               your melonDS save, copied ONCE (an existing save here is never overwritten)
#     README.md, LICENSE, NOTICE.md     the controls; GPL-3.0 and where the code comes from
# Running it again updates the program and the game; saves and settings in data\ are kept.
#   powershell -NoProfile -ExecutionPolicy Bypass -File 3-MAKE-GAME-FOLDER.ps1 [-DryRun]
[CmdletBinding()]
param([switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$rom = Find-Rom
$exe = Join-Path $BuildDir 'frontend\PhantomHourglass.exe'
$dll = Join-Path $BuildDir 'frontend\ph_game.dll'
if (-not (Test-Path $exe) -or -not (Test-Path $dll)) {
    if ($DryRun) { Step "not built yet ($exe): 2-BUILD.ps1 makes it" }
    else { Fail "not built yet: run 2-BUILD.ps1 first ($exe)" }
}

$copies = @(
    @($exe, (Join-Path $GameDir 'PhantomHourglass.exe')),
    @($dll, (Join-Path $GameDir 'ph_game.dll')),
    @($rom, (Join-Path $GameDir 'rom\azee.nds')),
    @((Join-Path $Repo 'README.md'), (Join-Path $GameDir 'README.md')),
    @((Join-Path $Repo 'LICENSE'), (Join-Path $GameDir 'LICENSE')),
    @((Join-Path $Repo 'NOTICE.md'), (Join-Path $GameDir 'NOTICE.md'))
)
# Left over from the first build, which used FFCCEOT-Recomp's own program (never the save).
$old = @('ffcc.exe', 'cfie_game.dll', 'data\settings.ini', 'data\saves\azee.wfc') | ForEach-Object { Join-Path $GameDir $_ }
$save = Join-Path $GameDir 'data\saves\azee.sav'
$play = @(
    '@echo off',
    'rem The Legend of Zelda: Phantom Hourglass (native recomp). Controls: README.md. Fullscreen: F11.',
    'cd /d "%~dp0"',
    'start "" PhantomHourglass.exe "%~dp0rom\azee.nds"'
)

if ($DryRun) {
    foreach ($c in $copies) { Step "would copy $($c[0]) -> $($c[1])" }
    foreach ($o in $old) { if (Test-Path $o) { Step "would remove the old $o" } }
    if (Test-Path $save) { Step "save already at ${save}: kept" }
    elseif (Test-Path -LiteralPath $MelonSave) { Step "would copy the melonDS save $MelonSave -> $save (once)" }
    else { Step 'no melonDS save found: the game starts with a new save' }
    Step "would write $(Join-Path $GameDir 'PLAY.cmd') and portable.txt"
    Step 'DRY RUN OK'
    exit 0
}

foreach ($d in @($GameDir, (Join-Path $GameDir 'rom'), (Join-Path $GameDir 'data\saves'))) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
foreach ($c in $copies) { Copy-Item -LiteralPath $c[0] -Destination $c[1] -Force }
foreach ($o in $old) { if (Test-Path $o) { Remove-Item -LiteralPath $o -Force; Step "removed the old $o" } }
Step 'copied the program, the game and the ROM'
if (Test-Path $save) {
    Step "save kept: $save"
} elseif (Test-Path -LiteralPath $MelonSave) {
    Copy-Item -LiteralPath $MelonSave -Destination $save
    Step "copied your melonDS save to $save (the original stays where it was)"
} else {
    Step 'no melonDS save found: the game starts with a new save'
}
Set-Content -Path (Join-Path $GameDir 'portable.txt') -Value 'Keep settings and saves in the data folder next to the program.' -Encoding ASCII
Set-Content -Path (Join-Path $GameDir 'PLAY.cmd') -Value $play -Encoding ASCII
Step "READY: double-click $(Join-Path $GameDir 'PLAY.cmd')"
exit 0
