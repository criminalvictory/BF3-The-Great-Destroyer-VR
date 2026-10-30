# Lists every block in BF3's memory laid out like a render view (as in the render camera's object:
# transform at -0x48 from the FOV, FOV pair, near and far plane, aspect) whose FOV is the given
# angle. With the main view widened, the ones still at the game's 55 degrees are what else BF3
# renders with, such as the first-person gun and arms. Read-only.
# Run with the 32-bit PowerShell (C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe).
#   tools\views.ps1 -Degrees 55
param([float]$Degrees = 55)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Views {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }

    static bool Unit(byte[] b, int o) {
        float x = BitConverter.ToSingle(b, o), y = BitConverter.ToSingle(b, o + 4), z = BitConverter.ToSingle(b, o + 8);
        float w = BitConverter.ToSingle(b, o + 12);
        float l = x * x + y * y + z * z;
        return w == 0 && l > 0.95f && l < 1.05f;
    }

    // Each hit: address of the FOV, then near, far, aspect, camera position.
    public static List<string> Find(IntPtr h, float fov) {
        var lines = new List<string>();
        long a = 0x10000;
        MBI m;
        byte[] buf = new byte[1 << 20];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;
            if (m.State == 0x1000 && writable && (m.Protect & 0x100) == 0) {
                for (long off = 0; off < size; off += buf.Length - 256) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    for (int i = 0x48; i + 0x18 <= r; i += 4) {
                        if (Math.Abs(BitConverter.ToSingle(buf, i) - fov) > 0.00001f) continue;
                        int t = i - 0x48;  // transform: three unit rows, then the position
                        if (!Unit(buf, t) || !Unit(buf, t + 16) || !Unit(buf, t + 32)) continue;
                        float near = BitConverter.ToSingle(buf, i + 8), far = BitConverter.ToSingle(buf, i + 12);
                        float aspect = BitConverter.ToSingle(buf, i + 16);
                        lines.Add(string.Format("{0:X8}  fov pair {1:0.#####} {2:0.#####}  near {3:0.####}  far {4:0.#}  aspect {5:0.####}  at ({6:0.##}, {7:0.##}, {8:0.##})",
                            start + off + i, BitConverter.ToSingle(buf, i), BitConverter.ToSingle(buf, i + 4), near, far, aspect,
                            BitConverter.ToSingle(buf, t + 48), BitConverter.ToSingle(buf, t + 52), BitConverter.ToSingle(buf, t + 56)));
                    }
                }
            }
            a = start + size;
        }
        return lines;
    }
}
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Views]::OpenProcess(0x0410, $false, $game.Id)
$fov = [float]($Degrees * [Math]::PI / 180.0)
Write-Host ('views with FOV {0:0.#####} rad:' -f $fov)
[Views]::Find($h, $fov) | ForEach-Object { $_ }
