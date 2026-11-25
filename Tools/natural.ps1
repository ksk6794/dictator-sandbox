# The scorecard of the natural body (docs\natural-body.md): runs a fixed set and prints one row for each criterion of
# the plan: what the body does, what people do where a figure is known, the proposed limit and how the two compare.
#   powershell -ExecutionPolicy Bypass -File Tools\natural.ps1
#   ... -Label before_n2          names the output: Saved\Natural\<Label> (default 'latest')
#   ... -Baseline before_track    prints that report's figures beside this one's
#   ... -Tune "name=value,..."    tuning overrides for every run
#   ... -Quick                    leaves out the stand of 150 falls on three seeds (C19, C20 and C21 from the four get-ups alone)
#   ... -NoPrior                  without the pose database (Tools\pose-prior.ps1 -Build)
#
# The set: a stand of 62 s; blows of 30 and 80 N*s on the chest from four sides; knockdowns by a blow of 200 N*s from eight
# sides and by two fists from sixteen; the get-ups from four sides; five strikes; 48 steady pushes (Tools\compliance.ps1); the stand
# of falls on three seeds (Tools\getup-seeds.ps1). Figures that need all of today's scenarios (C25) are read from the
# latest run of the suite.
#
# It reports and judges nothing: the limits are proposals until the owner has set them, and a row says "in" or "out"
# of the proposal. "-" is what cannot be measured before the part that builds it. Writes Saved\Natural\<Label>\report.json.
# Exit code: 0 a report was written, 2 a run gave no result.
param(
    [string]$Label = 'latest',
    [string]$Baseline,
    [string]$Tune,
    [switch]$Quick,
    [switch]$NoPrior,
    [int]$Parallel = 8
)
. "$PSScriptRoot\common.ps1"

$culture = [System.Globalization.CultureInfo]::InvariantCulture
$naturalRoot = Join-Path $RepoRoot 'Saved\Natural'
$out = Join-Path $naturalRoot $Label
if ($Baseline -eq $Label) { Write-Output 'NATURAL ERROR: a report cannot be its own baseline'; exit 2 }
$old = $null
if ($Baseline) {
    $old = Read-JsonFile (Join-Path $naturalRoot "$Baseline\report.json")
    if (-not $old) { Write-Output "NATURAL ERROR: there is no report '$Baseline' in $naturalRoot"; exit 2 }
}
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
$scenarioDir = Join-Path $out 'scenarios'
New-Item -ItemType Directory -Force $scenarioDir | Out-Null
$prior = Join-Path $RepoRoot 'Saved\PosePrior\prior.bin'
if (-not $NoPrior) {
    $priorSays = & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'pose-prior.ps1')
    if ($LASTEXITCODE -ne 0) { Write-Output "NATURAL ERROR: $priorSays (or -NoPrior)"; exit 2 }
}

# --- The set -------------------------------------------------------------------------------------------------------

$base = Read-JsonFile (Join-Path $RepoRoot 'Scenarios\stand_npc.json')
$trials = [ordered]@{}
function Add-Trial([string]$Set, [string]$Name, $Scenario) {
    $Scenario.name = $Name
    $Scenario.expect = [ordered]@{}
    if (-not $NoPrior) { $Scenario.pose_prior = $prior }
    $file = Join-Path $scenarioDir "$Name.json"
    Write-JsonFile $file $Scenario
    $trials[$Name] = [ordered]@{ set = $Set; name = $Name; file = $file; dir = (Join-Path $out "runs\$Name") }
}
function New-Blow([string]$Body, [double]$Ns, [double]$Angle, [double]$Duration) {
    $radians = $Angle * [Math]::PI / 180.0
    return [ordered]@{ name = ''; duration_s = $Duration; body = $base.body; events = @([ordered]@{ t = 3.0; type = 'impulse'; body = $Body; impulse_ns = $Ns
        direction = @([Math]::Round([Math]::Cos($radians), 6), [Math]::Round([Math]::Sin($radians), 6), 0) }) }
}
Add-Trial 'stand' 'stand' ([ordered]@{ name = ''; duration_s = 62.0; body = $base.body })
foreach ($ns in @(30, 80)) {
    foreach ($angle in @(0, 90, 180, 270)) { Add-Trial 'push' ('push_{0:000}_{1:000}' -f $ns, $angle) (New-Blow 'spine_04' $ns $angle 10.0) }
}
for ($angle = 0; $angle -lt 360; $angle += 45) {
    Add-Trial 'blow' ('blow_{0:000}' -f $angle) (New-Blow 'spine_04' 200 $angle 10.0)
}
# Twice as many by hand: a shove from a side is stepped off and gives no fall.
for ($angle = 0.0; $angle -lt 360; $angle += 22.5) {
    Add-Trial 'hand' ('hand_{0:000}' -f $angle) ([ordered]@{ name = ''; duration_s = 10.0; body = $base.body; events = @(Get-ShoveEvents -Radians ($angle * [Math]::PI / 180.0)) })
}
foreach ($existing in @('getup_back', 'getup_front', 'getup_left', 'getup_right', 'strike_slap_head', 'strike_punch_torso', 'strike_punch_belly_steps', 'strike_punch_shoulder', 'strike_kick_thigh')) {
    $source = Read-JsonFile (Join-Path $RepoRoot "Scenarios\$existing.json")
    $copy = [ordered]@{}
    foreach ($property in $source.PSObject.Properties) { $copy[$property.Name] = $property.Value }
    $set = 'strike'
    if ($existing -like 'getup_*') { $set = 'getup' }
    Add-Trial $set "set_$existing" $copy
}

