# Shows what a recorded clip asks of the body: the body hangs by its welded pelvis, its muscles are asked for the
# clip's pose at each of a list of times in the clip, one after the other, and a picture is taken of each once the
# body has got there. The pictures are joined into one sheet, labelled with the clip's times.
#   powershell -ExecutionPolicy Bypass -File Tools\clip-film.ps1 -Clip M_ragdoll_getup_stand_B -ClipTimes "0,0.5,1,1.5,2,2.5,3,3.5,4,4.5,5"
#   ... -Clip <name>          a clip of Epic's Game Animation Sample by its name (Tools\fetch-sample-anims.ps1), or any
#                             animation sequence by its path ("/Game/..."); several, separated by commas, are shown
#                             one after the other, each at every time of -ClipTimes; "|mirror" after a name (in quotes)
#                             is that clip's mirror image (R2's step 8; the default tag then ends in _mirror)
#   ... -Carry                the weld goes where the clip has the pelvis, so that the body is seen lying, kneeling and
#                             standing as the clip has it, not upright all along; -RaiseCm 100 keeps it clear of the floor
#   ... -HoldS 1.0            simulated seconds given to each pose before its picture
#   ... -Camera side|front|back|quarter, -Tune "name=value,...", -Overlay, -CropW, -Cols, -TileH   as Tools\film.ps1
#   ... -Tag <name>           output name (default: clip_<the clip's name>)
# The body is the one of Scenarios\clip_pose_hold.json, with the balance assist switched off. Arms and neck are soft
# (40 N*m/rad) and sag under their own weight by 10 to 20 degrees: add -Tune "muscles.arms.stiffness_nm_per_rad=300"
# to see the pose that is asked for rather than the one such muscles reach.
# Writes Saved\Films\<tag>\sheet.png. Exit code: 0 a sheet was written, 1 not.
param(
    [Parameter(Mandatory = $true)][string]$Clip,
    [string]$ClipTimes = '0,0.5,1,1.5,2,2.5,3,3.5,4,4.5,5',
    [double]$HoldS = 1.0,
    [switch]$Carry,
    [double]$RaiseCm = 0,
    [string]$Camera = 'quarter',
    [string]$Tune,
    [string]$Overlay,
    [string]$Tag,
    [double]$CropW = 0.6,
    [int]$Cols = 6,
    [int]$TileH = 400
)
. "$PSScriptRoot\common.ps1"

$clipPaths = @()
foreach ($one in @($Clip.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
    if ($one.StartsWith('/')) { $clipPaths += $one } else { $clipPaths += "/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/$one" }
}
# A clip's mirror image is its key with "|mirror" after it (R2's step 8): not a character a folder's name may hold.
if (-not $Tag) {
    $firstClip = $clipPaths[0].Split('/')[-1]
    $Tag = 'clip_' + $firstClip.Split('|')[0].Split('.')[0] + $(if ($firstClip.EndsWith('|mirror')) { '_mirror' } else { '' })
}
if ($Tag.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    Write-Output "CLIP FILM ERROR: the tag '$Tag' is not a name a folder can have (-Tag)"
    exit 1
}
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\clip_pose_hold.json')
if (-not $base) {
    Write-Output 'CLIP FILM ERROR: Scenarios\clip_pose_hold.json does not exist'
    exit 1
}
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$moments = @($ClipTimes.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) })
# The scenario's own pin and its switching off of the assist, then one pose after the other.
$eventList = @($base.events | Where-Object { $_.type -ne 'clip_pose' })
if ($Carry) {
    foreach ($pinEvent in @($eventList | Where-Object { $_.type -eq 'pin' })) {
        $pinEvent | Add-Member -NotePropertyName follow_clip -NotePropertyValue $true -Force
        $pinEvent | Add-Member -NotePropertyName raise_cm -NotePropertyValue $RaiseCm -Force
    }
}
$start = 1.5
$shotTimes = @()
$captions = @()
foreach ($clipPath in $clipPaths) {
    foreach ($moment in $moments) {
        $at = $start + $shotTimes.Count * $HoldS
        $eventList += [pscustomobject]@{ t = $at; type = 'clip_pose'; clip = $clipPath; time_s = $moment }
        $shotTimes += ($at + $HoldS - 0.05)
        $caption = $moment.ToString('F2', $culture)
        if ($clipPaths.Count -gt 1) { $caption = ($clipPath.Split('/')[-1].Split('.')[0] -replace '^M_ragdoll_', '') + ' ' + $caption }
        $captions += $caption
    }
}
$outDir = Join-Path $RepoRoot "Saved\Films\$Tag"
New-Item -ItemType Directory -Force $outDir | Out-Null
$eventFile = Join-Path $outDir 'events.json'
[System.IO.File]::WriteAllText($eventFile, (ConvertTo-Json @($eventList) -Depth 8))

$filmArguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'film.ps1'), '-Name', 'clip_pose_hold', '-Tag', $Tag,
    '-Events', $eventFile, '-Duration', ($start + $shotTimes.Count * $HoldS + 0.2).ToString($culture),
    '-Times', (($shotTimes | ForEach-Object { $_.ToString('F3', $culture) }) -join ','),
    '-Labels', ($captions -join ','),
    '-Camera', $Camera, '-CropW', $CropW.ToString($culture), '-Cols', $Cols, '-TileH', $TileH)
if ($Tune) { $filmArguments += @('-Tune', $Tune) }
if ($Overlay) { $filmArguments += @('-Overlay', $Overlay) }
& powershell @filmArguments
exit $LASTEXITCODE
