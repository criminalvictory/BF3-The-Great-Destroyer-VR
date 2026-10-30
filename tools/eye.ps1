# Depth 3D tuning by screenshot: writes build\depth3d.txt (if -Tuning is given), waits for the mod
# to pick it up, then saves the desktop window (which mirrors one eye) at full size, an 800x800
# overview, and a crop. Usage:
#   tools\eye.ps1 -Name a -Tuning 'strength 0.7 min 0.45 fill 1 show 0' -Crop 600,500,700,600
param(
    [Parameter(Mandatory)] [string]$Name,
    [string]$Tuning = '',
    [string]$Crop = '450,450,700,700'  # x,y,width,height (-File passes it as one string)
)
$box = @($Crop -split ',' | ForEach-Object { [int]$_ })

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace EyeCap -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint f);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
public struct RECT { public int L, T, R, B; }
'@

$root = Split-Path $PSScriptRoot
if ($Tuning) {
    Set-Content -Path (Join-Path $root 'build\depth3d.txt') -Value $Tuning -Encoding ascii
    Start-Sleep -Milliseconds 900
}
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = $game.MainWindowHandle
if ([EyeCap.U]::IsIconic($h)) { [void][EyeCap.U]::ShowWindow($h, 9); Start-Sleep -Milliseconds 800 }
$r = New-Object EyeCap.U+RECT
[void][EyeCap.U]::GetClientRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][EyeCap.U]::PrintWindow($h, $dc, 3)
$g.ReleaseHdc($dc); $g.Dispose()
$dir = Join-Path $root 'screenshots'
$small = New-Object System.Drawing.Bitmap $bmp, 800, 800
$small.Save((Join-Path $dir "$Name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$x = [Math]::Max(0, [Math]::Min($box[0], $bmp.Width - 1))
$y = [Math]::Max(0, [Math]::Min($box[1], $bmp.Height - 1))
$rect = New-Object System.Drawing.Rectangle $x, $y, ([Math]::Min($box[2], $bmp.Width - $x)), ([Math]::Min($box[3], $bmp.Height - $y))
$part = $bmp.Clone($rect, $bmp.PixelFormat)
$part.Save((Join-Path $dir "${Name}_crop.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$part.Dispose(); $small.Dispose(); $bmp.Dispose()
Write-Host "saved screenshots\$Name.png and ${Name}_crop.png"
