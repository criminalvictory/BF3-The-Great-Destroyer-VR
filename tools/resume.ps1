# After Battlelog's PLAY CAMPAIGN: waits for bf3.exe and its menu, clicks RESUME CAMPAIGN (client
# 410,602 in the 1600x1600 window) and waits until gameplay has started (the log shows the wide
# render view in use). Takes screenshots on the way (screenshots\resume_*.png).
param([int]$MenuWait = 32)

$tool = Join-Path $PSScriptRoot 'drawhunt.ps1'
$log = Join-Path (Split-Path $PSScriptRoot) 'build\bf3vr.log'
$t = 0
while (-not (Get-Process bf3 -ErrorAction SilentlyContinue) -and $t -lt 90) { Start-Sleep 1; $t++ }
if (-not (Get-Process bf3 -ErrorAction SilentlyContinue)) { Write-Host 'BF3 did not start'; exit 1 }
Start-Sleep $MenuWait
& powershell -NoProfile -ExecutionPolicy Bypass -File $tool -Action shot -Name resume_menu | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File $tool -Action click -X 410 -Y 602 -Name resume_click | Out-Null
$t = 0
do {
    Start-Sleep 3; $t += 3
    $line = Get-Content $log | Select-String 'FOV: source' | Select-Object -Last 1
} while ($t -lt 150 -and -not ($line -match 'render view 100'))
Write-Host "gameplay after $t s: $line"
