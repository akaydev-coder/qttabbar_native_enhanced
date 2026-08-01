param(
    [Parameter(Mandatory = $true)]
    [string]$Project,

    [string]$Configuration = "Debug",
    [string]$Platform = "Win32",
    [string]$Arch = "x86",
    [string[]]$ExtraMSBuildArgs = @()
)

$ErrorActionPreference = "Stop"

$repoRoot = $PSScriptRoot
$buildEnvironment = & (Join-Path $repoRoot "Build\Test-BuildEnvironment.ps1") -Quiet -PassThru
$msbuild = $buildEnvironment.MSBuild
$vcvars = $buildEnvironment.VcVarsAll

function Quote-CmdArg([string]$Value) {
    return ('"{0}"' -f ($Value -replace '"', '""'))
}

$quotedProject = Quote-CmdArg $Project
$quotedMSBuild = Quote-CmdArg $msbuild
$quotedVcVars = Quote-CmdArg $vcvars

$argsList = @(
    $quotedProject,
    "/t:Rebuild",
    (Quote-CmdArg "/p:Configuration=$Configuration"),
    (Quote-CmdArg "/p:Platform=$Platform"),
    (Quote-CmdArg "/p:SolutionDir=$repoRoot"),
    (Quote-CmdArg "/p:VCToolsVersion=$($buildEnvironment.VCToolsVersion)"),
    (Quote-CmdArg "/p:WindowsTargetPlatformVersion=$($buildEnvironment.WindowsSdkVersion)"),
    (Quote-CmdArg "/p:WixTargetsPath=$($buildEnvironment.WixTargets)"),
    "/verbosity:minimal"
) + ($ExtraMSBuildArgs | ForEach-Object {
    if ($_ -match '^".*"$') {
        $_
    } else {
        Quote-CmdArg $_
    }
})

$cmdFile = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), ".cmd")
$cmdLines = @(
    "@echo off",
    ("call {0} {1} {2} -vcvars_ver={3}" -f $quotedVcVars, $Arch, $buildEnvironment.WindowsSdkVersion, $buildEnvironment.VCToolsVersion),
    "if errorlevel 1 exit /b %errorlevel%",
    ("{0} {1} 2>&1" -f $quotedMSBuild, ($argsList -join " ")),
    "exit /b %errorlevel%"
)
[System.IO.File]::WriteAllLines($cmdFile, $cmdLines)
if ($env:QTT_BUILD_DEBUG) {
    Write-Output ($cmdLines -join [Environment]::NewLine)
}

$psi = [System.Diagnostics.ProcessStartInfo]::new("cmd.exe", "/d /c " + (Quote-CmdArg $cmdFile))
$psi.WorkingDirectory = (Get-Location).Path
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $false
$psi.RedirectStandardError = $false
$psi.EnvironmentVariables.Clear()

$seen = @{}
foreach ($key in [Environment]::GetEnvironmentVariables("Process").Keys) {
    if ($key -ieq "PATH") {
        continue
    }

    $normalizedKey = $key.ToUpperInvariant()
    if (-not $seen.ContainsKey($normalizedKey)) {
        $psi.EnvironmentVariables[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        $seen[$normalizedKey] = $true
    }
}

$psi.EnvironmentVariables["Path"] = [Environment]::GetEnvironmentVariable("Path", "Process")

try {
    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()

    exit $process.ExitCode
}
finally {
    Remove-Item -LiteralPath $cmdFile -Force -ErrorAction SilentlyContinue
}
