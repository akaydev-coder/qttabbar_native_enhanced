param(
    [switch]$Quiet,
    [switch]$PassThru
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildUtilities.ps1')

$manifestPath = Join-Path $PSScriptRoot 'Toolchain.psd1'
$manifestContent = [System.IO.File]::ReadAllText($manifestPath)
$toolchain = & ([scriptblock]::Create($manifestContent))
$errors = New-Object System.Collections.Generic.List[string]

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $errors.Add("Missing $Description`: $Path")
    }
}

function Assert-Version([string]$Actual, [string]$Expected, [string]$Description) {
    if ($Actual -ne $Expected) {
        $errors.Add("$Description version mismatch. Expected $Expected, found $Actual.")
    }
}

$vsRoot = $toolchain.VisualStudioRoot
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$vcToolsRoot = Join-Path $vsRoot ("VC\Tools\MSVC\{0}" -f $toolchain.VCToolsVersion)
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$wixRoot = $toolchain.WixRoot
$wixTargets = 'C:\Program Files (x86)\MSBuild\Microsoft\WiX\v3.x\Wix.targets'
$redistRoot = Join-Path $vsRoot ("VC\Redist\MSVC\{0}" -f $toolchain.VCRedistFolderVersion)
$netFxReference = 'C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8\mscorlib.dll'

Assert-File $msbuild 'MSBuild'
Assert-File $vcvars 'vcvarsall.bat'
Assert-File (Join-Path $vcToolsRoot 'bin\Hostx64\x64\cl.exe') 'pinned x64 compiler'
Assert-File (Join-Path $vcToolsRoot 'bin\Hostx64\x86\cl.exe') 'pinned x86 compiler'
Assert-File (Join-Path $sdkRoot ("Include\{0}\um\Windows.h" -f $toolchain.WindowsSdkVersion)) 'pinned Windows SDK headers'
Assert-File (Join-Path $sdkRoot ("Lib\{0}\um\x64\kernel32.lib" -f $toolchain.WindowsSdkVersion)) 'pinned Windows SDK x64 libraries'
Assert-File (Join-Path $sdkRoot ("Lib\{0}\um\x86\kernel32.lib" -f $toolchain.WindowsSdkVersion)) 'pinned Windows SDK x86 libraries'
Assert-File (Join-Path $wixRoot 'bin\candle.exe') 'WiX compiler'
Assert-File $wixTargets 'WiX MSBuild targets'
Assert-File $netFxReference '.NET Framework 4.8 targeting pack'

if (Test-Path -LiteralPath $msbuild -PathType Leaf) {
    Assert-Version ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($msbuild).FileVersion) $toolchain.MSBuildFileVersion 'MSBuild'
}

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
Assert-File $vswhere 'vswhere'
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $installationVersion = (& $vswhere -products Microsoft.VisualStudio.Product.BuildTools -property installationVersion | Select-Object -First 1).Trim()
    Assert-Version $installationVersion $toolchain.VisualStudioInstallationVersion 'Visual Studio Build Tools'
}

$candle = Join-Path $wixRoot 'bin\candle.exe'
if (Test-Path -LiteralPath $candle -PathType Leaf) {
    Assert-Version ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($candle).FileVersion) $toolchain.WixVersion 'WiX Toolset'
}

foreach ($architecture in 'x86', 'x64') {
    $entry = $toolchain.VCRedist[$architecture]
    $source = Join-Path $redistRoot $entry.FileName
    Assert-File $source "VC++ $architecture redistributable"
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $actualHash = Get-QTTSha256 $source
        if ($actualHash -ne $entry.Sha256) {
            $errors.Add("VC++ $architecture redistributable hash mismatch. Expected $($entry.Sha256), found $actualHash.")
        }
        Assert-Version ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($source).ProductVersion) $toolchain.VCRedistProductVersion "VC++ $architecture redistributable"
    }
}

if ($errors.Count -gt 0) {
    throw "Pinned build environment validation failed:`r`n - $($errors -join "`r`n - ")"
}

$result = [pscustomobject]@{
    Manifest          = $manifestPath
    MSBuild           = $msbuild
    VcVarsAll         = $vcvars
    VCToolsVersion    = $toolchain.VCToolsVersion
    WindowsSdkVersion = $toolchain.WindowsSdkVersion
    WixTargets        = $wixTargets
    RedistRoot        = $redistRoot
    VCRedist          = $toolchain.VCRedist
}

if (-not $Quiet) {
    Write-Host 'Pinned build environment verified:' -ForegroundColor Green
    Write-Host "  Visual Studio : $($toolchain.VisualStudioInstallationVersion)"
    Write-Host "  MSBuild       : $($toolchain.MSBuildFileVersion)"
    Write-Host "  MSVC          : $($toolchain.VCToolsVersion)"
    Write-Host "  Windows SDK   : $($toolchain.WindowsSdkVersion)"
    Write-Host "  WiX           : $($toolchain.WixVersion)"
    Write-Host "  VC++ Redist   : $($toolchain.VCRedistProductVersion)"
}

if ($PassThru) {
    $result
}
