# Copies the stock UE5 mannequin (mesh, physics asset, animations) from the engine's template
# resources into Content\Characters\Mannequins. The copy is not committed: it is Epic content that
# ships with every engine install, and bootstrap recreates it. Safe to re-run (skips when up to date).
#   powershell -ExecutionPolicy Bypass -File Tools\fetch-stock-content.ps1 [-Force]
param([switch]$Force)
. "$PSScriptRoot\common.ps1"

$source = Join-Path (Get-EngineRoot) 'Templates\TemplateResources\High\Characters\Content\Mannequins'
$destination = Join-Path $RepoRoot 'Content\Characters\Mannequins'
$marker = Join-Path $destination 'Meshes\SKM_Manny_Simple.uasset'

if (-not (Test-Path $source)) {
    Write-Output "FETCH ERROR: engine template content not found at $source"
    exit 2
}
if ((Test-Path $marker) -and -not $Force) {
    Write-Output "FETCH OK: stock mannequin already present"
    exit 0
}

New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
if (-not (Test-Path $marker)) {
    Write-Output "FETCH ERROR: copy finished but $marker is missing"
    exit 2
}
$count = @(Get-ChildItem $destination -Recurse -File).Count
Write-Output "FETCH OK: copied $count stock mannequin files to Content\Characters\Mannequins"
exit 0
