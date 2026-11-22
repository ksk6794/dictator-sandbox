# Regenerates Content\Generated from the text specs by running the GenerateAssets commandlet.
#   powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1
# Success is read from Saved\Tools\gen-assets.json, not from the process exit code.
param([int]$TimeoutSec = 1800)
. "$PSScriptRoot\common.ps1"

$resultFile = Join-Path $ToolsOut 'gen-assets.json'
$logFile = Join-Path $ToolsOut 'gen-assets.log'
if (Test-Path $resultFile) { Remove-Item $resultFile -Force }

$run = Invoke-Editor -ArgumentString "-run=GenerateAssets -Result=`"$resultFile`"" -LogFile $logFile -TimeoutSec $TimeoutSec
$result = Read-JsonFile $resultFile

if (-not $result) {
    Write-Output "GEN-ASSETS ERROR: no result file (exit code $($run.ExitCode), timed out: $($run.TimedOut)). Log: $logFile"
    Get-LogErrors $logFile | ForEach-Object { Write-Output "  $_" }
    exit 2
}
if ($result.ok) {
    Write-Output "GEN-ASSETS OK: $(@($result.generated).Count) asset(s) generated"
    exit 0
}
Write-Output "GEN-ASSETS FAILED:"
@($result.errors) | ForEach-Object { Write-Output "  $_" }
exit 1
