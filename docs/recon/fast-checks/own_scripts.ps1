# The agent's own scripts in a set of transcripts (docs/fast-checks.md, SP-12): every .ps1 or .py file that a Write call put
# under a scratchpad or under Saved\Recon. Prints the Write calls and the distinct files. Reads the transcripts only.
#   powershell -ExecutionPolicy Bypass -File docs\recon\fast-checks\own_scripts.ps1 [-Ids "<id>,<id>"]
# On the ten sessions of 25 to 27 November 2026: 346 Write calls, 173 distinct files.
param(
    [string]$TranscriptDir = (Join-Path $env:USERPROFILE '.claude\projects\C--Users-ksk67-Git-dictator-sandbox'),
    [string]$Ids = '0d9db39e-f7b0-4a76-ad64-f1532c35882e,0feed44e-a04c-406a-9741-28c76bfdee66,4fda821e-9d08-4be6-93e9-b51c3c34cad4,565e74a5-af96-4a72-a87b-790260277845,d862bcd0-a6a0-4669-90a8-e908c39edc55,d9632c6c-132a-439a-8b28-7e4327dbcae6,d9b071ef-9f47-47a3-823f-2b9ebd146215,e3fa22c7-9008-4fed-b8c9-1aa12fe24a6c,e6a65f6c-4931-4bd0-9acf-d9fbab1f36d7,e8f97410-b161-4678-892a-577dacd58ade'
)
$pattern = '"name":"Write","input":\{"file_path":"([^"]*)"'
$paths = New-Object System.Collections.ArrayList
foreach ($id in $Ids.Split(',')) {
    $file = Join-Path $TranscriptDir ($id.Trim() + '.jsonl')
    if (-not (Test-Path $file)) { continue }
    foreach ($match in (Select-String -Path $file -Pattern $pattern -AllMatches)) {
        foreach ($m in $match.Matches) {
            $path = $m.Groups[1].Value.Replace('\\', '\')
            if ($path -match '(scratchpad|Saved[\\/]Recon)' -and $path -match '\.(ps1|py)$') { [void]$paths.Add($path) }
        }
    }
}
'Write calls of own scripts: {0}; distinct files: {1}' -f $paths.Count, @($paths | Sort-Object -Unique).Count
