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
    # Flat by default: the OpenXR plugin is switched off for the run. -nohmd alone is not enough, because the
    # plugin still creates an OpenXR instance at start-up, which takes about 26 s on SteamVR with no headset.
    # Pass -VR to leave the plugin on.
    param(
        [string]$ArgumentString,
        [string]$LogFile,
        [int]$TimeoutSec = 900,
        [switch]$VR
    )
    New-Item -ItemType Directory -Force (Split-Path -Parent $LogFile) | Out-Null
    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }
    $hmd = '-nohmd -DisablePlugins=OpenXR'
    if ($VR) { $hmd = '' }
    $all = "`"$ProjectFile`" $ArgumentString $hmd -unattended -nopause -nosplash -nosound -abslog=`"$LogFile`""
    return (Invoke-Process -FilePath (Get-EditorCmd) -ArgumentString $all -TimeoutSec $TimeoutSec)
}

function Invoke-ScenarioBatch {
    # Runs Tools\scenario.ps1 once per job, $Parallel processes at a time, and returns when all have ended.
    # A job is a hashtable: Name or File, Out, and optionally Tune, TuningDir and CVars. Results are read by
    # the caller from each job's <Out>\metrics.json.
    # Every scenario process is independent and uses a fixed timestep, so running side by side changes no
    # result. It does change the wall-clock figures: a physics step measured while other runs compete for the
    # processor reads longer than it is.
    param([array]$Jobs, [int]$Parallel = 8)
    $scenarioScript = Join-Path $PSScriptRoot 'scenario.ps1'
    $queue = New-Object System.Collections.Queue
    foreach ($job in $Jobs) { $queue.Enqueue($job) }
    $running = @()
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($queue.Count -gt 0 -and $running.Count -lt [Math]::Max($Parallel, 1)) {
            $job = $queue.Dequeue()
            $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$scenarioScript`" -Quiet -Out `"$($job.Out)`""
            if ($job.File) { $arguments += " -File `"$($job.File)`"" } else { $arguments += " -Name `"$($job.Name)`"" }
            if ($job.Tune) { $arguments += " -Tune `"$($job.Tune)`"" }
            if ($job.TuningDir) { $arguments += " -TuningDir `"$($job.TuningDir)`"" }
            if ($job.CVars) { $arguments += " -CVars `"$($job.CVars)`"" }
            $running += Start-Process powershell -ArgumentList $arguments -PassThru -WindowStyle Hidden
        }
        Start-Sleep -Milliseconds 200
        $running = @($running | Where-Object { -not $_.HasExited })
    }
}

function Format-ScenarioVerdict {
    # The one-line verdict of a run from its metrics.json, as Tools\scenario.ps1 prints it.
    param([string]$Name, $Metrics, [string]$Out)
    if (-not $Metrics) { return "SCENARIO ERROR: $Name wrote no metrics.json. Log: $(Join-Path $Out 'run.log')" }
    if (-not $Metrics.ok) { return "SCENARIO ERROR: $Name could not run: $($Metrics.error)" }
    $verdict = 'FAIL'
    if ($Metrics.pass) { $verdict = 'PASS' }
    $mode = 'headless'
    if ($Metrics.rendered) { $mode = 'rendered' }
    return ("SCENARIO {0}: {1} ({2}, {3} frames, {4:N1} s simulated in {5:N1} s, hashes bodies={6} bones={7})" -f `
        $verdict, $Name, $mode, $Metrics.timing.frames, $Metrics.timing.sim_time_s, $Metrics.timing.wall_time_s, $Metrics.hashes.bodies, $Metrics.hashes.bones)
}

function Get-JsonPath {
    # A value of a parsed JSON object by a dotted path, "control.assist_load.force_share_mean"; $null if absent.
    param($Object, [string]$Path)
    $value = $Object
    foreach ($part in $Path.Split('.')) {
        if ($null -eq $value) { return $null }
        $property = $value.PSObject.Properties[$part]
        if (-not $property) { return $null }
        $value = $property.Value
    }
    return $value
}

function Get-LogErrors {
    # Error lines from an engine log, for a short failure report.
    param([string]$LogFile, [int]$Max = 20)
    if (-not (Test-Path $LogFile)) { return @() }
    return @(Select-String -Path $LogFile -Pattern 'Error:|Fatal error|Assertion failed|Unhandled Exception' |
        Select-Object -First $Max | ForEach-Object { $_.Line.Trim() })
}
