# Creates, rigs and assembles the project's MetaHuman by script.
#   powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1
#   ... -Steps create                 run only some steps (create, rig, textures, build, inspect)
#   ... -Name SandboxNPC -Quality MEDIUM
#   ... -Recreate                     delete the character and its assembled assets first, then run
#
# The 'rig' and 'textures' steps call Epic's MetaHuman cloud. On the first run a browser window opens
# for an Epic Games sign-in; after that the sign-in is remembered and the script runs unattended.
# Steps whose result already exists are skipped, so the script is safe to re-run.
# Success is read from Saved\Tools\metahuman.json. Exit code: 0 ok, 1 a step failed, 2 no result.
param(
    [string]$Name = 'SandboxNPC',
    [string]$Steps = 'create,rig,textures,build,inspect',
    [string]$Quality = 'MEDIUM',
    [switch]$Recreate,
    [int]$TimeoutSec = 3600
)
. "$PSScriptRoot\common.ps1"

if ($Recreate) {
    # Start from nothing: remove the character asset and its assembled output (both made by this script).
    $targets = @(
        (Join-Path $RepoRoot "Content\Characters\MetaHumans\$Name.uasset"),
        (Join-Path $RepoRoot "Content\MetaHumans\$Name")
    )
    foreach ($target in $targets) {
        if (Test-Path $target) { Remove-Item $target -Recurse -Force }
    }
}

$resultFile = Join-Path $ToolsOut 'metahuman.json'
$logFile = Join-Path $ToolsOut 'metahuman.log'
$script = Join-Path $PSScriptRoot 'python\metahuman_build.py'
if (Test-Path $resultFile) { Remove-Item $resultFile -Force }

# The Python script reads its inputs from the environment; the child process inherits these.
$env:SANDBOX_RESULT = $resultFile
$env:SANDBOX_MH_NAME = $Name
$env:SANDBOX_MH_STEPS = $Steps
$env:SANDBOX_MH_QUALITY = $Quality

# Full editor (not a commandlet): the cloud requests and the sign-in need the editor's main loop.
$editor = Join-Path (Get-EngineRoot) 'Engine\Binaries\Win64\UnrealEditor.exe'
$arguments = "`"$ProjectFile`" -ExecutePythonScript=`"$script`" -nohmd -DisablePlugins=OpenXR -nosplash -nopause -abslog=`"$logFile`""
New-Item -ItemType Directory -Force $ToolsOut | Out-Null
if (Test-Path $logFile) { Remove-Item $logFile -Force }
$run = Invoke-Process -FilePath $editor -ArgumentString $arguments -TimeoutSec $TimeoutSec

$result = Read-JsonFile $resultFile
if (-not $result) {
    Write-Output "METAHUMAN ERROR: no result file (exit code $($run.ExitCode), timed out: $($run.TimedOut)). Log: $logFile"
    Get-LogErrors $logFile | ForEach-Object { Write-Output "  $_" }
    exit 2
}

foreach ($step in $result.steps) {
    if ($step.ok) {
        Write-Output ("  [ok  ] {0}: {1} ({2} s)" -f $step.step, $step.detail, $step.seconds)
    } else {
        Write-Output ("  [FAIL] {0}: {1} ({2} s)" -f $step.step, $step.error, $step.seconds)
    }
}
foreach ($warning in @($result.warnings)) {
    if ($warning) { Write-Output ("  [warn] {0}" -f $warning) }
}
if ($result.ok) {
    Write-Output "METAHUMAN OK: $Name ($Steps). Result: $resultFile"
    exit 0
}
if ($result.error) { Write-Output "  $($result.error)" }
Write-Output "METAHUMAN FAILED: $Name. Result: $resultFile  Log: $logFile"
exit 1
