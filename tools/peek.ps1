# Reads BF3's memory from outside the game: follows a pointer chain from bf3.exe and prints the
# floats there, four to a line. Read-only. Run it with the 32-bit PowerShell
# (C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe) so bf3.exe's module base is found.
#   tools\peek.ps1 -Chain 1EE8D14,738 -Start 0 -Length 0x600
param(
    [Parameter(Mandatory)] [string[]]$Chain,  # hex: module offset, then offsets of each pointer
    [int]$Start = 0,
    [int]$Length = 0x400
)

Add-Type -Namespace Peek -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
'@

$Chain = @($Chain | ForEach-Object { $_ -split ',' } | Where-Object { $_ })  # -File passes "a,b" as one string
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Peek.M]::OpenProcess(0x0410, $false, $game.Id)  # QUERY_INFORMATION | VM_READ

function Read-Bytes([long]$address, [int]$size) {
    $buf = New-Object byte[] $size
    $read = 0
    # BF3 uses addresses above 2 GB; a 32-bit IntPtr is signed.
    $signed = if ($address -gt 0x7FFFFFFF) { $address - 0x100000000 } else { $address }
    if (-not [Peek.M]::ReadProcessMemory($h, [IntPtr][int]$signed, $buf, $size, [ref]$read) -or $read -ne $size) { return $null }
    return $buf
}

$address = $game.MainModule.BaseAddress.ToInt64() + [Convert]::ToInt64($Chain[0], 16)
foreach ($offset in ($Chain | Select-Object -Skip 1)) {
    $b = Read-Bytes $address 4
    if (-not $b) { Write-Host ('pointer at {0:X8} unreadable' -f $address); exit 1 }
    $address = [BitConverter]::ToUInt32($b, 0) + [Convert]::ToInt64($offset, 16)
}
Write-Host ('object at {0:X8}' -f $address)

$data = Read-Bytes ($address + $Start) $Length
if (-not $data) { Write-Host 'unreadable'; exit 1 }
for ($i = 0; $i -lt $Length; $i += 16) {
    $f = 0..3 | ForEach-Object {
        $v = [BitConverter]::ToSingle($data, $i + $_ * 4)
        if ([float]::IsNaN($v) -or [Math]::Abs($v) -gt 1e7 -or ($v -ne 0 -and [Math]::Abs($v) -lt 1e-6)) {
            '{0,12}' -f ('0x{0:X8}' -f [BitConverter]::ToUInt32($data, $i + $_ * 4))
        } else {
            '{0,12:0.#####}' -f $v
        }
    }
    Write-Host ('+{0:X4}  {1}' -f ($Start + $i), ($f -join ' '))
}
[void][Peek.M]::CloseHandle($h)
