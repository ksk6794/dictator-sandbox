# Copies the ragdoll clips of Epic's Game Animation Sample (get-ups, poses on the floor, flailing, reaching) and
# the skeleton they are made for into Content\Characters\UEFN_Mannequin, at the paths they have in the sample, so
# that they load as they are. The copy is not committed: it is Epic content, and the sample itself (5.4 GB) has
# to be downloaded by a person, from Fab or the Epic Games Launcher, as a project of its own.
#   powershell -ExecutionPolicy Bypass -File Tools\fetch-sample-anims.ps1 [-Source <the sample's project folder>] [-Force]
#   ... -PosePrior      also the clips the pose database is made of (Specs\pose_prior.json, "from_sample"; 240 MB)
# The sample is looked for in -Source, then in the environment variable GAME_ANIMATION_SAMPLE, then in
# Documents\Unreal Projects\GameAnimationSample. Only animation sequences are taken (M_ragdoll_*): its montages,
# blend spaces and chooser tables pull in the rest of the sample. Safe to re-run (skips when up to date).
# Exit code: 0 the clips are there, 2 the sample was not found or the copy is incomplete.
param([string]$Source, [switch]$Force, [switch]$PosePrior)
. "$PSScriptRoot\common.ps1"

if (-not $Source) { $Source = $env:GAME_ANIMATION_SAMPLE }
if (-not $Source) { $Source = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Unreal Projects\GameAnimationSample' }
$sampleContent = Join-Path $Source 'Content\Characters\UEFN_Mannequin'
$destination = Join-Path $RepoRoot 'Content\Characters\UEFN_Mannequin'
$skeleton = 'Meshes\SK_UEFN_Mannequin.uasset'
$clipDir = 'Animations\Ragdoll'
$marker = Join-Path $destination "$clipDir\M_ragdoll_getup_stand_B.uasset"

# The clips of the pose database, by the spec's patterns: to where they are in the sample, so that they load as they are.
function Copy-PriorClips {
    $spec = Read-JsonFile (Join-Path $RepoRoot 'Specs\pose_prior.json')
    $copied = 0
    $there = 0
    foreach ($set in $spec.from_sample) {
        $from = Join-Path $sampleContent "Animations\$($set.folder)"
        $to = Join-Path $destination "Animations\$($set.folder)"
        $have = @()
        if (Test-Path $to) { $have = @(Get-ChildItem $to -Recurse -Filter '*.uasset' -File | Where-Object { $_.BaseName -match $set.pattern -and $_.BaseName -notmatch $spec.exclude }) }
        if ($have.Count -eq $set.clips -and -not $Force) { $there += $have.Count; continue }
        if (-not (Test-Path $from)) {
            Write-Output "FETCH ERROR: $from was not found (Epic's Game Animation Sample; see the top of this script)"
            exit 2
        }
        $files = @(Get-ChildItem $from -Recurse -Filter '*.uasset' -File | Where-Object { $_.BaseName -match $set.pattern -and $_.BaseName -notmatch $spec.exclude })
        if ($files.Count -ne $set.clips) {
            Write-Output "FETCH ERROR: the sample has $($files.Count) clips of '$($set.pattern)' in $($set.folder), $($set.clips) were expected (Specs\pose_prior.json)"
            exit 2
        }
        foreach ($file in $files) {
            $target = Join-Path $to $file.FullName.Substring($from.Length + 1)
            New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
            Copy-Item -Path $file.FullName -Destination $target -Force
            $copied++
        }
    }
    Write-Output "FETCH OK: $copied clips of the pose database copied, $there were there"
}

if ((Test-Path $marker) -and (Test-Path (Join-Path $destination $skeleton)) -and -not $Force) {
    Write-Output "FETCH OK: the sample's ragdoll clips are already present"
    if ($PosePrior) { Copy-PriorClips }
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
if ($PosePrior) { Copy-PriorClips }
Write-Output "FETCH OK: copied $($clips.Count) ragdoll clips and their skeleton from $Source to Content\Characters\UEFN_Mannequin"
exit 0
