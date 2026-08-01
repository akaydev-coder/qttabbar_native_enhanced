param(
    [string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) 'Installer\Redist')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildUtilities.ps1')
$environment = & (Join-Path $PSScriptRoot 'Test-BuildEnvironment.ps1') -Quiet -PassThru

New-Item -ItemType Directory -Path $Destination -Force | Out-Null

foreach ($architecture in 'x86', 'x64') {
    $entry = $environment.VCRedist[$architecture]
    $source = Join-Path $environment.RedistRoot $entry.FileName
    $target = Join-Path $Destination $entry.FileName
    Copy-Item -LiteralPath $source -Destination $target -Force

    $actualHash = Get-QTTSha256 $target
    if ($actualHash -ne $entry.Sha256) {
        throw "Staged VC++ $architecture redistributable failed hash verification."
    }

    Write-Host "Staged $($entry.FileName) ($actualHash)"
}
