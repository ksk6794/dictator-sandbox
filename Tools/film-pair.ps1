# Two videos of one scenario side by side, the same camera and the same times: the body as the files have it (or with -TuneA) on
# the left, with -TuneB on the right. What docs/active-ragdoll.md, section 9, asks of every part: its video beside the one before.
#   powershell -ExecutionPolicy Bypass -File Tools\film-pair.ps1 -Name getup_push_weak -Around 8.3 -Span 3 -TuneB "getup.hit_by_impulse=1" -LabelA "switch 0" -LabelB "switch 1"
#   ... -File <path.json>            a scenario file from anywhere instead of Scenarios\<Name>.json
#   ... -Around 8.3 -Span 3 -Every 0.033   the times, as Tools\film.ps1 takes them (a picture every 0.033 s is real time at 30 per second)
#   ... -Camera side|front|back|quarter|"x,y,z;x,y,z"   as Tools\film.ps1
#   ... -TuneA, -TuneB "name=value,..."      tuning values of each side (none: the files as they are)
#   ... -TuningDirA, -TuningDirB <dir>       a directory of tuning files for a side (for what -Tune cannot give)
#   ... -LabelA, -LabelB             written on each side's frames
#   ... -Overlay all                 debug overlays on both
#   ... -Tag <name>                  output name (default <scenario>_pair): Saved\Films\<tag>\pair.mp4, and each side's own film in
#                                    Saved\Films\<tag>_a and <tag>_b
# The scenario runs to its duration whatever its end_when_standing_s says, so that both sides have every picture. Exit code: 0 the
# pair was written, 1 not.
param(
    [string]$Name,
    [string]$File,
    [string]$Tag,
    [double]$Around = 3.0,
    [double]$Span = 3.0,
    [double]$Every = 0.033,
    [string]$Camera = 'side',
    [string]$TuneA,
    [string]$TuneB,
    [string]$TuningDirA,
    [string]$TuningDirB,
    [string]$LabelA = 'A',
    [string]$LabelB = 'B',
    [string]$Overlay,
    [double]$Duration = 0
)
. "$PSScriptRoot\common.ps1"

if ($File -and -not $Name) { $Name = [System.IO.Path]::GetFileNameWithoutExtension($File) }
if (-not $Name) { Write-Output 'FILM PAIR ERROR: give -Name <scenario> or -File <scenario file>'; exit 1 }
if (-not $Tag) { $Tag = "${Name}_pair" }
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg) { Write-Output 'FILM PAIR ERROR: ffmpeg is not installed'; exit 1 }
$outDir = Join-Path $RepoRoot "Saved\Films\$Tag"
New-Item -ItemType Directory -Force $outDir | Out-Null

# The scenario, without the early end: a film is for looking at the whole of its time.
$sourceFile = Join-Path $RepoRoot "Scenarios\$Name.json"
if ($File) { $sourceFile = [System.IO.Path]::GetFullPath($File) }
$scenario = Read-JsonFile $sourceFile
if (-not $scenario) { Write-Output "FILM PAIR ERROR: $sourceFile does not exist"; exit 1 }
if ($scenario.PSObject.Properties['end_when_standing_s']) { $scenario.PSObject.Properties.Remove('end_when_standing_s') }
$source = Join-Path $outDir "$Name.json"
[System.IO.File]::WriteAllText($source, ($scenario | ConvertTo-Json -Depth 12))

$culture = [System.Globalization.CultureInfo]::InvariantCulture
# Both sides in one call of Tools\film.ps1, rendered through one queue (docs/fast-checks.md, SP-1).
$sides = @(@{ Suffix = 'a'; Tune = $TuneA; Dir = $TuningDirA }, @{ Suffix = 'b'; Tune = $TuneB; Dir = $TuningDirB })
$filmList = @()
foreach ($side in $sides) {
    $side.Tag = "${Tag}_$($side.Suffix)"
    # A film of an earlier run must not pass for this one's: its folder is the same on every run.
    $side.Video = Join-Path $RepoRoot "Saved\Films\$($side.Tag)\film.mp4"
    if (Test-Path $side.Video) { Remove-Item $side.Video -Force }
    $entry = [ordered]@{ file = $source; tag = $side.Tag; around = $Around; span = $Span; every = $Every; camera = $Camera; video = $true }
    if ($side.Tune) { $entry.tune = $side.Tune }
    if ($side.Dir) { $entry.tuning_dir = [System.IO.Path]::GetFullPath($side.Dir) }
    if ($Duration -gt 0) { $entry.duration = $Duration }
    $filmList += $entry
}
$listFile = Join-Path $outDir 'films.json'
[System.IO.File]::WriteAllText($listFile, (ConvertTo-Json @($filmList) -Depth 6))
$arguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'film.ps1'), '-Films', $listFile)
if ($Overlay) { $arguments += @('-Overlay', $Overlay) }
$printed = @(& powershell @arguments)
$videos = @()
foreach ($side in $sides) {
    $mine = @($printed | Where-Object { $_ -match "^SCENARIO \w+: $([regex]::Escape($side.Tag)) " -or $_ -match [regex]::Escape("Saved\Films\$($side.Tag)\") })
    $mine | ForEach-Object { Write-Output "  $($side.Suffix): $_" }
    if (-not ($mine | Where-Object { $_ -match '^SCENARIO (PASS|FAIL)' })) { Write-Output "FILM PAIR ERROR: side $($side.Suffix) did not run (a tuning name or value the build does not take?)"; exit 1 }
    if (-not (Test-Path $side.Video)) { Write-Output "FILM PAIR ERROR: side $($side.Suffix) made no video"; exit 1 }
    $videos += $side.Video
}

# Each side's label in its top left corner (ffmpeg's drawtext wants a font file and no colon or quote in the text), then side by side.
$font = 'C\:/Windows/Fonts/consola.ttf'
$clean = { param($text) ($text -replace "[:'\\%`"]", ' ') }
$labelTextA = & $clean $LabelA
$labelTextB = & $clean $LabelB
$draw = "drawtext=fontfile='$font':x=14:y=14:fontsize=30:fontcolor=yellow:box=1:boxcolor=black@0.6:boxborderw=6"
$filter = "[0:v]$draw`:text='$labelTextA'[a];[1:v]$draw`:text='$labelTextB'[b];[a][b]hstack=inputs=2"
$pair = Join-Path $outDir 'pair.mp4'
if (Test-Path $pair) { Remove-Item $pair -Force }
$null = Invoke-Process -FilePath $ffmpeg.Source -TimeoutSec 300 -ArgumentString "-y -loglevel error -i `"$($videos[0])`" -i `"$($videos[1])`" -filter_complex `"$filter`" -pix_fmt yuv420p `"$pair`""
if (-not (Test-Path $pair)) { Write-Output 'FILM PAIR ERROR: ffmpeg made no pair'; exit 1 }
Write-Output "FILM PAIR OK: $pair ($LabelA | $LabelB)"
exit 0
