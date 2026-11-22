# Shared helpers for the Tools scripts. Dot-source it:  . "$PSScriptRoot\common.ps1"
# Written for Windows PowerShell 5.1 (no &&, no ternary, no null-coalescing).

$ErrorActionPreference = 'Stop'

$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$script:ProjectFile = Join-Path $RepoRoot 'DictatorSandbox.uproject'
$script:ToolsOut = Join-Path $RepoRoot 'Saved\Tools'

function Get-EngineRoot {
    # UE_ROOT wins; otherwise the Launcher install that matches the project's EngineAssociation.
    if ($env:UE_ROOT) {
        if (Test-Path (Join-Path $env:UE_ROOT 'Engine\Build\BatchFiles\Build.bat')) { return $env:UE_ROOT }
        throw "UE_ROOT is set to '$($env:UE_ROOT)' but no engine was found there."
    }
    $association = (Get-Content $ProjectFile -Raw | ConvertFrom-Json).EngineAssociation
    $dat = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path $dat) {
        $entry = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -eq "UE_$association" } | Select-Object -First 1
        if ($entry -and (Test-Path (Join-Path $entry.InstallLocation 'Engine\Build\BatchFiles\Build.bat'))) {
            return $entry.InstallLocation
        }
    }
    throw "Unreal Engine $association not found. Install it with the Epic Games Launcher or set UE_ROOT."
}

function Get-EditorCmd {
    return (Join-Path (Get-EngineRoot) 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')
}

function Write-JsonFile {
    param([string]$Path, $Object)
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    $json = $Object | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($Path, $json, (New-Object System.Text.UTF8Encoding $false))
}

function Read-JsonFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    return (Get-Content $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
}

function Invoke-Process {
    # Runs a process to completion with an exact argument string.
    # Returns @{ ExitCode; Output (stdout + stderr lines); TimedOut }.
    param(
        [string]$FilePath,
        [string]$ArgumentString,
        [int]$TimeoutSec = 900,
        [switch]$CaptureOutput
    )
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $FilePath
    $info.Arguments = $ArgumentString
    $info.WorkingDirectory = $RepoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = [bool]$CaptureOutput
    $info.RedirectStandardError = [bool]$CaptureOutput

    $process = [System.Diagnostics.Process]::Start($info)
    $stdoutTask = $null
    $stderrTask = $null
    if ($CaptureOutput) {
        # Read both streams asynchronously so a full pipe can never block the child.
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
    }

    $timedOut = -not $process.WaitForExit($TimeoutSec * 1000)
    if ($timedOut) {
        try { $process.Kill() } catch {}
        $process.WaitForExit()
    }

    $lines = @()
    if ($CaptureOutput) {
        $text = $stdoutTask.Result + "`n" + $stderrTask.Result
        $lines = $text -split "`r?`n" | Where-Object { $_ -ne '' }
    }
    $exitCode = $process.ExitCode
    if ($timedOut) { $exitCode = -1 }
    return @{ ExitCode = $exitCode; Output = $lines; TimedOut = $timedOut }
}

function Invoke-Editor {
    # Runs UnrealEditor-Cmd.exe on the project, unattended, logging to $LogFile.
    param(
        [string]$ArgumentString,
        [string]$LogFile,
        [int]$TimeoutSec = 900
    )
    New-Item -ItemType Directory -Force (Split-Path -Parent $LogFile) | Out-Null
    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }
    $all = "`"$ProjectFile`" $ArgumentString -unattended -nopause -nosplash -nosound -abslog=`"$LogFile`""
    return (Invoke-Process -FilePath (Get-EditorCmd) -ArgumentString $all -TimeoutSec $TimeoutSec)
}

function Get-LogErrors {
    # Error lines from an engine log, for a short failure report.
    param([string]$LogFile, [int]$Max = 20)
    if (-not (Test-Path $LogFile)) { return @() }
    return @(Select-String -Path $LogFile -Pattern 'Error:|Fatal error|Assertion failed|Unhandled Exception' |
        Select-Object -First $Max | ForEach-Object { $_.Line.Trim() })
}
