# Prints BF3's memory as 4-byte values, four to a line, each also as a float where it looks like
# one; values inside bf3.exe are shown as "bf3+offset" (vtables, code, static data). Follows a
# pointer chain from bf3.exe like peek.ps1, or starts at an absolute address. Read-only.
# Run with the 32-bit PowerShell:
#   tools\dwords.ps1 -Chain 1EE8D14,738 -Start -0x40 -Length 0x100
#   tools\dwords.ps1 -Address 2A0B1C40 -Length 0x80
param([string[]]$Chain, [string]$Address, [string]$Start = '0', [int]$Length = 0x100)
# From the command line a chain may come as one "a,b,c" string, and an offset as "-0x40".
if ($Chain.Count -eq 1) { $Chain = $Chain[0] -split ',' }
$digits = $Start.TrimStart('-')
[int]$offset = if ($digits -match '^0x') { [Convert]::ToInt32($digits.Substring(2), 16) } else { [int]$digits }
if ($Start.StartsWith('-')) { $offset = -$offset }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Dwords {
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, int size, out int read);
    public static IntPtr Process;
    public static void Open(int pid) { Process = OpenProcess(0x0410, false, pid); }
    // BF3 is large-address aware: addresses above 2 GB don't fit a signed 32-bit IntPtr directly.
    public static byte[] Read(long address, int size) {
        var b = new byte[size]; int read;
        var at = new IntPtr(unchecked((int)(uint)address));
        return ReadProcessMemory(Process, at, b, size, out read) && read == size ? b : null;
    }
}
'@
$p = Get-Process bf3 -ErrorAction Stop
[Dwords]::Open($p.Id)
$module = $p.MainModule
$base = $module.BaseAddress.ToInt64()
$end = $base + $module.ModuleMemorySize
if ($Address) {
    $at = [Convert]::ToInt64($Address, 16)
} else {
    $at = $base + [Convert]::ToInt64($Chain[0], 16)
    for ($i = 1; $i -lt $Chain.Count; ++$i) {
        $b = [Dwords]::Read($at, 4)
        if (-not $b) { "unreadable at 0x{0:X}" -f $at; exit 1 }
        $at = [BitConverter]::ToUInt32($b, 0) + [Convert]::ToInt64($Chain[$i], 16)
    }
    "chain ends at 0x{0:X}" -f $at
}
$from = $at + $offset
$bytes = [Dwords]::Read($from, $Length)
if (-not $bytes) { "unreadable at 0x{0:X}" -f $from; exit 1 }
for ($o = 0; $o -lt $Length; $o += 16) {
    [int]$rel = $offset + $o
    $sign = if ($rel -lt 0) { '-' } else { '+' }
    $label = $sign + ('{0:X}' -f [Math]::Abs($rel))
    $line = '{0,6} {1:X8}:' -f $label, ($from + $o)
    for ($k = 0; $k -lt 16 -and $o + $k -lt $Length; $k += 4) {
        $v = [BitConverter]::ToUInt32($bytes, $o + $k)
        $f = [BitConverter]::ToSingle($bytes, $o + $k)
        if ($v -ge $base -and $v -lt $end) { $text = 'bf3+{0:X}' -f ($v - $base) }
        elseif ($v -ne 0 -and [Math]::Abs($f) -gt 1e-4 -and [Math]::Abs($f) -lt 1e6) { $text = '{0:X8}({1:G4})' -f $v, $f }
        else { $text = '{0:X8}' -f $v }
        $line += ' {0,-22}' -f $text
    }
    $line
}
