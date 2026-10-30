# One step of the draw hunt (skin.cpp), then a screenshot to judge it by.
#   -Action start     : begin the hunt (F15)
#   -Action vanished  : the arms disappeared in the last screenshot (F13)
#   -Action visible   : the arms were still there (F14)
#   -Action shot      : just take a screenshot
#   -Action flip      : F1, gun follows hand on/off (off keeps the arms centred in view)
#   -Action insert    : Insert, hand shift on/off
#   -Action delete    : Delete, wider render FOV on/off; pageup / pagedown change it
# Only sends input while BF3's window is in front.
#   -Action click -X x -Y y : left click at a point of the game window (full-size pixels)
#   -Action enter     : Enter
#   -Action f17       : F17, 3 s of turning as the right stick does (the turn's evenness is logged)
#   -Action fire -Seconds s : hold the left mouse button (fire) for s seconds
#   -Action hold -Vk k -Seconds s : hold a game key (by scan code) for s seconds
# A minimized game window is restored first.
param(
    [Parameter(Mandatory)] [ValidateSet('start', 'vanished', 'visible', 'shot', 'flip', 'insert', 'delete', 'pageup', 'pagedown', 'f11', 'f8', 'click', 'enter', 'f16', 'f17', 'pause', 'scroll', 'end', 'hold', 'fire')] [string]$Action,
    [string]$Name = 'hunt',
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Vk = 0x57,          # -Action hold: the key (default W, walk forward)
    [double]$Seconds = 5
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
[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern bool ClientToScreen(System.IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
public struct RECT { public int L, T, R, B; }
public struct POINT { public int X, Y; }
public static void Click() {
    var i = new INPUT[2];
    i[0].type = 0; i[0].mi.dwFlags = 0x0002;  // left down
    i[1].type = 0; i[1].mi.dwFlags = 0x0004;  // left up
    SendInput(2, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
// The game polls the buttons once a frame: a press and release sent together is missed.
public static void Button(bool down) {
    var i = new INPUT[1]; i[0].type = 0; i[0].mi.dwFlags = down ? 0x0002u : 0x0004u;
    SendInput(1, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
public static void Key(ushort vk, bool down) {
    var i = new INPUT[1]; i[0].type = 1; i[0].ki.wVk = vk; i[0].ki.dwFlags = down ? 0u : 0x2u;
    SendInput(1, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
// The game itself reads the keyboard by scan code: a key sent by its virtual-key code alone
// reaches our hooks but not the game.
public static void ScanKey(ushort vk, bool down) {
    var i = new INPUT[1]; i[0].type = 1; i[0].ki.wScan = (ushort)MapVirtualKey(vk, 0);
    i[0].ki.dwFlags = 0x8u | (down ? 0u : 0x2u);
    SendInput(1, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
if ([DrawHunt.U]::IsIconic($game.MainWindowHandle)) {
    [void][DrawHunt.U]::ShowWindow($game.MainWindowHandle, 9)  # SW_RESTORE
    Start-Sleep -Milliseconds 800
}
[void](New-Object -ComObject WScript.Shell).AppActivate($game.Id)
Start-Sleep -Milliseconds 300
$procId = [uint32]0
[void][DrawHunt.U]::GetWindowThreadProcessId([DrawHunt.U]::GetForegroundWindow(), [ref]$procId)
if ($procId -ne $game.Id) { Write-Host 'BF3 is not the focused window: no input sent.'; exit 2 }

$keys = @{ start = 0x7E; vanished = 0x7C; visible = 0x7D; flip = 0x70; insert = 0x2D; delete = 0x2E; pageup = 0x21; pagedown = 0x22; f11 = 0x7A; f8 = 0x77; enter = 0x0D; f16 = 0x7F; f17 = 0x80; pause = 0x13; scroll = 0x91; end = 0x23 }
if ($keys.ContainsKey($Action)) {
    [DrawHunt.U]::Key($keys[$Action], $true); Start-Sleep -Milliseconds 120; [DrawHunt.U]::Key($keys[$Action], $false)
    Start-Sleep -Milliseconds 1200
}
if ($Action -eq 'hold') {
    [DrawHunt.U]::ScanKey([uint16]$Vk, $true); Start-Sleep -Milliseconds ([int]($Seconds * 1000)); [DrawHunt.U]::ScanKey([uint16]$Vk, $false)
    Start-Sleep -Milliseconds 1200
}
if ($Action -eq 'fire') {
    [DrawHunt.U]::Button($true); Start-Sleep -Milliseconds ([int]($Seconds * 1000)); [DrawHunt.U]::Button($false)
    Start-Sleep -Milliseconds 1200
}
if ($Action -eq 'click') {
    $pt = New-Object DrawHunt.U+POINT
    $pt.X = $X; $pt.Y = $Y
    [void][DrawHunt.U]::ClientToScreen($game.MainWindowHandle, [ref]$pt)
    [void][DrawHunt.U]::SetCursorPos($pt.X, $pt.Y)
    Start-Sleep -Milliseconds 150
    [DrawHunt.U]::Click()
    Start-Sleep -Milliseconds 1500
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
