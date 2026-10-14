# 1-INSTALL-TOOLS.ps1 -- installs what building Phantom Hourglass needs (home PC; once).
#   Git, Python 3, CMake 3.28+, Ninja, LLVM (clang-cl), Visual Studio 2022 Build Tools with the C++ workload and the
#   Windows SDK (added on its own when Build Tools is there without it).
# Uses winget. Build Tools asks for admin once and downloads about 3 GB. Tools already installed are skipped.
#   powershell -NoProfile -ExecutionPolicy Bypass -File 1-INSTALL-TOOLS.ps1 [-DryRun]
[CmdletBinding()]
param([switch] $DryRun)
. (Join-Path $PSScriptRoot 'ph-common.ps1')

$missing = @(Get-MissingTools)
if ($missing.Count -eq 0) { Step 'all tools present'; exit 0 }
Step "missing: $($missing -join ', ')"
if (-not $DryRun -and -not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail 'winget is not available: install "App Installer" from the Microsoft Store, then run this again'
}
foreach ($m in $missing) {
    if ($m -eq 'Microsoft.VisualStudio.2022.BuildTools') {
        Run "install $m (C++ workload; asks for admin)" {
            winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override '--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --includeRecommended'
        }
    } elseif ($m -eq 'WindowsSDK') {
        # Build Tools is there without the SDK. First the standalone Windows SDK with winget (like the other tools), then,
        # if that did not bring it, the Visual Studio Installer adds it to Build Tools (asks for admin).
        $ok = Run-Optional 'install the Windows SDK (Microsoft.WindowsSDK.10.0.26100, about 2 GB)' {
            winget install --id Microsoft.WindowsSDK.10.0.26100 -e --accept-package-agreements --accept-source-agreements
        }
        if (-not $DryRun -and -not (Find-WinSdk)) {
            $vs = Find-VS
            $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
            Step 'add the Windows SDK to Build Tools with the Visual Studio Installer (Microsoft.VisualStudio.Component.Windows11SDK.26100; asks for admin)'
            # (No --wait: that is the Build Tools bootstrapper's option; setup.exe rejects it with exit 87.)
            $p = Start-Process -FilePath $setup -Verb RunAs -Wait -PassThru -ArgumentList @('modify', '--installPath', "`"$vs`"",
                '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100', '--passive', '--norestart')
            Add-Content -Path $Log -Value "Visual Studio Installer exit code: $($p.ExitCode)"
            if (-not (Find-WinSdk)) {
                # Windows lists the SDK as installed but its files are gone: installers then skip it and repair does not
                # bring the files back. Only removing it and installing it again does.
                $kitsLib = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
                if (-not (Test-Path $kitsLib)) {
                    Fail ("the Windows SDK is listed as installed but its files are gone ($kitsLib is missing). " +
                          "Settings > Apps > Installed apps: uninstall every 'Windows Software Development Kit', then run this again")
                }
                Fail "the Windows SDK is still missing (winget, then the Visual Studio Installer: exit $($p.ExitCode)); add it in Visual Studio Installer: Modify, Individual components, Windows 11 SDK"
            }
        }
    } else {
        $id = $m
        Run "install $id" { winget install --id $id -e --accept-package-agreements --accept-source-agreements }
    }
}
if ($DryRun) { Step 'DRY RUN OK'; exit 0 }
$still = @(Get-MissingTools)
if ($still.Count -gt 0) { Fail "still missing after install: $($still -join ', ') (a new PowerShell window may be needed)" }
Step 'all tools present'
exit 0
