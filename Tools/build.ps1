# Compiles the project from the command line.
#   powershell -ExecutionPolicy Bypass -File Tools\build.ps1
# Prints compiler errors in MSVC form and writes Saved\Tools\build.json. Exit code 0 = success.
# Close the Unreal Editor first: it locks the module DLLs.
param(
    [string]$Target = 'DictatorSandboxEditor',
    [string]$Configuration = 'Development',
    [int]$TimeoutSec = 3600
)
. "$PSScriptRoot\common.ps1"

$buildBat = Join-Path (Get-EngineRoot) 'Engine\Build\BatchFiles\Build.bat'
$logFile = Join-Path $ToolsOut 'build.log'
$resultFile = Join-Path $ToolsOut 'build.json'
New-Item -ItemType Directory -Force $ToolsOut | Out-Null

$started = Get-Date
# cmd /c strips the outermost pair of quotes, hence the doubled quote at each end.
$arguments = "/c `"`"$buildBat`" $Target Win64 $Configuration -Project=`"$ProjectFile`" -WaitMutex -NoHotReloadFromIDE`""
$run = Invoke-Process -FilePath $env:ComSpec -ArgumentString $arguments -TimeoutSec $TimeoutSec -CaptureOutput
$seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)

[System.IO.File]::WriteAllLines($logFile, [string[]]$run.Output)

$errors = @($run.Output | Where-Object { $_ -match '(: (fatal )?error |error [A-Z]+\d+|^ERROR:|^Error:|Unhandled exception)' } | Select-Object -Unique)
$warnings = @($run.Output | Where-Object { $_ -match ': warning ' } | Select-Object -Unique)
$ok = ($run.ExitCode -eq 0)

Write-JsonFile $resultFile ([ordered]@{
    ok         = $ok
    target     = $Target
    exit_code  = $run.ExitCode
    timed_out  = $run.TimedOut
    seconds    = $seconds
    errors     = $errors
    warnings   = $warnings
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
