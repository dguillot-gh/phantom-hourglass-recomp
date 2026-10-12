# 6-INSTALL-PHONE.ps1 -- installs the Android app on the phone (USB or wireless debugging) and gives it your game.
#   - installs apks\PhantomHourglass-latest.apk (keeps the app's data: an update, not a fresh install)
#   - copies your ROM into the app, if the app does not have it yet
#   - copies a save into the app ONLY if the app has none yet: the PC game folder's (PhantomHourglass\data\saves),
#     else your melonDS .ml1.sav. A save already on the phone is never replaced.
#   - opens the app
#   powershell -NoProfile -ExecutionPolicy Bypass -File 6-INSTALL-PHONE.ps1 [-DryRun]
[CmdletBinding()]
param([switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$apk = Join-Path $ApkDir 'PhantomHourglass-latest.apk'
if (-not (Test-Path $apk)) {
    if ($DryRun) { Step "no APK yet ($apk): 5-BUILD-ANDROID.ps1 makes it" } else { Fail "no APK: run 5-BUILD-ANDROID.ps1 first" }
}
$adb = Find-Adb
if (-not $adb) { Fail "adb not found (the Android SDK in the tools folder has it: $ToolsCache\android-sdk\platform-tools)" }
Step "adb: $adb"

# Runs adb; returns its output (and stops on failure unless -Soft).
function Adb([string[]] $a, [switch] $Soft) {
    if ($DryRun) { Step "would run: adb $($a -join ' ')"; return '' }
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $o = (& $adb @a 2>&1 | ForEach-Object { "$_" }) -join "`n"
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    Add-Content -Path $Log -Value "adb $($a -join ' ') -> $code`n$o"
    if ($code -ne 0 -and -not $Soft) { Fail "adb $($a -join ' ') failed: $o" }
    return $o
}

if (-not $DryRun) {
    $devices = @((Adb @('devices')) -split "`n" | Where-Object { $_ -match "`tdevice$" })
    if ($devices.Count -eq 0) { Fail 'no phone: plug it in (USB debugging on) or pair wireless debugging, then run this again' }
    Step "phone: $($devices[0] -replace "`t.*", '')"
}

Adb @('install', '-r', $apk) | Out-Null
Step 'app installed'

# The app's own files (a debug build lets adb reach them with run-as). Files go through a pipe: run-as cannot read adb's folder.
function In-App([string] $cmd) { return Adb @('shell', "run-as $Package sh -c '$cmd'") -Soft }
# True if the app has this file (not just text in an error message that happens to name it).
function Has-File([string] $path) { return (In-App "test -f $path && echo PRESENT") -match 'PRESENT' }
function Push-Into-App([string] $local, [string] $dest) {
    $tmp = '/data/local/tmp/ph-push.bin'
    Adb @('push', $local, $tmp) | Out-Null
    $dir = $dest.Substring(0, $dest.LastIndexOf('/'))
    Adb @('shell', "run-as $Package mkdir -p $dir && cat $tmp | run-as $Package sh -c 'cat > $dest'; rm -f $tmp") | Out-Null
}

$rom = Find-Rom
if ($DryRun -or -not (Has-File 'files/rom/azee.nds')) {
    Push-Into-App $rom 'files/rom/azee.nds'
    Step 'ROM copied into the app'
} else {
    Step 'the app has the ROM already'
}

$pcSave = Join-Path $GameDir 'data\saves\azee.sav'
$from = if (Test-Path $pcSave) { $pcSave } elseif (Test-Path -LiteralPath $MelonSave) { $MelonSave } else { $null }
if (-not $DryRun -and (Has-File 'files/saves/azee.sav')) {
    Step 'the app has a save already: kept (never replaced)'
} elseif ($from) {
    Push-Into-App $from 'files/saves/azee.sav'
    Step "save copied into the app from $from"
} else {
    Step 'no save to copy: the game starts with a new one'
}

Adb @('shell', 'am', 'start', '-n', "$Package/.LauncherActivity") -Soft | Out-Null
Step 'DONE: the app is open on the phone (Play)'
exit 0
