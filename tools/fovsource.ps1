# Finds where BF3 copies its render field of view from. The render view's FOV (+0x78 in the
# object at bf3.exe+1EE8D14 -> 738) is rewritten every frame, so writing it does nothing. This
# scans the game's memory for every copy of that exact value (radians) and of the same angle in
# degrees, then write-tests each one: changes it briefly and checks whether the render view's FOV
# follows. Every copy gets its own value back straight after. Campaign testing only.
# Crashed BF3 once (2026-10-30): it then rewrote each copy for 120 ms, and some copies were
# scratch space on a thread's stack, reused for a return address meanwhile; the game jumped to
# the written float. Now each copy is written once, only if it hasn't changed over a second (so
# not a stack or a per-frame value), and put back only if it still holds the test value.
# Run with the 32-bit PowerShell (C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe).
param(
    [switch]$ScanOnly,
    [string]$Below = 'FFFFFFFF'  # hex: only test copies below this address (the game's own data first)
)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class FovScan {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);

    public static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }

    public static float ReadFloat(IntPtr h, long a) {
        byte[] b = new byte[4]; int r;
        ReadProcessMemory(h, Ptr(a), b, 4, out r);
        return BitConverter.ToSingle(b, 0);
    }
    public static uint ReadUInt(IntPtr h, long a) {
        byte[] b = new byte[4]; int r;
        ReadProcessMemory(h, Ptr(a), b, 4, out r);
        return BitConverter.ToUInt32(b, 0);
    }
    public static void WriteFloat(IntPtr h, long a, float v) {
        int r; WriteProcessMemory(h, Ptr(a), BitConverter.GetBytes(v), 4, out r);
    }

    // Every 4-byte-aligned float in writable memory within `tolerance` of one of `values`.
    public static List<long> Find(IntPtr h, float[] values, float tolerance) {
        var hits = new List<long>();
        long a = 0x10000;
        MBI m;
        byte[] buf = new byte[1 << 20];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;  // READWRITE, EXECUTE_READWRITE
            if (m.State == 0x1000 && writable && (m.Protect & 0x100) == 0) {  // committed, not guard
                for (long off = 0; off < size; off += buf.Length) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    for (int i = 0; i + 4 <= r; i += 4) {
                        float f = BitConverter.ToSingle(buf, i);
                        foreach (float v in values)
                            if (Math.Abs(f - v) <= tolerance) { hits.Add(start + off + i); break; }
                    }
                }
            }
            a = start + size;
        }
        return hits;
    }
}
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [FovScan]::OpenProcess(0x0438, $false, $game.Id)
$module = $game.MainModule.BaseAddress.ToInt64()
$view = [FovScan]::ReadUInt($h, [FovScan]::ReadUInt($h, $module + 0x1EE8D14) + 0x738)
$fovAddress = $view + 0x78
$fov = [FovScan]::ReadFloat($h, $fovAddress)
$degrees = [float]($fov * 180.0 / [Math]::PI)
Write-Host ('render view at {0:X8}; FOV {1:0.#####} rad = {2:0.###} deg' -f $view, $fov, $degrees)

$hits = [FovScan]::Find($h, @($fov, $degrees), 0.000001)  # exact copies only
Write-Host "$($hits.Count) copies found"
$own = @(($view + 0x78), ($view + 0x7C), ($view + 0x220), ($view + 0x4F8), ($view + 0x4FC))
$limit = [Convert]::ToInt64($Below, 16)
$candidates = $hits | Where-Object { $own -notcontains $_ -and $_ -lt $limit }
# Only copies whose surroundings (64 bytes) hold still for a second: data, not stacks.
function Read-Block([long]$a) {
    $b = New-Object byte[] 64; $r = 0
    [void][FovScan]::ReadProcessMemory($h, [FovScan]::Ptr($a - 32), $b, 64, [ref]$r)
    [Convert]::ToBase64String($b)
}
$before = @{}
foreach ($a in $candidates) { $before[$a] = Read-Block $a }
Start-Sleep -Seconds 1
$candidates = $candidates | Where-Object { (Read-Block $_) -eq $before[$_] }
Write-Host "$($candidates.Count) of them hold still"
if ($ScanOnly) { $candidates | ForEach-Object { '{0:X8}  {1:0.#####}' -f $_, [FovScan]::ReadFloat($h, $_) }; exit 0 }

$found = @()
foreach ($a in $candidates) {
    # The last test's value must be gone from the render view first, or it looks like a follow.
    $settle = [Diagnostics.Stopwatch]::StartNew()
    while ([Math]::Abs([FovScan]::ReadFloat($h, $fovAddress) - $fov) -gt 0.0001 -and $settle.ElapsedMilliseconds -lt 500) {
        Start-Sleep -Milliseconds 5
    }
    $old = [FovScan]::ReadFloat($h, $a)
    $test = if ([Math]::Abs($old - $degrees) -lt 0.01) { [float]70.0 } else { [float]1.2 }
    if ([FovScan]::ReadFloat($h, $a) -ne $old) { continue }
    [FovScan]::WriteFloat($h, $a, $test)  # once
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $followed = $false
    while ($sw.ElapsedMilliseconds -lt 120) {
        if ([Math]::Abs([FovScan]::ReadFloat($h, $fovAddress) - $fov) -gt 0.01) { $followed = $true; break }
    }
    if ([FovScan]::ReadFloat($h, $a) -eq $test) { [FovScan]::WriteFloat($h, $a, $old) }
    if ($followed) {
        $now = [FovScan]::ReadFloat($h, $fovAddress)
        $where = if ($a -ge $module -and $a -lt $module + 0x3000000) { 'bf3.exe+{0:X}' -f ($a - $module) } else { '' }
        Write-Host ('SOURCE? {0:X8} {1} (held {2:0.###}: render FOV went to {3:0.#####})' -f $a, $where, $test, $now)
        $found += $a
    }
}
Write-Host "$($candidates.Count) copies tested, $($found.Count) moved the render FOV"
Start-Sleep -Milliseconds 200
Write-Host ('render FOV now {0:0.#####}' -f [FovScan]::ReadFloat($h, $fovAddress))
