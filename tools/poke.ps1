# Writes one float into BF3's memory (campaign testing only) and reports what happens to it and
# to a second address afterwards: whether the game puts its own value back, and whether a value
# derived from it follows. Run with the 32-bit PowerShell, like peek.ps1.
#   tools\poke.ps1 -Chain 1EE8D14,738,0 -Offset 0x78 -Value 1.2 -Watch 0x2F0
#   -Hold 3 keeps writing it for 3 seconds instead of once.
param(
    [Parameter(Mandatory)] [string[]]$Chain,
    [Parameter(Mandatory)] [string]$Offset,
    [Parameter(Mandatory)] [float]$Value,
    [string]$Watch = '',
    [double]$Hold = 0,
    # Refuse to write unless the target holds this now (NaN = don't check). A chain one step short
    # once landed on a pointer, and writing a float there crashed the game.
    [float]$Expect = [float]::NaN
)

Add-Type -Namespace Poke -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int n);
[DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int n);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
'@

$Chain = @($Chain | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Poke.M]::OpenProcess(0x0438, $false, $game.Id)  # QUERY_INFORMATION | VM_OPERATION | VM_READ | VM_WRITE

function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function Read-Float([long]$a) {
    $b = New-Object byte[] 4; $n = 0
    [void][Poke.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$n)
    [BitConverter]::ToSingle($b, 0)
}
function Read-UInt([long]$a) {
    $b = New-Object byte[] 4; $n = 0
    [void][Poke.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$n)
    [BitConverter]::ToUInt32($b, 0)
}

$object = $game.MainModule.BaseAddress.ToInt64() + [Convert]::ToInt64($Chain[0], 16)
foreach ($o in ($Chain | Select-Object -Skip 1)) { $object = (Read-UInt $object) + [Convert]::ToInt64($o, 16) }
$target = $object + [Convert]::ToInt64($Offset, 16)
$watched = if ($Watch) { $object + [Convert]::ToInt64($Watch, 16) } else { 0 }
function Report([string]$when) {
    $line = '{0,-14} value {1:0.#####}' -f $when, (Read-Float $target)
    if ($watched) { $line += '   watched {0:0.#####}' -f (Read-Float $watched) }
    Write-Host $line
}

Report 'before'
if (-not [float]::IsNaN($Expect) -and (Read-Float $target) -ne $Expect) {
    Write-Host ('refused: the target holds {0}, not {1}' -f (Read-Float $target), $Expect)
    exit 2
}
$bytes = [BitConverter]::GetBytes($Value)
$n = 0
$until = (Get-Date).AddSeconds($Hold)
do {
    [void][Poke.M]::WriteProcessMemory($h, (Ptr $target), $bytes, 4, [ref]$n)
    if ($Hold -gt 0) { Start-Sleep -Milliseconds 1 }
} while ((Get-Date) -lt $until)
Report 'just after'
Start-Sleep -Milliseconds 100
Report '100 ms later'
Start-Sleep -Milliseconds 1000
Report '1.1 s later'
[void][Poke.M]::CloseHandle($h)
