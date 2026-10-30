# Keeps a float at a pointer chain topped up for a while (campaign testing): whenever it reads
# between 0 and Full, writes Full. Reports drops and when the chain breaks (a death: the soldier
# goes away). Run with the 32-bit PowerShell.
#   tools\pin.ps1 -Chain 1F80B58,30,BC,3D8,164,B0 -Seconds 60
param([Parameter(Mandatory)] [string]$Chain, [double]$Seconds = 60, [float]$Full = 100)
# PowerShell names ignore case: the bytes need a name of their own.
Add-Type -Namespace Pin -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int n);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Pin.M]::OpenProcess(0x0438, $false, $game.Id)
$base = $game.MainModule.BaseAddress.ToInt64()
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function ReadU([long]$a) { $b = New-Object byte[] 4; $r = 0; if ([Pin.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$r)) { [BitConverter]::ToUInt32($b, 0) } else { $null } }
function Resolve() {
    $parts = $Chain -split ','
    $a = $base + [Convert]::ToInt64($parts[0], 16)
    foreach ($o in ($parts | Select-Object -Skip 1)) { $p = ReadU $a; if (-not $p) { return $null }; $a = $p + [Convert]::ToInt64($o, 16) }
    $a
}
$fullBytes = [BitConverter]::GetBytes($Full)
$until = (Get-Date).AddSeconds($Seconds)
$lastAddress = $null; $writes = 0
while ((Get-Date) -lt $until) {
    $a = Resolve
    if ($a -ne $lastAddress) { Write-Host ('{0:HH:mm:ss.f}  health at {1}' -f (Get-Date), $(if ($a) { '{0:X8}' -f $a } else { 'nothing (no soldier)' })); $lastAddress = $a }
    if ($a) {
        $u = ReadU $a
        if ($null -ne $u) {
            $v = [BitConverter]::ToSingle([BitConverter]::GetBytes([uint32]$u), 0)
            if ($v -gt 0 -and $v -lt $Full) {
                $n = 0; [void][Pin.M]::WriteProcessMemory($h, (Ptr $a), $fullBytes, 4, [ref]$n); ++$writes
                if ($writes -le 15) { Write-Host ('{0:HH:mm:ss.f}  was {1:0.##}, put back to {2}' -f (Get-Date), $v, $Full) }
            }
        }
    }
    Start-Sleep -Milliseconds 20
}
Write-Host "$writes top-ups"
