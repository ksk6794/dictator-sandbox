# Makes the database of recorded human poses that a run's poses are held against (metrics.pose_prior), from the
# clips Specs\pose_prior.json lists, or says what the one that is there was made of.
#   powershell -ExecutionPolicy Bypass -File Tools\pose-prior.ps1            (says whether it is there and current)
#   powershell -ExecutionPolicy Bypass -File Tools\pose-prior.ps1 -Build     (makes it; copies what is missing of the sample's clips first)
# The clips are carried over to the body's joints (FSandboxClipPose), a pose every 0.1 s and its mirror image
# (FSandboxPosePrior). The database is Epic's content in another shape: it is written to Saved\PosePrior and never
# committed, and neither are the clips. Saved\PosePrior\prior.json has its clips and its stamp; a run says the stamp
# of the database it used, and figures from two databases are not to be compared.
# Exit code: 0 the database is there and current, 1 it is not, 2 it could not be made.
param([switch]$Build)
. "$PSScriptRoot\common.ps1"

$specFile = Join-Path $RepoRoot 'Specs\pose_prior.json'
$outDir = Join-Path $RepoRoot 'Saved\PosePrior'
$database = Join-Path $outDir 'prior.bin'
$infoFile = Join-Path $outDir 'prior.json'
$spec = Read-JsonFile $specFile

if (-not $Build) {
    $info = Read-JsonFile $infoFile
    if (-not $info -or -not (Test-Path $database)) { Write-Output 'PRIOR MISSING: Tools\pose-prior.ps1 -Build makes it'; exit 1 }
    if ($info.spec_hash -ne (Get-FileHash $specFile).Hash) { Write-Output 'PRIOR OLD: Specs\pose_prior.json has changed since; build it again'; exit 1 }
    Write-Output "PRIOR OK: $($info.poses) poses of $(@($info.clips).Count) clips, stamp $($info.stamp)"
    exit 0
}

$fetch = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'fetch-sample-anims.ps1') -PosePrior
$fetch | Select-Object -First 3
if ($LASTEXITCODE -ne 0) { Write-Output 'PRIOR ERROR: the clips are not all there'; exit 2 }

