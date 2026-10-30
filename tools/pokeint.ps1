# Writes a 4-byte integer at absolute addresses in BF3's memory (campaign testing only), skipping
# any that don't hold -Expect now. Run with the 32-bit PowerShell.
#   tools\pokeint.ps1 -Addresses 'E3F9F824,EB3D4D04' -Value 21 -Expect 26
#   -FromFile uses the hits findints.ps1 last wrote instead (all of them, or -Pick '0,3,5')
param([string]$Addresses = '', [Parameter(Mandatory)] [int]$Value, [Parameter(Mandatory)] [int]$Expect,
      [switch]$FromFile, [string]$Pick = '', [string]$File = (Join-Path $env:TEMP 'bf3_findints.txt'))
Add-Type -Namespace PokeInt -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int n);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Output 'BF3 is not running.'; exit 1 }
$h = [PokeInt.M]::OpenProcess(0x0438, $false, $game.Id)
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
$list = @()
if ($FromFile) {
    $lines = @(Get-Content $File)
    $indices = if ($Pick) { $Pick.Split(',') | ForEach-Object { [int]$_ } } else { 0..($lines.Count - 1) }
    foreach ($i in $indices) { $list += [Convert]::ToInt64($lines[$i].Split(' ')[0], 16) }
} else {
    foreach ($s in $Addresses.Split(',')) { if ($s.Trim()) { $list += [Convert]::ToInt64($s.Trim(), 16) } }
}
foreach ($a in $list) {
    $b = New-Object byte[] 4; $r = 0
    [void][PokeInt.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$r)
    $now = [BitConverter]::ToInt32($b, 0)
    if ($now -ne $Expect) { Write-Output ('{0:X8}: holds {1}, not {2}: left alone' -f $a, $now, $Expect); continue }
    $w = [BitConverter]::GetBytes([int]$Value); $n = 0
    $ok = [PokeInt.M]::WriteProcessMemory($h, (Ptr $a), $w, 4, [ref]$n)
    Write-Output ('{0:X8}: {1} -> {2} {3}' -f $a, $now, $Value, $(if ($ok) { 'written' } else { 'FAILED' }))
}