$started = Get-Date
$jobs = @()
foreach ($trial in $trials.Values) {
    $job = @{ File = $trial.file; Out = $trial.dir }
    if ($Tune) { $job.Tune = $Tune }
    $jobs += $job
}
Invoke-ScenarioBatch -Jobs $jobs -Parallel $Parallel
$runs = @{}
foreach ($trial in $trials.Values) {
    $run = Read-JsonFile (Join-Path $trial.dir 'metrics.json')
    if (-not $run -or -not $run.ok) { Write-Output "NATURAL ERROR: '$($trial.name)' gave no result (see $($trial.dir))"; exit 2 }
    $runs[$trial.name] = $run
}
$compliance = Join-Path $out 'compliance'
$arguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'compliance.ps1'), '-Out', $compliance)
if ($Tune) { $arguments += @('-Tune', $Tune) }
$null = & powershell @arguments
if ($LASTEXITCODE -ne 0) { Write-Output "NATURAL ERROR: Tools\compliance.ps1 gave no result (see $compliance)"; exit 2 }
$pushed = @((Read-JsonFile (Join-Path $compliance 'compliance.json')).results)
$standRuns = @()
$stands = @()
if (-not $Quick) {
    $seedLabel = "natural_$Label"
    $arguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'getup-seeds.ps1'), '-Label', $seedLabel)
    if ($Tune) { $arguments += @('-Tune', $Tune) }
    $null = & powershell @arguments
    foreach ($seedDir in @(Get-ChildItem (Join-Path $RepoRoot "Saved\Matrix\getup_seeds\$seedLabel") -Directory)) {
        $stand = Read-JsonFile (Join-Path $seedDir.FullName 'getup-stand.json')
        if (-not $stand) { Write-Output "NATURAL ERROR: the stand of $($seedDir.Name) gave no result"; exit 2 }
        $stands += $stand
        foreach ($fall in $stand.results) {
            $file = Join-Path $seedDir.FullName "runs\$($fall.name)\metrics.json"
            $standRuns += [pscustomobject]@{ kind = $fall.kind; attempts = $fall.attempts; successes = $fall.successes; run = (Read-JsonFile $file) }
        }
    }
    if ($standRuns.Count -eq 0 -or @($standRuns | Where-Object { -not $_.run }).Count -gt 0) { Write-Output 'NATURAL ERROR: the stand of falls gave no result, or not all of them'; exit 2 }
}
$seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds)

# --- Figures ---------------------------------------------------------------------------------------------------------

function Of([string]$Set) { return @($trials.Values | Where-Object { $_.set -eq $Set } | ForEach-Object { $runs[$_.name] }) }
function Values($List, [string]$Path) { return @($List | ForEach-Object { Get-JsonPath $_ $Path } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ }) }
# A list that holds nothing is nothing: an empty one, or the $null an empty one becomes on its way through a function.
function Known($List) { return @(@($List) | Where-Object { $null -ne $_ }) }
function Most($List) { $known = @(Known $List); if ($known.Count -eq 0) { return $null }; return [double]($known | Measure-Object -Maximum).Maximum }
function Least($List) { $known = @(Known $List); if ($known.Count -eq 0) { return $null }; return [double]($known | Measure-Object -Minimum).Minimum }
function Average($List) { $known = @(Known $List); if ($known.Count -eq 0) { return $null }; return [double]($known | Measure-Object -Average).Average }
function Middle($List) {
    $sorted = @(Known $List | Sort-Object)
    if ($sorted.Count -eq 0) { return $null }
    return ([double]$sorted[[int][Math]::Floor(($sorted.Count - 1) / 2)] + [double]$sorted[[int][Math]::Floor($sorted.Count / 2)]) / 2
}
function ShareAt($List, [double]$Share) {
    $sorted = @(Known $List | Sort-Object)
    if ($sorted.Count -eq 0) { return $null }
    return [double]$sorted[[Math]::Max([int][Math]::Ceiling($Share * $sorted.Count) - 1, 0)]
}
function Num($Value, [string]$Format = 'F2') { if ($null -eq $Value) { return '-' }; return ([double]$Value).ToString($Format, $culture) }

