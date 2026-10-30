# Saves bf3.exe as it is in memory (its code is only decrypted there: the file on disk is wrapped)
# as a PE file whose sections sit at their memory offsets, so dumpbin can disassemble it:
#   dumpbin /disasm:nobytes /range:0x...,0x... build\bf3_mem.exe
# Unreadable pages are left zero. Read-only for the game. Run with the 32-bit PowerShell.
param([string]$Out = (Join-Path (Split-Path $PSScriptRoot) 'build\bf3_mem.exe'))
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Dump {
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, int size, out int read);
    public static byte[] Image(int pid, long baseAddress, int size) {
        IntPtr process = OpenProcess(0x0410, false, pid);
        var image = new byte[size];
        var page = new byte[4096];
        for (int o = 0; o < size; o += 4096) {
            int read;
            var at = new IntPtr(unchecked((int)(uint)(baseAddress + o)));
            if (ReadProcessMemory(process, at, page, 4096, out read) && read == 4096)
                Buffer.BlockCopy(page, 0, image, o, 4096);
        }
        return image;
    }
}
'@
$p = Get-Process bf3 -ErrorAction Stop
$base = $p.MainModule.BaseAddress.ToInt64()
$size = $p.MainModule.ModuleMemorySize
$image = [Dump]::Image($p.Id, $base, $size)
# Section headers: raw data at the memory offset, as long as the section is in memory.
$pe = [BitConverter]::ToInt32($image, 0x3C)
$sections = [BitConverter]::ToUInt16($image, $pe + 6)
$optional = [BitConverter]::ToUInt16($image, $pe + 20)
$table = $pe + 24 + $optional
for ($s = 0; $s -lt $sections; ++$s) {
    $h = $table + $s * 40
    $name = [Text.Encoding]::ASCII.GetString($image, $h, 8).TrimEnd([char]0)
    $virtualSize = [BitConverter]::ToUInt32($image, $h + 8)
    $rva = [BitConverter]::ToUInt32($image, $h + 12)
    [BitConverter]::GetBytes([uint32]$virtualSize).CopyTo($image, $h + 16)  # SizeOfRawData
    [BitConverter]::GetBytes([uint32]$rva).CopyTo($image, $h + 20)          # PointerToRawData
    '{0,-8} at 0x{1:X8} size 0x{2:X}' -f $name, ($base + $rva), $virtualSize
}
[IO.File]::WriteAllBytes($Out, $image)
"base 0x{0:X8}, {1} MB written to {2}" -f $base, [Math]::Round($size / 1MB, 1), $Out
