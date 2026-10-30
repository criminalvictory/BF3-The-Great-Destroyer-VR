# Finds code in bf3.exe that names a fixed address: bf3.exe has no address randomisation, so an
# instruction touching a global holds its absolute address as a 4-byte constant. Searches the
# running game's code (the file on disk is wrapped and doesn't hold the real code). Read-only.
# Run with the 32-bit PowerShell (C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe).
#   tools\coderefs.ps1 -Address 0238B4F8,0238B480
param([Parameter(Mandatory)] [string[]]$Address)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Refs {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    public static byte[] Read(IntPtr h, long address, int size) {
        byte[] all = new byte[size];
        const int chunk = 1 << 16;
        for (int off = 0; off < size; off += chunk) {
            int n = Math.Min(chunk, size - off), r;
            byte[] b = new byte[n];
            if (ReadProcessMemory(h, new IntPtr(unchecked((int)(uint)(address + off))), b, n, out r))
                Buffer.BlockCopy(b, 0, all, off, r);
        }
        return all;
    }
    public static List<int> Find(byte[] data, uint value) {
        var hits = new List<int>();
        byte b0 = (byte)value, b1 = (byte)(value >> 8), b2 = (byte)(value >> 16), b3 = (byte)(value >> 24);
        for (int i = 0; i + 4 <= data.Length; ++i)
            if (data[i] == b0 && data[i + 1] == b1 && data[i + 2] == b2 && data[i + 3] == b3) hits.Add(i);
        return hits;
    }
}
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Refs]::OpenProcess(0x0410, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
# The executable sections, from the loaded image's own headers.
$head = [Refs]::Read($h, $base, 0x1000)
$pe = [BitConverter]::ToInt32($head, 0x3C)
$count = [BitConverter]::ToUInt16($head, $pe + 6)
$table = $pe + 24 + [BitConverter]::ToUInt16($head, $pe + 20)
$Address = @($Address | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
for ($s = 0; $s -lt $count; ++$s) {
    $o = $table + $s * 40
    if (-not ([BitConverter]::ToUInt32($head, $o + 36) -band 0x20000000)) { continue }
    $name = [Text.Encoding]::ASCII.GetString($head, $o, 8).TrimEnd([char]0)
    $va = [BitConverter]::ToUInt32($head, $o + 12)
    $size = [BitConverter]::ToUInt32($head, $o + 8)
    $code = [Refs]::Read($h, $base + $va, $size)
    foreach ($a in $Address) {
        foreach ($i in [Refs]::Find($code, [Convert]::ToUInt32($a, 16))) {
            '{0} in {1}: constant at bf3.exe+{2:X}' -f $a, $name, ($va + $i)
        }
    }
}
