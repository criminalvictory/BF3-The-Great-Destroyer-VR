# Finds pointer paths from an object (a soldier, say) to addresses inside other objects (the ammo
# counts, say): follows every pointer in each object to objects that start with one of bf3.exe's
# vtables, breadth first, and prints each path as offsets. Read-only. Run with the 32-bit PowerShell.
#   tools\pathto.ps1 -Root E133AE40 -Targets 'E3F9F824,E9C47398' [-Depth 4] [-Span 0x800]
param([Parameter(Mandatory)] [string]$Root, [Parameter(Mandatory)] [string]$Targets, [int]$Depth = 4,
      [int]$Span = 0x800, [int]$MaxObjects = 200000)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class PathTo {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out int r);
    static byte[] Read(IntPtr h, uint a, int n) {
        byte[] b = new byte[n]; int r;
        if (!ReadProcessMemory(h, new IntPtr(unchecked((int)a)), b, n, out r) || r < n) return null;
        return b;
    }
    static bool IsVtable(uint v) { return v >= 0x01C00000 && v < 0x02600000; }
    public static List<string> Run(IntPtr h, uint root, uint[] targets, int depth, int span, int maxObjects) {
        var found = new List<string>();
        var parent = new Dictionary<uint, KeyValuePair<uint, int>>();  // object -> (parent, offset)
        var frontier = new List<uint> { root };
        parent[root] = new KeyValuePair<uint, int>(0, 0);
        for (int level = 0; level <= depth && frontier.Count > 0; ++level) {
            var next = new List<uint>();
            foreach (uint obj in frontier) {
                byte[] b = Read(h, obj, span);
                if (b == null) continue;
                foreach (uint t in targets) {
                    if (t >= obj && t < obj + span) {
                        var path = new List<string>();
                        for (uint o = obj; o != root; o = parent[o].Key) path.Insert(0, string.Format("+{0:X}", parent[o].Value));
                        found.Add(string.Format("{0:X8} is at {1:X8}+{2:X} (vtable {3:X8}); path from root: {4}",
                            t, obj, t - obj, BitConverter.ToUInt32(b, 0), path.Count == 0 ? "(root)" : string.Join(" -> ", path.ToArray())));
                    }
                }
                if (level == depth) continue;
                for (int i = 0; i + 4 <= span; i += 4) {
                    uint p = BitConverter.ToUInt32(b, i);
                    if (p < 0x00010000 || (p & 3) != 0 || parent.ContainsKey(p)) continue;
                    if (p >= 0x00400000 && p < 0x02600000) continue;  // bf3.exe itself
                    byte[] head = Read(h, p, 4);
                    if (head == null || !IsVtable(BitConverter.ToUInt32(head, 0))) continue;
                    parent[p] = new KeyValuePair<uint, int>(obj, i);
                    next.Add(p);
                    if (parent.Count > maxObjects) return found;
                }
            }
            frontier = next;
        }
        return found;
    }
}
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Output 'BF3 is not running.'; exit 1 }
$h = [PathTo]::OpenProcess(0x0410, $false, $game.Id)
$t = @($Targets.Split(',') | ForEach-Object { [Convert]::ToUInt32($_.Trim(), 16) })
$paths = [PathTo]::Run($h, [Convert]::ToUInt32($Root, 16), $t, $Depth, $Span, $MaxObjects)
$paths | Sort-Object { $_.Length } | Select-Object -First 40
Write-Output "$($paths.Count) paths"
