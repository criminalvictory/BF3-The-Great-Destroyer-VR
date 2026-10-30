# Snapshots every float between -Lo and -Hi in an object (absolute -Address, or the client soldier
# when none is given) and the objects its pointers lead to, a few times -Interval seconds apart,
# and prints those that changed, with their path and values: health climbing back after a hit
# shows up as a steady rise. Read-only. Run with the 32-bit PowerShell.
#   tools\treediff.ps1 -Address D90A9B40 -Snapshots 4 -Interval 1
param(
    [string]$Address = '',
    [float]$Lo = 0.5,
    [float]$Hi = 100000,
    [int]$Size = 0x1000,
    [int]$ChildSize = 0x400,
    [int]$Depth = 2,
    [int]$Snapshots = 4,
    [double]$Interval = 1
)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Diff {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    public static byte[] Read(IntPtr h, long a, int n) { byte[] b = new byte[n]; int r; return ReadProcessMemory(h, Ptr(a), b, n, out r) && r == n ? b : null; }
    public static List<long> Addresses = new List<long>();
    public static List<string> Paths = new List<string>();
    public static void Walk(IntPtr h, long a, int size, int childSize, int depth, float lo, float hi, string path, HashSet<long> seen) {
        if (!seen.Add(a)) return;
        byte[] d = Read(h, a, size);
        if (d == null) return;
        for (int i = 0; i + 4 <= size; i += 4) {
            float f = BitConverter.ToSingle(d, i);
            if (f >= lo && f <= hi) { Addresses.Add(a + i); Paths.Add(path + "+" + i.ToString("X")); }
            if (depth <= 0) continue;
            uint p = BitConverter.ToUInt32(d, i);
            if (p < 0x10000000u || (p & 3) != 0) continue;
            Walk(h, p, childSize, Math.Max(childSize / 2, 0x100), depth - 1, lo, hi, path + "+" + i.ToString("X") + "->", seen);
        }
    }
    public static float[] Snap(IntPtr h) {
        var v = new float[Addresses.Count];
        for (int i = 0; i < v.Length; ++i) { byte[] b = Read(h, Addresses[i], 4); v[i] = b == null ? float.NaN : BitConverter.ToSingle(b, 0); }
        return v;
    }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Diff]::OpenProcess(0x0410, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
if ($Address) { $root = [Convert]::ToInt64($Address, 16) }
else {
    $root = $base + 0x1F80B58
    foreach ($o in 0x30, 0xBC, 0x3D8) { $root = [BitConverter]::ToUInt32([Diff]::Read($h, $root, 4), 0) + $o }
    $root = [BitConverter]::ToUInt32([Diff]::Read($h, $root, 4), 0)
}
$seen = New-Object 'System.Collections.Generic.HashSet[long]'
[Diff]::Walk($h, $root, $Size, $ChildSize, $Depth, $Lo, $Hi, '', $seen)
Write-Host ('{0:X8}: {1} floats in range in {2} objects' -f $root, [Diff]::Addresses.Count, $seen.Count)
$snaps = @()
for ($s = 0; $s -lt $Snapshots; ++$s) {
    if ($s) { Start-Sleep -Milliseconds ([int]($Interval * 1000)) }
    $snaps += ,([Diff]::Snap($h))
}
for ($i = 0; $i -lt [Diff]::Addresses.Count; ++$i) {
    $vals = @($snaps | ForEach-Object { $_[$i] })
    $changed = $false
    for ($s = 1; $s -lt $vals.Count; ++$s) { if ([Math]::Abs($vals[$s] - $vals[0]) -gt 0.001) { $changed = $true } }
    if ($changed) { Write-Host ('{0:X8}  {1,-26} {2}' -f [Diff]::Addresses[$i], [Diff]::Paths[$i], (($vals | ForEach-Object { '{0:0.###}' -f $_ }) -join '  ')) }
}
