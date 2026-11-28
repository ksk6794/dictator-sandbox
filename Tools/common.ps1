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

# The engine processes of runs, of sessions without the headset and of the tests start without two things they do not need
# (docs/fast-checks.md, SP-2; the owner's decision 6): the DDC's Zen server, which every start otherwise waits 2.3 to 2.7 s
# for (it ends with the last process that uses it): the cache graph without it, InstalledNoZenLocalFallback; and
# UnrealBuildTool's check of the platforms at the start, which engine processes started together wait for one at a time
# (UE_SKIP_UBT_SDK_SETUP=1; TargetPlatformManagerModule.cpp). Not the generator, the lighting bake, the MetaHuman or a run in
# the headset; Config\DefaultEngine.ini stays as it is.
$script:FastStartDdc = '-ddc=InstalledNoZenLocalFallback'
function Set-FastStartEnvironment {
    # The platform check left out, for this process to start only.
    param([System.Diagnostics.ProcessStartInfo]$Info)
    $Info.EnvironmentVariables['UE_SKIP_UBT_SDK_SETUP'] = '1'
}

function Invoke-Process {
    # Runs a process to completion with an exact argument string.
    # Returns @{ ExitCode; Output (stdout + stderr lines); TimedOut }.
    # -FastStart: an engine process of a run, a test or a session without the headset (Set-FastStartEnvironment).
    param(
        [string]$FilePath,
        [string]$ArgumentString,
        [int]$TimeoutSec = 900,
        [switch]$CaptureOutput,
        [switch]$FastStart
    )
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $FilePath
    $info.Arguments = $ArgumentString
    $info.WorkingDirectory = $RepoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    if ($FastStart) { Set-FastStartEnvironment $info }
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
    # -FastStart: a run, a test or a session without the headset, which starts without the Zen server and the platform check
    # (Set-FastStartEnvironment; the cache graph is added unless the arguments name one). Never with -VR.
    param(
        [string]$ArgumentString,
        [string]$LogFile,
        [int]$TimeoutSec = 900,
        [switch]$VR,
        [switch]$FastStart
    )
    if ($VR -and $FastStart) { throw 'A run in the headset starts as it always has: -FastStart is not for -VR.' }
    New-Item -ItemType Directory -Force (Split-Path -Parent $LogFile) | Out-Null
    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }
    if ($FastStart -and $ArgumentString -notmatch '(^|\s)-ddc=') { $ArgumentString += " $script:FastStartDdc" }
    # Within the machine's room: nine engine processes at most, 1.5 GB left free (Test-EngineRoom).
    $need = 2.6
    if ($ArgumentString -match '(^|\s)-nullrhi(\s|$)') { $need = 1.8 }
    Wait-EngineRoom -NeedGB $need -What 'an engine process'
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
    return (Invoke-Process -FilePath (Get-EditorCmd) -ArgumentString $all -TimeoutSec $TimeoutSec -FastStart:$FastStart)
}

