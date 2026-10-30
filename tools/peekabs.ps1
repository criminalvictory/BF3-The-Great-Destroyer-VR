# Prints floats around an absolute address in BF3's memory, four to a line. Read-only.
# Run with the 32-bit PowerShell. tools\peekabs.ps1 -Address 26008644 -Before 0x40 -Length 0x80
param(
    [Parameter(Mandatory)] [string]$Address,
    [int]$Before = 0x40,
    [int]$Length = 0x80
)
Add-Type -Namespace PeekAbs -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [PeekAbs.M]::OpenProcess(0x0410, $false, $game.Id)
$a = [Convert]::ToInt64($Address, 16) - $Before
$signed = if ($a -gt 0x7FFFFFFF) { $a - 0x100000000 } else { $a }
$buf = New-Object byte[] $Length
$n = 0
if (-not [PeekAbs.M]::ReadProcessMemory($h, [IntPtr][int]$signed, $buf, $Length, [ref]$n)) { Write-Host 'unreadable'; exit 1 }
for ($i = 0; $i -lt $Length; $i += 16) {
    $f = 0..3 | ForEach-Object {
        $v = [BitConverter]::ToSingle($buf, $i + $_ * 4)
        if ([float]::IsNaN($v) -or [Math]::Abs($v) -gt 1e7 -or ($v -ne 0 -and [Math]::Abs($v) -lt 1e-6)) { '{0,12}' -f ('0x{0:X8}' -f [BitConverter]::ToUInt32($buf, $i + $_ * 4)) }
        else { '{0,12:0.#####}' -f $v }
    }
    Write-Host ('{0:X8}  {1}' -f ($a + $i), ($f -join ' '))
}
