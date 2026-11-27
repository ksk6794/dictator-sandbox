# Builds static lighting (CPU Lightmass) and reflection captures for a generated map, from the command line.
#   powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Map /Game/Generated/Maps/TestRoom] [-Quality Preview]
# Quality: Preview, Medium, High, Production. Run Tools\gen-assets.ps1 first; it creates the unlit map.
# Skips the bake when the map has not changed since the last successful bake at this quality; -Force rebakes.
# Success means the map's <Map>_BuiltData package exists and was written by this run.
# Writes Saved\Tools\bake-lighting.json. Exit code: 0 baked, 1 no lighting data produced, 2 the run broke.
param(
    [string]$Map = '/Game/Generated/Maps/TestRoom',
    [ValidateSet('Preview', 'Medium', 'High', 'Production')][string]$Quality = 'Preview',
    [switch]$Force,
    [int]$TimeoutSec = 3600
)
. "$PSScriptRoot\common.ps1"

$resultFile = Join-Path $ToolsOut 'bake-lighting.json'
if (Test-Path $resultFile) { Remove-Item $resultFile -Force }
$logFile = Join-Path $ToolsOut 'bake-lighting.log'
$relative = $Map -replace '^/Game/', ''
$mapFile = Join-Path $RepoRoot ("Content\" + ($relative -replace '/', '\') + '.umap')
$builtDataFile = Join-Path $RepoRoot ("Content\" + ($relative -replace '/', '\') + '_BuiltData.uasset')

if (-not (Test-Path $mapFile)) {
    Write-Output "BAKE ERROR: $mapFile does not exist. Run Tools\gen-assets.ps1 first."
    exit 2
}
# The bake re-saves the map, so file times cannot tell whether lighting is current. A stamp written after
# a successful bake records the map's write time and the quality; a match means nothing changed since.
$stampFile = Join-Path $RepoRoot ("Content\" + ($relative -replace '/', '\') + '.bake.json')
$stamp = Read-JsonFile $stampFile
if ((Test-Path $builtDataFile) -and $stamp -and -not $Force) {
    $mapTicks = (Get-Item $mapFile).LastWriteTimeUtc.Ticks
    if ($stamp.map_write_ticks -eq $mapTicks -and $stamp.quality -eq $Quality) {
        # Up to date is a result too (Tools\bootstrap.ps1 decides by the result file).
        Write-JsonFile $resultFile ([ordered]@{ ok = $true; up_to_date = $true; map = $Map; quality = $Quality; built_data = $builtDataFile })
        Write-Output "BAKE OK: $Map is up to date ($Quality quality; use -Force to rebake)"
        exit 0
    }
}
if (Test-Path $builtDataFile) { Remove-Item $builtDataFile -Force }

$started = Get-Date
$arguments = "-run=ResavePackages -BuildLighting -BuildReflectionCaptures -AllowCommandletRendering -MapsOnly -ProjectOnly -Quality=$Quality -Map=$Map"
$run = Invoke-Editor -ArgumentString $arguments -LogFile $logFile -TimeoutSec $TimeoutSec
$seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$baked = Test-Path $builtDataFile
$bytes = 0
if ($baked) { $bytes = (Get-Item $builtDataFile).Length }
$errors = @(Get-LogErrors $logFile)
$lightingLines = @()
if (Test-Path $logFile) {
    $lightingLines = @(Select-String -Path $logFile -Pattern 'LogLightmass|LogStaticLightingSystem|Lighting build|LightingBuild' |
        Select-Object -Last 8 | ForEach-Object { $_.Line.Trim() })
}

Write-JsonFile $resultFile ([ordered]@{
    # A bake that timed out is no bake, whatever it left behind (Tools\bootstrap.ps1 decides by this file).
    ok              = ($baked -and -not $run.TimedOut)
    map             = $Map
    quality         = $Quality
    seconds         = $seconds
    built_data      = $builtDataFile
    built_data_size = $bytes
    exit_code       = $run.ExitCode
    timed_out       = $run.TimedOut
    errors          = $errors
    log             = $logFile
})

if ($run.TimedOut) {
    Write-Output "BAKE ERROR: timed out after $TimeoutSec s. Log: $logFile"
    exit 2
}
if ($baked) {
    Write-JsonFile $stampFile ([ordered]@{ map_write_ticks = (Get-Item $mapFile).LastWriteTimeUtc.Ticks; quality = $Quality })
    Write-Output ("BAKE OK: {0} ({1} quality, {2} s, lighting data {3:N0} bytes)" -f $Map, $Quality, $seconds, $bytes)
    exit 0
}
Write-Output "BAKE FAILED: no lighting data was written for $Map (exit code $($run.ExitCode), $seconds s). Log: $logFile"
$errors | Select-Object -First 10 | ForEach-Object { Write-Output "  $_" }
$lightingLines | ForEach-Object { Write-Output "  $_" }
exit 1
