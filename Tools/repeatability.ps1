# Measures run-to-run deviation of one scenario: N headless runs, optionally one GPU-rendered run,
# each compared with the first headless run.
#   powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive [-Runs 3] [-IncludeRendered]
# Writes Saved\Scenarios\<name>\repeat\repeatability.json.
# Exit code: 0 every run identical to the first, 1 deviation found, 2 a run failed to produce results.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Runs = 3,
    [switch]$IncludeRendered
)
. "$PSScriptRoot\common.ps1"
# Each run in a process of its own, not in a pool's (Tools\pool.ps1): the deviation between processes is what is measured.
$env:SANDBOX_NO_POOL = '1'

$base = Join-Path $RepoRoot "Saved\Scenarios\$Name\repeat"
$scenarioScript = Join-Path $PSScriptRoot 'scenario.ps1'
$compareScript = Join-Path $PSScriptRoot 'compare-runs.ps1'

$dirs = @()
for ($i = 1; $i -le $Runs; $i++) {
    $dir = Join-Path $base "run$i"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $scenarioScript -Name $Name -Out $dir -Quiet | ForEach-Object { Write-Output "  run$i $_" }
    if ($LASTEXITCODE -eq 2) { Write-Output "REPEATABILITY ERROR: run $i did not produce results"; exit 2 }
    $dirs += $dir
}
if ($IncludeRendered) {
    $dir = Join-Path $base 'rendered'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $scenarioScript -Name $Name -Out $dir -Rendered -Quiet | ForEach-Object { Write-Output "  rendered $_" }
    if ($LASTEXITCODE -eq 2) { Write-Output "REPEATABILITY ERROR: the rendered run did not produce results"; exit 2 }
    $dirs += $dir
}

$comparisons = @()
$allIdentical = $true
for ($i = 1; $i -lt $dirs.Count; $i++) {
    $resultFile = Join-Path $dirs[$i] 'compare-to-run1.json'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $compareScript -A $dirs[0] -B $dirs[$i] -Result $resultFile -Quiet | Out-Null
    if ($LASTEXITCODE -eq 2) { Write-Output "REPEATABILITY ERROR: cannot compare $($dirs[$i]) with run1"; exit 2 }
    $comparison = Read-JsonFile $resultFile
    if (-not $comparison.identical) { $allIdentical = $false }
    $comparisons += [ordered]@{
        run                  = (Split-Path -Leaf $dirs[$i])
        rendered             = $comparison.b.rendered
        identical            = $comparison.identical
        body_hash_equal      = $comparison.body_hash_equal
        bone_hash_equal      = $comparison.bone_hash_equal
        max_body_pos_dev_cm  = $comparison.max_body_pos_dev_cm
        max_body_rot_dev_deg = $comparison.max_body_rot_dev_deg
        max_bone_pos_dev_cm  = $comparison.max_bone_pos_dev_cm
        max_bone_rot_dev_deg = $comparison.max_bone_rot_dev_deg
        first_divergence_t   = $comparison.first_divergence_t
    }
}

$reference = Read-JsonFile (Join-Path $dirs[0] 'metrics.json')
Write-JsonFile (Join-Path $base 'repeatability.json') ([ordered]@{
    scenario      = $Name
    headless_runs = $Runs
    rendered_run  = [bool]$IncludeRendered
    all_identical = $allIdentical
    reference     = [ordered]@{ bodies = $reference.hashes.bodies; bones = $reference.hashes.bones; engine_version = $reference.engine_version }
    comparisons   = $comparisons
})

if ($allIdentical) {
    Write-Output "REPEATABILITY IDENTICAL: $Name, $($dirs.Count) runs, hashes bodies=$($reference.hashes.bodies) bones=$($reference.hashes.bones)"
    exit 0
}
Write-Output "REPEATABILITY DEVIATION: $Name"
foreach ($comparison in $comparisons) {
    Write-Output ("  {0}: identical={1} bodies {2:G4} cm / {3:G4} deg, bones {4:G4} cm / {5:G4} deg, first divergence t={6}" -f `
        $comparison.run, $comparison.identical, $comparison.max_body_pos_dev_cm, $comparison.max_body_rot_dev_deg, $comparison.max_bone_pos_dev_cm, $comparison.max_bone_rot_dev_deg, $comparison.first_divergence_t)
}
exit 1
