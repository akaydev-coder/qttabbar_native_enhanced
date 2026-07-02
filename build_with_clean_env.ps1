param(
    [Parameter(Mandatory = $true)]
    [string]$Project,

    [string]$Configuration = "Debug",
    [string]$Platform = "Win32",
    [string]$Arch = "x86",
    [string[]]$ExtraMSBuildArgs = @()
)

$ErrorActionPreference = "Stop"

$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

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
    (Quote-CmdArg "/p:WixTargetsPath=C:\Program Files (x86)\MSBuild\Microsoft\WiX\v3.x\Wix.targets"),
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
    ("call {0} {1}" -f $quotedVcVars, $Arch),
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
