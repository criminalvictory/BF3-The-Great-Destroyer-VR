# Lists every place a 4-byte value (a pointer, usually) sits in BF3's writable memory, with the
# dword -Back bytes before it: when objects hold a pointer at a known offset, that is their type.
# Read-only. Run with the 32-bit PowerShell.
#   tools\findref.ps1 -Value 2A0B1C40 -Back 0x14
param([Parameter(Mandatory)] [string]$Value, [int]$Back = 0x14, [int]$Max = 200)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FindRef {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    public static List<long> Find(IntPtr h, uint value) {
        var hits = new List<long>();
        long a = 0x10000; MBI m; byte[] buf = new byte[1 << 20];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;
            if (m.State == 0x1000 && writable && (m.Protect & 0x100) == 0) {
                for (long off = 0; off < size; off += buf.Length) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    for (int i = 0; i + 4 <= r; i += 4)
                        if (BitConverter.ToUInt32(buf, i) == value) hits.Add(start + off + i);
                }
            }
            a = start + size;
        }
        return hits;
    }
    public static uint ReadU(IntPtr h, long a) { byte[] b = new byte[4]; int r; ReadProcessMemory(h, Ptr(a), b, 4, out r); return BitConverter.ToUInt32(b, 0); }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [FindRef]::OpenProcess(0x0410, $false, $game.Id)
$hits = [FindRef]::Find($h, [Convert]::ToUInt32($Value, 16))
$shown = 0
foreach ($a in $hits) {
    if (++$shown -gt $Max) { break }
    $type = [FindRef]::ReadU($h, $a - $Back)
    $mark = if ($type -ge 0x00400000 -and $type -lt 0x02600000) { '  (bf3.exe)' } else { '' }
    Write-Host ('{0:X8}  at -0x{1:X}: {2:X8}{3}' -f $a, $Back, $type, $mark)
}
Write-Host "$($hits.Count) places"
