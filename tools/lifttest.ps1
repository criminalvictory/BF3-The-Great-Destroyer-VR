# Runs the skinning lift test in game without a player: walks a little, switches the lift on
# (Insert), takes screenshots before and after, walks again, switches it off. Only ever sends
# input while BF3's window is in front, so nothing can be typed into another program.
# Usage: powershell -ExecutionPolicy Bypass -File tools\lifttest.ps1

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace LiftTest -Name U -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public System.IntPtr dwExtraInfo; }
[StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public System.IntPtr dwExtraInfo; }
[StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; [FieldOffset(8)] public MOUSEINPUT mi; }
[DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint f);
public struct RECT { public int L, T, R, B; }
public static void Key(ushort vk, ushort scan, bool down) {
    var i = new INPUT[1]; i[0].type = 1; i[0].ki.wVk = vk; i[0].ki.wScan = scan;
    i[0].ki.dwFlags = (scan != 0 ? 0x8u : 0u) | (down ? 0u : 0x2u);
    SendInput(1, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
public static void MouseMove(int dx, int dy) {
    var i = new INPUT[1]; i[0].type = 0; i[0].mi.dx = dx; i[0].mi.dy = dy; i[0].mi.dwFlags = 0x1;
    SendInput(1, i, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
}
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$hwnd = $game.MainWindowHandle
$outDir = Join-Path (Split-Path $PSScriptRoot) 'screenshots'
New-Item -ItemType Directory -Force $outDir | Out-Null

function GameInFront {
    $procId = [uint32]0
    [void][LiftTest.U]::GetWindowThreadProcessId([LiftTest.U]::GetForegroundWindow(), [ref]$procId)
    return $procId -eq $game.Id
}
function Require-Front {
    if (-not (GameInFront)) { Write-Host 'BF3 is not the focused window: stopping, no more input sent.'; exit 2 }
}
function Shot($name) {
    $r = New-Object LiftTest.U+RECT
    [void][LiftTest.U]::GetClientRect($hwnd, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc()
    [void][LiftTest.U]::PrintWindow($hwnd, $dc, 3)
    $g.ReleaseHdc($dc); $g.Dispose()
    $path = Join-Path $outDir "lift_$name.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Write-Host "shot $path"
}
function Walk($ms) {
    Require-Front
    [LiftTest.U]::Key(0, 0x11, $true)   # W
    Start-Sleep -Milliseconds $ms
    [LiftTest.U]::Key(0, 0x11, $false)
}
function Turn($dx) {
    Require-Front
    for ($i = 0; $i -lt 10; $i++) { [LiftTest.U]::MouseMove([int]($dx / 10), 0); Start-Sleep -Milliseconds 20 }
}
function Press-Insert {
    Require-Front
    [LiftTest.U]::Key(0x2D, 0, $true); Start-Sleep -Milliseconds 120; [LiftTest.U]::Key(0x2D, 0, $false)
}

Require-Front
Write-Host 'Walking and turning to feed the log...'
Walk 1500; Turn 300; Walk 1500; Turn -300; Start-Sleep -Milliseconds 800
Shot 'before'
Press-Insert
Start-Sleep -Milliseconds 1500
Shot 'lifted'
Write-Host 'Walking with the lift on...'
Walk 1200; Turn 200; Start-Sleep -Milliseconds 800
Shot 'lifted_after_walk'
Press-Insert
Start-Sleep -Milliseconds 1500
Shot 'after'
Write-Host 'Done.'
