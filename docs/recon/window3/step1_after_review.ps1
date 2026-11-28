# After the independent reading of the window (17:41): the suite at 0 against audit_before, and level 1 of getup.strict_release
# again (a hit judged by the flinch now taken when the attempt ends in the same tick), held run by run against the level-1 runs before.
# Every tool's output goes to a file first and is read from there (a pipe to Select-Object -First can stop the tool that feeds it).
#   powershell -ExecutionPolicy Bypass -File docs\recon\window3\step1_after_review.ps1
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Set-Location $repo
$log = Join-Path $repo 'Saved\Recon\window3'
function Invoke-ToFile([string]$Name, [string[]]$ToolArgs, [int]$Lines = 1) {
    $file = Join-Path $log "$Name.txt"
    & powershell -ExecutionPolicy Bypass -File @ToolArgs *> $file
    Get-Content $file | Select-Object -First $Lines
}
Invoke-ToFile 'after_review_suite0' @('Tools\suite.ps1', '-Baseline', 'audit_before') 1
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Suites\checks_w3s1_l1 -Name w3s1_l1_suite *> $null
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Matrix\getup_seeds\w3s1_hold1_l1 -Name w3s1_hold1_l1 *> $null
& powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -SaveRuns Saved\Matrix\getup_seeds\w3s1_near_l1 -Name w3s1_near_l1 *> $null
Invoke-ToFile 'after_review_suite1' @('Tools\suite.ps1', '-Tune', 'getup.strict_release=1', '-Label', 'w3s1_l1b') 1
Invoke-ToFile 'after_review_cmp_suite1' @('Tools\baseline.ps1', '-CompareRuns', 'Saved\Suites\w3s1_l1b', '-Name', 'w3s1_l1_suite') 8
Invoke-ToFile 'after_review_hold1' @('Tools\getup-seeds.ps1', '-Label', 'w3s1_hold1_l1b', '-HoldHands', '1', '-Tune', 'getup.strict_release=1') 1
Invoke-ToFile 'after_review_cmp_hold1' @('Tools\baseline.ps1', '-CompareRuns', 'Saved\Matrix\getup_seeds\w3s1_hold1_l1b', '-Name', 'w3s1_hold1_l1') 6
Invoke-ToFile 'after_review_near' @('Tools\getup-seeds.ps1', '-Label', 'w3s1_near_l1b', '-Near', '70', '-Tune', 'getup.strict_release=1') 1
Invoke-ToFile 'after_review_cmp_near' @('Tools\baseline.ps1', '-CompareRuns', 'Saved\Matrix\getup_seeds\w3s1_near_l1b', '-Name', 'w3s1_near_l1') 6
'AFTER REVIEW DONE'
