# Writes one float at an absolute address in BF3's memory (campaign testing only), refusing unless
# it holds -Expect now, then reports it and the -Chains values (as in watch.ps1) for a few seconds:
# whether the game puts its own value back and whether other copies follow.
# Run with the 32-bit PowerShell.
#   tools\pokeabs.ps1 -Address E4E476A0 -Value 60 -Expect 100 -Chains '1F80B58,30,BC,3D8,164,B0'
param(
    [Parameter(Mandatory)] [string]$Address,
    [Parameter(Mandatory)] [float]$Value,
    [Parameter(Mandatory)] [float]$Expect,
    [string]$Chains = '',
    [double]$Seconds = 8,
    # How far the value may have moved from -Expect (a value the game keeps changing, like health).
    [float]$Tolerance = 0
)
Add-Type -Namespace PokeAbs -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int n);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [PokeAbs.M]::OpenProcess(0x0438, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function ReadU([long]$a) { $b = New-Object byte[] 4; $r = 0; if ([PokeAbs.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$r)) { return [BitConverter]::ToUInt32($b, 0) } return $null }
function ReadF([long]$a) { $u = ReadU $a; if ($null -eq $u) { [float]::NaN } else { [BitConverter]::ToSingle([BitConverter]::GetBytes([uint32]$u), 0) } }
function Resolve([string]$chain) {
    $parts = $chain -split ','
    $a = $base + [Convert]::ToInt64($parts[0], 16)
    foreach ($o in ($parts | Select-Object -Skip 1)) { $p = ReadU $a; if ($null -eq $p -or $p -eq 0) { return $null }; $a = $p + [Convert]::ToInt64($o, 16) }
    $a
}
$target = [Convert]::ToInt64($Address, 16)
$list = if ($Chains) { $Chains -split ';' } else { @() }
function Report([string]$when) {
    $line = '{0,-8} value {1:0.###}' -f $when, (ReadF $target)
    foreach ($c in $list) { $a = Resolve $c; $line += '   ' + $(if ($null -ne $a) { '{0:0.###}' -f (ReadF $a) } else { 'x' }) }
    Write-Host $line
}
Report 'before'
if (-not ([Math]::Abs((ReadF $target) - $Expect) -le $Tolerance)) { Write-Host ('refused: it holds {0}, not {1}' -f (ReadF $target), $Expect); exit 2 }
$n = 0
[void][PokeAbs.M]::WriteProcessMemory($h, (Ptr $target), [BitConverter]::GetBytes($Value), 4, [ref]$n)
$start = Get-Date
foreach ($ms in 0, 50, 150, 300, 600, 1000, 2000, 3000, 4000, 6000, 8000, 12000, 16000) {
    if ($ms -gt $Seconds * 1000) { break }
    while (((Get-Date) - $start).TotalMilliseconds -lt $ms) { Start-Sleep -Milliseconds 5 }
    Report ('{0} ms' -f $ms)
}
