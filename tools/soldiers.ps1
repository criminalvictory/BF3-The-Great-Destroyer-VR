# Lists the server's soldiers from the entity table in bf3.exe (0x022B8EE4, 256 slots; the god
# mode finds the player's there), with each one's data, its health module data (data +0xE8) and
# that module's TimeForCorpse (+0x08). Read-only. Run with the 32-bit PowerShell.
Add-Type -Namespace Soldiers -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Soldiers.M]::OpenProcess(0x0410, $false, $game.Id)
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function U([long]$a) { $b = New-Object byte[] 4; $r = 0; if ([Soldiers.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$r)) { [BitConverter]::ToUInt32($b, 0) } else { 0 } }
function F([long]$a) { [BitConverter]::ToSingle([BitConverter]::GetBytes([uint32](U $a)), 0) }
$types = @{}
for ($i = 0; $i -lt 256; ++$i) {
    $e = U (0x022B8EE4 + 4 * $i)
    if (-not $e) { continue }
    $vt = U $e
    $types['{0:X8}' -f $vt]++
    if ($vt -ne 0x020E1268) { continue }
    $data = U ($e + 0x14); $module = U ($data + 0xE8)
    Write-Host ('slot {0,3}: soldier {1:X8} health {2,8:0.#} data {3:X8} module {4:X8} (vtable {5:X8}) TimeForCorpse {6}' -f $i, $e, (F ($e + 0x20)), $data, $module, (U $module), (F ($module + 8)))
}
Write-Host ('entry types: ' + (($types.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join ', '))
