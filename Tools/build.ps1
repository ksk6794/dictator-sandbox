# Compiles the project from the command line.
#   powershell -ExecutionPolicy Bypass -File Tools\build.ps1
#   ... -Rebuild      a full build: the project's modules cleaned and built again (the engine is installed and stays)
#   ... -Timestamps   UnrealBuildTool's log with times (its own Log.txt, copied to Saved\Tools\build-ubt.log), and each
#                     action's end from the executor's start (finished_s). A build that runs the header tool loses them
#                     after it (the tool sets the log's timestamps off), so a header's build has none
# Prints compiler errors in MSVC form and writes Saved\Tools\build.json, with every action of the build and its own time
# (actions: wall_s, cpu_s), which UnrealBuildTool prints when Saved\UnrealBuildTool\BuildConfiguration.xml asks for it
# (ParallelExecutor, bShowPerActionCompilationTimes: Tools\bench.ps1 -Builds reads them). Exit code 0 = success.
# Close the Unreal Editor first: it locks the module DLLs.
param(
    [string]$Target = 'DictatorSandboxEditor',
    [string]$Configuration = 'Development',
    [int]$TimeoutSec = 3600,
    [switch]$Rebuild,
    [switch]$Timestamps
)
. "$PSScriptRoot\common.ps1"

$buildBat = Join-Path (Get-EngineRoot) 'Engine\Build\BatchFiles\Build.bat'
$logFile = Join-Path $ToolsOut 'build.log'
$resultFile = Join-Path $ToolsOut 'build.json'
$ubtCopy = Join-Path $ToolsOut 'build-ubt.log'
New-Item -ItemType Directory -Force $ToolsOut | Out-Null
# What this build is to make, removed first: an earlier build's result must not pass for this one's.
foreach ($stale in @($ubtCopy, $resultFile)) { if (Test-Path $stale) { Remove-Item $stale -Force } }

$extra = ''
if ($Rebuild) { $extra += ' -Rebuild' }
if ($Timestamps) { $extra += ' -Timestamps' }
$started = Get-Date
# cmd /c strips the outermost pair of quotes, hence the doubled quote at each end.
$arguments = "/c `"`"$buildBat`" $Target Win64 $Configuration -Project=`"$ProjectFile`" -WaitMutex -NoHotReloadFromIDE$extra`""
$run = Invoke-Process -FilePath $env:ComSpec -ArgumentString $arguments -TimeoutSec $TimeoutSec -CaptureOutput
$seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)

[System.IO.File]::WriteAllLines($logFile, [string[]]$run.Output)

$errors = @($run.Output | Where-Object { $_ -match '(: (fatal )?error |error [A-Z]+\d+|^ERROR:|^Error:|Unhandled exception)' } | Select-Object -Unique)
$warnings = @($run.Output | Where-Object { $_ -match ': warning ' } | Select-Object -Unique)
$ok = ($run.ExitCode -eq 0)

# The actions with their own times, as UnrealBuildTool prints them when asked to: "[3/10] (Wall: 8.12s CPU: 13.13s Mem: 3.07 GB)
# Compile [x64] SandboxBodyController.cpp". An action's line is written when it ends.
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$actions = @()
foreach ($line in @($run.Output)) {
    if ($line -match '^\[(\d+)/(\d+)\] (?:\(Wall: ([\d\.]+)s(?: CPU: ([\d\.]+)s)?[^)]*\) )?(.+)$') {
        $entry = [ordered]@{ n = [int]$Matches[1]; of = [int]$Matches[2]; action = $Matches[5].Trim(); wall_s = $null; cpu_s = $null; finished_s = $null }
        if ($Matches[3]) { $entry.wall_s = [double]::Parse($Matches[3], $culture) }
        if ($Matches[4]) { $entry.cpu_s = [double]::Parse($Matches[4], $culture) }
        $actions += $entry
    }
}
# With -Timestamps, each action's end from the executor's start, from UnrealBuildTool's own log: its lines carry the time
# since it started ("[00:00:04.512] [3/10] ..."), until the header tool, if it runs, turns that off.
if ($Timestamps) {
    $ubtLog = Join-Path $env:LOCALAPPDATA 'UnrealBuildTool\Log.txt'
    if (Test-Path $ubtLog) {
        Copy-Item $ubtLog $ubtCopy -Force
        $toSeconds = { param($text) $parts = $text.Split(':'); return [double]$parts[0] * 3600 + [double]$parts[1] * 60 + [double]::Parse($parts[2], $culture) }
        $executorStart = $null
        foreach ($line in [System.IO.File]::ReadAllLines($ubtCopy)) {
            if ($null -eq $executorStart -and $line -match '^\[(\d\d:\d\d:\d\d\.\d{3})\] Using .* executor to run \d+ action') { $executorStart = & $toSeconds $Matches[1]; continue }
            if ($null -ne $executorStart -and $line -match '^\[(\d\d:\d\d:\d\d\.\d{3})\] \[(\d+)/(\d+)\] ') {
                $number = [int]$Matches[2]
                foreach ($entry in @($actions | Where-Object { $_.n -eq $number })) { $entry.finished_s = [Math]::Round((& $toSeconds $Matches[1]) - $executorStart, 3) }
            }
        }
    }
}

Write-JsonFile $resultFile ([ordered]@{
    ok         = $ok
    target     = $Target
    exit_code  = $run.ExitCode
    timed_out  = $run.TimedOut
    seconds    = $seconds
    rebuild    = [bool]$Rebuild
    errors     = $errors
    warnings   = $warnings
    actions    = $actions
    log        = $logFile
})

if ($ok) {
    Write-Output "BUILD OK ($Target, $seconds s, $($warnings.Count) warning(s))"
    $warnings | Select-Object -First 20 | ForEach-Object { Write-Output $_ }
    exit 0
}

Write-Output "BUILD FAILED ($Target, exit code $($run.ExitCode), $seconds s)"
if ($errors.Count -gt 0) {
    $errors | Select-Object -First 40 | ForEach-Object { Write-Output $_ }
} else {
    # No recognisable error line: show the tail so the cause is still visible.
    $run.Output | Select-Object -Last 30 | ForEach-Object { Write-Output $_ }
}
Write-Output "Full log: $logFile"
exit 1
