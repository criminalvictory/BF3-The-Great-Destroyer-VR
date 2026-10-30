# Captures the BF3 window several times a second and saves the frames as one contact sheet,
# so movement can be reviewed like a short clip.
# Usage: powershell -ExecutionPolicy Bypass -File tools\burst.ps1 [-Seconds 10] [-PerSecond 2] [-Delay 5]
param(
    [int]$Seconds = 10,
    [int]$PerSecond = 2,
    [int]$Delay = 5
)

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace BurstCap -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint f);
public struct RECT { public int L, T, R, B; }
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$hwnd = $game.MainWindowHandle
$rect = New-Object BurstCap.U+RECT
[void][BurstCap.U]::GetClientRect($hwnd, [ref]$rect)
$w = $rect.R - $rect.L
$h = $rect.B - $rect.T

# Countdown beeps, then one high beep when capture starts and a low one when it ends.
for ($i = 0; $i -lt $Delay; $i++) { [Console]::Beep(600, 120); Start-Sleep -Milliseconds 880 }
[Console]::Beep(1200, 250)

$count = $Seconds * $PerSecond
$interval = [int](1000 / $PerSecond)
$frames = @()
$times = @()
$start = Get-Date
for ($i = 0; $i -lt $count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc()
    [void][BurstCap.U]::PrintWindow($hwnd, $dc, 3)
    $g.ReleaseHdc($dc)
    $g.Dispose()
    $frames += $bmp
    $times += ((Get-Date) - $start).TotalSeconds
    Start-Sleep -Milliseconds $interval
}
[Console]::Beep(400, 400)

# Contact sheet: 4 columns of thumbnails, each labelled with its time.
$cols = 4
$tw = 400
$th = [int]($h * $tw / $w)
$rows = [math]::Ceiling($count / $cols)
$sheet = New-Object System.Drawing.Bitmap ($cols * $tw), ($rows * $th)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$font = New-Object System.Drawing.Font 'Arial', 14, ([System.Drawing.FontStyle]::Bold)
for ($i = 0; $i -lt $count; $i++) {
    $x = ($i % $cols) * $tw
    $y = [math]::Floor($i / $cols) * $th
    $g.DrawImage($frames[$i], $x, $y, $tw, $th)
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 70, 24)
    $g.DrawString(('{0:N1}s' -f $times[$i]), $font, [System.Drawing.Brushes]::Yellow, ($x + 4), ($y + 2))
    $frames[$i].Dispose()
}
$g.Dispose()

$dir = Join-Path (Split-Path $PSScriptRoot) 'screenshots'
New-Item -ItemType Directory -Force $dir | Out-Null
$out = Join-Path $dir ('burst_{0:HHmmss}.png' -f (Get-Date))
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "Saved $out"
