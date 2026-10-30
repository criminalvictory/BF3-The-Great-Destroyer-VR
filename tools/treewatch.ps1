# Finds every float equal to -Value in an object (absolute -Address) and in the objects its
# pointers lead to (-Depth levels), then polls them every 30 ms and prints each change with its
# path, next to the client health. Shows which copy of the health the game really uses while the
# player takes damage. Read-only. Run with the 32-bit PowerShell.
#   tools\treewatch.ps1 -Address E4E47510 -Seconds 60
param(
    [Parameter(Mandatory)] [string]$Address,
    [float]$Value = 100,
    [int]$Size = 0x1000,
    [int]$ChildSize = 0x400,
    [int]$Depth = 2,
    [double]$Seconds = 60,
    [int]$MaxLines = 400
)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Tree {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    public static byte[] Read(IntPtr h, long a, int n) { byte[] b = new byte[n]; int r; return ReadProcessMemory(h, Ptr(a), b, n, out r) && r == n ? b : null; }
    public static List<long> Addresses = new List<long>();
    public static List<string> Paths = new List<string>();
    public static void Walk(IntPtr h, long a, int size, int childSize, int depth, float value, string path, HashSet<long> seen) {
        if (!seen.Add(a)) return;
        byte[] d = Read(h, a, size);
        if (d == null) return;
        for (int i = 0; i + 4 <= size; i += 4) {
            if (BitConverter.ToSingle(d, i) == value) { Addresses.Add(a + i); Paths.Add(path + "+" + i.ToString("X")); }
            if (depth <= 0) continue;
            uint p = BitConverter.ToUInt32(d, i);
            if (p < 0x10000000u || (p & 3) != 0) continue;  // heap objects only (bf3.exe sits low)
            Walk(h, p, childSize, Math.Max(childSize / 2, 0x100), depth - 1, value, path + "+" + i.ToString("X") + "->", seen);
        }
    }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Tree]::OpenProcess(0x0410, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
$root = [Convert]::ToInt64($Address, 16)
$seen = New-Object 'System.Collections.Generic.HashSet[long]'
[Tree]::Walk($h, $root, $Size, $ChildSize, $Depth, $Value, '', $seen)
Write-Host ("{0} values at {1} in {2} objects" -f [Tree]::Addresses.Count, $Value, $seen.Count)
function ReadF([long]$a) { $b = [Tree]::Read($h, $a, 4); if ($b) { [BitConverter]::ToSingle($b, 0) } else { [float]::NaN } }
function ClientHealth() {
    $a = $base + 0x1F80B58
    foreach ($o in 0x30, 0xBC, 0x3D8, 0x164) { $b = [Tree]::Read($h, $a, 4); if (-not $b) { return [float]::NaN }; $a = [BitConverter]::ToUInt32($b, 0) + $o }
    $b = [Tree]::Read($h, $a, 4); if (-not $b) { return [float]::NaN }
    ReadF ([BitConverter]::ToUInt32($b, 0) + 0xB0)
}
$n = [Tree]::Addresses.Count
$last = New-Object float[] $n
for ($i = 0; $i -lt $n; ++$i) { $last[$i] = $Value }
$lastClient = ClientHealth
$lines = 0
$until = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $until -and $lines -lt $MaxLines) {
    $c = ClientHealth
    if ($c -ne $lastClient) { Write-Host ('{0:HH:mm:ss.ff}  client health {1:0.###}' -f (Get-Date), $c); $lastClient = $c; ++$lines }
    for ($i = 0; $i -lt $n; ++$i) {
        $v = ReadF ([Tree]::Addresses[$i])
        if ($v -ne $last[$i] -and -not ([float]::IsNaN($v) -and [float]::IsNaN($last[$i]))) {
            Write-Host ('{0:HH:mm:ss.ff}  {1:X8} {2,-24} {3:0.###} -> {4:0.###}' -f (Get-Date), [Tree]::Addresses[$i], [Tree]::Paths[$i], $last[$i], $v)
            $last[$i] = $v; ++$lines
        }
    }
    Start-Sleep -Milliseconds 30
}
