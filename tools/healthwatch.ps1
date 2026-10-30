# Finds every float that reads 100 in the soldier (bf3.exe+1F80B58 -> 30 -> BC -> 3D8) and in the
# objects its pointers lead to, then watches them for a while and prints each that changes, with
# its path. Shows which of them is the player's health while the player takes damage. Read-only.
# Run with the 32-bit PowerShell.  tools\healthwatch.ps1 -Seconds 40
param([double]$Seconds = 40, [int]$Size = 0x1000, [int]$ChildSize = 0x400)
Add-Type -Namespace HW -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [HW.M]::OpenProcess(0x0410, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function Read([long]$a, [int]$n) { $b = New-Object byte[] $n; $r = 0; if ([HW.M]::ReadProcessMemory($h, (Ptr $a), $b, $n, [ref]$r) -and $r -eq $n) { return ,$b } return $null }
function U([long]$a) { $b = Read $a 4; if ($b) { [BitConverter]::ToUInt32($b, 0) } else { 0 } }
$soldier = 0
for ($t = 0; $t -lt 100 -and -not $soldier; ++$t) {
    $soldier = U ((U ((U ((U ($base + 0x1F80B58)) + 0x30)) + 0xBC)) + 0x3D8)
    if (-not $soldier) { Start-Sleep -Milliseconds 300 }
}
if (-not $soldier) { Write-Host 'no soldier'; exit 1 }
Write-Host ('soldier {0:X8}' -f $soldier)
$data = Read $soldier $Size
$watch = @()  # each: address, label
for ($i = 0; $i -le $Size - 4; $i += 4) {
    if ([BitConverter]::ToSingle($data, $i) -eq 100) { $watch += ,@(($soldier + $i), ('+{0:X}' -f $i)) }
    $p = [BitConverter]::ToUInt32($data, $i)
    if ($p -lt 0x01000000 -or ($p -ge 0x00400000 -and $p -lt 0x02500000)) { continue }
    $child = Read $p $ChildSize
    if (-not $child) { continue }
    for ($j = 0; $j -le $ChildSize - 4; $j += 4) {
        if ([BitConverter]::ToSingle($child, $j) -eq 100) { $watch += ,@(($p + $j), ('+{0:X} -> +{1:X}' -f $i, $j)) }
    }
}
Write-Host "$($watch.Count) values at 100"
$last = @{}
$until = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $until) {
    foreach ($w in $watch) {
        $b = Read $w[0] 4
        $v = if ($b) { [BitConverter]::ToSingle($b, 0) } else { [float]::NaN }
        $key = $w[1]
        if ($last.ContainsKey($key) -and $last[$key] -ne $v) {
            Write-Host ('{0:HH:mm:ss.f}  {1,-16} {2:X8}  {3} -> {4}' -f (Get-Date), $key, $w[0], $last[$key], $v)
        }
        $last[$key] = $v
    }
    Start-Sleep -Milliseconds 100
}
