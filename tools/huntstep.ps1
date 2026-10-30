# One draw-hunt step judged from a clean eye dump (stereo mode; the right eye is the game's own
# picture with "bias 1"): sends the answer (vanished / visible / none), waits, saves the eyes and
# writes screenshots\<Name>_right.png and its crop around the gun.
#   tools\huntstep.ps1 -Answer vanished -Name h3
param([ValidateSet('vanished', 'visible', 'none', 'start')] [string]$Answer = 'none', [string]$Name = 'step',
      [string]$Crop = '600,700,800,900')
$root = Split-Path $PSScriptRoot
if ($Answer -ne 'none') { & (Join-Path $PSScriptRoot 'drawhunt.ps1') -Action $Answer -Name huntkey | Out-Null }
Start-Sleep -Milliseconds 1200
$n = Get-Random -Minimum 1000 -Maximum 999999
Set-Content (Join-Path $root 'build\depth3d.txt') "strength 0.7 min 0.3 gun 2.4 mask 32 bias 1 debug 0 fill 1 show 1 hold 0 save $n" -Encoding ascii
Start-Sleep -Milliseconds 900
& (Join-Path $PSScriptRoot 'eyebmp.ps1') -Name $Name -Eye right -Crop $Crop | Out-Null
$line = Select-String -Path (Join-Path $root 'build\bf3vr.log') -Pattern 'Draw hunt' | Select-Object -Last 1
Write-Host $line.Line
