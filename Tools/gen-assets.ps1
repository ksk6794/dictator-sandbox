# Regenerates Content\Generated from the text specs by running the GenerateAssets commandlet.
#   powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1 [-Force]
# Room maps are regenerated only when their spec is newer than the map, because regenerating discards
# the baked lighting; -Force regenerates them anyway.
# Success is read from Saved\Tools\gen-assets.json, not from the process exit code.
param([switch]$Force, [int]$TimeoutSec = 1800)
. "$PSScriptRoot\common.ps1"

$resultFile = Join-Path $ToolsOut 'gen-assets.json'
$logFile = Join-Path $ToolsOut 'gen-assets.log'
if (Test-Path $resultFile) { Remove-Item $resultFile -Force }

$arguments = "-run=GenerateAssets -Result=`"$resultFile`""
if ($Force) { $arguments += ' -Force' }
$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec
$result = Read-JsonFile $resultFile

if (-not $result) {
    Write-Output "GEN-ASSETS ERROR: no result file (exit code $($run.ExitCode), timed out: $($run.TimedOut)). Log: $logFile"
    Get-LogErrors $logFile | ForEach-Object { Write-Output "  $_" }
    exit 2
}
if ($result.ok) {
    Write-Output "GEN-ASSETS OK: $(@($result.generated).Count) asset(s) generated, $(@($result.up_to_date).Count) up to date"
    exit 0
}
Write-Output "GEN-ASSETS FAILED:"
@($result.errors) | ForEach-Object { Write-Output "  $_" }
exit 1
