# For a field entry of BF3's reflection tables (as fields.ps1 prints them), finds the type that
# owns it: the nearest field-list start at or before it that a type info points to (its name is
# 0x14 bytes before that pointer). Read-only. Run with the 32-bit PowerShell.
#   tools\owner.ps1 -Field 02432768
param([Parameter(Mandatory)] [string]$Field, [int]$MaxBack = 120)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Owner {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    public static byte[] Read(IntPtr h, long a, int n) { byte[] b = new byte[n]; int r; ReadProcessMemory(h, new IntPtr(unchecked((int)(uint)a)), b, n, out r); return b; }
    public static uint U(IntPtr h, long a) { return BitConverter.ToUInt32(Read(h, a, 4), 0); }
    public static string Text(IntPtr h, long a) {
        if (a < 0x400000 || a > 0x2600000) return null;
        byte[] b = Read(h, a, 64); int n = Array.IndexOf(b, (byte)0); if (n <= 0) return null;
        for (int i = 0; i < n; ++i) if (b[i] < 32 || b[i] > 126) return null;
        return System.Text.Encoding.ASCII.GetString(b, 0, n);
    }
    // Where in bf3.exe's image each value between lo and hi (a multiple of 4 apart from lo... any) is held.
    public static Dictionary<uint, List<long>> Refs(IntPtr h, long start, long end, uint lo, uint hi) {
        var hits = new Dictionary<uint, List<long>>();
        const int chunk = 1 << 20;
        for (long a = start; a < end; a += chunk) {
            byte[] b = Read(h, a, chunk);
            for (int i = 0; i + 4 <= chunk; i += 4) {
                uint v = BitConverter.ToUInt32(b, i);
                if (v < lo || v > hi) continue;
                List<long> l; if (!hits.TryGetValue(v, out l)) hits[v] = l = new List<long>();
                l.Add(a + i);
            }
        }
        return hits;
    }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Owner]::OpenProcess(0x0410, $false, $game.Id)
$f = [Convert]::ToInt64($Field, 16)
$refs = [Owner]::Refs($h, 0x00400000, 0x02600000, [uint32]($f - 12 * $MaxBack), [uint32]$f)
for ($k = 0; $k -le $MaxBack; ++$k) {
    $start = [uint32]($f - 12 * $k)
    if (-not $refs.ContainsKey($start)) { continue }
    foreach ($r in $refs[$start]) {
        $info = $r - 0x14
        $name = [Owner]::Text($h, [Owner]::U($h, $info))
        if ($name) { Write-Host ('field {0:X8} is number {1} of the {2} type (field list {3:X8}, type info {4:X8})' -f $f, $k, $name, $start, $info); exit 0 }
    }
}
Write-Host 'no owner found'
