# Finds where two 4-byte integers sit close together in BF3's writable memory (the ammo in the
# magazine and the spare ammo, say), or re-checks earlier hits after the values change.
# Read-only. Run with the 32-bit PowerShell.
#   tools\findints.ps1 -First 28 -Second 372 [-Within 16]   (writes the hits to -Out)
#   tools\findints.ps1 -First 25 -Second 372 -Recheck       (keeps the hits that now hold these)
param([Parameter(Mandatory)] [int]$First, [int]$Second = -1, [int]$Within = 16, [switch]$Recheck,
      [string]$Out = (Join-Path $env:TEMP 'bf3_findints.txt'), [int]$Max = 60)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FindInts {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    // Each hit: the first value's address and the second's offset from it (0 = no second value).
    public static List<long[]> Find(IntPtr h, int first, int second, int within) {
        var hits = new List<long[]>();
        long a = 0x10000; MBI m; byte[] buf = new byte[1 << 20];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;
            if (m.State == 0x1000 && writable && (m.Protect & 0x100) == 0) {
                for (long off = 0; off < size; off += buf.Length - 64) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    int limit = (off + buf.Length - 64 < size) ? r - 64 : r;
                    for (int i = 0; i + 4 <= limit; i += 4) {
                        if (BitConverter.ToInt32(buf, i) != first) continue;
                        if (second < 0) { hits.Add(new long[] { start + off + i, 0 }); continue; }
                        for (int d = -within; d <= within; d += 4) {
                            if (d == 0 || i + d < 0 || i + d + 4 > r) continue;
                            if (BitConverter.ToInt32(buf, i + d) == second) { hits.Add(new long[] { start + off + i, d }); break; }
                        }
                    }
                }
            }
            a = start + size;
        }
        return hits;
    }
    public static int ReadI(IntPtr h, long a) { byte[] b = new byte[4]; int r; ReadProcessMemory(h, Ptr(a), b, 4, out r); return BitConverter.ToInt32(b, 0); }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Output 'BF3 is not running.'; exit 1 }
$h = [FindInts]::OpenProcess(0x0410, $false, $game.Id)
$kept = @()
if ($Recheck) {
    foreach ($line in Get-Content $Out) {
        $parts = $line.Split(' ')
        $a = [Convert]::ToInt64($parts[0], 16); $d = [int]$parts[1]
        if ([FindInts]::ReadI($h, $a) -ne $First) { continue }
        if ($Second -ge 0 -and $d -ne 0 -and [FindInts]::ReadI($h, $a + $d) -ne $Second) { continue }
        $kept += $line
    }
} else {
    foreach ($hit in [FindInts]::Find($h, $First, $Second, $Within)) { $kept += ('{0:X8} {1}' -f $hit[0], $hit[1]) }
}
Set-Content $Out $kept -Encoding ascii
$shown = 0
foreach ($line in $kept) {
    if (++$shown -gt $Max) { break }
    $parts = $line.Split(' ')
    Write-Output ('{0}  second at {1:+0;-0}' -f $parts[0], [int]$parts[1])
}
Write-Output "$($kept.Count) places"
