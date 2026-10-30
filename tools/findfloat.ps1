# Lists every float between -Lo and -Hi in BF3's writable memory, with the dword before its
# object if one sits at -Back. Read-only. Run with the 32-bit PowerShell.
#   tools\findfloat.ps1 -Lo 90000 -Hi 99999.99
param([float]$Lo, [float]$Hi, [int]$Max = 300)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FindFloat {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    public static List<long> Hits = new List<long>();
    public static List<float> Values = new List<float>();
    public static void Find(IntPtr h, float lo, float hi) {
        long a = 0x10000; MBI m; byte[] buf = new byte[1 << 20];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;
            if (m.State == 0x1000 && writable && (m.Protect & 0x100) == 0) {
                for (long off = 0; off < size; off += buf.Length) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    for (int i = 0; i + 4 <= r; i += 4) {
                        float f = BitConverter.ToSingle(buf, i);
                        if (f >= lo && f <= hi) { Hits.Add(start + off + i); Values.Add(f); }
                    }
                }
            }
            a = start + size;
        }
    }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [FindFloat]::OpenProcess(0x0410, $false, $game.Id)
[FindFloat]::Find($h, $Lo, $Hi)
for ($i = 0; $i -lt [Math]::Min($Max, [FindFloat]::Hits.Count); ++$i) { Write-Host ('{0:X8}  {1}' -f [FindFloat]::Hits[$i], [FindFloat]::Values[$i]) }
Write-Host "$([FindFloat]::Hits.Count) places"
