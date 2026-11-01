# After Battlelog's PLAY CAMPAIGN: waits for bf3.exe and its menu, clicks RESUME CAMPAIGN and waits
# until gameplay has started (a new log line with the game's own 55-degree view and the render view
# in use). The menu keeps its size in pixels around the window's centre, so the button is at
# (centre x - 350, centre y - 198) of the window's client area as it is at the time (720,882 at
# 2160 square; 1810,882 in stereo, twice as wide). With the window fitted to the monitor ([window] fit), the
# game still draws, and lays the menu out, at the size it's told (the mod's log says which) and the window shows it
# scaled: so is the button's place. With -Stereo, F8 afterwards: the headset's view
# (only once gameplay runs: pressed in the menu, the window doubled and the click missed).
# Takes screenshots on the way (screenshots\resume_*.png).
param([int]$MenuWait = 32, [switch]$Stereo)

$root = Split-Path $PSScriptRoot
$tool = Join-Path $PSScriptRoot 'drawhunt.ps1'
$log = Join-Path $root 'build\bf3vr.log'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ResumeWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
}
'@
# RESUME CAMPAIGN's place in the window's client area as it is: in the game's own pixels around the middle of the
# size it draws at, scaled into the window when it's fitted to the monitor (the mod's last "fitted" line).
function ButtonAt($client) {
    $drawW = $client.R; $drawH = $client.B
    $fit = Get-Content $log | Select-String 'Window: fitted to the monitor, .* the game still draws (\d+)x(\d+)|Window: as big as the game draws again' | Select-Object -Last 1
    if ($fit -and $fit.Matches[0].Groups[1].Success) { $drawW = [int]$fit.Matches[0].Groups[1].Value; $drawH = [int]$fit.Matches[0].Groups[2].Value }
    if ($drawW -le 0 -or $drawH -le 0) { $drawW = $client.R; $drawH = $client.B }
    $script:x = [int](($drawW / 2 - 350) * $client.R / $drawW); $script:y = [int](($drawH / 2 - 198) * $client.B / $drawH)
}
$t = 0
while (-not (Get-Process bf3 -ErrorAction SilentlyContinue) -and $t -lt 90) { Start-Sleep 1; $t++ }
if (-not (Get-Process bf3 -ErrorAction SilentlyContinue)) { Write-Host 'BF3 did not start'; exit 1 }
Start-Sleep $MenuWait
& powershell -NoProfile -ExecutionPolicy Bypass -File $tool -Action shot -Name resume_menu | Out-Null
$window = (Get-Process bf3).MainWindowHandle
$client = New-Object ResumeWin+RECT
[void][ResumeWin]::GetClientRect($window, [ref]$client)
ButtonAt $client
$before = (Get-Content $log).Count
& powershell -NoProfile -ExecutionPolicy Bypass -File $tool -Action click -X $x -Y $y -Name resume_click | Out-Null
$t = 0
do {
    Start-Sleep 3; $t += 3
    # The game's own view in use, the player's gun found (the FOV line doesn't always come), or the mod's own
    # switch to the headset's view as the player's soldier comes in: a level opening on a scripted ride (Comrades'
    # drive, 2026-11-01) has neither of the others, and the clicks every 24 s went on into the level.
    $line = Get-Content $log | Select-Object -Skip $before | Select-String 'FOV: source 55\.0, render view 100|Ammo: the gun in hand|Mode -> stereo \(the player' | Select-Object -Last 1
    # The click doesn't always take (2026-10-30 23:00: the menu stayed): clicked again every 24 s.
    if (-not $line -and $t % 24 -eq 0 -and $t -le 96) {
        # The window may have grown since (it opened at 1280x720 once, 2026-10-30 23:59): measured again.
        [void][ResumeWin]::GetClientRect((Get-Process bf3).MainWindowHandle, [ref]$client)
        ButtonAt $client
        & powershell -NoProfile -ExecutionPolicy Bypass -File $tool -Action click -X $x -Y $y -Name resume_click | Out-Null
    }
} while ($t -lt 180 -and -not $line)
if (-not $line) { Write-Host "no gameplay after $t s (clicked $x,$y in a $($client.R)x$($client.B) window; see screenshots\resume_click.png)"; exit 1 }
Write-Host "gameplay after $t s (clicked $x,$y): $line"
if ($Stereo) {
    Start-Sleep 3
    # The mod switches to the headset's view by itself as the soldier comes in: F8 then would switch it back to the
    # floating screen (the review, 2026-11-01). The mode now is the log's last word on it.
    $mode = Get-Content $log | Select-String 'Config: mode=(\w+)|Mode -> (stereo|theater)' | Select-Object -Last 1
    $now = if ($mode) { @($mode.Matches[0].Groups[1].Value, $mode.Matches[0].Groups[2].Value) -ne '' | Select-Object -First 1 } else { '' }
    if ($now -eq 'stereo') { Write-Host 'already in stereo (the mod switched by itself): no F8' }
    else {
        $before = (Get-Content $log).Count
        & powershell -NoProfile -ExecutionPolicy Bypass -File $tool -Action f8 | Out-Null
        Start-Sleep 5
        Get-Content $log | Select-Object -Skip $before | Select-String 'Mode ->|Window: resizing|Window: fitted to the monitor|Window: as big as the game draws' | Select-Object -First 3 | ForEach-Object { Write-Host $_.Line }
    }
}
