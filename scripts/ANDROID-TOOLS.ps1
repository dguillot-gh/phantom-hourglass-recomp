# ANDROID-TOOLS.ps1 -- gets what building the Android app needs, once (RUN-ANDROID.ps1 runs it first).
#   Java 17 (Microsoft OpenJDK, with winget) and the Android SDK: platform 35, build-tools 34, NDK 28.2 and adb, in the
#   tools folder (ph-common.ps1: PH_TOOLS_CACHE, else ..\recomp-backups\tools-cache, else tools-cache in the workspace).
# A Java 17 or an Android SDK with that NDK already on the PC (Android Studio's, JAVA_HOME, ANDROID_HOME) is used as it
# is. The first time downloads about 1.5 GB. Android's SDK licence is shown and you accept it yourself (answer y).
#   powershell -NoProfile -ExecutionPolicy Bypass -File ANDROID-TOOLS.ps1 [-DryRun]
[CmdletBinding()]
param([switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

Refresh-Path
$ProgressPreference = 'SilentlyContinue'  # (Windows PowerShell's download progress bar makes downloads crawl)

# ---- Java 17 ----
$java = Find-Java
if (-not $java) {
    if (-not $DryRun -and -not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail 'no Java 17, and winget is not available to install it: install "App Installer" from the Microsoft Store, or Java 17 yourself'
    }
    Run 'install Java 17 (Microsoft OpenJDK)' { winget install --id Microsoft.OpenJDK.17 -e --accept-package-agreements --accept-source-agreements }
    Refresh-Path
    $java = Find-Java
    if (-not $java -and -not $DryRun) { Fail 'Java 17 was installed but is not found yet: open a new PowerShell window and run this again' }
}
if ($java) { Step "Java: $java" }

# ---- the Android SDK ----
$sdk = Find-Sdk
if ($sdk) {
    Step "Android SDK: $sdk (NDK $NdkVersion present)"
    Step 'Android tools present'
    exit 0
}
$sdkDir = Join-Path $ToolsCache 'android-sdk'
$sdkManager = Join-Path $sdkDir 'cmdline-tools\latest\bin\sdkmanager.bat'
Step "no Android SDK with NDK $NdkVersion yet: setting one up in $sdkDir"
if (-not (Test-Path $sdkManager)) {
    $url = 'https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip'
    if ($DryRun) {
        Step "would download Android's command-line tools ($url) into $sdkDir\cmdline-tools\latest"
    } else {
        New-Item -ItemType Directory -Force -Path $sdkDir | Out-Null
        $zip = Join-Path $sdkDir 'commandlinetools-win.zip'
        $unpack = Join-Path $sdkDir 'cmdline-tools-unpack'
        Step "downloading Android's command-line tools (about 150 MB)"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $unpack -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $sdkDir 'cmdline-tools') | Out-Null
        Move-Item -Path (Join-Path $unpack 'cmdline-tools') -Destination (Join-Path $sdkDir 'cmdline-tools\latest')
        Remove-Item -Recurse -Force -Path $unpack, $zip
        if (-not (Test-Path $sdkManager)) { Fail "the command-line tools did not unpack as expected ($sdkManager missing)" }
    }
}
if ($DryRun) {
    Step "would show Android's SDK licence for you to accept, then install platform-tools, platforms;android-35, build-tools;34.0.0, ndk;$NdkVersion"
    Step 'DRY RUN OK'
    exit 0
}
$env:JAVA_HOME = Split-Path -Parent (Split-Path -Parent $java)
Step "Android's SDK licence follows: read it and answer y to accept (nothing is installed if you do not)"
& $sdkManager "--sdk_root=$sdkDir" --licenses
if ($LASTEXITCODE -ne 0) { Fail "the Android SDK licence was not accepted, so the SDK was not installed" }
Run 'install the Android SDK parts: NDK, platform 35, build-tools, adb (about 1.3 GB)' {
    & $sdkManager "--sdk_root=$sdkDir" 'platform-tools' 'platforms;android-35' 'build-tools;34.0.0' "ndk;$NdkVersion"
}
if (-not (Find-Sdk)) { Fail "the SDK was installed but NDK $NdkVersion is not in $sdkDir" }
Step 'Android tools present'
exit 0
