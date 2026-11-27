# The count of docs/fast-checks.md, sections 1 and 2, over the ten agent sessions of 25 to 27 November 2026: the time split
# (time_split.ps1) and every call that built or started the engine (runs.ps1, then runs_check.ps1). Reads the transcripts only.
#   powershell -ExecutionPolicy Bypass -File docs\recon\fast-checks\ten_sessions.ps1 [-Out <dir>] [-Ids "<id>,<id>"]
# -TranscriptDir is where Claude Code keeps the project's transcripts (<session id>.jsonl). Writes <Out>\runs.csv.
param(
    [string]$TranscriptDir = (Join-Path $env:USERPROFILE '.claude\projects\C--Users-ksk67-Git-dictator-sandbox'),
    [string]$Ids = '0d9db39e-f7b0-4a76-ad64-f1532c35882e,0feed44e-a04c-406a-9741-28c76bfdee66,4fda821e-9d08-4be6-93e9-b51c3c34cad4,565e74a5-af96-4a72-a87b-790260277845,d862bcd0-a6a0-4669-90a8-e908c39edc55,d9632c6c-132a-439a-8b28-7e4327dbcae6,d9b071ef-9f47-47a3-823f-2b9ebd146215,e3fa22c7-9008-4fed-b8c9-1aa12fe24a6c,e6a65f6c-4931-4bd0-9acf-d9fbab1f36d7,e8f97410-b161-4678-892a-577dacd58ade',
    [string]$Out = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'Saved\FastChecks')
)
$files = @($Ids.Split(',') | ForEach-Object { Join-Path $TranscriptDir ($_.Trim() + '.jsonl') } | Where-Object { Test-Path $_ })
New-Item -ItemType Directory -Force $Out | Out-Null
"sessions found: $($files.Count)"
'=== time_split (model, tools, waits on background work, waits for the owner)'
& (Join-Path $PSScriptRoot 'time_split.ps1') -Files $files
'=== calls that built or started the engine'
$csv = Join-Path $Out 'runs.csv'
& (Join-Path $PSScriptRoot 'runs.ps1') -Files $files -OutCsv $csv
& (Join-Path $PSScriptRoot 'runs_check.ps1') -Csv $csv
