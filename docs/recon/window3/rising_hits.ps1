# Decision 79 on more than two scenarios: protect_hit_rising_trunk's body (laid on its back, getting up) struck at five moments of the
# rise, on the head, the chest and a thigh, from behind and from the front, with 15, 50 and 120 N*s; each run at getup.strict_release 0
# and 1 through one sweep. Read: the head's speed down after the cover (C16: under 2.0 m/s by hand, under 3.2 for blows), the world's
# pull on the falling body (control.authority, Falling), whether the attempt was given up to cover.
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\rising_hits.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$out = Join-Path $repo 'Saved\Recon\window3\rising_hits'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
$base = Get-Content (Join-Path $repo 'Scenarios\protect_hit_rising_trunk.json') -Raw | ConvertFrom-Json
$count = 0
foreach ($t in @(3.3, 3.6, 3.9, 4.2, 4.5)) {
    foreach ($bodyName in @('head', 'spine_04', 'thigh_r')) {
        foreach ($ns in @(15, 50, 120)) {
            foreach ($way in @(@(1, 0, 0), @(-1, 0, 0))) {
                $s = $base | ConvertTo-Json -Depth 20 | ConvertFrom-Json
                $tag = '{0}_{1}_{2}_{3}' -f ([int]($t * 10)), $bodyName, $ns, ($(if ($way[0] -gt 0) { 'x' } else { 'mx' }))
                $s.name = "rising_hit_$tag"
                $s.events = @([ordered]@{ t = $t; type = 'impulse'; body = $bodyName; impulse_ns = $ns; direction = $way })
                $s.expect = [ordered]@{ max_joint_stretch_cm = 2.0 }
                $s | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $out "rising_hit_$tag.json") -Encoding utf8
                $count++
            }
        }
    }
}
"$count scenario files in $out"
& powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name w3_rising_hits -Files $out -Sets '|getup.strict_release=1' -Metrics 'control.end_state' *> (Join-Path $repo 'Saved\Recon\window3\rising_hits_sweep.txt')
Get-Content (Join-Path $repo 'Saved\Recon\window3\rising_hits_sweep.txt') | Select-Object -First 2
# The figures, run by run: every metrics.json under the sweep.
$rows = foreach ($m in Get-ChildItem (Join-Path $repo 'Saved\Sweeps\w3_rising_hits') -Recurse -Filter metrics.json) {
    $j = Get-Content $m.FullName -Raw | ConvertFrom-Json
    $cover = @($j.control.behaviour.covers) | Select-Object -First 1
    $fall = $j.control.authority.states.falling
    $lying = $j.control.authority.states.lying
    [pscustomobject]@{
        run = $m.Directory.Name
        set = $m.Directory.Parent.Name
        gave_up_protect = $j.control.get_up.given_up_by.protect
        covered = [bool]$cover
        head_down_mps = if ($cover) { [double]$cover.head_down_mps } else { $null }
        one_within_s = if ($cover) { [double]$cover.one_within_s } else { $null }
        falling_peak = if ($fall) { [double]$fall.peak } else { 0.0 }
        falling_mean = if ($fall) { [double]$fall.mean } else { 0.0 }
        lying_peak = if ($lying) { [double]$lying.peak } else { 0.0 }
        end_state = $j.control.end_state
    }
}
$rows | Export-Csv (Join-Path $repo 'Saved\Recon\window3\rising_hits.csv') -NoTypeInformation
"$(@($rows).Count) runs read"
