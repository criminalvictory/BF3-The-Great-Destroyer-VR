# Turns the eye images the mod saved (build\eye_left.bmp, eye_right.bmp: depth3d.txt "save N")
# into PNGs in screenshots\ (an 800x800 overview and a full-size crop), and with -Distances
# prints the distance in metres at points of a grey "debug 2" image (black 0, white 2 m).
#   tools\eyebmp.ps1 -Name gun -Crop 760,700,500,700
#   tools\eyebmp.ps1 -Name dist -Distances '900,900;1000,1200'
param(
    [Parameter(Mandatory)] [string]$Name,
    [string]$Crop = '450,450,700,700',
    [string]$Distances = '',
    [ValidateSet('left', 'right', 'both')] [string]$Eye = 'both',
    # The points are a "debug 3" stencil image: print the stencil value instead of a distance.
    [switch]$Stencil
)
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$box = @($Crop -split ',' | ForEach-Object { [int]$_ })
$eyes = if ($Eye -eq 'both') { @('left', 'right') } else { @($Eye) }
foreach ($e in $eyes) {
    $path = Join-Path $root "build\eye_$e.bmp"
    if (-not (Test-Path $path)) { Write-Host "no $path"; continue }
    $bmp = New-Object System.Drawing.Bitmap $path
    $small = New-Object System.Drawing.Bitmap $bmp, 800, 800
    $small.Save((Join-Path $root "screenshots\${Name}_$e.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $small.Dispose()
    $rect = New-Object System.Drawing.Rectangle $box[0], $box[1], ([Math]::Min($box[2], $bmp.Width - $box[0])), ([Math]::Min($box[3], $bmp.Height - $box[1]))
    $part = $bmp.Clone($rect, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $part.Save((Join-Path $root "screenshots\${Name}_${e}_crop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $part.Dispose()
    if ($Distances) {
        foreach ($p in ($Distances -split ';')) {
            $xy = @($p -split ',' | ForEach-Object { [int]$_ })
            $c = $bmp.GetPixel($xy[0], $xy[1])
            $v = $c.G / 255.0
            $linear = if ($v -le 0.04045) { $v / 12.92 } else { [Math]::Pow(($v + 0.055) / 1.055, 2.4) }
            if ($Stencil) { Write-Host ('{0} eye at {1},{2}: stencil {3}' -f $e, $xy[0], $xy[1], [Math]::Round($linear * 255)) }
            else { Write-Host ('{0} eye at {1},{2}: grey {3} -> {4:0.000} m' -f $e, $xy[0], $xy[1], $c.G, ($linear * 2)) }
        }
    }
    $bmp.Dispose()
}
Write-Host "saved screenshots\${Name}_<eye>.png and _crop.png"
