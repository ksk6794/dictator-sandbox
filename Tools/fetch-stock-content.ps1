# Copies the stock UE5 mannequin (mesh, physics asset, animations) from the engine's template
# resources into Content\Characters\Mannequins, and the hands of the engine's XR mannequins (the player's
# visible hands, decision 76 of docs/active-ragdoll.md) into Content\XRMannequins. The copies are not
# committed: they are Epic content that ships with every engine install, and bootstrap recreates them.
# Safe to re-run (skips what is up to date).
#   powershell -ExecutionPolicy Bypass -File Tools\fetch-stock-content.ps1 [-Force]
# Writes its verdict to Saved\Tools\fetch-stock-content.json as well (Tools\bootstrap.ps1 decides by it).
#
# The XR pack's assets name each other by /Game/XRMannequins/..., so the copy lands there and needs no
# redirection. Only what the two Manny hands need is copied: the meshes, their skeleton and LOD settings,
# the materials and Manny's textures; the pack's Animation Blueprint, its Blueprint, its animations and
# Quinn's hands are left out (the fingers are posed by our own C++, FSandboxHandRig).
param([switch]$Force)
. "$PSScriptRoot\common.ps1"

$resultFile = Join-Path $ToolsOut 'fetch-stock-content.json'
if (Test-Path $resultFile) { Remove-Item $resultFile -Force }
function Stop-Fetch([int]$Code, [string]$Line) {
    Write-JsonFile $resultFile ([ordered]@{ ok = ($Code -eq 0); exit_code = $Code; verdict = $Line })
    Write-Output $Line
    exit $Code
}

$engine = Get-EngineRoot
$source = Join-Path $engine 'Templates\TemplateResources\High\Characters\Content\Mannequins'
$destination = Join-Path $RepoRoot 'Content\Characters\Mannequins'
$marker = Join-Path $destination 'Meshes\SKM_Manny_Simple.uasset'

$xrSource = Join-Path $engine 'Templates\TemplateResources\Standard\XRMannequins\Content'
$xrDestination = Join-Path $RepoRoot 'Content\XRMannequins'
$xrFiles = @(
    'Meshes\SKM_MannyXR_left.uasset',
    'Meshes\SKM_MannyXR_right.uasset',
    'Meshes\SK_MannequinsXR.uasset',
    'Meshes\SkeletalMeshLODSettings_MannequinsXR.uasset',
    'Materials\M_Mannequin.uasset',
    'Materials\Functions\CA_Mannequin.uasset',
    'Materials\Functions\ChromaticCurve.uasset',
    'Materials\Functions\MF_Diffraction.uasset',
    'Materials\Functions\MF_logo3layers.uasset',
    'Materials\Functions\ML_BaseColorFallOff.uasset',
    'Materials\Instances\Manny\MI_Manny_01.uasset',
    'Materials\Instances\Manny\MI_Manny_02.uasset',
    'Textures\Shared\T_UE_Logo_M.uasset'
)

if (-not (Test-Path $source)) {
    Stop-Fetch 2 "FETCH ERROR: engine template content not found at $source"
}
if (-not (Test-Path $xrSource)) {
    Stop-Fetch 2 "FETCH ERROR: the engine's XR mannequins not found at $xrSource"
}

$said = @()
# A copy that throws (common.ps1 stops on any error) still leaves a verdict for the bootstrap to read.
try {
    if ((Test-Path $marker) -and -not $Force) {
        $said += 'stock mannequin already present'
    }
    else {
        New-Item -ItemType Directory -Force $destination | Out-Null
        Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
        if (-not (Test-Path $marker)) {
            Stop-Fetch 2 "FETCH ERROR: copy finished but $marker is missing"
        }
        $said += "copied $(@(Get-ChildItem $destination -Recurse -File).Count) stock mannequin files to Content\Characters\Mannequins"
    }

    # Manny's textures, every one (the materials name them by path).
    $mannyTextures = Join-Path $xrSource 'Textures\Manny'
    if (-not (Test-Path $mannyTextures)) {
        Stop-Fetch 2 "FETCH ERROR: the engine's XR mannequins have no Textures\Manny ($xrSource)"
    }
    $xrWanted = @($xrFiles) + @(Get-ChildItem $mannyTextures -File -Filter '*.uasset' | ForEach-Object { "Textures\Manny\$($_.Name)" })
    $xrMissing = @($xrWanted | Where-Object { -not (Test-Path (Join-Path $xrDestination $_)) })
    if ($xrMissing.Count -eq 0 -and -not $Force) {
        $said += 'XR hands already present'
    }
    else {
        foreach ($file in $xrWanted) {
            $from = Join-Path $xrSource $file
            if (-not (Test-Path $from)) {
                Stop-Fetch 2 "FETCH ERROR: $file is not in the engine's XR mannequins ($xrSource)"
            }
            $to = Join-Path $xrDestination $file
            New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
            Copy-Item -Path $from -Destination $to -Force
        }
        $said += "copied $($xrWanted.Count) XR hand files to Content\XRMannequins"
    }
}
catch {
    Stop-Fetch 2 "FETCH ERROR: the copy failed: $($_.Exception.Message)"
}
Stop-Fetch 0 ("FETCH OK: " + ($said -join '; '))
