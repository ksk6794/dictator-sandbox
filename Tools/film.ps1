# Renders a scenario with a picture at each of a list of simulated times and joins the pictures into one sheet,
# so that a motion can be looked at frame by frame: what a body does between a blow and the floor is not in
# the last frame, and not in the metrics.
#   powershell -ExecutionPolicy Bypass -File Tools\film.ps1 -Name hit_chest_200_falls -Times "3.0,3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"
#   ... -Name "a,b,c"                        several scenarios, each a film of its own with the same settings, rendered
#                                            through one queue (Saved\Films\<name> each; -Tag is not taken then)
#   ... -Films <file.json>                   several films with settings of their own, through one queue: a JSON array of
#                                            objects with any of name, file, tag, times, around, span, every, camera, events
#                                            (a file), duration, tune, tuning_dir, labels, video, video_fps, crop_w, cols,
#                                            tile_h; what a film leaves out is this call's value (Tools\film-pair.ps1, the
#                                            stand's and the knockdowns' -Film). -Overlay is the call's, for every film
#   ... -Around 3.0 -Span 0.6 -Every 0.033   instead of -Times: a dense strip from 0.1 s before that time on (a frame is
#                                            1/90 s), for questions of timing that twelve pictures over six seconds cannot answer
#   ... -Video                               also Saved\Films\<tag>\film.mp4 (ffmpeg): the pictures as frames, at -VideoFps (default 1/Every, real time)
#   ... -Camera side|front|back|quarter      where the camera stands (4.2 m from the spawn point, at chest height)
#   ... -Camera "400,-120,90;0,-120,60"      or its location and the point it looks at, cm
#   ... -Events <file.json>                  a JSON array of events that replaces the scenario's own
#   ... -Duration 6                          another duration, seconds
#   ... -Tune "name=value,..."               tuning values for this run
#   ... -TuningDir <dir>                     the tuning files from this directory instead of Config\Tuning
#   ... -Overlay all                         debug overlays in the pictures
#   ... -CropW 0.6 -Cols 6 -TileH 400        share of each picture's width kept, pictures per row, height of one
#   ... -File <path.json>                    a scenario file from anywhere instead of Scenarios\<Name>.json
#   ... -Tag <name>                          output name (default: the scenario's)
#   ... -Labels "0.0,0.5,1.0"                what to write on the pictures instead of their times, one for each
#   ... -Parallel 2                          rendered engine processes side by side for several films (each takes the
#                                            memory measured in docs/testing.md, "Faster checks, stage 1"; the default
#                                            keeps 1.5 GB free)
# Writes Saved\Films\<tag>\sheet.png next to the run's own results. The scenario's expectations are dropped:
# this is for looking, the suite does the judging. A film prints the run's verdict line (as Tools\scenario.ps1 prints it)
# and FILM OK with its sheet; several films print a line for all of them first. Exit code: 0 every sheet was written, 1 not.
param(
    [string]$Name,
    [string]$File,
    [string]$Tag,
    [string]$Films,
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
    [string]$TuningDir,
    [string]$Overlay,
    [double]$CropW = 0.6,
    [int]$Cols = 6,
    [int]$TileH = 400,
    [string]$Labels,
    [int]$Parallel = 0
)
. "$PSScriptRoot\common.ps1"
$culture = [System.Globalization.CultureInfo]::InvariantCulture

function Get-FilmTimes([string]$List, [double]$At, [double]$Long, [double]$Step) {
    # -Around: on whole frames, so that no two pictures are of the same one.
    if ($At -lt 0) { return $List }
    $frames = [math]::Max([int][math]::Round($Step * 90.0), 1)
    $first = [int][math]::Round([math]::Max($At - 0.1, 0.0) * 90.0)
    $count = [int][math]::Floor($Long * 90.0 / $frames) + 1
    return (@(0..($count - 1)) | ForEach-Object { (($first + $_ * $frames) / 90.0).ToString('F3', $culture) }) -join ','
}
function Get-Field($Object, [string]$Field, $Default) {
    if ($null -ne $Object -and $Object.PSObject.Properties[$Field] -and $null -ne $Object.$Field -and "$($Object.$Field)" -ne '') { return $Object.$Field }
    return $Default
}

