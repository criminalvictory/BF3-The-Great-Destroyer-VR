# Tries weapon.txt settings one after another and saves the game's own picture for each (stereo
# mode: the right eye with "bias 1", saved by the mod itself, so no screenshot shows an old frame),
# then puts them side by side in screenshots\<Name>_sheet.png.
#   tools\weapontest.ps1 -Name mag -States @('', "4 hide`npass 1: 0`npass 2: 4-5")
param([Parameter(Mandatory)] [string]$Name, [Parameter(Mandatory)] [AllowEmptyString()] [string[]]$States,
      [string]$Crop = '300,300,1000,1000')
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$weapon = Join-Path $root 'build\weapon.txt'
$depth = Join-Path $root 'build\depth3d.txt'
$box = @($Crop -split ',' | ForEach-Object { [int]$_ })
$tiles = @()
for ($i = 0; $i -lt $States.Count; ++$i) {
    Set-Content $weapon $States[$i] -Encoding ascii
    Start-Sleep -Milliseconds 1000
    $n = Get-Random -Minimum 1000 -Maximum 999999
    Set-Content $depth "strength 0.7 min 0.3 gun 2.4 mask 32 bias 1 debug 0 fill 1 show 1 hold 0 save $n" -Encoding ascii
    Start-Sleep -Milliseconds 900
    $bmp = Join-Path $root 'build\eye_right.bmp'
    $img = [System.Drawing.Image]::FromFile($bmp)
    $tile = New-Object System.Drawing.Bitmap 500, 500
    $g = [System.Drawing.Graphics]::FromImage($tile)
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, 500, 500), (New-Object System.Drawing.Rectangle $box[0], $box[1], $box[2], $box[3]), [System.Drawing.GraphicsUnit]::Pixel)
    $label = if ($States[$i]) { $States[$i] -replace "`n", ' | ' } else { '(nothing)' }
    $g.FillRectangle([System.Drawing.Brushes]::Black, 0, 0, 500, 22)
    $g.DrawString("$i $label", (New-Object System.Drawing.Font 'Arial', 11), [System.Drawing.Brushes]::Yellow, 3, 3)
    $g.Dispose(); $img.Dispose()
    $tiles += $tile
}
Set-Content $weapon '' -Encoding ascii
Set-Content $depth 'strength 0.7 min 0.3 gun 2.4 mask 32 bias 0 debug 0 fill 1 show 0 hold 0' -Encoding ascii
$cols = [Math]::Min(3, $tiles.Count); $rows = [Math]::Ceiling($tiles.Count / $cols)
$sheet = New-Object System.Drawing.Bitmap ($cols * 500), ($rows * 500)
$g = [System.Drawing.Graphics]::FromImage($sheet)
for ($i = 0; $i -lt $tiles.Count; ++$i) { $g.DrawImage($tiles[$i], ($i % $cols) * 500, [Math]::Floor($i / $cols) * 500); $tiles[$i].Dispose() }
$g.Dispose()
$out = Join-Path $root "screenshots\$($Name)_sheet.png"
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
Write-Output "sheet $out"
