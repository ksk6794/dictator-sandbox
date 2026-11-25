# Renders a scenario with a picture at each of a list of simulated times and joins the pictures into one sheet,
# so that a motion can be looked at frame by frame: what a body does between a blow and the floor is not in
# the last frame, and not in the metrics.
#   powershell -ExecutionPolicy Bypass -File Tools\film.ps1 -Name hit_chest_200_falls -Times "3.0,3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"
#   ... -Around 3.0 -Span 0.6 -Every 0.033   instead of -Times: a dense strip from 0.1 s before that time on (a frame is
#                                            1/90 s), for questions of timing that twelve pictures over six seconds cannot answer
#   ... -Video                               also Saved\Films\<tag>\film.mp4 (ffmpeg): the pictures as frames, at -VideoFps (default 1/Every, real time)
#   ... -Camera side|front|back|quarter      where the camera stands (4.2 m from the spawn point, at chest height)
#   ... -Camera "400,-120,90;0,-120,60"      or its location and the point it looks at, cm
#   ... -Events <file.json>                  a JSON array of events that replaces the scenario's own
#   ... -Duration 6                          another duration, seconds
#   ... -Tune "name=value,..."               tuning values for this run
#   ... -Overlay all                         debug overlays in the pictures
#   ... -CropW 0.6 -Cols 6 -TileH 400        share of each picture's width kept, pictures per row, height of one
#   ... -File <path.json>                    a scenario file from anywhere instead of Scenarios\<Name>.json
#   ... -Tag <name>                          output name (default: the scenario's)
#   ... -Labels "0.0,0.5,1.0"                what to write on the pictures instead of their times, one for each
# Writes Saved\Films\<tag>\sheet.png next to the run's own results. The scenario's expectations are dropped:
# this is for looking, the suite does the judging. Exit code: 0 a sheet was written, 1 not.
param(
    [string]$Name,
    [string]$File,
    [string]$Tag,
    [string]$Times = '3.0,3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0',
    [double]$Around = -1,
    [double]$Span = 0.6,
    [double]$Every = 0.033,
    [switch]$Video,
    [double]$VideoFps = 0,
    [string]$Camera = 'side',
    [string]$Events,
    [double]$Duration = 0,
    [string]$Tune,
    [string]$Overlay,
    [double]$CropW = 0.6,
    [int]$Cols = 6,
    [int]$TileH = 400,
    [string]$Labels
)
. "$PSScriptRoot\common.ps1"

if ($Around -ge 0) {
    # On whole frames, so that no two pictures are of the same one.
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    $frames = [math]::Max([int][math]::Round($Every * 90.0), 1)
    $first = [int][math]::Round([math]::Max($Around - 0.1, 0.0) * 90.0)
    $count = [int][math]::Floor($Span * 90.0 / $frames) + 1
    $Times = (@(0..($count - 1)) | ForEach-Object { (($first + $_ * $frames) / 90.0).ToString('F3', $culture) }) -join ','
}

if ($File -and -not $Name) { $Name = [System.IO.Path]::GetFileNameWithoutExtension($File) }
if (-not $Name) {
    Write-Output 'FILM ERROR: give -Name <scenario> or -File <scenario file>'
    exit 1
}
if (-not $Tag) { $Tag = $Name }
$outDir = Join-Path $RepoRoot "Saved\Films\$Tag"
New-Item -ItemType Directory -Force $outDir | Out-Null
$sourceFile = Join-Path $RepoRoot "Scenarios\$Name.json"
if ($File) { $sourceFile = [System.IO.Path]::GetFullPath($File) }
$film = Read-JsonFile $sourceFile
if (-not $film) {
    Write-Output "FILM ERROR: $sourceFile does not exist"
    exit 1
}
$film.name = $Tag
$views = @{
    side    = '420,0,105;0,0,95'
    front   = '0,420,105;0,0,95'
    back    = '0,-420,105;0,0,95'
    quarter = '300,300,120;0,0,95'
}
$view = $Camera
if ($views.ContainsKey($Camera)) { $view = $views[$Camera] }
$viewParts = $view.Split(';')
$cameraSpec = [pscustomobject]@{
    location_cm = @($viewParts[0].Split(',') | ForEach-Object { [double]$_ })
    look_at_cm  = @($viewParts[1].Split(',') | ForEach-Object { [double]$_ })
}
$film | Add-Member -NotePropertyName camera -NotePropertyValue $cameraSpec -Force
if ($Events) {
    $eventList = ConvertFrom-Json (Get-Content $Events -Raw)
    $film | Add-Member -NotePropertyName events -NotePropertyValue @($eventList) -Force
}
if ($Duration -gt 0) { $film.duration_s = $Duration }
$film | Add-Member -NotePropertyName expect -NotePropertyValue ([pscustomobject]@{}) -Force
$filmFile = Join-Path $outDir "$Tag.json"
[System.IO.File]::WriteAllText($filmFile, ($film | ConvertTo-Json -Depth 12))

