param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$builder = Join-Path $repoRoot 'build_with_clean_env.ps1'
$powershell = (Get-Process -Id $PID).Path
. (Join-Path $repoRoot 'Build\BuildUtilities.ps1')

& (Join-Path $repoRoot 'Build\Test-BuildEnvironment.ps1')

function Invoke-CleanBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Project,
        [string]$Platform = 'AnyCPU',
        [string]$Arch = 'x86',
        [string[]]$ExtraMSBuildArgs = @()
    )

    Write-Host "`nBuilding $Project ($Configuration|$Platform)..." -ForegroundColor Cyan
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $builder,
        '-Project', $Project,
        '-Configuration', $Configuration,
        '-Platform', $Platform,
        '-Arch', $Arch
    )
    if ($ExtraMSBuildArgs.Count -gt 0) {
        $arguments += '-ExtraMSBuildArgs'
        $arguments += $ExtraMSBuildArgs
    }

    & $powershell @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $Project with exit code $LASTEXITCODE."
    }
}

Push-Location $repoRoot
try {
    Invoke-CleanBuild 'QTHookLib\QTHookLib.vcxproj' -Platform 'Win32' -Arch 'x86'
    Invoke-CleanBuild 'QTHookLib\QTHookLib.vcxproj' -Platform 'x64' -Arch 'x64'
    Invoke-CleanBuild 'native\QTTabBarNative\QTTabBarNative.vcxproj' -Platform 'x64' -Arch 'x64'

    $managedProjects = @(
        'QTTabBar\QTTabBar.csproj',
        'Plugins\QTQuick\QTQuick.csproj',
        'Plugins\TurnOffRepeat\TurnOffRepeat.csproj',
        'Plugins\CreateNewItem\CreateNewItem.csproj',
        'Plugins\Memo\Memo.csproj',
        'Plugins\MigemoLoader\MigemoLoader.csproj',
        'Plugins\QTClock\QTClock.csproj',
        'Plugins\QTWindowManager\QTWindowManager.csproj',
        'Plugins\QTFileTools\QTFileTools.csproj',
        'Plugins\QTFolderButton\QTFolderButton.csproj',
        'Plugins\QTViewModeButton\ViewModeButton.csproj',
        'Plugins\ActivateByMouseHover\ActivateByMouseHover.csproj',
        'Plugins\ShowStatusBar\ShowStatusBar.csproj'
    )
    foreach ($project in $managedProjects) {
        Invoke-CleanBuild $project
    }
    Invoke-CleanBuild 'SetHome\SetHome.csproj' -Platform 'x86' -Arch 'x86'

    Invoke-CleanBuild 'Installer\Installer.wixproj' -Platform 'x86' -Arch 'x86' -ExtraMSBuildArgs @('/p:SuppressValidation=True')
    Invoke-CleanBuild 'Installer\Bundle.wixproj' -Platform 'x86' -Arch 'x86'

    $bundle = Join-Path $repoRoot 'Installer\bin\Release\QTTabBar Setup.exe'
    if (-not (Test-Path -LiteralPath $bundle -PathType Leaf)) {
        throw "Bundle output was not produced: $bundle"
    }

    [xml]$bundleDefinition = Get-Content -LiteralPath (Join-Path $repoRoot 'Installer\Bundle.wxs') -Raw
    $bundleName = [string]$bundleDefinition.Wix.Bundle.Name
    if (-not $bundleName.StartsWith('QTTabBar ', [System.StringComparison]::Ordinal)) {
        throw "Unexpected bundle name: $bundleName"
    }
    $releaseLabel = $bundleName.Substring('QTTabBar '.Length)
    $versionedBundle = Join-Path $repoRoot "Installer\bin\Release\QTTabBar Setup - $releaseLabel.exe"
    Copy-Item -LiteralPath $bundle -Destination $versionedBundle -Force
    $hash = Get-QTTSha256 $versionedBundle

    Write-Host "`nRelease build complete." -ForegroundColor Green
    Write-Host "  Installer: $versionedBundle"
    Write-Host "  SHA256   : $hash"
}
finally {
    Pop-Location
}
