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
        # Build Tools is there without the SDK: add it with the Visual Studio Installer (asks for admin).
        $vs = Find-VS
        $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
        if ($DryRun) { Step "would add the Windows SDK to $vs with $setup" }
        else {
            Step 'add the Windows SDK to Build Tools (Microsoft.VisualStudio.Component.Windows11SDK.26100; asks for admin, about 2 GB)'
            $p = Start-Process -FilePath $setup -Verb RunAs -Wait -PassThru -ArgumentList @('modify', '--installPath', "`"$vs`"",
                '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100', '--passive', '--norestart', '--wait')
            Add-Content -Path $Log -Value "Visual Studio Installer exit code: $($p.ExitCode)"
            if (-not (Find-WinSdk)) { Fail "the Windows SDK is still missing after the Visual Studio Installer (exit $($p.ExitCode)); add it in Visual Studio Installer: Modify, Individual components, Windows 11 SDK" }
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
