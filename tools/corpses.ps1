# Watches the server's soldiers (the bf3.exe entity table, see soldiers.ps1) and reports when one
# dies (health reaches 0) and when it leaves the table (its body removed), with how long it lay
# there. -Set N first writes N into every listed soldier's TimeForCorpse (campaign testing).
# Run with the 32-bit PowerShell.   tools\corpses.ps1 -Seconds 180 [-Set 600]
param([double]$Seconds = 120, [float]$Set = 0)
Add-Type -Namespace Corpses -Name M -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(System.IntPtr p, System.IntPtr addr, byte[] buf, int size, out int n);
'@
$game = Get-Process bf3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { Write-Host 'BF3 is not running.'; exit 1 }
$h = [Corpses.M]::OpenProcess(0x0438, $false, $game.Id)
function Ptr([long]$a) { if ($a -gt 0x7FFFFFFF) { [IntPtr][int]($a - 0x100000000) } else { [IntPtr][int]$a } }
function U([long]$a) { $b = New-Object byte[] 4; $r = 0; if ([Corpses.M]::ReadProcessMemory($h, (Ptr $a), $b, 4, [ref]$r)) { [BitConverter]::ToUInt32($b, 0) } else { 0 } }
function F([long]$a) { [BitConverter]::ToSingle([BitConverter]::GetBytes([uint32](U $a)), 0) }
function Soldiers() {
    $list = @{}
    for ($i = 0; $i -lt 256; ++$i) {
        $e = U (0x022B8EE4 + 4 * $i)
        if ($e -and (U $e) -eq 0x020E1268) { $list[$e] = F ($e + 0x20) }
    }
    $list
}
if ($Set -gt 0) {
    $done = @{}
    foreach ($e in (Soldiers).Keys) {
        $module = U ((U ($e + 0x14)) + 0xE8)
        if ((U $module) -ne 0x020C8A58 -or $done[$module]) { continue }
        $done[$module] = $true
        $before = F ($module + 8); $n = 0
        [void][Corpses.M]::WriteProcessMemory($h, (Ptr ($module + 8)), [BitConverter]::GetBytes($Set), 4, [ref]$n)
        Write-Host ('TimeForCorpse at {0:X8}: {1} -> {2}' -f $module, $before, (F ($module + 8)))
    }
}
$seen = @{}; $died = @{}
$start = Get-Date; $until = $start.AddSeconds($Seconds)
while ((Get-Date) -lt $until) {
    $now = Get-Date; $t = ($now - $start).TotalSeconds
    $list = Soldiers
    foreach ($e in $list.Keys) {
        if (-not $seen.ContainsKey($e)) { $seen[$e] = $true; Write-Host ('{0,6:0.0} s  soldier {1:X8} appears, health {2:0.#}' -f $t, $e, $list[$e]) }
        if ($list[$e] -le 0 -and -not $died.ContainsKey($e)) { $died[$e] = $t; Write-Host ('{0,6:0.0} s  soldier {1:X8} died' -f $t, $e) }
    }
    foreach ($e in @($seen.Keys)) {
        if (-not $list.ContainsKey($e)) {
            $lay = if ($died.ContainsKey($e)) { ' after lying dead {0:0.0} s' -f ($t - $died[$e]) } else { ' (not seen dead)' }
            Write-Host ('{0,6:0.0} s  soldier {1:X8} removed{2}' -f $t, $e, $lay)
            $seen.Remove($e); $died.Remove($e)
        }
    }
    Start-Sleep -Milliseconds 250
}
Write-Host ('{0} soldiers listed at the end' -f (Soldiers).Count)
