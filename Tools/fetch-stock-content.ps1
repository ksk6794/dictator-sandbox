# Copies the stock UE5 mannequin (mesh, physics asset, animations) from the engine's template
# resources into Content\Characters\Mannequins. The copy is not committed: it is Epic content that
# ships with every engine install, and bootstrap recreates it. Safe to re-run (skips when up to date).
#   powershell -ExecutionPolicy Bypass -File Tools\fetch-stock-content.ps1 [-Force]
# Writes its verdict to Saved\Tools\fetch-stock-content.json as well (Tools\bootstrap.ps1 decides by it).
param([switch]$Force)
. "$PSScriptRoot\common.ps1"

$resultFile = Join-Path $ToolsOut 'fetch-stock-content.json'
if (Test-Path $resultFile) { Remove-Item $resultFile -Force }
function Stop-Fetch([int]$Code, [string]$Line) {
    Write-JsonFile $resultFile ([ordered]@{ ok = ($Code -eq 0); exit_code = $Code; verdict = $Line })
    Write-Output $Line
    exit $Code
}

$source = Join-Path (Get-EngineRoot) 'Templates\TemplateResources\High\Characters\Content\Mannequins'
$destination = Join-Path $RepoRoot 'Content\Characters\Mannequins'
$marker = Join-Path $destination 'Meshes\SKM_Manny_Simple.uasset'

if (-not (Test-Path $source)) {
    Stop-Fetch 2 "FETCH ERROR: engine template content not found at $source"
}
if ((Test-Path $marker) -and -not $Force) {
    Stop-Fetch 0 "FETCH OK: stock mannequin already present"
}

New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
if (-not (Test-Path $marker)) {
    Stop-Fetch 2 "FETCH ERROR: copy finished but $marker is missing"
}
$count = @(Get-ChildItem $destination -Recurse -File).Count
Stop-Fetch 0 "FETCH OK: copied $count stock mannequin files to Content\Characters\Mannequins"
