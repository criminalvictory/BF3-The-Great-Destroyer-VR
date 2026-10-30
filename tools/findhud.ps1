# Lists blocks in BF3's writable memory shaped like the weapons' HudData (from the game's own
# type tables): CrosshairScaleMin/Max +0/+4, CrosshairOpacityMin/Max +8/+C, OpacityModifier +10,
# CrosshairTypeId +14, WeaponClass +18, HudPropertyList +30, flags +34..+42 (ShowMinimap +34,
# InfiniteAmmo +35, HideCrosshairWhenAimOnFriend +36, HideAmmo +37, ...). Read-only.
# Run with the 32-bit PowerShell.
param([int]$Max = 100)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FindHud {
    [StructLayout(LayoutKind.Sequential)]
    struct MBI { public IntPtr Base, AllocBase; public uint AllocProtect; public IntPtr Size; public uint State, Protect, Type; }
    [DllImport("kernel32.dll")] static extern int VirtualQueryEx(IntPtr p, IntPtr a, out MBI m, int len);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static IntPtr Ptr(long a) { return new IntPtr(unchecked((int)(uint)a)); }
    static bool In(float f, float lo, float hi) { return f >= lo && f <= hi; }
    public static bool Strict = true;
    public static List<string> Find(IntPtr h) {
        var hits = new List<string>();
        long a = 0x10000; MBI m; byte[] buf = new byte[(1 << 20) + 0x44];
        while (a < 0xFFFF0000L && VirtualQueryEx(h, Ptr(a), out m, Marshal.SizeOf(typeof(MBI))) != 0) {
            long start = (long)(uint)m.Base.ToInt32(), size = (long)(uint)m.Size.ToInt32();
            if (size <= 0) break;
            bool writable = (m.Protect & 0x04) != 0 || (m.Protect & 0x40) != 0;
            if (m.State == 0x1000 && writable && (m.Protect & 0x100) == 0) {
                for (long off = 0; off < size; off += 1 << 20) {
                    int n = (int)Math.Min(buf.Length, size - off), r;
                    if (!ReadProcessMemory(h, Ptr(start + off), buf, n, out r)) continue;
                    for (int i = 0; i + 0x44 <= r && i < (1 << 20); i += 4) {
                        float smin = BitConverter.ToSingle(buf, i), smax = BitConverter.ToSingle(buf, i + 4);
                        float omin = BitConverter.ToSingle(buf, i + 8), omax = BitConverter.ToSingle(buf, i + 12);
                        float mod = BitConverter.ToSingle(buf, i + 16);
                        if (!In(smin, 0.01f, 10) || !In(smax, smin, 10) || !In(omin, 0, 1) || !In(omax, omin, 1) || omax <= 0 || !In(mod, 0, 10)) continue;
                        int type = BitConverter.ToInt32(buf, i + 0x14), cls = BitConverter.ToInt32(buf, i + 0x18);
                        if (type < 0 || type > 64 || cls < 0 || cls > 64) continue;
                        bool flags = true;
                        for (int k = 0x34; k <= 0x42; ++k) if (buf[i + k] > 1) { flags = false; break; }
                        if (!flags) continue;
                        uint list = BitConverter.ToUInt32(buf, i + 0x30);
                        if (list != 0 && list < 0x10000) continue;
                        // A soldier's weapon shows the minimap and fades its crosshair in and out.
                        if (Strict && (buf[i + 0x34] != 1 || mod <= 0 || omax < 0.05f)) continue;
                        hits.Add(string.Format("{0:X8}  scale {1}-{2} opacity {3}-{4} modifier {5} type {6} class {7} minimap {8} hideAmmo {9}",
                            start + off + i, smin, smax, omin, omax, mod, type, cls, buf[i + 0x34], buf[i + 0x37]));
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
$h = [FindHud]::OpenProcess(0x0410, $false, $game.Id)
$hits = [FindHud]::Find($h)
$hits | Select-Object -First $Max | ForEach-Object { Write-Host $_ }
Write-Host "$($hits.Count) places"