$characters = Join-Path $RepoRoot 'Content\Characters'
$clips = @()
$sets = @()
foreach ($set in $spec.in_project) { $sets += [pscustomobject]@{ folder = $set.folder; pattern = $set.pattern; clips = $set.clips } }
foreach ($set in $spec.from_sample) { $sets += [pscustomobject]@{ folder = "UEFN_Mannequin/Animations/$($set.folder)"; pattern = $set.pattern; clips = $set.clips } }
foreach ($set in $sets) {
    $folder = Join-Path $characters $set.folder.Replace('/', '\')
    $found = @()
    if (Test-Path $folder) {
        $found = @(Get-ChildItem $folder -Recurse -Filter '*.uasset' -File | Where-Object { $_.BaseName -match $set.pattern -and $_.BaseName -notmatch $spec.exclude } | Sort-Object FullName)
    }
    if ($found.Count -ne $set.clips) {
        Write-Output "PRIOR ERROR: $($set.folder) has $($found.Count) clips of '$($set.pattern)', $($set.clips) were expected"
        exit 2
    }
    foreach ($file in $found) {
        $below = $file.FullName.Substring($characters.Length + 1).Replace('\', '/')
        $clips += ('/Game/Characters/' + $below.Substring(0, $below.Length - '.uasset'.Length))
    }
}
$clips = @($clips | Select-Object -Unique)

New-Item -ItemType Directory -Force $outDir | Out-Null
$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$scenario = [ordered]@{
    name = 'pose_prior_build'; duration_s = 1.0; pose_prior = $database; pose_prior_clips = $clips
    body = $base.body; expect = [ordered]@{}
}
$scenarioFile = Join-Path $outDir 'pose_prior_build.json'
Write-JsonFile $scenarioFile $scenario
# Neither the old database nor its account is left to be taken for the new one if a check below fails.
foreach ($old in @($database, $infoFile)) { if (Test-Path $old) { Remove-Item $old -Force } }
$runDir = Join-Path $outDir 'run'
$runOut = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'scenario.ps1') -File $scenarioFile -Out $runDir -Quiet
$line = $null
if (Test-Path (Join-Path $runDir 'run.log')) {
    $line = Get-Content (Join-Path $runDir 'run.log') | Where-Object { $_ -match 'Pose database: (\d+) poses of (\d+) clips, stamp ([0-9a-f]+)' } | Select-Object -First 1
}
if (-not $line -or -not (Test-Path $database)) {
    Write-Output 'PRIOR ERROR: the run made no database'
    $runOut | Select-Object -First 4
    exit 2
}
$null = $line -match 'Pose database: (\d+) poses of (\d+) clips, stamp ([0-9a-f]+)'
$stamp = $Matches[3]
$poses = [int]$Matches[1]

# Known answers. A clip of the database, asked of a body welded by its pelvis, is as far from the nearest pose as the
# muscles are from the clip (a few degrees, 'error' below); and a pose no one takes, the elbows turned out over the
# head with the head thrown back, is far from all of them.
function Measure-Pose([string]$Name, $Events, [double]$Duration) {
    $check = [ordered]@{ name = $Name; duration_s = $Duration; settle_s = 3.0; pose_prior = $database; body = $base.body; events = @($Events); expect = [ordered]@{} }
    $file = Join-Path $outDir "$Name.json"
    Write-JsonFile $file $check
    $null = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'scenario.ps1') -File $file -Out (Join-Path $outDir $Name) -Quiet
    return (Read-JsonFile (Join-Path $outDir "$Name\metrics.json"))
}
$weld = [ordered]@{ t = 0.0; type = 'pin'; body = 'pelvis'; lock_rotation = $true; follow_clip = $true; raise_cm = 60 }
$clipEvents = @($weld, [ordered]@{ t = 0.1; type = 'clip_pose'; clip = '/Game/Characters/UEFN_Mannequin/Animations/Walk/M_Neutral_Walk_Loop_F'; time_s = 0.3 })
$clipFile = Get-ChildItem (Join-Path $characters 'UEFN_Mannequin\Animations\Walk') -Recurse -Filter 'M_Neutral_Walk_Loop_F.uasset' | Select-Object -First 1
$clipEvents[1].clip = '/Game/Characters/' + $clipFile.FullName.Substring($characters.Length + 1).Replace('\', '/').Replace('.uasset', '')
$strange = 'Sandbox.Tune poses.stance.upperarm_l.swing1_deg=120,poses.stance.upperarm_l.twist_deg=70,poses.stance.upperarm_r.swing1_deg=120,poses.stance.upperarm_r.twist_deg=-70,poses.stance.lowerarm_l.swing1_deg=100,poses.stance.lowerarm_r.swing1_deg=100,poses.stance.neck_01.swing1_deg=60,poses.stance.thigh_l.swing1_deg=40,poses.stance.calf_r.swing1_deg=70'
$asked = Measure-Pose 'prior_check_clip' $clipEvents 5.0
$odd = Measure-Pose 'prior_check_strange' @([ordered]@{ t = 0.0; type = 'pin'; body = 'pelvis'; lock_rotation = $true }, [ordered]@{ t = 0.1; type = 'console'; command = $strange }) 5.0
$askedDeg = Get-JsonPath $asked 'metrics.pose_prior.mean_deg'
$errorDeg = Get-JsonPath $asked 'control.pose.error_mean_deg'
$oddDeg = Get-JsonPath $odd 'metrics.pose_prior.mean_deg'
Write-Output ('  check: a clip of the database asked of the body reads {0:N1} degrees (its muscles are {1:N1} from the clip), a pose no one takes {2:N1}' -f [double]$askedDeg, [double]$errorDeg, [double]$oddDeg)
if ($null -eq $askedDeg -or $null -eq $oddDeg -or [double]$askedDeg -gt 6 -or [double]$oddDeg -lt 15) {
    Write-Output 'PRIOR ERROR: the likeness does not read what it has to on these two'
    Remove-Item $database -Force
    exit 2
}

$info = [ordered]@{ stamp = $stamp; poses = $poses; spec_hash = (Get-FileHash $specFile).Hash; clips = $clips }
Write-JsonFile $infoFile $info
Write-Output "PRIOR OK: $($info.poses) poses of $($clips.Count) clips, stamp $($info.stamp), $([Math]::Round((Get-Item $database).Length / 1MB, 1)) MB in $database"
exit 0
