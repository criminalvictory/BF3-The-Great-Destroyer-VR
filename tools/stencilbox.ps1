# Share of a box of the picture marked first-person (stencil 32: the gun and arms) in a clean
# eye dump: a number for "is that part of the gun there" that a screenshot by eye can't give.
# Stereo mode. Optionally sends a draw-hunt answer first.
#   tools\stencilbox.ps1 -Box 920,1140,55,180 [-Answer vanished]
#   -Color instead reports the box's mean brightness (0-255) in the game's own picture: a part whose
#   colour pass is hidden leaves a black shape where its depth pass still draws.
param([string]$Box = '920,1140,55,180', [ValidateSet('vanished', 'visible', 'none', 'start')] [string]$Answer = 'none',
      [int]$Mask = 32, [switch]$Color)
$root = Split-Path $PSScriptRoot
if ($Answer -ne 'none') { & (Join-Path $PSScriptRoot 'drawhunt.ps1') -Action $Answer -Name huntkey | Out-Null }
Start-Sleep -Milliseconds 1200
$n = Get-Random -Minimum 1000 -Maximum 999999
$debug = if ($Color) { 0 } else { 3 }
Set-Content (Join-Path $root 'build\depth3d.txt') "strength 0.7 min 0.3 gun 2.4 mask 32 bias 1 debug $debug fill 1 show 1 hold 0 save $n" -Encoding ascii
Start-Sleep -Milliseconds 900
Add-Type -AssemblyName System.Drawing
$b = @($Box -split ',' | ForEach-Object { [int]$_ })
$bmp = New-Object System.Drawing.Bitmap (Join-Path $root 'build\eye_right.bmp')
$marked = 0; $total = 0; $sum = 0.0
for ($y = $b[1]; $y -lt $b[1] + $b[3]; $y += 2) {
    for ($x = $b[0]; $x -lt $b[0] + $b[2]; $x += 2) {
        $c = $bmp.GetPixel($x, $y)
        $sum += ($c.R + $c.G + $c.B) / 3.0
        $v = $c.G / 255.0
        $linear = if ($v -le 0.04045) { $v / 12.92 } else { [Math]::Pow(($v + 0.055) / 1.055, 2.4) }
        if (([int][Math]::Round($linear * 255)) -band $Mask) { ++$marked }
        ++$total
    }
}
$bmp.Dispose()
$line = Select-String -Path (Join-Path $root 'build\bf3vr.log') -Pattern 'Draw hunt' | Select-Object -Last 1
if ($Color) { Write-Output ('{0:0} brightness   ({1})' -f ($sum / $total), $line.Line) }
else { Write-Output ('{0:P0} of the box marked   ({1})' -f ($marked / $total), $line.Line) }