$rows = @()
# Judged: every figure listed has to lie between its two bounds ($null: no bound). Nothing listed: not judged. A figure
# that is not known leaves the row open, unless another is out.
function Row([string]$Id, [string]$What, [string]$Text, [string]$People, [string]$Limit, $Judged = @(), $Figures = @{}) {
    $verdict = '-'
    if (@($Judged).Count -gt 0) {
        $verdict = 'in'
        foreach ($each in $Judged) {
            $value = $each[0]
            if ($null -eq $value) { if ($verdict -eq 'in') { $verdict = '-' }; continue }
            if (($null -ne $each[1] -and [double]$value -lt [double]$each[1] - 1e-9) -or ($null -ne $each[2] -and [double]$value -gt [double]$each[2] + 1e-9)) { $verdict = 'out' }
        }
    }
    $script:rows += [pscustomobject][ordered]@{ id = $Id; what = $What; value = $Text; people = $People; limit = $Limit; verdict = $verdict; figures = $Figures }
}

$stand = $runs['stand']
$upright = @(Of 'push') + @(Of 'strike') + @($stand)
$massKg = [double](@($stand.body.bodies | ForEach-Object { $_.mass_kg }) | Measure-Object -Sum).Sum
$weightN = $massKg * 9.8
# The ankle's height is taken as 8 cm: the load stiffness is the weight times the centre of mass's height over it.
$comHeightM = [double]$stand.energy.final_potential_j / $weightN - 0.08
$loadStiffness = $weightN * $comHeightM

# By state, over the stand, the pushes and the blows; the torque in N*m (its share is of weight times the standing height of the centre of mass).
$standAssist = [double]$stand.control.assist_load.force_share_mean
$standNm = [double]$stand.control.assist_load.torque_share_mean * $weightN * ($comHeightM + 0.08)
$hitRuns = @(Of 'push') + @(Of 'blow')
$flinchForce = Most (Values $hitRuns 'control.assist_by_state.flinch.force_share_max')
$staggerForce = Most (Values $hitRuns 'control.assist_by_state.stagger.force_share_max')
$staggerNm = Most (Values $hitRuns 'control.assist_by_state.stagger.torque_share_max')
if ($null -ne $staggerNm) { $staggerNm *= $weightN * ($comHeightM + 0.08) }
Row 'C1' 'The world pulls on an upright body with' "standing $(Num $standAssist 'F4') of body weight and $(Num $standNm 'F1') N*m on average; at most $(Num $flinchForce 'F3') of body weight in a flinch, $(Num $staggerForce 'F3') and $(Num $staggerNm 'F0') N*m (pelvis, chest and head added up) in a stagger" 'nothing' '0' @(@($standAssist, $null, 0), @($standNm, $null, 0), @($flinchForce, $null, 0), @($staggerForce, $null, 0), @($staggerNm, $null, 0)) @{ standing_force_share = $standAssist; standing_torque_nm = $standNm; flinch_force_share = $flinchForce; stagger_force_share = $staggerForce; stagger_torque_nm = $staggerNm }

# Of the runs in which the body got up at all.
$getupFrom = @(Of 'getup')
if ($standRuns.Count -gt 0) { $getupFrom = @($standRuns | ForEach-Object { $_.run }) }
$getupShare = Average (Values @($getupFrom | Where-Object { [double]$_.control.get_up.attempts -gt 0 }) 'control.get_up.assist_force_share_mean')
Row 'C2' 'The world pulls on a body getting up with' "$(Num $getupShare) of body weight" 'nothing' '0.10 or less' @(, @($getupShare, $null, 0.10)) @{ assist_share = $getupShare }

$tuning = $stand.control.tuning
$legs = [double]$tuning.'muscles.legs.stiffness_nm_per_rad'
$ankleRatio = 2.0 * $legs / $loadStiffness
Row 'C3' 'Ankle stiffness of both legs over m g h' "$(Num $ankleRatio) ($(Num $legs 'F0') N*m/rad a leg, m g h $(Num $loadStiffness 'F0'))" '0.64 to 0.91' '0.6 to 1.0' @(, @($ankleRatio, 0.6, 1.0)) @{ ratio = $ankleRatio }

