# R3's videos for the owner: pairs of films (switch 0 against 1, the same camera and times), every film of every pair through one
# call of Tools\film.ps1 (one queue), then each pair put side by side as Tools\film-pair.ps1 does it.
#   powershell -ExecutionPolicy Bypass -File Saved\Recon\r3\pairs.ps1 -List Saved\Recon\r3\pairs.json
# The list: a JSON array of { tag, file, around, span, camera, tune_a, tune_b, label_a, label_b }.
param([Parameter(Mandatory = $true)][string]$List, [int]$Parallel = 3, [double]$Every = 0.033)
. "$PSScriptRoot\..\..\..\Tools\common.ps1"
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg) { Write-Output 'PAIRS ERROR: ffmpeg is not installed'; exit 1 }
# (Windows PowerShell hands a JSON array on as one object: through a variable it is the array.)
$raw = Get-Content $List -Raw | ConvertFrom-Json
$pairs = @($raw)
$films = @()
foreach ($pair in $pairs) {
    $outDir = Join-Path $RepoRoot "Saved\Films\$($pair.tag)"
    New-Item -ItemType Directory -Force $outDir | Out-Null
    # The scenario without its early end, so that both sides have every picture.
    $scenario = Read-JsonFile ([System.IO.Path]::GetFullPath($pair.file))
    if ($scenario.PSObject.Properties['end_when_standing_s']) { $scenario.PSObject.Properties.Remove('end_when_standing_s') }
    $source = Join-Path $outDir 'scenario.json'
    [System.IO.File]::WriteAllText($source, ($scenario | ConvertTo-Json -Depth 12))
    foreach ($side in @('a', 'b')) {
        $tag = "$($pair.tag)_$side"
        $video = Join-Path $RepoRoot "Saved\Films\$tag\film.mp4"
        if (Test-Path $video) { Remove-Item $video -Force }
        $entry = [ordered]@{ file = $source; tag = $tag; around = $pair.around; span = $pair.span; every = $Every; camera = $pair.camera; video = $true }
        $tune = $pair."tune_$side"
        if ($tune) { $entry.tune = $tune }
        $films += $entry
    }
}
$listFile = Join-Path $RepoRoot 'Saved\Recon\r3\pairs_films.json'
[System.IO.File]::WriteAllText($listFile, (ConvertTo-Json @($films) -Depth 6))
$printed = @(& powershell -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'Tools\film.ps1') -Films $listFile -Parallel $Parallel)
$printed | Select-Object -First 1 | ForEach-Object { Write-Output $_ }
$font = 'C\:/Windows/Fonts/consola.ttf'
$clean = { param($text) ($text -replace "[:'\\%`"]", ' ') }
$draw = "drawtext=fontfile='$font':x=14:y=14:fontsize=30:fontcolor=yellow:box=1:boxcolor=black@0.6:boxborderw=6"
$made = 0
foreach ($pair in $pairs) {
    $a = Join-Path $RepoRoot "Saved\Films\$($pair.tag)_a\film.mp4"
    $b = Join-Path $RepoRoot "Saved\Films\$($pair.tag)_b\film.mp4"
    if (-not (Test-Path $a) -or -not (Test-Path $b)) { Write-Output "PAIRS: $($pair.tag) has no video on a side"; continue }
    $filter = "[0:v]$draw`:text='$(& $clean $pair.label_a)'[a];[1:v]$draw`:text='$(& $clean $pair.label_b)'[b];[a][b]hstack=inputs=2"
    $out = Join-Path $RepoRoot "Saved\Films\$($pair.tag)\pair.mp4"
    if (Test-Path $out) { Remove-Item $out -Force }
    $null = Invoke-Process -FilePath $ffmpeg.Source -TimeoutSec 300 -ArgumentString "-y -loglevel error -i `"$a`" -i `"$b`" -filter_complex `"$filter`" -pix_fmt yuv420p `"$out`""
    if (Test-Path $out) { $made++; Write-Output "PAIR OK: $out" } else { Write-Output "PAIRS: ffmpeg made no pair for $($pair.tag)" }
}
Write-Output "PAIRS DONE: $made of $($pairs.Count)"
