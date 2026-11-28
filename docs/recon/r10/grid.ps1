# Joins chosen pictures of one or two films (shot_<time>.png, the nearest to each asked time) into one grid, a row a film, cropped to
# the middle of each picture: for looking at a few moments of a long film-pair.
#   powershell -ExecutionPolicy Bypass -File docs\recon\r10\grid.ps1 -Films "r10_staydown_lying_a,r10_staydown_lying_b" -Times "2,3,4,6" -Out x.png
param(
    [Parameter(Mandatory = $true)][string]$Films,
    [Parameter(Mandatory = $true)][string]$Times,
    [Parameter(Mandatory = $true)][string]$Out,
    [double]$CropW = 0.45,
    [double]$CropH = 0.7,
    [int]$TileW = 400
)
Add-Type -AssemblyName System.Drawing
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$filmList = @($Films.Split(',') | ForEach-Object { $_.Trim() })
$timeList = @($Times.Split(',') | ForEach-Object { [double]::Parse($_.Trim(), $culture) })
$rows = @()
foreach ($film in $filmList) {
    $dir = Join-Path $repo "Saved\Films\$film"
    $shots = @(Get-ChildItem $dir -Filter 'shot_*.png' | ForEach-Object { [pscustomobject]@{ t = [double]::Parse($_.BaseName.Substring(5), $culture); path = $_.FullName } })
    $rows += , @($timeList | ForEach-Object { $want = $_; ($shots | Sort-Object { [Math]::Abs($_.t - $want) } | Select-Object -First 1) })
}
$first = [System.Drawing.Image]::FromFile($rows[0][0].path)
$cw = [int]($first.Width * $CropW); $ch = [int]($first.Height * $CropH)
$x0 = [int](($first.Width - $cw) / 2); $y0 = [int](($first.Height - $ch) / 2)
$first.Dispose()
$tileH = [int]($TileW * $ch / $cw)
$bitmap = New-Object System.Drawing.Bitmap ($TileW * $timeList.Count), ($tileH * $rows.Count)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$font = New-Object System.Drawing.Font 'Consolas', 14
for ($r = 0; $r -lt $rows.Count; $r++) {
    for ($c = 0; $c -lt $timeList.Count; $c++) {
        $shot = $rows[$r][$c]
        $image = [System.Drawing.Image]::FromFile($shot.path)
        $graphics.DrawImage($image, (New-Object System.Drawing.Rectangle ($c * $TileW), ($r * $tileH), $TileW, $tileH), (New-Object System.Drawing.Rectangle $x0, $y0, $cw, $ch), [System.Drawing.GraphicsUnit]::Pixel)
        $image.Dispose()
        $graphics.DrawString(('{0} {1:N2}' -f $filmList[$r].Split('_')[-1], $shot.t), $font, [System.Drawing.Brushes]::Yellow, ($c * $TileW + 4), ($r * $tileH + 4))
    }
}
$bitmap.Save($Out)
Write-Output "GRID: $Out"
