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

function Get-HeadsetBlocker {
    # Why a run in the headset must not be started now, or nothing if it may be.
    # A -VR launch with SteamVR not running starts SteamVR itself (the OpenXR instance does), and the engine then
    # creates its graphics device while SteamVR's compositor is taking the headset's display. On 24 November 2026
    # that ended in a blue screen in the NVIDIA driver (nvlddmkm.sys, bugcheck 0x3B) and a reboot of the machine
    # with every uncommitted change open. So SteamVR has to be up, with its compositor, before the engine starts.
    # SANDBOX_VR_UNCHECKED=1 skips the check.
    if ($env:SANDBOX_VR_UNCHECKED -eq '1') { return $null }
    $missing = @()
    foreach ($name in @('vrserver', 'vrcompositor')) {
        if (-not (Get-Process -Name $name -ErrorAction SilentlyContinue)) { $missing += $name }
    }
    if ($missing.Count -gt 0) {
        return "SteamVR is not running ($($missing -join ', ') missing). Start SteamVR, put the headset on and wake the controllers first; a launch that has to start SteamVR itself crashed the graphics driver and rebooted the machine"
    }
    # Running is not ready: the crash was the engine's graphics device made while the compositor was coming up.
    # So the compositor has to have been there for a while, and SteamVR must not say that it is starting or going.
    $compositor = @(Get-Process -Name 'vrcompositor' -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1)
    if ($compositor.Count -gt 0 -and $compositor[0].StartTime) {
        $upFor = ((Get-Date) - $compositor[0].StartTime).TotalSeconds
        if ($upFor -lt 30) {
            return ("SteamVR's compositor started {0:N0} s ago and may still be taking the headset's display. Wait half a minute and start again" -f $upFor)
        }
    }
    $state = Get-HeadsetState
    if ($state -and $state -notin @('Ready', 'Standby')) {
        return "SteamVR reports '$state', neither ready nor standing by. Start SteamVR again and wait until it is ready"
    }
    return $null
}

function Get-HeadsetState {
    # 'Ready', 'Standby' or another state SteamVR last reported in its monitor's log, or nothing if that is not
    # known. Standby is a headset nobody is wearing.
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return $null }
    $log = Join-Path $steam 'logs\vrmonitor.txt'
    if (-not (Test-Path $log)) { return $null }
    $last = Get-Content $log -Tail 400 -ErrorAction SilentlyContinue | Select-String -Pattern "to 'SteamVRSystemState_(\w+)'" | Select-Object -Last 1
    if ($last) { return $last.Matches[0].Groups[1].Value }
    return $null
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
    if ($VR) {
        # The scripts that take -VR have asked already and said why in their own words; this is the last guard.
        $blocker = Get-HeadsetBlocker
        if ($blocker) { throw "A run in the headset was not started: $blocker." }
        $hmd = ''
        # OpenXR Toolkit, an API layer that hooks every OpenXR application on the machine, crashed the session
        # 7 s after its start (an access violation in XR_APILAYER_MBUCCHIA_toolkit.dll, called from the D3D12
        # RHI). Its manifest names this variable as the way to leave it out; it is set for this process and its
        # children only, and the installation stays as it is.
        $env:DISABLE_XR_APILAYER_MBUCCHIA_toolkit = '1'
    }
    $all = "`"$ProjectFile`" $ArgumentString $hmd -unattended -nopause -nosplash -nosound -abslog=`"$LogFile`""
    return (Invoke-Process -FilePath (Get-EditorCmd) -ArgumentString $all -TimeoutSec $TimeoutSec)
}

