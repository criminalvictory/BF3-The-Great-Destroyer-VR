# Polls floats at pointer chains from bf3.exe every 100 ms for a while and prints each change.
# Read-only. Run with the 32-bit PowerShell.
#   tools\watch.ps1 -Chains '1F80B58,30,BC,3D8,14,EC;1F80B58,30,BC,3D8,164,B0' -Seconds 40
param(
    [Parameter(Mandatory)] [string]$Chains,
    [double]$Seconds = 30
)
Add-Type -Namespace Watch -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Watch.M]::OpenProcess(0x0410, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function ReadU([long]$a) { $b = New-Object byte[] 4; $r = 0; if ([Watch.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$r)) { return [BitConverter]::ToUInt32($b, 0) } return $null }
function Resolve([string]$chain) {
    $parts = $chain -split ','
    $a = $base + [Convert]::ToInt64($parts[0], 16)
    foreach ($o in ($parts | Select-Object -Skip 1)) {
        $p = ReadU $a
        if ($null -eq $p -or $p -eq 0) { return $null }
        $a = $p + [Convert]::ToInt64($o, 16)
    }
    return $a
}
$list = $Chains -split ';'
$last = @{}
$until = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $until) {
    $line = @()
    $changed = $false
    for ($k = 0; $k -lt $list.Count; ++$k) {
        $a = Resolve $list[$k]
        $v = if ($null -ne $a) { $u = ReadU $a; if ($null -ne $u) { [BitConverter]::ToSingle([BitConverter]::GetBytes([uint32]$u), 0) } else { 'x' } } else { 'x' }
        $text = if ($v -is [float]) { '{0:0.###}' -f $v } else { $v }
        if ($last[$k] -ne $text) { $changed = $true; $last[$k] = $text }
        $line += ('{0}={1}' -f [char](65 + $k), $text)
    }
    if ($changed) { Write-Host ('{0:HH:mm:ss.f}  {1}' -f (Get-Date), ($line -join '  ')) }
    Start-Sleep -Milliseconds 100
}
