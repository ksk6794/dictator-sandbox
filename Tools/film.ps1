# Renders a scenario with a picture at each of a list of simulated times and joins the pictures into one sheet,
# so that a motion can be looked at frame by frame: what a body does between a blow and the floor is not in
# the last frame, and not in the metrics.
#   powershell -ExecutionPolicy Bypass -File Tools\film.ps1 -Name hit_chest_200_falls -Times "3.0,3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"
#   ... -Camera side|front|back|quarter      where the camera stands (4.2 m from the spawn point, at chest height)
#   ... -Camera "400,-120,90;0,-120,60"      or its location and the point it looks at, cm
#   ... -Events <file.json>                  a JSON array of events that replaces the scenario's own
#   ... -Duration 6                          another duration, seconds
#   ... -Tune "name=value,..."               tuning values for this run
#   ... -Overlay all                         debug overlays in the pictures
#   ... -CropW 0.6 -Cols 6 -TileH 400        share of each picture's width kept, pictures per row, height of one
#   ... -Tag <name>                          output name (default: the scenario's)
#   ... -Labels "0.0,0.5,1.0"                what to write on the pictures instead of their times, one for each
# Writes Saved\Films\<tag>\sheet.png next to the run's own results. The scenario's expectations are dropped:
# this is for looking, the suite does the judging. Exit code: 0 a sheet was written, 1 not.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Tag,
    [string]$Times = '3.0,3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0',
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

if (-not $Tag) { $Tag = $Name }
$outDir = Join-Path $RepoRoot "Saved\Films\$Tag"
New-Item -ItemType Directory -Force $outDir | Out-Null
$film = Read-JsonFile (Join-Path $RepoRoot "Scenarios\$Name.json")
if (-not $film) {
    Write-Output "FILM ERROR: Scenarios\$Name.json does not exist"
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
exit 0
