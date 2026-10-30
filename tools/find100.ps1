# Lists every float equal to a value (default 100) inside an object reached by a pointer chain
# from bf3.exe, and inside the objects its first pointers lead to (one level down). Read-only.
# Run with the 32-bit PowerShell.  tools\find100.ps1 -Chain 1F80B58,30,BC,3D8 -Size 0x1000
param(
    [Parameter(Mandatory)] [string]$Chain,
    [int]$Size = 0x1000,
    [int]$ChildSize = 0x400,
    [float]$Value = 100
)
Add-Type -Namespace F100 -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [F100.M]::OpenProcess(0x0410, $false, $game.Id)
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function Read([long]$a, [int]$n) { $b = New-Object byte[] $n; $r = 0; if ([F100.M]::ReadProcessMemory($h, (Ptr $a), $b, $n, [ref]$r) -and $r -eq $n) { return ,$b } return $null }
$parts = $Chain -split ','
$addr = $game.MainModule.BaseAddress.ToInt64() + [Convert]::ToInt64($parts[0], 16)
foreach ($o in ($parts | Select-Object -Skip 1)) {
    $b = Read $addr 4
    if (-not $b) { Write-Host 'chain broken'; exit 1 }
    $addr = [BitConverter]::ToUInt32($b, 0) + [Convert]::ToInt64($o, 16)
}
$b = Read $addr 4
$object = [BitConverter]::ToUInt32($b, 0)
Write-Host ('object {0:X8}' -f $object)
$data = Read $object $Size
if (-not $data) { Write-Host 'object unreadable'; exit 1 }
for ($i = 0; $i -le $Size - 4; $i += 4) {
    if ([BitConverter]::ToSingle($data, $i) -eq $Value) { Write-Host ('  +{0:X4} = {1}' -f $i, $Value) }
}
# One level down: each pointer-looking field, its first ChildSize bytes.
for ($i = 0; $i -le $Size - 4; $i += 4) {
    $p = [BitConverter]::ToUInt32($data, $i)
    if ($p -lt 0x01000000 -or ($p -ge 0x00400000 -and $p -lt 0x02500000)) { continue }
    $child = Read $p $ChildSize
    if (-not $child) { continue }
    for ($j = 0; $j -le $ChildSize - 4; $j += 4) {
        if ([BitConverter]::ToSingle($child, $j) -eq $Value) { Write-Host ('  [+{0:X4}] {1:X8} +{2:X4} = {3}' -f $i, $p, $j, $Value) }
    }
}