$wrist = [double]$tuning.'muscles.arms.stiffness_nm_per_rad'
$neck = [double]$tuning.'muscles.neck.stiffness_nm_per_rad'
Row 'C4' 'Stiffness of the relaxed wrist and neck' "$(Num $wrist 'F0') and $(Num $neck 'F0') N*m/rad" '3 to 4 and 24' '10 or less; 20 to 30' @(@($wrist, $null, 10), @($neck, 20, 30)) @{ wrist = $wrist; neck = $neck }

# Published peak torques of the stronger direction, men (docs\natural-body.md).
$peaks = [ordered]@{ foot_l = @(112, 112); thigh_l = @(178, 178); calf_l = @(247, 247); upperarm_l = @(61, 83); lowerarm_l = @(51, 51); hand_l = @(25, 25); neck_01 = @(52, 52); spine_02 = @(236, 465) }
$apart = @()
foreach ($joint in $peaks.Keys) {
    $cap = Get-JsonPath $stand "control.effort.joints.$joint.cap_nm"
    if ($null -ne $cap -and ([double]$cap -gt 1.2 * $peaks[$joint][1] -or [double]$cap -lt 0.8 * $peaks[$joint][0])) {
        $people = "$($peaks[$joint][0])"
        if ($peaks[$joint][0] -ne $peaks[$joint][1]) { $people += " to $($peaks[$joint][1])" }
        $apart += "$($joint.Replace('_l', '').Replace('_01', '').Replace('_02', '')) $(Num $cap 'F0') against $people"
    }
}
Row 'C5' 'Torque caps more than a fifth from a man''s' "$($apart.Count) of $($peaks.Count): $($apart -join ', ')" 'the table' '0' @(, @($apart.Count, $null, 0)) @{ joints_apart = $apart.Count }

$chest = @($pushed | Where-Object { $_.body -eq 'spine_04' -and $_.way -eq 'front' -and $_.force_n -eq 20 })[0]
$forearm = @($pushed | Where-Object { $_.body -eq 'lowerarm_l' -and $_.way -eq 'front' -and $_.force_n -eq 20 })[0]
Row 'C6' 'The same push on a relaxed and a braced body' "no tone yet; 20 N from the front moves the chest $(Num $chest.peak_along_cm) cm ($(Num $chest.peak_stiffness_n_per_cm 'F1') N/cm) and the forearm $(Num $forearm.peak_along_cm) cm ($(Num $forearm.peak_stiffness_n_per_cm 'F1') N/cm)" 'the wrist 5 to 20 times' '2 or more' @() @{ chest_n_per_cm = $chest.peak_stiffness_n_per_cm; forearm_n_per_cm = $forearm.peak_stiffness_n_per_cm }

$capRuns = @($upright | Where-Object { $_.control.end_state -eq 'standing' })
$capWorst = Most (Values $capRuns 'control.effort.at_cap_joint_share_max')
$capRun = @($capRuns | Sort-Object { -[double]$_.control.effort.at_cap_joint_share_max })[0]
Row 'C7' 'Upright time of a muscle at its cap' "$(Num (100 * $capWorst) 'F1')% at most ($($capRun.control.effort.at_cap_joint) in $($capRun.scenario)); $(Num (100 * [double]$stand.control.effort.at_cap_joint_share_max) 'F1')% standing" '-' '5% or less' @(, @($capWorst, $null, 0.05)) @{ share = $capWorst }