function Invoke-ScenarioBatch {
    # Runs the jobs, $Parallel engine processes at a time, and returns when all have ended.
    # A job is a hashtable: Name or File, Out, and optionally Tune, TuningDir and CVars. Results are read by
    # the caller from each job's <Out>\metrics.json (and <Out>\run.log).
    # Each engine process works through a shared queue of jobs (-ScenarioQueue=, UScenarioBatchSubsystem): engine
    # start-up, about 90% of a single headless run, is paid once per process, and every job still runs in a world
    # of its own with its own tuning. Jobs with other console variables go to a queue of their own. Set
    # SANDBOX_SEPARATE_PROCESSES=1 to run every job in a process of its own instead (Tools\scenario.ps1).
    # Runs side by side change no result, as every run uses a fixed timestep. They do change the wall-clock
    # figures: a physics step measured while other runs compete for the processor reads longer than it is.
    param([array]$Jobs, [int]$Parallel = 8)
    if ($env:SANDBOX_SEPARATE_PROCESSES -ne '1') {
        Invoke-ScenarioQueue -Jobs $Jobs -Parallel $Parallel
        return
    }
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

function New-ScenarioQueue {
    # Writes the jobs as a queue directory under Saved\Queue (format in UScenarioBatchSubsystem) and returns its path.
    # Each job's stale results are removed first, so that they can never be mistaken for this run's.
    # A job: Name or File, Out, and optionally Tune, TuningDir, Shot, ShotAt.
    param([array]$Jobs)
    $queueRoot = Join-Path $RepoRoot 'Saved\Queue'
    $queue = Join-Path $queueRoot ([guid]::NewGuid().ToString('N').Substring(0, 12))
    New-Item -ItemType Directory -Force (Join-Path $queue 'taken') | Out-Null
    # The longest runs first, so that a long one does not start last and keep every other process waiting for it.
    $ordered = @($Jobs | ForEach-Object {
        if ($_.File) { $file = $_.File } else { $file = Join-Path $RepoRoot "Scenarios\$($_.Name).json" }
        $seconds = 0.0
        try { $seconds = [double](Get-Content $file -Raw | ConvertFrom-Json).duration_s } catch {}
        [pscustomobject]@{ Job = $_; Seconds = $seconds }
    } | Sort-Object -Property Seconds -Descending | ForEach-Object { $_.Job })
    $number = 0
    foreach ($job in $ordered) {
        $out = [System.IO.Path]::GetFullPath($job.Out)
        New-Item -ItemType Directory -Force $out | Out-Null
        foreach ($stale in @('metrics.json', 'trace.json', 'series.csv', 'run.log', 'final.png')) {
            $path = Join-Path $out $stale
            if (Test-Path $path) { Remove-Item $path -Force }
        }
        Get-ChildItem $out -Filter 'shot_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
        if ($job.File) { $scenario = [System.IO.Path]::GetFullPath($job.File) } else { $scenario = Join-Path $RepoRoot "Scenarios\$($job.Name).json" }
        $entry = [ordered]@{ scenario = $scenario; out = $out }
        if ($job.Tune) { $entry.tune = [string]$job.Tune }
        if ($job.TuningDir) { $entry.tuning_dir = [System.IO.Path]::GetFullPath($job.TuningDir) }
        if ($job.Shot) { $entry.shot = [System.IO.Path]::GetFullPath($job.Shot) }
        if ($job.ShotAt) { $entry.shot_at = [string]$job.ShotAt }
        $text = $entry | ConvertTo-Json -Compress
        [System.IO.File]::WriteAllText((Join-Path $queue ('{0:D5}.json' -f $number)), $text, (New-Object System.Text.UTF8Encoding($false)))
        $number++
    }
    return $queue
}

function Get-QueueArguments {
    # Engine arguments of a process that works through a queue. The first run of a process differs bit for bit from
    # every later one (UScenarioBatchSubsystem), so every process runs a short warm-up first: every job then starts
    # from the same state, alone or in a batch, in any process and any order.
    param([string]$Queue, [int]$Fps = 90)
    $warmup = Join-Path $PSScriptRoot 'batch_warmup.json'
    return "-game -ScenarioQueue=`"$Queue`" -ScenarioWarmup=`"$warmup`" -UseFixedTimeStep -FPS=$Fps"
}

function Invoke-ScenarioQueue {
    # The jobs of Invoke-ScenarioBatch through job queues: one queue directory per set of console variables, and
    # up to $Parallel engine processes on each, which take jobs from it until it is empty.
    param([array]$Jobs, [int]$Parallel = 8)
    $queueRoot = Join-Path $RepoRoot 'Saved\Queue'
    New-Item -ItemType Directory -Force $queueRoot | Out-Null
    # Queues of earlier calls that are an hour old are left-overs.
    Get-ChildItem $queueRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -lt (Get-Date).AddHours(-1) } |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    $groups = [ordered]@{}
    foreach ($job in $Jobs) {
        $key = [string]$job.CVars
        if (-not $groups.Contains($key)) { $groups[$key] = New-Object System.Collections.ArrayList }
        [void]$groups[$key].Add($job)
    }
    $processes = @()
    $queues = @()
    $groupIndex = 0
    foreach ($key in $groups.Keys) {
        $groupJobs = $groups[$key]
        $queue = New-ScenarioQueue -Jobs $groupJobs
        $queues += $queue
        $count = [Math]::Min([Math]::Max($Parallel, 1), $groupJobs.Count)
        for ($i = 0; $i -lt $count; $i++) {
            $arguments = "`"$ProjectFile`" $(Get-QueueArguments $queue) -nullrhi -nohmd -DisablePlugins=OpenXR -unattended -nopause -nosplash -nosound -abslog=`"$(Join-Path $queue "process_$i.log")`""
            if ($key) { $arguments += " -dpcvars=`"$key`"" }
            $info = New-Object System.Diagnostics.ProcessStartInfo
            $info.FileName = Get-EditorCmd
            $info.Arguments = $arguments
            $info.WorkingDirectory = $RepoRoot
            $info.UseShellExecute = $false
            $info.CreateNoWindow = $true
            $processes += [System.Diagnostics.Process]::Start($info)
        }
    }
    foreach ($process in $processes) { $process.WaitForExit() }
    # A job without a result was lost: its process took it and ended before writing one. The queue's logs are kept
    # for a look (Saved\Queue\<id>\process_<n>.log), and so are those of a queue that still holds jobs.
    $lost = @($Jobs | Where-Object { -not (Test-Path (Join-Path ([System.IO.Path]::GetFullPath($_.Out)) 'metrics.json')) })
    foreach ($queue in $queues) {
        if ($lost.Count -eq 0 -and @(Get-ChildItem $queue -Filter '*.json' -File).Count -eq 0) { Remove-Item $queue -Recurse -Force -ErrorAction SilentlyContinue }
    }
    if ($lost.Count -gt 0) {
        Write-Output ("QUEUE WARNING: {0} job(s) of {1} ended without a result; logs kept in {2}" -f $lost.Count, @($Jobs).Count, ($queues -join ', '))
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