# --- The films asked for ----------------------------------------------------------------------------------------------------
$asked = @()
if ($Films) {
    if (-not (Test-Path $Films)) { Write-Output "FILM ERROR: $Films does not exist"; exit 1 }
    # ConvertFrom-Json of Windows PowerShell hands a JSON array on as one object: the pipe takes it apart.
    $asked = @((Get-Content $Films -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
    if ($asked.Count -eq 0) { Write-Output "FILM ERROR: $Films lists no film"; exit 1 }
} else {
    if ($File -and -not $Name) { $Name = [System.IO.Path]::GetFileNameWithoutExtension($File) }
    if (-not $Name) {
        Write-Output 'FILM ERROR: give -Name <scenario>, -File <scenario file> or -Films <list.json>'
        exit 1
    }
    $scenarioNames = @($Name.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($scenarioNames.Count -gt 1 -and ($Tag -or $File)) { Write-Output 'FILM ERROR: several -Name take neither -Tag nor -File (each film is tagged with its scenario)'; exit 1 }
    foreach ($one in $scenarioNames) {
        $entry = [pscustomobject]@{ name = $one }
        if ($File) { $entry | Add-Member -NotePropertyName file -NotePropertyValue $File }
        if ($Tag) { $entry | Add-Member -NotePropertyName tag -NotePropertyValue $Tag }
        $asked += $entry
    }
}

$views = @{
    side    = '420,0,105;0,0,95'
    front   = '0,420,105;0,0,95'
    back    = '0,-420,105;0,0,95'
    quarter = '300,300,120;0,0,95'
}
$list = @()
foreach ($one in $asked) {
    $filmName = [string](Get-Field $one 'name' '')
    $filmFile = [string](Get-Field $one 'file' '')
    if ($filmFile -and -not $filmName) { $filmName = [System.IO.Path]::GetFileNameWithoutExtension($filmFile) }
    if (-not $filmName) { Write-Output 'FILM ERROR: a film of the list has neither name nor file'; exit 1 }
    $filmTag = [string](Get-Field $one 'tag' $filmName)
    $sourceFile = Join-Path $RepoRoot "Scenarios\$filmName.json"
    if ($filmFile) { $sourceFile = [System.IO.Path]::GetFullPath($filmFile) }
    $film = Read-JsonFile $sourceFile
    if (-not $film) { Write-Output "FILM ERROR: $sourceFile does not exist"; exit 1 }
    if (@($list | Where-Object { $_.tag -eq $filmTag }).Count -gt 0) { Write-Output "FILM ERROR: two films are tagged '$filmTag'"; exit 1 }
    $outDir = Join-Path $RepoRoot "Saved\Films\$filmTag"
    New-Item -ItemType Directory -Force $outDir | Out-Null
    # A sheet or a video of an earlier call must not pass for this one's.
    foreach ($stale in @('sheet.png', 'film.mp4', 'film.txt')) { $path = Join-Path $outDir $stale; if (Test-Path $path) { Remove-Item $path -Force } }
    $film.name = $filmTag
    $view = [string](Get-Field $one 'camera' $Camera)
    if ($views.ContainsKey($view)) { $view = $views[$view] }
    $viewParts = $view.Split(';')
    $cameraSpec = [pscustomobject]@{
        location_cm = @($viewParts[0].Split(',') | ForEach-Object { [double]::Parse($_, $culture) })
        look_at_cm  = @($viewParts[1].Split(',') | ForEach-Object { [double]::Parse($_, $culture) })
    }
    $film | Add-Member -NotePropertyName camera -NotePropertyValue $cameraSpec -Force
    $eventsFile = [string](Get-Field $one 'events' $Events)
    if ($eventsFile) {
        $eventList = ConvertFrom-Json (Get-Content $eventsFile -Raw)
        $film | Add-Member -NotePropertyName events -NotePropertyValue @($eventList) -Force
    }
    $filmDuration = [double](Get-Field $one 'duration' $Duration)
    if ($filmDuration -gt 0) { $film.duration_s = $filmDuration }
    $film | Add-Member -NotePropertyName expect -NotePropertyValue ([pscustomobject]@{}) -Force
    $scenarioCopy = Join-Path $outDir "$filmTag.json"
    [System.IO.File]::WriteAllText($scenarioCopy, ($film | ConvertTo-Json -Depth 12))
    $filmTimes = Get-FilmTimes ([string](Get-Field $one 'times' $Times)) ([double](Get-Field $one 'around' $Around)) ([double](Get-Field $one 'span' $Span)) ([double](Get-Field $one 'every' $Every))
    $fps = 90
    if ($film.fixed_fps) { $fps = [int]$film.fixed_fps }
    $list += [pscustomobject]@{
        tag = $filmTag; out = $outDir; scenario = $scenarioCopy; times = $filmTimes; fps = $fps
        tune = [string](Get-Field $one 'tune' $Tune); tuning_dir = [string](Get-Field $one 'tuning_dir' $TuningDir)
        labels = [string](Get-Field $one 'labels' $Labels); video = [bool](Get-Field $one 'video' ([bool]$Video)); video_fps = [double](Get-Field $one 'video_fps' $VideoFps)
        crop_w = [double](Get-Field $one 'crop_w' $CropW); cols = [int](Get-Field $one 'cols' $Cols); tile_h = [int](Get-Field $one 'tile_h' $TileH)
    }
}

# --- One queue of rendered processes ---------------------------------------------------------------------------------------
$jobs = @()
foreach ($one in $list) {
    $job = @{ File = $one.scenario; Out = $one.out; Shot = (Join-Path $one.out 'final.png'); ShotAt = $one.times; Rendered = $true; Fps = $one.fps }
    if ($one.tune) { $job.Tune = $one.tune }
    if ($one.tuning_dir) { $job.TuningDir = [System.IO.Path]::GetFullPath($one.tuning_dir) }
    if ($Overlay) { $job.Overlay = $Overlay }
    $jobs += $job
}
$processes = $Parallel
if ($processes -le 0) { $processes = Get-RenderedParallel }
# A run that hangs is ended, as Tools\scenario.ps1 ended one after 900 s: that, and 300 s more for every further film.
Invoke-ScenarioBatch -Jobs $jobs -Parallel ([Math]::Max([Math]::Min($processes, $jobs.Count), 1)) -TimeoutSec (900 + 300 * ($jobs.Count - 1))

# --- Sheets ----------------------------------------------------------------------------------------------------------------
Add-Type -AssemblyName System.Drawing
function New-FilmSheet($one) {
    # The pictures of one film joined into its sheet, and its video if asked for. Returns the lines to print.
    $printed = @()
    $tiles = @()
    $shotList = @($one.times.Split(',') | ForEach-Object { [double]::Parse($_, $culture) })
    $labelList = @()
    if ($one.labels) { $labelList = @($one.labels.Split(',')) }
    for ($shotIndex = 0; $shotIndex -lt $shotList.Count; $shotIndex++) {
        $time = $shotList[$shotIndex]
        $picture = Join-Path $one.out ('shot_{0}.png' -f $time.ToString('F3', $culture))
        $caption = $time.ToString('F2', $culture)
        if ($shotIndex -lt $labelList.Count) { $caption = $labelList[$shotIndex].Trim() }
        if (Test-Path $picture) { $tiles += [pscustomobject]@{ T = $time; Path = $picture; Caption = $caption } }
    }
    if ($tiles.Count -eq 0) { return @{ ok = $false; lines = @("FILM ERROR: the run saved no pictures (see $($one.out))") } }
    $first = [System.Drawing.Image]::FromFile($tiles[0].Path)
    $sourceW = $first.Width
    $sourceH = $first.Height
    $first.Dispose()
    $cropPx = [int]($sourceW * $one.crop_w)
    $tileW = [int]($cropPx * $one.tile_h / $sourceH)
    $rows = [int][math]::Ceiling($tiles.Count / $one.cols)
    $sheet = New-Object System.Drawing.Bitmap ($tileW * [math]::Min($one.cols, $tiles.Count)), ($one.tile_h * $rows)
    $canvas = [System.Drawing.Graphics]::FromImage($sheet)
    $canvas.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $font = New-Object System.Drawing.Font 'Consolas', 12, ([System.Drawing.FontStyle]::Bold)
    for ($index = 0; $index -lt $tiles.Count; $index++) {
        $picture = [System.Drawing.Image]::FromFile($tiles[$index].Path)
        $from = New-Object System.Drawing.Rectangle ([int](($sourceW - $cropPx) / 2)), 0, $cropPx, $sourceH
        $x = ($index % $one.cols) * $tileW
        $y = [int][math]::Floor($index / $one.cols) * $one.tile_h
        $canvas.DrawImage($picture, (New-Object System.Drawing.Rectangle $x, $y, $tileW, $one.tile_h), $from, [System.Drawing.GraphicsUnit]::Pixel)
        $canvas.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, [int]($canvas.MeasureString($tiles[$index].Caption, $font).Width) + 4, 20)
        $canvas.DrawString($tiles[$index].Caption, $font, [System.Drawing.Brushes]::Yellow, $x + 2, $y + 1)
        $picture.Dispose()
    }
    $canvas.Dispose()
    $sheetFile = Join-Path $one.out 'sheet.png'
    $sheet.Save($sheetFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    $printed += "FILM OK: $($tiles.Count) pictures in $sheetFile"
    if ($one.video) {
        $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
        if (-not $ffmpeg) { $printed += 'FILM WARNING: ffmpeg is not installed, no video'; return @{ ok = $true; lines = $printed } }
        $fpsOfVideo = $one.video_fps
        if ($fpsOfVideo -le 0) {
            $fpsOfVideo = 15
            if ($tiles.Count -gt 1 -and $tiles[-1].T -gt $tiles[0].T) { $fpsOfVideo = ($tiles.Count - 1) / ($tiles[-1].T - $tiles[0].T) }
        }
        $fpsText = $fpsOfVideo.ToString('F3', $culture)
        # The pictures in order, each for one frame; ffmpeg reads the list.
        $listFile = Join-Path $one.out 'film.txt'
        $length = (1.0 / $fpsOfVideo).ToString('F5', $culture)
        $lines = @($tiles | ForEach-Object { "file '$(Split-Path -Leaf $_.Path)'"; "duration $length" })
        [System.IO.File]::WriteAllLines($listFile, $lines)
        $videoFile = Join-Path $one.out 'film.mp4'
        $null = Invoke-Process -FilePath $ffmpeg.Source -TimeoutSec 120 -ArgumentString "-y -loglevel error -f concat -safe 0 -i `"$listFile`" -vf `"scale=trunc(iw/2)*2:trunc(ih/2)*2,fps=$fpsText`" -pix_fmt yuv420p `"$videoFile`""
        if (Test-Path $videoFile) { $printed += "FILM VIDEO: $videoFile" } else { $printed += 'FILM WARNING: ffmpeg made no video' }
    }
    return @{ ok = $true; lines = $printed }
}

$made = 0
$report = @()
foreach ($one in $list) {
    $metrics = Read-JsonFile (Join-Path $one.out 'metrics.json')
    $report += (Format-ScenarioVerdict $one.tag $metrics $one.out)
    $sheetResult = New-FilmSheet $one
    if ($sheetResult.ok) { $made++ }
    $report += $sheetResult.lines
}
if ($list.Count -gt 1) {
    $word = 'OK'
    if ($made -lt $list.Count) { $word = 'FAILED' }
    Write-Output ("FILMS {0}: {1} of {2} sheets written ({3})" -f $word, $made, $list.Count, (($list | ForEach-Object { $_.tag }) -join ', '))
}
$report | ForEach-Object { Write-Output $_ }
if ($made -lt $list.Count) { exit 1 }
exit 0