function Invoke-ScenarioBatch {
    # Runs the jobs, $Parallel engine processes at a time, and returns when all have ended.
    # A job is a hashtable: Name or File, Out, and optionally Tune, TuningDir and CVars; a rendered one (Tools\film.ps1)
    # also Rendered, Shot (<Out>\final.png), ShotAt, Overlay and Fps. Results are read by the caller from each job's
    # <Out>\metrics.json (and <Out>\run.log).
    # Each engine process works through a shared queue of jobs (-ScenarioQueue=, UScenarioBatchSubsystem): engine
    # start-up, about 90% of a single headless run, is paid once per process, and every job still runs in a world
    # of its own with its own tuning. Jobs that differ in what belongs to a process (console variables, rendering,
    # overlays, the frame rate) go to a queue of their own. Set SANDBOX_SEPARATE_PROCESSES=1 to run every job in a
    # process of its own instead (Tools\scenario.ps1).
    # Runs side by side change no result, as every run uses a fixed timestep. They do change the wall-clock
    # figures: a physics step measured while other runs compete for the processor reads longer than it is.
    # Inside a check set (Tools\checks.ps1, SANDBOX_CHECK_QUEUE; SP-3) the jobs go to the set's queue and $Parallel is the
    # set's; jobs with console variables or a renderer still run in processes of their own. -Alone: a run that has to be
    # alone for its timing (the suite's second run of a scenario over the step limit): inside a set it waits until every
    # process of the set has ended.
    # -TimeoutSec: the processes of a queue are ended after that long and their jobs reported lost (0: no limit; a film's).
    param([array]$Jobs, [int]$Parallel = 8, [switch]$Alone, [int]$TimeoutSec = 0)
    if ($env:SANDBOX_SEPARATE_PROCESSES -ne '1') {
        if ($env:SANDBOX_CHECK_QUEUE -and (Test-Path (Join-Path $env:SANDBOX_CHECK_QUEUE 'owner.pid'))) {
            if ($Alone) { Invoke-AloneInCheckQueue -Jobs $Jobs; return }
            $inSet = @($Jobs | Where-Object { -not $_.CVars -and -not $_.Rendered -and -not $_.Shot })
            $ofTheirOwn = @($Jobs | Where-Object { $_.CVars -or $_.Rendered -or $_.Shot })
            if ($inSet.Count -gt 0) { Submit-CheckJobs -Jobs $inSet }
            if ($ofTheirOwn.Count -gt 0) { Invoke-ScenarioQueue -Jobs $ofTheirOwn -Parallel $Parallel -TimeoutSec $TimeoutSec }
            return
        }
        # A running pool with today's fingerprint takes the headless jobs (SP-4); a run that must be alone, several jobs asked for one
        # at a time (suite.ps1 -Parallel 1, for physics step times nothing disturbs), a renderer and console variables keep processes
        # of their own.
        $poolDir = $null
        if (-not $Alone -and -not ($Parallel -le 1 -and @($Jobs).Count -gt 1)) { $poolDir = Get-LivePool }
        if ($poolDir) {
            $inPool = @($Jobs | Where-Object { -not $_.CVars -and -not $_.Rendered -and -not $_.Shot })
            $ofTheirOwn = @($Jobs | Where-Object { $_.CVars -or $_.Rendered -or $_.Shot })
            if ($inPool.Count -gt 0) {
                $env:SANDBOX_CHECK_QUEUE = $poolDir
                $script:PoolMode = $true
                try { Submit-CheckJobs -Jobs $inPool } finally { $env:SANDBOX_CHECK_QUEUE = $null; $script:PoolMode = $false }
            }
            if ($ofTheirOwn.Count -gt 0) { Invoke-ScenarioQueue -Jobs $ofTheirOwn -Parallel $Parallel -TimeoutSec $TimeoutSec }
            return
        }
        Invoke-ScenarioQueue -Jobs $Jobs -Parallel $Parallel -TimeoutSec $TimeoutSec
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
            # A rendered job's pictures: scenario.ps1 -Shot saves <Out>\final.png, which is where a job's Shot is.
            if ($job.Shot) { $arguments += ' -Shot'; if ($job.ShotAt) { $arguments += " -ShotAt `"$($job.ShotAt)`"" } } elseif ($job.Rendered) { $arguments += ' -Rendered' }
            if ($job.Overlay) { $arguments += " -Overlay `"$($job.Overlay)`"" }
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
        if ($env:SANDBOX_PHYSICS_ASSET -and $number -eq 0) { Write-Host "QUEUE NOTE: SANDBOX_PHYSICS_ASSET is set: every scenario of this queue runs on $($env:SANDBOX_PHYSICS_ASSET)" }
        $entry = New-ScenarioJobEntry -Job $job -Queue $queue -Number ('{0:D5}' -f $number)
        $text = $entry | ConvertTo-Json -Compress
        [System.IO.File]::WriteAllText((Join-Path $queue ('{0:D5}.json' -f $number)), $text, (New-Object System.Text.UTF8Encoding($false)))
        $number++
    }
    return $queue
}

function New-ScenarioJobEntry {
    # One job as a queue's file holds it (format in UScenarioBatchSubsystem). The job's stale results are removed first, so
    # that they can never be mistaken for this run's. $Number names the copy of the scenario that SANDBOX_PHYSICS_ASSET makes.
    param($Job, [string]$Queue, [string]$Number)
    $out = [System.IO.Path]::GetFullPath($Job.Out)
    New-Item -ItemType Directory -Force $out | Out-Null
    foreach ($stale in @('metrics.json', 'trace.json', 'series.csv', 'hands.csv', 'run.log', 'final.png')) {
        $path = Join-Path $out $stale
        if (Test-Path $path) { Remove-Item $path -Force }
    }
    Get-ChildItem $out -Filter 'shot_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
    if ($Job.File) { $scenario = [System.IO.Path]::GetFullPath($Job.File) } else { $scenario = Join-Path $RepoRoot "Scenarios\$($Job.Name).json" }
    # Another body for every run of a tool, without touching a scenario: SANDBOX_PHYSICS_ASSET=PA_SandboxNPCNeck puts
    # that asset in the place of PA_SandboxNPC in a copy of each scenario, and where its head is part of another body
    # (SANDBOX_HEAD_BODY=neck_01, in today's PA_SandboxNPCNeck and PA_SandboxNPCToes) what a
    # scenario does to the head it does to that.
    if ($env:SANDBOX_PHYSICS_ASSET) {
        $copy = Join-Path $Queue "scenario_$Number"
        New-Item -ItemType Directory -Force $copy | Out-Null
        $copyFile = Join-Path $copy ([System.IO.Path]::GetFileName($scenario))
        $scenarioText = [System.IO.File]::ReadAllText($scenario)
        $scenarioText = $scenarioText -creplace 'PA_SandboxNPC\.PA_SandboxNPC"', "$($env:SANDBOX_PHYSICS_ASSET).$($env:SANDBOX_PHYSICS_ASSET)`""
        if ($env:SANDBOX_HEAD_BODY) {
            $scenarioText = $scenarioText -creplace '("(?:body|at_bone|bone|equals)"\s*:\s*)"head"', "`$1`"$($env:SANDBOX_HEAD_BODY)`""
        }
        [System.IO.File]::WriteAllText($copyFile, $scenarioText, (New-Object System.Text.UTF8Encoding($false)))
        $scenario = $copyFile
    }
    $entry = [ordered]@{ scenario = $scenario; out = $out }
    if ($Job.Tune) { $entry.tune = [string]$Job.Tune }
    if ($Job.TuningDir) { $entry.tuning_dir = [System.IO.Path]::GetFullPath($Job.TuningDir) }
    if ($Job.Shot) { $entry.shot = [System.IO.Path]::GetFullPath($Job.Shot) }
    if ($Job.ShotAt) { $entry.shot_at = [string]$Job.ShotAt }
    return $entry
}

function Get-QueueArguments {
    # Engine arguments of a process that works through a queue. The first run of a process differs bit for bit from
    # every later one (UScenarioBatchSubsystem), so every process runs a short warm-up first: every job then starts
    # from the same state, alone or in a batch, in any process and any order.
    # The cache graph without the Zen server (SP-2), but in the headset (-Headset).
    param([string]$Queue, [int]$Fps = 90, [switch]$Headset)
    $warmup = Join-Path $PSScriptRoot 'batch_warmup.json'
    $arguments = "-game -ScenarioQueue=`"$Queue`" -ScenarioWarmup=`"$warmup`" -UseFixedTimeStep -FPS=$Fps"
    # SP-6: the NPC's meshes and physics asset loaded once after the warm-up and kept (Tools\batch_keep.json); SANDBOX_KEEP_ASSETS=0 not.
    if ($env:SANDBOX_KEEP_ASSETS -ne '0') { $arguments += " -ScenarioKeep=`"$(Join-Path $PSScriptRoot 'batch_keep.json')`"" }
    if (-not $Headset) { $arguments += " $script:FastStartDdc" }
    return $arguments
}

function Get-RenderedParallel {
    # How many rendered engine processes may run side by side (Tools\film.ps1): one takes 2.5 GB of the machine's memory at its
    # peak, against 1.7 GB without a renderer (docs/testing.md, "Faster checks, stage 1"), every start leaves 1.5 GB free, and
    # they share one GPU: four at most.
    $room = [Math]::Floor(((Get-FreeMemoryGB) - 1.5) / 2.6)
    return [int][Math]::Max(1, [Math]::Min(4, $room))
}

function Invoke-ScenarioQueue {
    # The jobs of Invoke-ScenarioBatch through job queues: one queue directory for each set of what belongs to a process
    # (console variables, rendering, overlays, the frame rate), and up to $Parallel engine processes on each, which take
    # jobs from it until it is empty. -TimeoutSec: after that long the processes still running are ended (0: no limit).
    param([array]$Jobs, [int]$Parallel = 8, [int]$TimeoutSec = 0)
    $queueRoot = Join-Path $RepoRoot 'Saved\Queue'
    New-Item -ItemType Directory -Force $queueRoot | Out-Null
    # Queues of earlier calls that are an hour old are left-overs, but a check set's whose owner still runs. Several tools may
    # clean at the same time (the seeds of the stand do): a folder another one has just taken away ends the removal, and must
    # not end the run.
    try {
        Get-ChildItem $queueRoot -Directory -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -lt (Get-Date).AddHours(-1) } | Where-Object {
            $ownerFile = Join-Path $_.FullName 'owner.pid'
            $owner = 0
            if (Test-Path $ownerFile) { try { $owner = [int]([System.IO.File]::ReadAllText($ownerFile).Trim()) } catch { } }
            -not ($owner -gt 0 -and (Get-Process -Id $owner -ErrorAction SilentlyContinue))
        } | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    } catch { }
    $groups = [ordered]@{}
    foreach ($job in $Jobs) {
        $fps = 90
        if ($job.Fps) { $fps = [int]$job.Fps }
        $key = '{0}|{1}|{2}|{3}' -f [string]$job.CVars, [bool]$job.Rendered, [string]$job.Overlay, $fps
        if (-not $groups.Contains($key)) { $groups[$key] = New-Object System.Collections.ArrayList }
        [void]$groups[$key].Add($job)
    }
    $processes = @()
    $queues = @()
    $groupIndex = 0
    foreach ($key in $groups.Keys) {
        $groupJobs = $groups[$key]
        $first = $groupJobs[0]
        $fps = 90
        if ($first.Fps) { $fps = [int]$first.Fps }
        # Rendered offscreen as a single run with -Shot is (Tools\scenario.ps1), or without a renderer.
        $display = '-nullrhi'
        if ($first.Rendered) { $display = '-RenderOffscreen -windowed -ResX=1280 -ResY=720' }
        $queue = New-ScenarioQueue -Jobs $groupJobs
        $queues += $queue
        $count = [Math]::Min([Math]::Max($Parallel, 1), $groupJobs.Count)
        # Within the machine's room (Test-EngineRoom): the first process of a queue waits for it, the others are left out
        # when there is none, and the queue's jobs go to fewer processes.
        $need = 1.8
        if ($first.Rendered) { $need = 2.6 }
        for ($i = 0; $i -lt $count; $i++) {
            if ($i -eq 0) { Wait-EngineRoom -NeedGB $need -What 'a queue of runs' }
            else {
                # The processes just started have not taken their memory yet: it is counted as taken. A pool's idle process asked to
                # give way (Test-EngineRoom) is waited for, 5 s at most.
                $why = Test-EngineRoom -NeedGB $need -PendingGB ($need * $i)
                if ($why -and (Test-Path (Get-PoolFile)) -and (Request-PoolYield) -gt 0) {
                    $until = (Get-Date).AddSeconds(5)
                    while ($why -and (Get-Date) -lt $until) {
                        Start-Sleep -Milliseconds 250
                        $why = Test-EngineRoom -NeedGB $need -PendingGB ($need * $i)
                    }
                }
                if ($why) { [Console]::Error.WriteLine("QUEUE NOTE: $i engine process(es) for $($groupJobs.Count) job(s), no room for more ($why)"); break }
            }
            $arguments = "`"$ProjectFile`" $(Get-QueueArguments $queue -Fps $fps) $display -nohmd -DisablePlugins=OpenXR -unattended -nopause -nosplash -nosound -abslog=`"$(Join-Path $queue "process_$i.log")`""
            if ($first.CVars) { $arguments += " -dpcvars=`"$($first.CVars)`"" }
            if ($first.Overlay) { $arguments += " -Overlay=`"$($first.Overlay)`"" }
            $info = New-Object System.Diagnostics.ProcessStartInfo
            $info.FileName = Get-EditorCmd
            $info.Arguments = $arguments
            $info.WorkingDirectory = $RepoRoot
            $info.UseShellExecute = $false
            $info.CreateNoWindow = $true
            Set-FastStartEnvironment $info
            $processes += [System.Diagnostics.Process]::Start($info)
        }
    }
    if ($TimeoutSec -gt 0) {
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        $ended = $false
        foreach ($process in $processes) {
            $left = [int][Math]::Max(0, ($deadline - (Get-Date)).TotalMilliseconds)
            if (-not $process.WaitForExit($left)) { try { $process.Kill() } catch { }; $process.WaitForExit(); $ended = $true }
        }
        if ($ended) { Write-Output "QUEUE WARNING: the queue ran longer than $TimeoutSec s; its engine processes still running were ended" }
    } else {
        foreach ($process in $processes) { $process.WaitForExit() }
    }
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

function Test-EngineRoom {
    # Why one more engine process may not start now, or nothing if it may: at most 9 on the machine, and every start leaves
    # 1.5 GB free (a process takes 1.7 GB at its peak, 2.5 GB rendered: docs/testing.md, "Faster checks, stage 1"). Every
    # launcher of runs, tests and sessions asks it (Invoke-Editor, Invoke-ScenarioQueue, a check set's queue).
    # -PendingGB: memory that processes the caller has just started will still take. The count is not shared between launchers
    # that start at the same instant; each counts what it has started itself.
    # A pool's idle processes give way to a start that has no room (Request-PoolYield), but not to the pool's own (-NoYield).
    param([double]$NeedGB = 1.8, [double]$PendingGB = 0, [switch]$NoYield)
    $engines = @(Get-EngineProcesses).Count
    if ($engines -ge 9) {
        if (-not $NoYield) { [void](Request-PoolYield) }
        return "$engines engine processes run on the machine, 9 at most"
    }
    $free = Get-FreeMemoryGB
    if ($free - $PendingGB - $NeedGB -lt 1.5) {
        if (-not $NoYield) { [void](Request-PoolYield) }
        return ('{0:N1} GB of memory free, {1:N1} GB of it still to be taken by processes just started, {2:N1} GB needed' -f $free, $PendingGB, ($NeedGB + 1.5))
    }
    return $null
}

function Wait-EngineRoom {
    # Waits until one more engine process may start, and says so once if it has to wait (on the error stream, so that a tool's
    # first line stays its verdict).
    param([double]$NeedGB = 1.8, [string]$What = 'an engine process')
    $said = $null
    while ($true) {
        $why = Test-EngineRoom -NeedGB $NeedGB
        if (-not $why) { return }
        if ($said -ne $why) { [Console]::Error.WriteLine("QUEUE NOTE: $What waits for room ($why)"); $said = $why }
        Start-Sleep -Milliseconds 500
    }
}

# --- One queue for several tools (Tools\checks.ps1; docs/fast-checks.md, SP-3) ---------------------------------------------
# A tool that runs inside a check set starts no engine processes of its own: Invoke-ScenarioBatch hands its jobs to the set's
# queue (SANDBOX_CHECK_QUEUE, a folder under Saved\Queue) and waits for their results; the process that made the queue
# (checks.ps1, or getup-seeds.ps1 and natural.ps1 run alone) keeps up to its number of engine processes on it while its tools
# run. A tool's plan, run and judgement meet at Invoke-ScenarioBatch: what comes before a call is the plan, the call is the
# run, what follows is the judgement, and a tool that calls it again with what it read (the stand's draws again, the grab's
# probes and pulls) asks for another round. A job's file name begins with its simulated length, so that the processes take
# the longest first, whichever tool planned it and when. Alone, outside a set, each tool runs as it always has.

function Enter-CheckQueue {
    # Joins the check set this process runs in (its parent's), or makes one that this process keeps, its owner, with up to
    # $Parallel engine processes (9 at most). Returns the queue's folder.
    param([int]$Parallel = 8)
    if ($env:SANDBOX_CHECK_QUEUE -and (Test-Path (Join-Path $env:SANDBOX_CHECK_QUEUE 'owner.pid'))) {
        return $env:SANDBOX_CHECK_QUEUE
    }
    $queue = Join-Path $RepoRoot ('Saved\Queue\check_' + [guid]::NewGuid().ToString('N').Substring(0, 12))
    New-Item -ItemType Directory -Force (Join-Path $queue 'taken') | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $queue 'owner.pid'), "$PID")
    $env:SANDBOX_CHECK_QUEUE = $queue
    $script:CheckQueue = @{ dir = $queue; parallel = [Math]::Max([Math]::Min($Parallel, 9), 1); processes = (New-Object System.Collections.ArrayList); started = 0; most = 0; note = $null }
    return $queue
}

function Test-CheckQueueOwner {
    return [bool]($script:CheckQueue -and $env:SANDBOX_CHECK_QUEUE -and $script:CheckQueue.dir -eq $env:SANDBOX_CHECK_QUEUE)
}

function Update-CheckQueue {
    # The owner's duty, done every quarter second by whatever loop it waits in: an engine process for every job waiting, up to
    # its number and within the room of the machine (Test-EngineRoom). A process ends when it finds the queue empty; a job
    # that comes later gets a new one. While a tool asks to run a job alone (the suite's second run of a scenario over the step
    # limit, for its timing), no process is started, and the request is granted once every process of the queue has ended.
    if (-not (Test-CheckQueueOwner)) { return }
    $q = $script:CheckQueue
    $alive = @($q.processes | Where-Object { -not $_.HasExited })
    # A request whose process has ended (killed before its own clean-up) is taken away, or the set would wait for good.
    foreach ($stale in @(Get-ChildItem $q.dir -Filter 'alone_*.request' -File -ErrorAction SilentlyContinue)) {
        $asker = 0
        try { $asker = [int]([System.IO.File]::ReadAllText($stale.FullName).Trim()) } catch { }
        if ($asker -gt 0 -and -not (Get-Process -Id $asker -ErrorAction SilentlyContinue)) { Remove-Item $stale.FullName -Force -ErrorAction SilentlyContinue }
    }
    $requests = @(Get-ChildItem $q.dir -Filter 'alone_*.request' -File -ErrorAction SilentlyContinue | Sort-Object Name)
    $grantFile = Join-Path $q.dir 'alone.granted'
    if ($requests.Count -gt 0) {
        $granted = $null
        if (Test-Path $grantFile) { try { $granted = [System.IO.File]::ReadAllText($grantFile).Trim() } catch { } }
        if ($alive.Count -eq 0 -and @($requests | ForEach-Object { $_.BaseName }) -notcontains $granted) {
            # Alone on the machine: no other run either (the bootstrap's sessions beside the set wait for it to end), for two
            # minutes at most, so that another session's long batch cannot hold the set.
            if (-not $q.aloneSince) { $q.aloneSince = Get-Date }
            $others = @(Get-Process -Name 'UnrealEditor-Cmd' -ErrorAction SilentlyContinue).Count
            if ($others -eq 0 -or ((Get-Date) - $q.aloneSince).TotalSeconds -gt 120) {
                [System.IO.File]::WriteAllText($grantFile, $requests[0].BaseName)
                $q.aloneSince = $null
            }
        }
        return
    }
    $q.aloneSince = $null
    if (Test-Path $grantFile) { Remove-Item $grantFile -Force -ErrorAction SilentlyContinue }
    $waiting = @(Get-ChildItem $q.dir -Filter '*.json' -File -ErrorAction SilentlyContinue).Count
    $more = [Math]::Min($q.parallel - $alive.Count, $waiting)
    for ($i = 0; $i -lt $more; $i++) {
        # The processes started in the last 20 s have not taken their memory yet: it is counted as taken.
        $young = @($q.processes | Where-Object { -not $_.HasExited -and ((Get-Date) - $_.StartTime).TotalSeconds -lt 20 }).Count
        $why = Test-EngineRoom -PendingGB (1.7 * $young)
        if ($why) {
            if ($q.note -ne $why) { $q.note = $why; [Console]::Error.WriteLine("QUEUE NOTE: a job of the check set waits for room ($why)") }
            break
        }
        $number = $q.started
        $q.started++
        $arguments = "`"$ProjectFile`" $(Get-QueueArguments $q.dir) -nullrhi -nohmd -DisablePlugins=OpenXR -unattended -nopause -nosplash -nosound -abslog=`"$(Join-Path $q.dir "process_$number.log")`""
        $info = New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName = Get-EditorCmd
        $info.Arguments = $arguments
        $info.WorkingDirectory = $RepoRoot
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        Set-FastStartEnvironment $info
        [void]$q.processes.Add([System.Diagnostics.Process]::Start($info))
        $count = @($q.processes | Where-Object { -not $_.HasExited }).Count
        if ($count -gt $q.most) { $q.most = $count }
    }
}

function Get-CheckQueueFirstJobs {
    # When each tool's first job was taken, by the tool's name in its jobs' names (SANDBOX_CHECK_TOOL): tool -> time. The owner's.
    $first = [ordered]@{}
    if (-not (Test-CheckQueueOwner)) { return $first }
    foreach ($marker in @(Get-ChildItem (Join-Path $script:CheckQueue.dir 'taken') -Filter '*.taken' -File -ErrorAction SilentlyContinue)) {
        if ($marker.Name -match '^\d{6}_\d{4}_(.+)-\d+x\d+\.json\.taken$') {
            $tool = $Matches[1]
            if (-not $first.Contains($tool) -or $marker.CreationTime -lt $first[$tool]) { $first[$tool] = $marker.CreationTime }
        }
    }
    return $first
}

function Wait-CheckChildren {
    # The owner waits for its tools (processes started with Start-Process -PassThru), doing its duty meanwhile.
    param([array]$Children)
    while (@($Children | Where-Object { $_ -and -not $_.HasExited }).Count -gt 0) {
        Update-CheckQueue
        Start-Sleep -Milliseconds 250
    }
}

function Exit-CheckQueue {
    # The owner, when every tool of the set has ended: waits for the processes, keeps the queue's logs if a job was lost or
    # one is left, and leaves the set. Returns what the queue did, or nothing for a process that does not own the queue.
    if (-not (Test-CheckQueueOwner)) { return $null }
    $q = $script:CheckQueue
    foreach ($process in $q.processes) { $process.WaitForExit() }
    $left = @(Get-ChildItem $q.dir -Filter '*.json' -File -ErrorAction SilentlyContinue).Count
    $lost = @(Get-ChildItem $q.dir -Filter 'lost_*.txt' -File -ErrorAction SilentlyContinue).Count
    if ($left -eq 0 -and $lost -eq 0) { Remove-Item $q.dir -Recurse -Force -ErrorAction SilentlyContinue }
    $env:SANDBOX_CHECK_QUEUE = $null
    $script:CheckQueue = $null
    return [ordered]@{ processes = $q.started; most = $q.most; left = $left; lost = $lost; dir = $q.dir }
}

$script:CheckBatch = 0
function Submit-CheckJobs {
    # Invoke-ScenarioBatch inside a check set: the jobs into the set's queue, each file written whole before it can be taken and
    # named by its length (the longest first) and its batch; then waits for every result. A job that its process took and ended
    # without a result is lost (QUEUE WARNING), and never run again in silence.
    param([array]$Jobs)
    $queue = $env:SANDBOX_CHECK_QUEUE
    $script:CheckBatch++
    $tool = 'tool'
    if ($env:SANDBOX_CHECK_TOOL) { $tool = $env:SANDBOX_CHECK_TOOL -replace '[^A-Za-z0-9_\-]', '' }
    $batch = '{0}-{1}x{2}' -f $tool, $PID, $script:CheckBatch
    # The pool's queue outlives every submitter, and an id comes back: a name of its own for every batch in it.
    if ($script:PoolMode) { $batch += '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) }
    $mine = @()
    $number = 0
    foreach ($job in $Jobs) {
        $entry = New-ScenarioJobEntry -Job $job -Queue $queue -Number ('{0}_{1:D4}' -f $batch, $number)
        $seconds = 0.0
        try { $seconds = [double](Get-Content $entry.scenario -Raw | ConvertFrom-Json).duration_s } catch {}
        # The longest first; of jobs as long, the first of every batch before the second of any, so that the tools' rounds
        # (the three stands of the seeds) go on side by side and none is left alone at the end.
        $key = [Math]::Max(0, 999999 - [int][Math]::Round($seconds * 100.0))
        $name = '{0:D6}_{1:D4}_{2}.json' -f $key, $number, $batch
        $part = Join-Path $queue ($name + '.part')
        [System.IO.File]::WriteAllText($part, ($entry | ConvertTo-Json -Compress), (New-Object System.Text.UTF8Encoding($false)))
        Move-Item $part (Join-Path $queue $name)
        $mine += @{ name = $name; out = $entry.out; done = $false; lost = $false; back = $false; job = $job }
        $number++
    }
    $ownerPid = 0
    try { $ownerPid = [int]([System.IO.File]::ReadAllText((Join-Path $queue 'owner.pid')).Trim()) } catch {}
    while (@($mine | Where-Object { -not $_.done -and -not $_.lost }).Count -gt 0) {
        Update-CheckQueue
        Update-Pool
        foreach ($item in @($mine | Where-Object { -not $_.done -and -not $_.lost })) {
            # Done once its process says that every file of it is written and its run.log closed (the job's .done marker,
            # UScenarioBatchSubsystem::JobDone): metrics.json comes before series.csv, hands.csv and the log's close.
            $doneMarker = Join-Path $queue "taken\$($item.name).done"
            if (Test-Path $doneMarker) { $item.done = $true; continue }
            $marker = Join-Path $queue "taken\$($item.name).taken"
            if (-not (Test-Path $marker)) { continue }
            $holder = 0
            try { $holder = [int]([System.IO.File]::ReadAllText($marker).Trim()) } catch {}
            if ($holder -gt 0 -and -not (Get-Process -Id $holder -ErrorAction SilentlyContinue)) {
                if (Test-Path $doneMarker) { $item.done = $true; continue }
                $item.lost = $true
                [System.IO.File]::WriteAllText((Join-Path $queue "lost_$($item.name).txt"), "process $holder ended without a result for $($item.out)")
            }
        }
        if (-not (Test-CheckQueueOwner) -and $ownerPid -gt 0 -and -not (Get-Process -Id $ownerPid -ErrorAction SilentlyContinue)) {
            foreach ($item in @($mine | Where-Object { -not $_.done -and -not $_.lost })) { $item.lost = $true }
            Write-Output "QUEUE WARNING: the process that keeps the check set's queue ($ownerPid) has ended"
            break
        }
        # A pool that ended with jobs of this batch nobody took (stopped, or its file gone, and no process of it left): they are taken
        # back and run in processes of their own. (While a process of it runs, it may still be taking one: not before.)
        if ($script:PoolMode -and (Test-PoolEnding) -and @(Get-PoolAlive (Read-Pool)).Count -eq 0) {
            foreach ($item in @($mine | Where-Object { -not $_.done -and -not $_.lost })) {
                if (Receive-PoolJobBack -Queue $queue -Name $item.name) { $item.back = $true; $item.done = $true }
            }
        }
        # A job whose file is gone with no marker was never taken (a process writes the marker before it moves the file): the queue's
        # folder was removed under it (pool.ps1 -Start). It runs in a process of its own.
        if ($script:PoolMode) {
            foreach ($item in @($mine | Where-Object { -not $_.done -and -not $_.lost })) {
                $marker = Join-Path $queue "taken\$($item.name).taken"
                $file = Join-Path $queue $item.name
                if (-not (Test-Path $file) -and -not (Test-Path $marker)) {
                    Start-Sleep -Milliseconds 100
                    if (-not (Test-Path $file) -and -not (Test-Path $marker)) { $item.back = $true; $item.done = $true }
                }
            }
        }
        Start-Sleep -Milliseconds 250
    }
    $back = @($mine | Where-Object { $_.back })
    if ($back.Count -gt 0) {
        [Console]::Error.WriteLine(("QUEUE NOTE: the pool ended with {0} job(s) of this batch not taken; they run in processes of their own" -f $back.Count))
        $inPool = $env:SANDBOX_CHECK_QUEUE
        $wasPool = $script:PoolMode
        $env:SANDBOX_CHECK_QUEUE = $null
        $script:PoolMode = $false
        try { Invoke-ScenarioQueue -Jobs @($back | ForEach-Object { $_.job }) -Parallel 8 } finally { $env:SANDBOX_CHECK_QUEUE = $inPool; $script:PoolMode = $wasPool }
    }
    $lost = @($mine | Where-Object { $_.lost })
    if ($lost.Count -gt 0) {
        Write-Output ("QUEUE WARNING: {0} job(s) of {1} ended without a result; logs kept in {2}" -f $lost.Count, $mine.Count, $queue)
    }
}

# --- The pool (Tools\pool.ps1; docs/fast-checks.md, SP-4; docs/recon/sp4_plan.md) --------------------------------------------
# Engine processes that wait for jobs after their queue is empty (-ScenarioPoolIdleS, UScenarioBatchSubsystem), on one queue,
# Saved\Pool\queue, with Saved\Pool\pool.json naming them and the fingerprint they were started with. Invoke-ScenarioBatch hands a
# live pool with today's fingerprint its headless jobs, as a tool in a check set hands the set's queue its jobs (Submit-CheckJobs),
# and tops the pool up while its jobs wait. Nothing of it runs unless Tools\pool.ps1 -Start has started a pool; SANDBOX_NO_POOL=1
# ignores a running one.

$script:PoolMode = $false

function Get-PoolDir { return (Join-Path $RepoRoot 'Saved\Pool\queue') }
function Get-PoolFile { return (Join-Path $RepoRoot 'Saved\Pool\pool.json') }

function Get-PoolPrint {
    # What a pool's process reads once at its start and a job then depends on: the project's DLLs, the engine's settings, the
    # warm-up, the assets kept and the motion library, the pose database. One hash of their names, sizes and times. Not in it: the
    # content's assets (gen-assets.ps1 and bake-lighting.ps1 stop the pool; metahuman.ps1 and the fetch scripts do not: stop it
    # by hand before them, Tools\pool.ps1 -Stop), the engine's own build and the .uproject.
    $parts = New-Object System.Collections.ArrayList
    $files = @()
    $files += @(Get-ChildItem (Join-Path $RepoRoot 'Binaries\Win64') -Filter '*.dll' -File -ErrorAction SilentlyContinue)
    $files += @(Get-ChildItem (Join-Path $RepoRoot 'Config') -Filter '*.ini' -File -ErrorAction SilentlyContinue)
    foreach ($relative in @('Tools\batch_warmup.json', 'Tools\batch_keep.json', 'Specs\motion_library.json', 'Saved\PosePrior\prior.bin')) {
        $full = Join-Path $RepoRoot $relative
        if (Test-Path $full) { $files += Get-Item $full }
    }
    foreach ($file in @($files | Sort-Object FullName)) { [void]$parts.Add(('{0}|{1}|{2}' -f $file.FullName, $file.Length, $file.LastWriteTimeUtc.Ticks)) }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes(($parts -join "`n")))
    return (($bytes | Select-Object -First 8 | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Read-Pool {
    $file = Get-PoolFile
    if (-not (Test-Path $file)) { return $null }
    try { return ([System.IO.File]::ReadAllText($file) | ConvertFrom-Json) } catch { return $null }
}

function Lock-Pool {
    # The pool's file is read, changed and written by several tools at once (pool.ps1, submitters topping the pool up, Stop-Pool):
    # each holds this lock file open alone for that time, 30 s at most. Dispose the returned stream to let go.
    $path = Join-Path $RepoRoot 'Saved\Pool\pool.lock'
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    $deadline = (Get-Date).AddSeconds(30)
    while ($true) {
        try { return [System.IO.File]::Open($path, [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None) }
        catch {
            if ((Get-Date) -gt $deadline) { throw "the pool's lock ($path) was held for 30 s" }
            Start-Sleep -Milliseconds 50
        }
    }
}

function Write-Pool {
    # Whole: written beside it and put in its place in one step (File.Replace; a move onto it deletes it first), so that a process
    # reading it never finds it missing or half written. A reader holding it open for a moment makes the replace wait. Under Lock-Pool.
    param($Pool)
    $file = Get-PoolFile
    $part = "$file.$PID.part"
    [System.IO.File]::WriteAllText($part, ($Pool | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding($false)))
    for ($try = 0; $try -lt 40; $try++) {
        try {
            if (Test-Path $file) { [System.IO.File]::Replace($part, $file, [NullString]::Value) } else { [System.IO.File]::Move($part, $file) }
            return
        } catch { Start-Sleep -Milliseconds 25 }
    }
    throw "the pool's file could not be replaced ($file)"
}

function Get-PoolAlive {
    # The pool's processes that still run, as pool.json names them ({ id, start }): by id, name and start time, since an id can be
    # another program's by now.
    param($Pool)
    if (-not $Pool) { return @() }
    $alive = @()
    foreach ($entry in @($Pool.processes)) {
        if (-not $entry -or -not $entry.id) { continue }
        $process = Get-Process -Id ([int]$entry.id) -ErrorAction SilentlyContinue
        if (-not $process -or $process.ProcessName -notlike 'UnrealEditor*') { continue }
        $start = $null
        try { $start = $process.StartTime } catch { }
        if ($start -and $entry.start -and [Math]::Abs(($start - [datetime]$entry.start).TotalSeconds) -gt 1.0) { continue }
        $alive += $entry
    }
    return $alive
}

function Get-PoolOpenJobs {
    # The ids of the processes that hold a job of the pool not yet done: an engine process that still runs and was started before
    # it wrote the marker (a job whose process ended without it is lost, not open, whatever program has its id by now).
    $open = @()
    foreach ($marker in @(Get-ChildItem (Join-Path (Get-PoolDir) 'taken') -Filter '*.taken' -File -ErrorAction SilentlyContinue)) {
        if (Test-Path ($marker.FullName -replace '\.taken$', '.done')) { continue }
        $holder = 0
        try { $holder = [int]([System.IO.File]::ReadAllText($marker.FullName).Trim()) } catch { }
        if ($holder -le 0) { continue }
        $process = Get-Process -Id $holder -ErrorAction SilentlyContinue
        if (-not $process -or $process.ProcessName -notlike 'UnrealEditor*') { continue }
        $start = $null
        try { $start = $process.StartTime } catch { }
        if ($start -and $start -gt $marker.CreationTime.AddSeconds(1)) { continue }
        $open += $holder
    }
    return $open
}

function Receive-PoolJobBack {
    # Takes a job of the pool back from its queue as a process takes one (UScenarioBatchSubsystem::TakeJob): under the job's lock file,
    # only if no process has written its marker; the marker then names no process (0) and the job's file is removed. True if taken.
    param([string]$Queue, [string]$Name)
    $taken = Join-Path $Queue 'taken'
    $marker = Join-Path $taken "$Name.taken"
    $file = Join-Path $Queue $Name
    if ((Test-Path $marker) -or -not (Test-Path $file)) { return $false }
    $lock = $null
    try { $lock = [System.IO.File]::Open((Join-Path $taken "$Name.lock"), [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None) } catch { return $false }
    try {
        if (Test-Path $marker) { return $false }
        [System.IO.File]::WriteAllText($marker, '0')
    } finally { $lock.Dispose() }
    try { Remove-Item $file -Force -ErrorAction Stop } catch { }
    return $true
}

function Test-PoolEnding {
    # The pool takes no new batches: told to stop, draining for a stop, or its file gone.
    $dir = Get-PoolDir
    return [bool]((Test-Path (Join-Path $dir 'stop')) -or (Test-Path (Join-Path $dir 'draining')) -or -not (Test-Path (Get-PoolFile)))
}

function Get-LivePool {
    # The pool's queue when a pool runs with today's fingerprint and takes batches, or nothing. A pool with another fingerprint (the
    # settings or the build changed under it) is told to stop. Not with SANDBOX_NO_POOL=1, nor with SANDBOX_SEPARATE_PROCESSES=1
    # (every job its own process: handed to a pool, scenario.ps1 would hand itself to itself).
    if ($env:SANDBOX_NO_POOL -eq '1' -or $env:SANDBOX_SEPARATE_PROCESSES -eq '1') { return $null }
    $pool = Read-Pool
    if (-not $pool) { return $null }
    $dir = Get-PoolDir
    if (Test-PoolEnding) { return $null }
    if (@(Get-PoolAlive $pool).Count -eq 0) { return $null }
    if ($pool.print -ne (Get-PoolPrint)) {
        [System.IO.File]::WriteAllText((Join-Path $dir 'stop'), 'the fingerprint changed')
        [Console]::Error.WriteLine('QUEUE NOTE: the pool was started on another build or settings and is told to stop')
        return $null
    }
    return $dir
}

function Start-PoolProcess {
    param($Pool)
    $dir = Get-PoolDir
    $number = [int]$Pool.started
    $arguments = "`"$ProjectFile`" $(Get-QueueArguments $dir) -ScenarioPoolIdleS=$([int]$Pool.idle_s) -ScenarioPoolPrint=$($Pool.print) -nullrhi -nohmd -DisablePlugins=OpenXR -unattended -nopause -nosplash -nosound -abslog=`"$(Join-Path $dir "process_$number.log")`""
    # Started through the shell, which hands the process none of this one's handles: started as the queues' processes are, it held
    # the output of whoever started it (a call with its output redirected, `pool.ps1 -Start *> file`) open until it exited, minutes
    # later. Its environment is this one's, with the platform check left out for its start (Set-FastStartEnvironment).
    $previous = $env:UE_SKIP_UBT_SDK_SETUP
    $env:UE_SKIP_UBT_SDK_SETUP = '1'
    try {
        $process = Start-Process -FilePath (Get-EditorCmd) -ArgumentList $arguments -WorkingDirectory $RepoRoot -WindowStyle Hidden -PassThru
    } finally {
        if ($null -eq $previous) { Remove-Item Env:\UE_SKIP_UBT_SDK_SETUP -ErrorAction SilentlyContinue } else { $env:UE_SKIP_UBT_SDK_SETUP = $previous }
    }
    $start = $null
    try { $start = $process.StartTime.ToString('o') } catch { }
    $Pool.started = $number + 1
    $Pool.processes = @(@(Get-PoolAlive $Pool) + [pscustomobject]@{ id = $process.Id; start = $start })
    return $process
}

function Update-Pool {
    # While a batch waits in the pool: a process for every waiting job, up to the pool's number and within the room of the machine;
    # none once the pool is ending.
    if (-not $script:PoolMode -or (Test-PoolEnding)) { return }
    $waiting = @(Get-ChildItem (Get-PoolDir) -Filter '*.json' -File -ErrorAction SilentlyContinue).Count
    if ($waiting -eq 0) { return }
    $pool = Read-Pool
    if (-not $pool -or (@(Get-PoolAlive $pool).Count -ge [int]$pool.parallel)) { return }
    # A failure here (the lock held by a stuck tool, the file not replaced) is said and the batch waits on: it never ends the tool.
    try {
        $lock = Lock-Pool
        try {
            $pool = Read-Pool
            if (-not $pool -or (Test-PoolEnding)) { return }
            $alive = @(Get-PoolAlive $pool)
            $more = [Math]::Min([int]$pool.parallel - $alive.Count, $waiting)
            if ($more -le 0) { return }
            $pool.processes = $alive
            for ($i = 0; $i -lt $more; $i++) {
                # Each start looks again: a stop that began meanwhile (Stop-Pool's draining) gets no new process.
                if ((Test-PoolEnding) -or (Test-EngineRoom -PendingGB (1.7 * $i) -NoYield)) { break }
                [void](Start-PoolProcess $pool)
                Write-Pool $pool
            }
        } finally { $lock.Dispose() }
    } catch { [Console]::Error.WriteLine("QUEUE NOTE: the pool could not be topped up ($($_.Exception.Message))") }
}

function Request-PoolYield {
    # Another start needs room: one of the pool's idle processes (no job of it in hand) is asked to exit, unless one already is.
    # Returns how many are asked and still run.
    param([int]$Count = 1)
    $pool = Read-Pool
    if (-not $pool) { return 0 }
    $dir = Get-PoolDir
    $busy = @(Get-PoolOpenJobs)
    $asked = 0
    foreach ($entry in @(Get-PoolAlive $pool)) {
        $id = [int]$entry.id
        if ($busy -contains $id) { continue }
        $yield = Join-Path $dir "yield_$id"
        if (Test-Path $yield) { $asked++; continue }
        if ($asked -ge $Count) { continue }
        try { [System.IO.File]::WriteAllText($yield, "$PID"); $asked++ } catch { }
    }
    return $asked
}

function Stop-Pool {
    # Takes no new batches (draining), waits until the jobs in the pool are done (5 minutes at most, then refuses and lets the pool go
    # on: a batch of another session is not ended), tells the processes to stop and waits for them (30 s, then refuses: they would
    # hold the DLLs). Returns nothing when there is no pool or it stopped, or why it did not.
    param([int]$WaitS = 300)
    $file = Get-PoolFile
    if (-not (Test-Path $file)) { return $null }
    try { return (Stop-PoolNow -WaitS $WaitS) } catch { return "the stop failed: $($_.Exception.Message)" }
}

function Stop-PoolNow {
    # Stop-Pool's work; what it throws, Stop-Pool says.
    param([int]$WaitS)
    $file = Get-PoolFile
    $dir = Get-PoolDir
    New-Item -ItemType Directory -Force $dir | Out-Null
    $draining = Join-Path $dir 'draining'
    [System.IO.File]::WriteAllText($draining, "$PID")
    # A top-up under way (Update-Pool holds the lock while it starts processes) ends before the processes are counted.
    $barrier = Lock-Pool
    $barrier.Dispose()
    $deadline = (Get-Date).AddSeconds($WaitS)
    while ($true) {
        if (@(Get-PoolAlive (Read-Pool)).Count -eq 0) { break }
        $waiting = @(Get-ChildItem $dir -Filter '*.json' -File -ErrorAction SilentlyContinue).Count
        $open = @(Get-PoolOpenJobs).Count
        if ($waiting -eq 0 -and $open -eq 0) { break }
        if ((Get-Date) -gt $deadline) {
            Remove-Item $draining -Force -ErrorAction SilentlyContinue
            return "the pool still has $waiting job(s) waiting and $open running after $WaitS s"
        }
        Start-Sleep -Milliseconds 500
    }
    [System.IO.File]::WriteAllText((Join-Path $dir 'stop'), "$PID")
    $deadline = (Get-Date).AddSeconds(30)
    while (@(Get-PoolAlive (Read-Pool)).Count -gt 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    $still = @(Get-PoolAlive (Read-Pool))
    if ($still.Count -gt 0) { return ('its process(es) {0} still run 30 s after the stop' -f (($still | ForEach-Object { $_.id }) -join ', ')) }
    $lock = Lock-Pool
    try {
        # Looked at again under the lock: no process of it may be left to hold the DLLs.
        $still = @(Get-PoolAlive (Read-Pool))
        if ($still.Count -gt 0) { return ('its process(es) {0} still run after the stop' -f (($still | ForEach-Object { $_.id }) -join ', ')) }
        Remove-Item $file -Force -ErrorAction SilentlyContinue
        Remove-Item $draining, (Join-Path $dir 'stop') -Force -ErrorAction SilentlyContinue
    } finally { $lock.Dispose() }
    return $null
}

$script:AloneCount = 0
function Invoke-AloneInCheckQueue {
    # A run that has to be alone for its timing, inside a check set: asks the owner, waits until every engine process of the
    # set has ended (and every other run of the machine, two minutes at most), runs in a process of its own, and lets the set
    # go on. Each request has a name of its own, so that the grant of an earlier one is never taken for it.
    param([array]$Jobs)
    $queue = $env:SANDBOX_CHECK_QUEUE
    $script:AloneCount++
    $request = Join-Path $queue ('alone_{0}_{1}.request' -f $PID, $script:AloneCount)
    [System.IO.File]::WriteAllText($request, "$PID")
    try {
        $grantFile = Join-Path $queue 'alone.granted'
        while ($true) {
            Update-CheckQueue
            $granted = $null
            if (Test-Path $grantFile) { try { $granted = [System.IO.File]::ReadAllText($grantFile).Trim() } catch { } }
            if ($granted -eq [System.IO.Path]::GetFileNameWithoutExtension($request)) { break }
            Start-Sleep -Milliseconds 250
        }
        $inSet = $env:SANDBOX_CHECK_QUEUE
        $env:SANDBOX_CHECK_QUEUE = $null
        try { Invoke-ScenarioQueue -Jobs $Jobs -Parallel 1 } finally { $env:SANDBOX_CHECK_QUEUE = $inSet }
    } finally {
        Remove-Item $request -Force -ErrorAction SilentlyContinue
    }
}

function Get-ShoveEvents {
    # Two fists side by side that go from 70 cm before the chest through where it was, along the way given by the angle
    # (0: +x, 90: +y), wait, and go back over it: the events of a scenario.
    param([double]$Radians, [double]$At = 3.0, [double]$Mps = 4.0, [double]$Cm = 320.0)
    $way = @([Math]::Cos($Radians), [Math]::Sin($Radians))
    $took = [Math]::Round($Cm / 100.0 / $Mps, 3)
    $events = @()
    foreach ($hand in @(@('hand_left', 12.0), @('hand_right', -12.0))) {
        $side = @((-$way[1] * $hand[1]), ($way[0] * $hand[1]))
        $place = { param([double]$along, [double]$height) return @([Math]::Round($way[0] * $along + $side[0], 3), [Math]::Round($way[1] * $along + $side[1], 3), $height) }
        $events += [ordered]@{ t = $At; type = 'effector'; effector = $hand[0]; path = @(
            [ordered]@{ t = 0.0; location_cm = (& $place -70 128) }, [ordered]@{ t = $took; location_cm = (& $place ($Cm - 70) 128) },
            [ordered]@{ t = ($took + 1.0); location_cm = (& $place ($Cm - 70) 158) }, [ordered]@{ t = ($took + 2.0); location_cm = (& $place -70 198) }) }
    }
    return $events
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

function Get-RunHashes {
    # Both hashes of every run under a folder: every metrics.json found below it, by the path of its folder relative to
    # the one given ('.' for a metrics.json in the folder itself). A run that could not run has no hashes and keeps its
    # verdict, so that "no result" is never read as "the same". Returns an ordered table: path -> { bodies, bones, ok, pass }.
    param([string]$Folder)
    $root = [System.IO.Path]::GetFullPath($Folder).TrimEnd('\')
    $set = [ordered]@{}
    if (-not (Test-Path $root)) { return $set }
    $files = @(Get-ChildItem $root -Recurse -Filter 'metrics.json' -File -ErrorAction SilentlyContinue | Sort-Object FullName)
    foreach ($file in $files) {
        $relative = $file.DirectoryName.Substring($root.Length).TrimStart('\')
        if (-not $relative) { $relative = '.' }
        $run = $null
        try { $run = Read-JsonFile $file.FullName } catch { $run = $null }
        $entry = [ordered]@{ bodies = $null; bones = $null; ok = $false; pass = $false }
        if ($run) {
            $entry.ok = [bool]$run.ok
            $entry.pass = [bool]$run.pass
            if ($run.hashes) { $entry.bodies = [string]$run.hashes.bodies; $entry.bones = [string]$run.hashes.bones }
        }
        $set[$relative] = $entry
    }
    return $set
}

function Compare-RunHashes {
    # Two sets of Get-RunHashes, run by run by both hashes: the paths with the same hashes, those that differ (or whose
    # run gave no hashes in either), those of the first set the second lacks, and those only the second has.
    param($Before, $After)
    $same = @()
    $changed = @()
    $missing = @()
    $added = @()
    foreach ($key in @($Before.Keys)) {
        $was = $Before[$key]
        if (-not $After.Contains($key)) { $missing += $key; continue }
        $is = $After[$key]
        if ($was.bodies -and $was.bones -and $was.bodies -eq $is.bodies -and $was.bones -eq $is.bones) { $same += $key; continue }
        $changed += [ordered]@{ run = $key; before = "$($was.bodies) $($was.bones)"; after = "$($is.bodies) $($is.bones)"; pass_before = $was.pass; pass_after = $is.pass }
    }
    foreach ($key in @($After.Keys)) { if (-not $Before.Contains($key)) { $added += $key } }
    return [ordered]@{ same = $same; changed = $changed; missing = $missing; added = $added }
}

function ConvertTo-RunHashSet {
    # A set of Get-RunHashes read back from JSON (a PSCustomObject) as the ordered table Compare-RunHashes takes.
    param($Object)
    $set = [ordered]@{}
    if ($null -eq $Object) { return $set }
    foreach ($property in $Object.PSObject.Properties) {
        $value = $property.Value
        $set[$property.Name] = [ordered]@{ bodies = [string]$value.bodies; bones = [string]$value.bones; ok = [bool]$value.ok; pass = [bool]$value.pass }
    }
    return $set
}

function Get-EngineProcesses {
    # The engine processes running on this machine (the project's runs, tests and sessions, another session's, the owner's
    # editor): Id, name and start time.
    return @(Get-Process -Name 'UnrealEditor*' -ErrorAction SilentlyContinue | ForEach-Object {
        $start = $null
        try { $start = $_.StartTime } catch { }
        [pscustomobject]@{ Id = $_.Id; Name = $_.ProcessName; StartTime = $start }
    })
}

function Get-FreeMemoryGB {
    # The machine's available physical memory, GB (GlobalMemoryStatusEx; a tenth of a second faster than asking CIM, which every
    # start of an engine process pays: Test-EngineRoom).
    Add-Type -AssemblyName Microsoft.VisualBasic
    return [Math]::Round([double](New-Object Microsoft.VisualBasic.Devices.ComputerInfo).AvailablePhysicalMemory / 1GB, 2)
}

function Get-LogErrors {
    # Error lines from an engine log, for a short failure report.
    param([string]$LogFile, [int]$Max = 20)
    if (-not (Test-Path $LogFile)) { return @() }
    return @(Select-String -Path $LogFile -Pattern 'Error:|Fatal error|Assertion failed|Unhandled Exception' |
        Select-Object -First $Max | ForEach-Object { $_.Line.Trim() })
}
