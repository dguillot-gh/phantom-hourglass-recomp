# 5-BUILD-ANDROID.ps1 -- builds the Android app (APK) of Phantom Hourglass (home PC).
# Needs a PC build first (2-BUILD.ps1): the game's C (ph-recomp\build\release\recomp_generated) and SDL's source come
# from it. Uses the Android toolchain in the tools folder (ph-common.ps1's $ToolsCache, PH_TOOLS_CACHE: Java 17, the
# Android SDK with NDK 28.2, Gradle's cache, an optional signing key), and the PC's CMake 3.28+ and Ninja.
# The APK lands in apks\PhantomHourglass-<time>.apk. The first build takes a while (the whole game for ARM64).
#   powershell -NoProfile -ExecutionPolicy Bypass -File 5-BUILD-ANDROID.ps1 [-DryRun]
[CmdletBinding()]
param([switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

# ---- inputs from the PC build ----
$generated = Join-Path $BuildDir 'recomp_generated'
$sdlSrc = Join-Path $BuildDir '_deps\sdl3-src'
foreach ($p in @((Join-Path $generated 'func_table.c'), (Join-Path $sdlSrc 'android-project\app\src\main\java\org\libsdl\app\SDLActivity.java'))) {
    if (-not (Test-Path $p)) {
        if ($DryRun) { Step "not there yet (2-BUILD.ps1 makes it): $p" } else { Fail "missing ${p}: run 2-BUILD.ps1 first" }
    }
}

# ---- tools ----
Refresh-Path
$java = Find-Java
$sdk = Find-Sdk
# (A dry run reports what is missing and carries on, to check the rest.)
function Need([string] $text) { if ($DryRun) { Step "missing: $text" } else { Fail $text } }
if (-not $java) { Need "no Java 17: run ANDROID-TOOLS.ps1 (RUN-ANDROID.ps1 runs it first)" }
if (-not $sdk) { Need "no Android SDK with NDK ${NdkVersion}: run ANDROID-TOOLS.ps1 (RUN-ANDROID.ps1 runs it first)" }
$cmakeExe = $null; $cmakeVersion = ''; $cmakeDir = ''
if (Get-CMakeVersionOk) {
    $cmakeExe = (Get-Command cmake).Source
    $cmakeVersion = if ("$((& cmake --version) | Select-Object -First 1)" -match '(\d+\.\d+\.\d+)') { $matches[1] } else { '' }
    $cmakeDir = Split-Path -Parent (Split-Path -Parent $cmakeExe)   # ...\CMake (above bin)
} else {
    Need 'CMake 3.28+ is needed: run 1-INSTALL-TOOLS.ps1'
}
$ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
$ninjaPath = if ($ninjaCmd) { $ninjaCmd.Source } else { Need 'Ninja is needed: run 1-INSTALL-TOOLS.ps1'; 'ninja.exe' }
Step "Java: $java"
Step "Android SDK: $sdk"
Step "CMake $cmakeVersion at $cmakeDir; Ninja: $ninjaPath"

# ---- the GPU renderer's OpenGL ES shaders: every one compiled by the NDK's glslc first (tools\check_gles_shaders.py).
# A failure is reported, not fatal: the software renderer does not need them.
$glslc = $null
if ($sdk) { $glslc = Get-ChildItem -Path (Join-Path $sdk "ndk\$NdkVersion\shader-tools") -Filter 'glslc.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 }
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { $py = Get-Command py -ErrorAction SilentlyContinue }
$patched = Join-Path $BuildDir 'third_party\melonds_patched\OpenGL_shaders'
if ($glslc -and $py -and $cmakeExe) {
    $ok = Run-Optional 'check the GPU renderer''s OpenGL ES shaders with the NDK''s glslc' {
        & $py.Source (Join-Path $Repo 'tools\check_gles_shaders.py') $cmakeExe $glslc.FullName $patched }
    if (-not $ok) { Warn "an OpenGL ES shader does not compile (see $Log): the phone's GPU renderer may not start" }
} else {
    Warn 'cannot check the OpenGL ES shaders here (needs Python, CMake and the NDK''s glslc)'
}

# ---- the project: SDL's Java (must match the native SDL exactly) and local.properties ----
$sdlJavaFrom = Join-Path $sdlSrc 'android-project\app\src\main\java\org\libsdl\app'
$sdlJavaTo = Join-Path $AndroidDir 'app\src\main\java\org\libsdl\app'
$props = Join-Path $AndroidDir 'local.properties'
$propsText = @(
    "sdk.dir=$("$sdk".Replace('\', '/'))",
    "cmake.dir=$("$cmakeDir".Replace('\', '/'))"
)
if ($DryRun) {
    Step "would copy SDL's Java $sdlJavaFrom -> $sdlJavaTo"
    Step "would write $props"
} else {
    New-Item -ItemType Directory -Force -Path $sdlJavaTo | Out-Null
    Copy-Item -Path (Join-Path $sdlJavaFrom '*.java') -Destination $sdlJavaTo -Force
    Set-Content -Path $props -Value $propsText -Encoding ASCII
    Step "SDL's Java copied; local.properties written"
}

# ---- Gradle ----
$env:JAVA_HOME = Split-Path -Parent (Split-Path -Parent $java)
$env:GRADLE_USER_HOME = Join-Path $ToolsCache 'gradle'
$env:PATH = "$(Split-Path -Parent $ninjaPath);$env:PATH"
$wrapper = Join-Path $AndroidDir 'gradle\wrapper\gradle-wrapper.jar'
$gradleArgs = @('-classpath', $wrapper, 'org.gradle.wrapper.GradleWrapperMain', '--no-daemon', '-p', $AndroidDir,
                ':app:assembleDebug', "-PphCmakeVersion=$cmakeVersion", "-PphNinja=$ninjaPath",
                "-PphGeneratedDir=$generated", "-PphSdlSource=$sdlSrc")
# One signing key for every PC (the PS1 project's): the phone only installs an update signed like the installed app.
$key = Join-Path $ToolsCache 'debug.keystore'
if (Test-Path $key) {
    $gradleArgs += @("-Pandroid.injected.signing.store.file=$key", '-Pandroid.injected.signing.store.password=android',
                     '-Pandroid.injected.signing.key.alias=androiddebugkey', '-Pandroid.injected.signing.key.password=android')
    Step "signing with the shared key $key"
}
$t0 = Get-Date
Run 'Gradle: build the APK (the first time compiles the whole game for ARM64)' { & $java @gradleArgs }
if ($DryRun) { Step 'DRY RUN OK'; exit 0 }
Step ("APK built in {0:N1} min" -f ((Get-Date) - $t0).TotalMinutes)

$apk = Join-Path $AndroidDir 'app\build\outputs\apk\debug\app-debug.apk'
if (-not (Test-Path $apk)) { Fail "Gradle finished but $apk is missing" }
New-Item -ItemType Directory -Force -Path $ApkDir | Out-Null
$out = Join-Path $ApkDir "PhantomHourglass-$Stamp.apk"
Copy-Item -LiteralPath $apk -Destination $out
Copy-Item -LiteralPath $apk -Destination (Join-Path $ApkDir 'PhantomHourglass-latest.apk') -Force
Step "APK: $out ($([int]((Get-Item $out).Length / 1MB)) MB)"
exit 0
