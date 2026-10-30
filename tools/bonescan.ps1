# Shifts each of the gun's bones alone (weapon.txt) and measures how much a box of the game's
# own picture changes against an unshifted frame: the bone that moves the part in the box (the
# magazine by default) changes it most. Stereo mode with clean eye dumps.
#   tools\bonescan.ps1 -Box 912,1150,36,170 -Count 59
param([string]$Box = '912,1150,36,170', [int]$Count = 59, [string]$Shift = '0 -0.1 0')
$root = Split-Path $PSScriptRoot
Add-Type -AssemblyName System.Drawing
$b = @($Box -split ',' | ForEach-Object { [int]$_ })
function Dump() {
    $n = Get-Random -Minimum 1000 -Maximum 999999
    Set-Content (Join-Path $root 'build\depth3d.txt') "strength 0.7 min 0.3 gun 2.4 mask 32 bias 1 debug 0 fill 1 show 1 hold 0 save $n" -Encoding ascii
    Start-Sleep -Milliseconds 700
    $bmp = New-Object System.Drawing.Bitmap (Join-Path $root 'build\eye_right.bmp')
    $values = New-Object System.Collections.Generic.List[double]
    for ($y = $b[1]; $y -lt $b[1] + $b[3]; $y += 2) { for ($x = $b[0]; $x -lt $b[0] + $b[2]; $x += 2) { $c = $bmp.GetPixel($x, $y); $values.Add(($c.R + $c.G + $c.B) / 3.0) } }
    $bmp.Dispose()
    ,$values
}
Set-Content (Join-Path $root 'build\weapon.txt') '' -Encoding ascii; Start-Sleep -Milliseconds 500
$base = Dump
for ($k = 0; $k -lt $Count; ++$k) {
    Set-Content (Join-Path $root 'build\weapon.txt') "$k shift $Shift" -Encoding ascii
    Start-Sleep -Milliseconds 500
    $v = Dump
    $diff = 0.0; for ($i = 0; $i -lt $v.Count; ++$i) { $diff += [Math]::Abs($v[$i] - $base[$i]) }
    Write-Output ('bone {0,2}: change {1:0.0}' -f $k, ($diff / $v.Count))
}
Set-Content (Join-Path $root 'build\weapon.txt') '' -Encoding ascii
