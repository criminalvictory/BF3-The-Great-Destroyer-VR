# Prints a run of BF3's reflection field entries (12 bytes each: name pointer, flags with the
# byte offset in the high half, type pointer) starting at an absolute address, with the names and
# the type names. Read-only. Run with the 32-bit PowerShell.
#   tools\fields.ps1 -Address 0242F8B0 -Count 20
param([Parameter(Mandatory)] [string]$Address, [int]$Count = 16)
Add-Type -Namespace Fields -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Fields.M]::OpenProcess(0x0410, $false, $game.Id)
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function ReadBytes([long]$a, [int]$n) { $b = New-Object byte[] $n; $r = 0; [void][Fields.M]::ReadProcessMemory($h, (Ptr $a), $b, $n, [ref]$r); ,$b }
function U([long]$a) { [BitConverter]::ToUInt32((ReadBytes $a 4), 0) }
function Text([long]$a) {
    if ($a -lt 0x10000) { return '' }
    $b = ReadBytes $a 64; $n = [Array]::IndexOf($b, [byte]0); if ($n -lt 0) { $n = 64 }
    $s = [Text.Encoding]::ASCII.GetString($b, 0, $n)
    if ($s -match '^[\x20-\x7E]+$') { $s } else { '?' }
}
$a = [Convert]::ToInt64($Address, 16)
for ($i = 0; $i -lt $Count; ++$i) {
    $name = U $a; $flags = U ($a + 4); $type = U ($a + 8)
    # A type's info starts with a pointer to its name, in the builds seen so far.
    $typeName = if ($type -ge 0x00400000 -and $type -lt 0x02600000) { Text (U $type) } else { '' }
    Write-Host ('{0:X8}  +{1:X3}  {2,-32} type {3:X8} {4}  (flags {5:X4})' -f $a, ($flags -shr 16), (Text $name), $type, $typeName, ($flags -band 0xFFFF))
    $a += 12
}
