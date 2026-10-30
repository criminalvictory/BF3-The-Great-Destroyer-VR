# Lists every place a piece of ASCII text appears in BF3's writable memory, with the dword just
# before the object it may sit in (to tell game objects by their type). Read-only.
# Run with the 32-bit PowerShell.  tools\findtext.ps1 -Text DESKTOP-HC4UTGH
#   -All also searches read-only memory (the game's own constant data), -Context N prints N
#   characters around each place.
param([Parameter(Mandatory)] [string]$Text, [switch]$All, [int]$Context = 0)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FindText {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    public static bool All = false;
    public static string Around(IntPtr h, long a, int n) {
        byte[] b = new byte[2 * n]; int r;
        if (!ReadProcessMemory(h, Ptr(a - n), b, b.Length, out r)) return "";
        var s = new System.Text.StringBuilder();
        foreach (byte c in b) s.Append(c >= 32 && c < 127 ? (char)c : '.');
        return s.ToString();
    }
    public static List<long> Find(IntPtr h, byte[] needle) {
        var hits = new List<long>();
        long a = 0x10000; MBI m; byte[] buf = new byte[1 << 20];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;
            bool readable = (m.Protect & 0xEE) != 0;
            if (m.State == 0x1000 && (All ? readable : writable) && (m.Protect & 0x100) == 0) {
                for (long off = 0; off < size; off += buf.Length - 64) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    for (int i = 0; i + needle.Length <= r; ++i) {
                        int k = 0;
                        while (k < needle.Length && buf[i + k] == needle[k]) ++k;
                        if (k == needle.Length) hits.Add(start + off + i);
                    }
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
$h = [FindText]::OpenProcess(0x0410, $false, $game.Id)
[FindText]::All = [bool]$All
$hits = [FindText]::Find($h, [Text.Encoding]::ASCII.GetBytes($Text))
foreach ($a in ($hits | Sort-Object -Unique)) {
    if ($Context) { Write-Host ('{0:X8}  {1}' -f $a, [FindText]::Around($h, $a, $Context)) }
    else { Write-Host ('{0:X8}  dword at -0x28: {1:X8}' -f $a, [FindText]::ReadU($h, $a - 0x28)) }
}
Write-Host "$($hits.Count) places"
