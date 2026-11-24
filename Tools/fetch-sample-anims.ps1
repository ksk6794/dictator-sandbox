# Copies the ragdoll clips of Epic's Game Animation Sample (get-ups, poses on the floor, flailing, reaching) and
# the skeleton they are made for into Content\Characters\UEFN_Mannequin, at the paths they have in the sample, so
# that they load as they are. The copy is not committed: it is Epic content, and the sample itself (5.4 GB) has
# to be downloaded by a person, from Fab or the Epic Games Launcher, as a project of its own.
#   powershell -ExecutionPolicy Bypass -File Tools\fetch-sample-anims.ps1 [-Source <the sample's project folder>] [-Force]
# The sample is looked for in -Source, then in the environment variable GAME_ANIMATION_SAMPLE, then in
# Documents\Unreal Projects\GameAnimationSample. Only animation sequences are taken (M_ragdoll_*): its montages,
# blend spaces and chooser tables pull in the rest of the sample. Safe to re-run (skips when up to date).
# Exit code: 0 the clips are there, 2 the sample was not found or the copy is incomplete.
param([string]$Source, [switch]$Force)
. "$PSScriptRoot\common.ps1"

if (-not $Source) { $Source = $env:GAME_ANIMATION_SAMPLE }
if (-not $Source) { $Source = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Unreal Projects\GameAnimationSample' }
$sampleContent = Join-Path $Source 'Content\Characters\UEFN_Mannequin'
$destination = Join-Path $RepoRoot 'Content\Characters\UEFN_Mannequin'
$skeleton = 'Meshes\SK_UEFN_Mannequin.uasset'
$clipDir = 'Animations\Ragdoll'
$marker = Join-Path $destination "$clipDir\M_ragdoll_getup_stand_B.uasset"

if ((Test-Path $marker) -and (Test-Path (Join-Path $destination $skeleton)) -and -not $Force) {
    Write-Output "FETCH OK: the sample's ragdoll clips are already present"
    exit 0
}
if (-not (Test-Path (Join-Path $sampleContent $skeleton))) {
    Write-Output "FETCH ERROR: Epic's Game Animation Sample was not found at $Source (download it from Fab or the Epic Games Launcher, create a project from it, and pass its folder with -Source or in GAME_ANIMATION_SAMPLE)"
    exit 2
}

New-Item -ItemType Directory -Force (Join-Path $destination 'Meshes') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $destination $clipDir) | Out-Null
Copy-Item -Path (Join-Path $sampleContent $skeleton) -Destination (Join-Path $destination $skeleton) -Force
$clips = @(Get-ChildItem (Join-Path $sampleContent $clipDir) -Filter 'M_ragdoll_*.uasset' -File)
foreach ($clip in $clips) { Copy-Item -Path $clip.FullName -Destination (Join-Path $destination "$clipDir\$($clip.Name)") -Force }
if (-not (Test-Path $marker)) {
    Write-Output "FETCH ERROR: copy finished but $marker is missing"
    exit 2
}
Write-Output "FETCH OK: copied $($clips.Count) ragdoll clips and their skeleton from $Source to Content\Characters\UEFN_Mannequin"
exit 0
