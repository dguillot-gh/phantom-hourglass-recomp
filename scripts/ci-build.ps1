# ci-build.ps1 -- what an automatic build on a build PC does (a self-hosted GitHub runner; see BUILD-PC.md).
#   1. copies your ROM in from -RomSource (a NAS folder) if the workspace has none
#   2. recompiles it to C (2-BUILD.ps1 -GenerateOnly: no PC game, quicker on a small PC)
#   3. builds the APK (5-BUILD-ANDROID.ps1; signed with PH_KEYSTORE when set)
#   4. with -ReleaseRepo, uploads it as a GitHub Release of that repository (keep it PRIVATE: the APK contains the game)
# The repository's workspace is the folder it is in, as for the other scripts. Tools: the usual ones (1-INSTALL-TOOLS.ps1)
# and the Android tools folder (PH_TOOLS_CACHE, with NDK 28.2: ANDROID-TOOLS.ps1 once by hand, it asks for the licence).
#   powershell -NoProfile -ExecutionPolicy Bypass -File ci-build.ps1 [-RomSource <folder>] [-ReleaseRepo owner/name]
#              [-Sha <commit>] [-Keep N] [-Clean] [-DryRun]
[CmdletBinding()]
param([string] $RomSource = $env:PH_ROM_SOURCE, [string] $ReleaseRepo = '', [string] $Sha = '', [int] $Keep = 5,
      [switch] $Clean, [switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

Refresh-Path  # (with the tools folder's Python, Git and gh)

# ---- room on the disk: the C and the Android build take about 6 GB ----
$drive = Get-PSDrive -Name ((Split-Path -Qualifier $Root).TrimEnd(':')) -ErrorAction SilentlyContinue
if ($drive -and $drive.Free -lt 8GB) { Fail ("only {0:N1} GB free on {1}: at least 8 GB is needed" -f ($drive.Free / 1GB), $drive.Name) }

# ---- the ROM ----
$have = @(Get-ChildItem -Path $Root, $RomsDir, $RomDir -Filter '*.nds' -File -ErrorAction SilentlyContinue)  # (where Find-Rom looks)
if ($have.Count -eq 0) {
    if (-not $RomSource) { Fail "no ROM in $Root and no -RomSource (or PH_ROM_SOURCE) to copy one from" }
    if (-not (Test-Path -LiteralPath $RomSource)) { Fail "the ROM folder $RomSource cannot be reached (NAS password saved for this user? BUILD-PC.md)" }
    $roms = @(Get-ChildItem -LiteralPath $RomSource -Filter '*.nds' -File)
    if ($roms.Count -eq 0) { Fail "no .nds file in $RomSource" }
    foreach ($r in $roms) {
        if ($DryRun) { Step "would copy $($r.FullName) into $Root" } else { Copy-Item -LiteralPath $r.FullName -Destination $Root }
    }
    Step "ROM copied in from $RomSource"
}
if (-not $DryRun) { $null = Find-Rom }

# ---- build ----
if ($Clean) {
    foreach ($d in @($BuildDir, (Join-Path $AndroidDir 'app\.cxx'), (Join-Path $AndroidDir 'app\build'))) {
        if (Test-Path $d) {
            if ($DryRun) { Step "would remove $d" } else { Step "clean: removing $d"; Remove-Item -Recurse -Force $d }
        }
    }
}
$t0 = Get-Date
Step '---- 2-BUILD.ps1 -GenerateOnly'
$global:LASTEXITCODE = 0
& (Join-Path $PSScriptRoot '2-BUILD.ps1') -GenerateOnly -DryRun:$DryRun
if ($LASTEXITCODE -ne 0) { Fail '2-BUILD.ps1 failed (see logs\progress.log)' }
# Not ANDROID-TOOLS.ps1 itself: it may ask for the SDK licence, and nobody is there to answer.
if (-not (Find-Java) -or -not (Find-Sdk)) {
    $msg = "no Java 17 or Android SDK with NDK $NdkVersion in ${ToolsCache}: run ANDROID-TOOLS.ps1 once by hand on this PC (BUILD-PC.md)"
    if ($DryRun) { Step "missing: $msg" } else { Fail $msg }
}
Step '---- 5-BUILD-ANDROID.ps1'
$global:LASTEXITCODE = 0
& (Join-Path $PSScriptRoot '5-BUILD-ANDROID.ps1') -DryRun:$DryRun
if ($LASTEXITCODE -ne 0) { Fail '5-BUILD-ANDROID.ps1 failed (see logs\progress.log)' }
if (-not $DryRun) { Step ("built in {0:N1} min" -f ((Get-Date) - $t0).TotalMinutes) }

# ---- the Release ----
if (-not $ReleaseRepo) { if ($DryRun) { Step 'DRY RUN OK' } else { Step 'DONE (no -ReleaseRepo: APK left in apks\)' }; exit 0 }
Refresh-Path
$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) { Fail 'gh (GitHub CLI) not found: it uploads the Release' }
$short = if ($Sha) { $Sha -replace '^(.{7}).*$', '$1' } else { 'manual' }  # (no .NET calls: constrained-language safe)
$tag = "ph-$Stamp-$short"
$apk = Join-Path $ApkDir 'PhantomHourglass-latest.apk'
$named = Join-Path $ApkDir "PhantomHourglass-$short.apk"
$title = "Phantom Hourglass - $(Get-Date -Format 'yyyy-MM-dd HH:mm') ($short)"
$notes = "Built from phantom-hourglass-recomp $short. Contains the game: private, do not share."
if ($DryRun) {
    Step "would upload $named as Release $tag ($title) to $ReleaseRepo, keeping the newest $Keep Phantom Hourglass Releases"
    Step 'DRY RUN OK'
    exit 0
}
Copy-Item -LiteralPath $apk -Destination $named -Force
Run "upload Release $tag to $ReleaseRepo" { & $gh.Source release create $tag $named --repo $ReleaseRepo --title $title --notes $notes }
# Older Phantom Hourglass Releases beyond the newest $Keep go (with their tags); other games' Releases are not touched.
$old = @(& $gh.Source release list --repo $ReleaseRepo --limit 200 --json tagName,createdAt --jq '.[] | select(.tagName | startswith("ph-")) | .tagName' |
         Where-Object { $_ } | Sort-Object -Descending | Select-Object -Skip $Keep)
foreach ($t in $old) { Run-Optional "remove old Release $t" { & $gh.Source release delete $t --repo $ReleaseRepo --cleanup-tag --yes } | Out-Null }
Remove-Item -LiteralPath $named -Force
Step "DONE: Release $tag"
exit 0
