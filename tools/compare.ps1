# Puts the draw hunt's reference screenshot and the latest one side by side, small, so each
# hunt step can be judged at a glance. Usage: tools\compare.ps1 -Name hunt_01
param([Parameter(Mandatory)] [string]$Name)

Add-Type -AssemblyName System.Drawing
$dir = Join-Path (Split-Path $PSScriptRoot) 'screenshots'
$a = [System.Drawing.Image]::FromFile((Join-Path $dir 'hunt_00_reference.png'))
$b = [System.Drawing.Image]::FromFile((Join-Path $dir "$Name.png"))
$w = 380
$h = [int]($a.Height * $w / $a.Width)
$sheet = New-Object System.Drawing.Bitmap ($w * 2 + 8), $h
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::Black)
$g.DrawImage($a, 0, 0, $w, $h)
$g.DrawImage($b, $w + 8, 0, $w, $h)
$font = New-Object System.Drawing.Font 'Arial', 11, ([System.Drawing.FontStyle]::Bold)
$g.DrawString('reference', $font, [System.Drawing.Brushes]::Yellow, 4, 4)
$g.DrawString($Name, $font, [System.Drawing.Brushes]::Yellow, $w + 12, 4)
$g.Dispose(); $a.Dispose(); $b.Dispose()
$out = Join-Path $dir 'hunt_compare.png'
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host $out
