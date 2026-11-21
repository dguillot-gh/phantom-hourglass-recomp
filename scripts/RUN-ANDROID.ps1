# RUN-ANDROID.ps1 -- the Android app, after the PC build (RUN-ALL.ps1): build the APK, install it on the phone.
#   ANDROID-TOOLS.ps1     Java 17 and the Android SDK, once (asks you to accept Android's SDK licence)
#   5-BUILD-ANDROID.ps1   the APK (apks\PhantomHourglass-latest.apk)
#   6-INSTALL-PHONE.ps1   install, copy the ROM and (only if the phone has none) a save, open the app
#   powershell -NoProfile -ExecutionPolicy Bypass -File RUN-ANDROID.ps1 [-Region usa|eur] [-Rom <path>] [-DryRun] [-NoPhone]
[CmdletBinding()]
param([string] $Region = '', [string] $Rom = '', [switch] $DryRun, [switch] $NoPhone)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$steps = @('ANDROID-TOOLS.ps1', '5-BUILD-ANDROID.ps1')
if (-not $NoPhone) { $steps += '6-INSTALL-PHONE.ps1' }
foreach ($s in $steps) {
    Step "---- $s"
    $global:LASTEXITCODE = 0
    if ($s -eq '6-INSTALL-PHONE.ps1') {
        & (Join-Path $PSScriptRoot $s) -Region $Region -Rom $Rom -DryRun:$DryRun
    } else {
        & (Join-Path $PSScriptRoot $s) -DryRun:$DryRun
    }
    if ($LASTEXITCODE -ne 0) { Fail "$s failed (see logs\progress.log)" }
}
if ($DryRun) { Step 'DRY RUN OK' } else { Step 'ANDROID DONE' }
