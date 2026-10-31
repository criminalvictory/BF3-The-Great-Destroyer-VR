# Finds the gun in the player's hands from outside the game, the way ammo.cpp does, and prints its
# firing object: the address, then dwords as hex, int and float. Read-only. Run it with the 32-bit
# PowerShell (C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe).
#   tools\gunammo.ps1 -Start 0xC0 -Length 0x80
#   tools\gunammo.ps1 -Follow 0x10 -Length 0x100   (the object a pointer at +0x10 leads to)
#   tools\gunammo.ps1 -Find 30 -Depth 2 -Length 0x300   (where the value 30 sits, and the paths)
param(
    [int]$Start = 0,
    [int]$Length = 0x200,
    [int[]]$Follow = @(),  # pointer offsets to follow from the firing object first
    [int]$Find = 0,        # instead: where this value sits, up to $Depth pointers from the object
    [int]$Depth = 2
)

Add-Type -Namespace GunAmmo -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
'@

$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [GunAmmo.M]::OpenProcess(0x0410, $false, $game.Id)
$base = [uint32]$game.MainModule.BaseAddress.ToInt32()

function Bytes([uint32]$address, [int]$count) {
    $buf = New-Object byte[] $count
    $read = 0
    $ptr = [IntPtr][BitConverter]::ToInt32([BitConverter]::GetBytes($address), 0)  # above 2 GB too
    if (-not [GunAmmo.M]::ReadProcessMemory($h, $ptr, $buf, $count, [ref]$read) -or $read -ne $count) { return $null }
    return $buf
}
function Dword([uint32]$address) {
    $b = Bytes $address 4
    if ($null -eq $b) { return [uint32]0 }
    return [BitConverter]::ToUInt32($b, 0)
}

$context = Dword ($base + 0x1F80B58)
$players = if ($context) { Dword ($context + 0x30) } else { 0 }
$player = if ($players) { Dword ($players + 0xBC) } else { 0 }
$client = if ($player) { Dword ($player + 0x3D8) } else { 0 }
$data = if ($client) { Dword ($client + 0x14) } else { 0 }
if (-not $data -or (Dword $data) -ne ($base + 0x1DB0CC8)) { Write-Host 'No soldier (not in gameplay?).'; exit 1 }
$slots = Bytes ($base + 0x1EB8EE4) (256 * 4)
$server = 0
for ($i = 0; $i -lt 256; $i++) {
    $e = [BitConverter]::ToUInt32($slots, $i * 4)
    if ($e -and (Dword $e) -eq ($base + 0x1CE1268) -and (Dword ($e + 0x14)) -eq $data) { $server = $e; break }
}
if (-not $server) { Write-Host 'The server soldier was not found.'; exit 1 }
$weapons = Dword ($server + 0x4C0)
$firing = if ($weapons) { Dword ($weapons + 0x248) } else { 0 }
if (-not $firing -or (Dword $firing) -ne ($base + 0x1DAD810)) { Write-Host 'No gun in hand.'; exit 1 }
$loaded = [int](Dword ($firing + 0xF4)); $spare = [int](Dword ($firing + 0xF8))
'firing object {0:X8}: {1} loaded, {2} spare' -f $firing, $loaded, $spare

if ($Find) {
    # Every dword equal to $Find within $Length of the firing object and of what its pointers lead
    # to, $Depth pointers deep: the path to each.
    $seen = @{}
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue(@($firing, '', 0))
    while ($queue.Count) {
        $item = $queue.Dequeue(); $obj = [uint32]$item[0]; $path = $item[1]; $depth = $item[2]
        if ($seen.ContainsKey($obj)) { continue }
        $seen[$obj] = $true
        $buf = Bytes $obj $Length
        if ($null -eq $buf) { continue }
        for ($o = 0; $o -lt $Length; $o += 4) {
            $u = [BitConverter]::ToUInt32($buf, $o)
            if ($u -eq $Find) {
                $nextInt = if ($o + 8 -le $Length) { [BitConverter]::ToInt32($buf, $o + 4) } else { 0 }
                '{0}+{1:X} = {2} (next {3}) in {4:X8}' -f $path, $o, $Find, $nextInt, $obj
            }
            if ($depth -lt $Depth -and $u -ge 0x10000 -and ($u -band 3) -eq 0 -and ($u -lt $base -or $u -ge $base + 0x3000000)) {
                $queue.Enqueue(@($u, ('{0}+{1:X}>' -f $path, $o), ($depth + 1)))
            }
        }
    }
    exit 0
}

$at = $firing
foreach ($f in $Follow) {
    $next = Dword ($at + $f)
    $vtable = Dword $next
    $where = if ($vtable -ge $base -and $vtable -lt $base + 0x3000000) { 'bf3+{0:X}' -f ($vtable - $base) } else { 'not in bf3.exe' }
    '+{0:X} -> {1:X8} (first dword {2:X8}, {3})' -f $f, $next, $vtable, $where
    $at = $next
    if (-not $at) { exit 1 }
}
$buf = Bytes ($at + $Start) $Length
if ($null -eq $buf) { Write-Host 'unreadable'; exit 1 }
for ($o = 0; $o -lt $Length; $o += 4) {
    $u = [BitConverter]::ToUInt32($buf, $o); $i = [BitConverter]::ToInt32($buf, $o); $f = [BitConverter]::ToSingle($buf, $o)
    $note = ''
    if ($u -ge $base -and $u -lt $base + 0x3000000) { $note = "bf3+{0:X}" -f ($u - $base) }
    elseif ([Math]::Abs($f) -gt 1e-4 -and [Math]::Abs($f) -lt 1e6) { $note = '{0:G6}' -f $f }
    '+{0:X3}  {1:X8}  {2,11}  {3}' -f ($Start + $o), $u, $i, $note
}
[void][GunAmmo.M]::CloseHandle($h)
