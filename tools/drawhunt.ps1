# One step of the draw hunt (skin.cpp), then a screenshot to judge it by.
#   -Action start     : begin the hunt (F15)
#   -Action vanished  : the arms disappeared in the last screenshot (F13)
#   -Action visible   : the arms were still there (F14)
#   -Action shot      : just take a screenshot
#   -Action flip      : F1, gun follows hand on/off (off keeps the arms centred in view)
#   -Action insert    : Insert, hand shift on/off
# Only sends input while BF3's window is in front.
param(
    [Parameter(Mandatory)] [ValidateSet('start', 'vanished', 'visible', 'shot', 'flip', 'insert')] [string]$Action,
    [string]$Name = 'hunt'
)

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace DrawHunt -Name U -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public System.IntPtr dwExtraInfo; }
[StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public System.IntPtr dwExtraInfo; }
[StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; [FieldOffset(8)] public MOUSEINPUT mi; }
[DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint f);
public struct RECT { public int L, T, R, B; }
public static void Key(ushort vk, bool down) {
    var i = new INPUT[1]; i[0].type = 1; i[0].ki.wVk = vk; i[0].ki.dwFlags = down ? 0u : 0x2u;
    SendInput(1, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
[void](New-Object -ComObject WScript.Shell).AppActivate($game.Id)
Start-Sleep -Milliseconds 300
$procId = [uint32]0
[void][DrawHunt.U]::GetWindowThreadProcessId([DrawHunt.U]::GetForegroundWindow(), [ref]$procId)
if ($procId -ne $game.Id) { Write-Host 'BF3 is not the focused window: no input sent.'; exit 2 }

$keys = @{ start = 0x7E; vanished = 0x7C; visible = 0x7D; flip = 0x70; insert = 0x2D }
if ($keys.ContainsKey($Action)) {
    [DrawHunt.U]::Key($keys[$Action], $true); Start-Sleep -Milliseconds 120; [DrawHunt.U]::Key($keys[$Action], $false)
    Start-Sleep -Milliseconds 1200
}

$hwnd = $game.MainWindowHandle
$r = New-Object DrawHunt.U+RECT
[void][DrawHunt.U]::GetClientRect($hwnd, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][DrawHunt.U]::PrintWindow($hwnd, $dc, 3)
$g.ReleaseHdc($dc); $g.Dispose()
# Half size is plenty to see whether the arms are there.
$small = New-Object System.Drawing.Bitmap $bmp, ([int]($bmp.Width / 2)), ([int]($bmp.Height / 2))
$dir = Join-Path (Split-Path $PSScriptRoot) 'screenshots'
New-Item -ItemType Directory -Force $dir | Out-Null
$path = Join-Path $dir "$Name.png"
$small.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$small.Dispose(); $bmp.Dispose()
Write-Host "shot $path"