$runArguments = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'scenario.ps1'), '-File', $filmFile, '-Shot', '-ShotAt', $Times, '-Out', $outDir, '-Quiet')
if ($Tune) { $runArguments += @('-Tune', $Tune) }
if ($Overlay) { $runArguments += @('-Overlay', $Overlay) }
$runOutput = & powershell @runArguments
$runOutput | Select-Object -First 1

Add-Type -AssemblyName System.Drawing
$tiles = @()
$shotList = @($Times.Split(',') | ForEach-Object { [double]$_ })
$labelList = @()
if ($Labels) { $labelList = @($Labels.Split(',')) }
for ($shotIndex = 0; $shotIndex -lt $shotList.Count; $shotIndex++) {
    $time = $shotList[$shotIndex]
    $picture = Join-Path $outDir ('shot_{0:F3}.png' -f $time)
    $caption = '{0:F2}' -f $time
    if ($shotIndex -lt $labelList.Count) { $caption = $labelList[$shotIndex].Trim() }
    if (Test-Path $picture) { $tiles += [pscustomobject]@{ T = $time; Path = $picture; Caption = $caption } }
}
if ($tiles.Count -eq 0) {
    Write-Output "FILM ERROR: the run saved no pictures (see $outDir)"
    exit 1
}
$first = [System.Drawing.Image]::FromFile($tiles[0].Path)
$sourceW = $first.Width
$sourceH = $first.Height
$first.Dispose()
$cropPx = [int]($sourceW * $CropW)
$tileW = [int]($cropPx * $TileH / $sourceH)
$rows = [int][math]::Ceiling($tiles.Count / $Cols)
$sheet = New-Object System.Drawing.Bitmap ($tileW * [math]::Min($Cols, $tiles.Count)), ($TileH * $rows)
$canvas = [System.Drawing.Graphics]::FromImage($sheet)
$canvas.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font = New-Object System.Drawing.Font 'Consolas', 12, ([System.Drawing.FontStyle]::Bold)
for ($index = 0; $index -lt $tiles.Count; $index++) {
    $picture = [System.Drawing.Image]::FromFile($tiles[$index].Path)
    $from = New-Object System.Drawing.Rectangle ([int](($sourceW - $cropPx) / 2)), 0, $cropPx, $sourceH
    $x = ($index % $Cols) * $tileW
    $y = [int][math]::Floor($index / $Cols) * $TileH
    $canvas.DrawImage($picture, (New-Object System.Drawing.Rectangle $x, $y, $tileW, $TileH), $from, [System.Drawing.GraphicsUnit]::Pixel)
    $canvas.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, [int]($canvas.MeasureString($tiles[$index].Caption, $font).Width) + 4, 20)
    $canvas.DrawString($tiles[$index].Caption, $font, [System.Drawing.Brushes]::Yellow, $x + 2, $y + 1)
    $picture.Dispose()
}
$canvas.Dispose()
$sheetFile = Join-Path $outDir 'sheet.png'
$sheet.Save($sheetFile, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Output "FILM OK: $($tiles.Count) pictures in $sheetFile"
if ($Video) {
    $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (-not $ffmpeg) { Write-Output 'FILM WARNING: ffmpeg is not installed, no video'; exit 0 }
    if ($VideoFps -le 0) {
        $VideoFps = 15
        if ($tiles.Count -gt 1 -and $tiles[-1].T -gt $tiles[0].T) { $VideoFps = ($tiles.Count - 1) / ($tiles[-1].T - $tiles[0].T) }
    }
    $fpsText = $VideoFps.ToString('F3', [System.Globalization.CultureInfo]::InvariantCulture)
    # The pictures in order, each for one frame; ffmpeg reads the list.
    $listFile = Join-Path $outDir 'film.txt'
    $length = (1.0 / $VideoFps).ToString('F5', [System.Globalization.CultureInfo]::InvariantCulture)
    $lines = @($tiles | ForEach-Object { "file '$(Split-Path -Leaf $_.Path)'"; "duration $length" })
    [System.IO.File]::WriteAllLines($listFile, $lines)
    $videoFile = Join-Path $outDir 'film.mp4'
    $null = Invoke-Process -FilePath $ffmpeg.Source -TimeoutSec 120 -ArgumentString "-y -loglevel error -f concat -safe 0 -i `"$listFile`" -vf `"scale=trunc(iw/2)*2:trunc(ih/2)*2,fps=$fpsText`" -pix_fmt yuv420p `"$videoFile`""
    if (Test-Path $videoFile) { Write-Output "FILM VIDEO: $videoFile" } else { Write-Output 'FILM WARNING: ffmpeg made no video' }
}
exit 0
