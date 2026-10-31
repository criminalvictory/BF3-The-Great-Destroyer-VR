# After Battlelog's PLAY CAMPAIGN (Claude in Chrome): waits for bf3.exe; if Battlelog opened its
# campaign page instead of starting the game (2026-10-30 23:40), brings Chrome to the front and
# clicks the page's yellow LAUNCH CAMPAIGN button, found on the screen by its colour (Chrome's
# info bars move it), then waits again. Then tools\resume.ps1 -Stereo.
#   tools\launch.ps1
param()
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
# The middle of the biggest patch of the button's yellow on the screen, or $null.
function FindButton {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $shot = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($shot)
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $g.Dispose()
    $xs = 0L; $ys = 0L; $n = 0
    for ($y = 0; $y -lt $b.Height; $y += 4) {
        for ($x = 0; $x -lt $b.Width; $x += 4) {
            $c = $shot.GetPixel($x, $y)
            if ($c.R -gt 220 -and $c.G -gt 150 -and $c.G -lt 215 -and $c.B -lt 70) { $xs += $x; $ys += $y; $n++ }
        }
    }
    $shot.Dispose()
    if ($n -lt 40) { return $null }
    return @([int]($xs / $n), [int]($ys / $n))
}
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Launch {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  public static void Click(int x, int y) { SetCursorPos(x, y); System.Threading.Thread.Sleep(150); mouse_event(2, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(80); mouse_event(4, 0, 0, 0, UIntPtr.Zero); }
}
'@
function WaitForGame([int]$seconds) {
    for ($t = 0; $t -lt $seconds; $t++) { if (Get-Process bf3 -ErrorAction SilentlyContinue) { return $true }; Start-Sleep 1 }
    return $false
}
if (WaitForGame 25) { Write-Host 'BF3 started'; exit 0 }
$chrome = Get-Process chrome | Where-Object { $_.MainWindowTitle -like '*Battlelog*' } | Select-Object -First 1
if (-not $chrome) { Write-Host 'no Battlelog window'; exit 1 }
[Launch]::ShowWindow($chrome.MainWindowHandle, 3) | Out-Null
[Launch]::SetForegroundWindow($chrome.MainWindowHandle) | Out-Null
Start-Sleep 2
$at = FindButton
if (-not $at) { Write-Host 'no LAUNCH CAMPAIGN button on the screen'; exit 1 }
[Launch]::Click($at[0], $at[1])
Write-Host "clicked LAUNCH CAMPAIGN at $($at[0]),$($at[1])"
if (WaitForGame 40) { Write-Host 'BF3 started'; exit 0 }
Write-Host 'BF3 did not start'
exit 1