$hits = @()
foreach ($run in (Of 'push')) { foreach ($reaction in @($run.control.reactions)[0..0]) { if ($reaction) { $hits += [pscustomobject]@{ run = $run.scenario; r = $reaction } } } }
$targets = @($hits | ForEach-Object { $_.r.first_target_change_s } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$strengths = @($hits | ForEach-Object { $_.r.first_strength_change_s } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
$lifts = @($hits | ForEach-Object { $_.r.step_lift_s } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
# A step that lifted and has not landed 1.5 s after the hit is as late as that.
$lands = @($hits | Where-Object { $null -ne $_.r.step_lift_s } | ForEach-Object { if ($null -ne $_.r.step_land_s) { [double]$_.r.step_land_s } else { 1.5 } })
Row 'C8' 'Mechanics come first' "the strength changes $(Num (Least $strengths) 'F3') s and a target $(Num (Least $targets) 'F3') s after the hit is felt" 'trunk 106 ms, head 142 ms' 'react_passive_first' @() @{ first_change_s = (Least $strengths) }
Row 'C9' 'The reflex after a hit that was not seen' "$(Num (Middle $targets) 'F3') s ($(Num (Least $targets) 'F3') to $(Num (Most $targets) 'F3'), $($targets.Count) hits)" '0.07 to 0.11 s' '0.07 to 0.11 s' @(@((Least $targets), 0.07, 0.11), @((Most $targets), 0.07, 0.11)) @{ median_s = (Middle $targets) }
Row 'C10' 'The recovery step' "lifts at $(Num (Least $lifts) 'F3') to $(Num (Most $lifts) 'F3') s, down by $(Num (Most $lands) 'F3') s ($($lifts.Count) of $($hits.Count) pushes stepped)" 'lift 0.24 to 0.33, down 0.44 to 0.58' 'lift 0.20 or later, down by 0.58' @(@((Least $lifts), 0.20, $null), @((Most $lands), $null, 0.58)) @{ first_lift_s = (Least $lifts); last_land_s = (Most $lands) }
Row 'C11' 'A blow that is seen is met' 'no perception yet' 'moves 0.6 as far; 65 to 145 ms before' '0.7 or less; 50 ms' @()
Row 'C12' 'A gesture is not taken for a blow' 'no perception yet' '-' '0' @()
Row 'C13' 'The body looks' 'no gaze yet' '-' '15 degrees; more than half' @()

# Falls: "forward" and "sideways" by the way the fall went (the centre of mass, 90 is the way the body faces).
function Get-FallWay($run) {
    $angle = [double]$run.control.fall.direction_deg
    if ($angle -gt 225 -and $angle -lt 315) { return 'back' }
    if ($angle -gt 45 -and $angle -lt 135) { return 'forward' }
    return 'side'
}
$byHand = @(Of 'hand' | Where-Object { $null -ne $_.control.fall.started_t })
$byBlow = @(Of 'blow' | Where-Object { $null -ne $_.control.fall.started_t })
$shareOf = { param($list, $field) if (@($list).Count -eq 0) { return $null }; return @(@($list) | Where-Object { (Get-JsonPath $_ "control.fall.$field") -eq $true }).Count / @($list).Count }
$handNotBack = @($byHand | Where-Object { (Get-FallWay $_) -ne 'back' })
$blowNotBack = @($byBlow | Where-Object { (Get-FallWay $_) -ne 'back' })
$c14 = & $shareOf $handNotBack 'hands_ahead_first'
$c14blow = & $shareOf $blowNotBack 'hands_ahead_first'
$c14any = & $shareOf $handNotBack 'hands_first'
$c14anyBlow = & $shareOf $blowNotBack 'hands_first'
Row 'C14' 'A hand or forearm meets the floor ahead of the chest, before the trunk' "by hand $(Num (100 * $c14) 'F0')% of $($handNotBack.Count) falls forward and sideways ($(Num (100 * $c14any) 'F0')% wherever it lands); by blows of 200 N*s $(Num (100 * $c14blow) 'F0')% of $($blowNotBack.Count) ($(Num (100 * $c14anyBlow) 'F0')%)" '98% (sideways)' '80% or more' @(, @($c14, 0.8, $null)) @{ by_hand = $c14; by_blow = $c14blow; anywhere_by_hand = $c14any; anywhere_by_blow = $c14anyBlow }
$c15 = & $shareOf $byHand 'head_struck'
$c15blow = & $shareOf $byBlow 'head_struck'
$c15touch = & $shareOf $byHand 'head_touched'
$c15touchBlow = & $shareOf $byBlow 'head_touched'
# The owner's rule (25 November 2026): a head that strikes, not one that is laid down; the share that touches stands beside it.
Row 'C15' 'The head strikes the floor (0.5 m/s or more)' "by hand $(Num (100 * $c15) 'F0')% of $($byHand.Count) falls ($(Num (100 * $c15touch) 'F0')% touch it at all); by blows $(Num (100 * $c15blow) 'F0')% of $($byBlow.Count) ($(Num (100 * $c15touchBlow) 'F0')%)" '11% (sideways)' '15% or less' @(, @($c15, $null, 0.15)) @{ by_hand = $c15; by_blow = $c15blow; touched_by_hand = $c15touch; touched_by_blow = $c15touchBlow }
$headHand = @(Values $byHand 'control.fall.head_impact_speed_mps')
$headBlow = @(Values $byBlow 'control.fall.head_impact_speed_mps')
# Falls in which no head met the floor at all are the aim, not a figure that is missing.
$headHand90 = ShareAt $headHand 0.9
if ($null -eq $headHand90 -and $byHand.Count -gt 0) { $headHand90 = 0.0 }
$headBlow90 = ShareAt $headBlow 0.9
if ($null -eq $headBlow90 -and $byBlow.Count -gt 0) { $headBlow90 = 0.0 }
Row 'C16' 'How fast the head goes down at the floor' "by hand $(Num $headHand90) m/s (90th percentile of $($headHand.Count) that met it); by blows $(Num $headBlow90) m/s (of $($headBlow.Count))" '1.7 soft, 3.2 hard' '2.0 by hand; under 3.2 by blows' @(@($headHand90, $null, 2.0), @($headBlow90, $null, 3.2)) @{ by_hand_mps = $headHand90; by_blow_mps = $headBlow90 }
$restS = Most (Values $byBlow 'control.fall.rest_after_s')
# A body that never came to rest within its run has no figure: that is not a short one.
$restless = @($byBlow | Where-Object { $null -eq $_.control.fall.rest_after_s }).Count
$gainJ = Most (Values $byBlow 'energy.max_gain_j')
$sinkCm = Most (Values $byBlow 'penetration.max_world_cm')
Row 'C17' 'A fall still ends as M3 asked' "at rest after $(Num $restS) s at most ($restless never), $(Num $gainJ 'F1') J gained, $(Num $sinkCm) cm into the floor ($($byBlow.Count) blows)" '-' '3 s, 40 J, 2 cm' @(@($restless, $null, 0), @($restS, $null, 3), @($gainJ, $null, 40), @($sinkCm, $null, 2)) @{ rest_s = $restS; gain_j = $gainJ; sink_cm = $sinkCm }

$lyingStill = @(Values ($byBlow + $byHand) 'metrics.stillness.longest_still_by_state_s.lying')
Row 'C18' 'A lying body is still for' "$(Num (Most $lyingStill) 'F1') s at most, $(Num (Middle $lyingStill) 'F1') s in the median of $($lyingStill.Count) falls (runs of 10 s)" 'breathe 12 to 20 a minute' '1.0 s or less' @(, @((Most $lyingStill), $null, 1.0)) @{ most_s = (Most $lyingStill) }

$getupRuns = @(Of 'getup')
# From the first move of the first attempt to standing for good, whatever lay between.
function Get-GetUpTime($run) {
    $states = @($run.control.states)
    $first = @($states | Where-Object { $_.state -eq 'get_up' })[0]
    $last = $states[-1]
    if (-not $first -or $last.state -ne 'standing' -or [double]$last.t -le [double]$first.t) { return $null }
    $stoodAt = @($states | Where-Object { $_.state -eq 'standing' -and [double]$_.t -gt [double]$first.t })[0]
    return [double]$stoodAt.t - [double]$first.t
}
$floorTimes = @(Known @($getupRuns | ForEach-Object { Get-GetUpTime $_ }))
$attempts = [double](@(Values $getupRuns 'control.get_up.attempts') | Measure-Object -Sum).Sum
$successes = [double](@(Values $getupRuns 'control.get_up.successes') | Measure-Object -Sum).Sum
$poseHolds = @(Values $getupRuns 'metrics.stillness.target_still_longest_by_state_s.get_up')
$bodyHolds = @(Values $getupRuns 'metrics.stillness.longest_still_by_state_s.get_up')
$from = 'four get-ups'
if ($standRuns.Count -gt 0) {
    $attempts = [double](@($standRuns | ForEach-Object { $_.attempts }) | Measure-Object -Sum).Sum
    $successes = [double](@($standRuns | ForEach-Object { $_.successes }) | Measure-Object -Sum).Sum
    $poseHolds = @($standRuns | ForEach-Object { Get-JsonPath $_.run 'metrics.stillness.target_still_longest_by_state_s.get_up' } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    $bodyHolds = @($standRuns | ForEach-Object { Get-JsonPath $_.run 'metrics.stillness.longest_still_by_state_s.get_up' } | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    $floorTimes = @(Known @($standRuns | Where-Object { $_.kind -eq 'floor' } | ForEach-Object { Get-GetUpTime $_.run }))
    $from = "$($standRuns.Count) falls on $($stands.Count) seeds"
}
$givenUp = $null
if ($attempts -gt 0) { $givenUp = 1.0 - $successes / $attempts }
Row 'C19' 'A get-up holds a pose' "asks for one pose for $(Num (Most $poseHolds) 'F1') s at most, the body is still for $(Num (Most $bodyHolds) 'F1') s; $(Num (100 * $givenUp) 'F0')% of $(Num $attempts 'F0') attempts given up ($from)" '-' '1.0 s; 10% or less' @(@((Most $poseHolds), $null, 1.0), @((Most $bodyHolds), $null, 1.0), @($givenUp, $null, 0.10)) @{ pose_held_s = (Most $poseHolds); body_still_s = (Most $bodyHolds); given_up = $givenUp }
if ($stands.Count -gt 0) {
    $first = @($stands | ForEach-Object { @($_.results | Where-Object { $_.end_state -eq 'standing' -and $_.attempts -eq 1 }).Count })
    $stood = @($stands | ForEach-Object { [double]$_.stand / [double]$_.falls })
    Row 'C20' 'Falls that end standing, and at the first attempt' "$(Num (100 * (Least $stood)) 'F0') to $(Num (100 * (Most $stood)) 'F0')%; $($first -join ', ') of 50" '-' '95%; 45 or more' @(@((Least $stood), 0.95, $null), @((Least $first), 45, $null)) @{ stood_least = (Least $stood); first_least = (Least $first) }
} else {
    Row 'C20' 'Falls that end standing, and at the first attempt' 'not run (-Quick)' '-' '95%; 45 or more' @()
}
Row 'C21' 'From the first move to standing, bare floor' "$(Num (Middle $floorTimes) 'F1') s in the median of $($floorTimes.Count) ($(Num (Least $floorTimes) 'F1') to $(Num (Most $floorTimes) 'F1'))" '2.6 to 2.9 young; 4.2 at 20 to 50' '2.6 to 4.2 s' @(, @((Middle $floorTimes), 2.6, 4.2)) @{ median_s = (Middle $floorTimes) }

$sway = $stand.metrics.sway
$copMmS = 10 * [double]$sway.cop_speed_mean_cmps
$copMl = 10 * [double]$sway.cop_rms_ml_cm
$periodText = 'no period'
if ($null -ne $sway.period_s) { $periodText = "period $(Num $sway.period_s) s (correlation $(Num $sway.period_correlation))" }
$periodCount = 0
if ($null -ne $sway.period_s) { $periodCount = 1 }
Row 'C22' 'Sway of the stance' "centre of pressure $(Num $copMmS 'F1') mm/s, side to side $(Num $copMl 'F1') mm RMS (fore and aft $(Num (10 * [double]$sway.cop_rms_ap_cm) 'F1')); $periodText" '9.2 +- 1.6 mm/s; 4.0 +- 1.3 mm' '6 to 12 mm/s; 2 to 6 mm; no period' @(@($copMmS, 6, 12), @($copMl, 2, 6), @($periodCount, $null, 0)) @{ cop_mmps = $copMmS; cop_ml_mm = $copMl; period_s = $sway.period_s }
Row 'C23' 'No two answers are the same' 'identical by design (no variation yet)' 'a quarter get up alike' '2 cm apart after 2 s' @()
Row 'C24' 'The outcome tables hold on three seeds' 'no variation yet' '-' 'all pass' @()

# From the latest run of the suite, which has to be of the build that is there: its figures are not of this set, nor of -Tune.
$suite = Read-JsonFile (Join-Path $ToolsOut 'suite.json')
$jumps = @()
if ($suite) {
    foreach ($result in @($suite.results | Where-Object { $_.scenario -notlike 'measure_*' -and $_.scenario -notlike 'tune_*' })) {
        $run = Read-JsonFile (Join-Path $RepoRoot "Saved\Scenarios\$($result.scenario)\metrics.json")
        if ($run -and $run.control.mode -eq 'active') {
            $jumps += [pscustomobject]@{ scenario = $result.scenario; torque = [double]$run.control.pose.command_step_max_nm; torque_joint = $run.control.pose.command_step_joint; torque_t = $run.control.pose.command_step_t; turn = [double]$run.control.pose.target_step_max_deg; turn_joint = $run.control.pose.target_step_joint; turn_t = $run.control.pose.target_step_t
                strength = [double]$run.control.pose.strength_step_max; strength_joint = $run.control.pose.strength_step_joint; strength_t = $run.control.pose.strength_step_t }
        }
    }
}
$turns = @($jumps | Sort-Object { -$_.turn })
$drops = @($jumps | Sort-Object { -$_.strength })
$torques = @($jumps | Sort-Object { -$_.torque })
if ($turns.Count -gt 0) {
    Row 'C25' 'Largest jump in a frame: of the torque a command asks for, of a target, of a strength' "$(Num $torques[0].torque 'F0') N*m ($($torques[0].torque_joint), $($torques[0].scenario) at $(Num $torques[0].torque_t) s); $(Num $turns[0].turn 'F1') degrees ($($turns[0].turn_joint), $($turns[0].scenario) at $(Num $turns[0].turn_t) s); strength by $(Num (100 * $drops[0].strength) 'F0')% ($($drops[0].strength_joint), $($drops[0].scenario)); $($turns.Count) scenarios" '-' 'today''s, less what is a defect' @() @{ torque_nm = $torques[0].torque; target_deg = $turns[0].turn; strength = $drops[0].strength }
}
Row 'C26' 'Hit, pushed and held in every new phase' 'the get-up has its three scenarios' '-' 'each new phase its three' @()

# --- Beside the criteria: the pose's likeness, the time beyond the joints' ranges, the head -------------------------------------

$beside = @()
foreach ($set in @('stand', 'push', 'strike', 'blow', 'hand', 'getup')) {
    $list = @(Of $set)
    $beside += [pscustomobject][ordered]@{
        set = $set; runs = $list.Count
        pose_mean_deg = $(if ($NoPrior) { $null } else { Average (Values $list 'metrics.pose_prior.mean_deg') })
        pose_p95_deg = $(if ($NoPrior) { $null } else { Most (Values $list 'metrics.pose_prior.p95_deg') })
        pose_worst_deg = $(if ($NoPrior) { $null } else { Most (Values $list 'metrics.pose_prior.worst_deg') })
        pose_worst_in = $(if ($NoPrior) { '' } else { $worstRun = @($list | Sort-Object { -[double]$_.metrics.pose_prior.worst_deg })[0]; "$($worstRun.scenario) at $(Num $worstRun.metrics.pose_prior.worst_t) s, $($worstRun.metrics.pose_prior.worst_state)" })
        beyond_range_share = Average (Values $list 'metrics.limit_excess_share')
        head_accel_max_mps2 = Most (Values $list 'metrics.head_accel_max_mps2')
        head_jerk_rms_mps3 = Most (Values $list 'metrics.head_jerk_rms_mps3')
        power_peak_w = Most (Values $list 'control.effort.power_peak_w')
    }
}

# --- The report ------------------------------------------------------------------------------------------------------

if ($old) {
    if ($old.prior_stamp -and -not $NoPrior -and $old.prior_stamp -ne $stand.metrics.pose_prior.stamp) { Write-Output "NATURAL WARNING: '$Baseline' used another pose database ($($old.prior_stamp)): the pose figures of the two are not to be compared" }
}
$in = @($rows | Where-Object { $_.verdict -eq 'in' }).Count
$outside = @($rows | Where-Object { $_.verdict -eq 'out' }).Count
Write-Output "NATURAL OK: $($rows.Count) criteria in $seconds s; $in within the proposed limit, $outside outside, $($rows.Count - $in - $outside) not measured yet or not judged"
foreach ($row in $rows) {
    Write-Output ('  {0,-4} {1,-4} {2}: {3}' -f $row.id, $row.verdict, $row.what, $row.value)
    Write-Output ('            people: {0}; limit (proposed): {1}' -f $row.people, $row.limit)
    if ($old) {
        $was = @($old.criteria | Where-Object { $_.id -eq $row.id })[0]
        if ($was -and $was.value -ne $row.value) { Write-Output ('            was ({0}): {1}' -f $was.verdict, $was.value) }
    }
}
Write-Output '  beside the criteria (pose: distance to the nearest recorded pose, degrees; a figure to compare, not to meet)'
Write-Output ('  {0,-7} {1,4} {2,9} {3,8} {4,8} {5,13} {6,11} {7,11} {8,9}  {9}' -f 'set', 'runs', 'pose mean', 'p95', 'worst', 'beyond range', 'head m/s2', 'jerk m/s3', 'power W', 'worst pose in')
foreach ($line in $beside) {
    Write-Output ('  {0,-7} {1,4} {2,9} {3,8} {4,8} {5,13} {6,11} {7,11} {8,9}  {9}' -f $line.set, $line.runs, (Num $line.pose_mean_deg 'F1'), (Num $line.pose_p95_deg 'F1'), (Num $line.pose_worst_deg 'F1'),
        "$(Num (100 * $line.beyond_range_share) 'F2')%", (Num $line.head_accel_max_mps2 'F0'), (Num $line.head_jerk_rms_mps3 'F0'), (Num $line.power_peak_w 'F0'), $line.pose_worst_in)
}
$stampNow = $null
if (-not $NoPrior) { $stampNow = $stand.metrics.pose_prior.stamp }
$report = [ordered]@{ label = $Label; date = (Get-Date -Format 'yyyy-MM-dd HH:mm'); tune = $Tune; quick = [bool]$Quick; seconds = $seconds; prior_stamp = $stampNow
    criteria = $rows; beside = $beside; compliance = $pushed; largest_torque_jumps = @($torques | Select-Object -First 20); largest_target_jumps = @($turns | Select-Object -First 12); largest_strength_jumps = @($drops | Select-Object -First 12) }
$reportFile = Join-Path $out 'report.json'
Write-JsonFile $reportFile $report
Write-Output "  result: $reportFile"
exit 0
