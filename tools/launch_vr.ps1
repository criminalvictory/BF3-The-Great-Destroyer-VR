# BF3 in VR in one double-click (the desktop shortcuts run this): starts the mod's injector, opens Battlelog for
# its PLAY CAMPAIGN button (the one click left to you: EA's sign-in and the game's launch stay Battlelog's own),
# waits for the game and the mod, then clicks RESUME CAMPAIGN. The mod switches to the headset's view by itself
# the moment you're in the game.
#   tools\launch_vr.ps1          resume the campaign where it was left
#   tools\launch_vr.ps1 -Menu    stop at the campaign menu (to pick a mission: Replay Missions)
param([switch]$Menu)

$root = Split-Path $PSScriptRoot
$injector = Join-Path $root 'build\injector.exe'
$log = Join-Path $root 'build\bf3vr.log'
$Host.UI.RawUI.WindowTitle = 'BF3 VR launcher'

function Say([string]$text) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $text) }

if (Get-Process bf3 -ErrorAction SilentlyContinue) {
    Say 'Battlefield 3 is already running. Close it first, then run this again (the mod loads only as the game starts).'
    Read-Host 'Press Enter to close'
    exit 1
}
if (-not (Test-Path $injector)) {
    Say "The mod isn't built: $injector is missing."
    Read-Host 'Press Enter to close'
    exit 1
}
# Whatever was built last is what loads (build\bf3vr.dll, next to the injector): say which build that is.
$dll = Join-Path $root 'build\bf3vr.dll'
if (-not (Test-Path $dll)) {
    Say "The mod isn't built: $dll is missing (a build may be under way: try again in a few minutes)."
    Read-Host 'Press Enter to close'
    exit 1
}
$change = git -C $root log -1 --format=%s 2>$null
Say ("The mod's build from {0:ddd d MMM, HH:mm}{1}" -f (Get-Item $dll).LastWriteTime, $(if ($change) { ": $change" } else { '' }))

# Other graphics hooks in the game folder (ReShade and the like) break the headset's view: say so first.
$gameDir = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\EA Games\Battlefield 3' -ErrorAction SilentlyContinue).'Install Dir'
if (-not $gameDir) { $gameDir = 'E:\SteamLibrary\steamapps\common\Battlefield 3' }
$hooks = foreach ($name in 'dxgi.dll', 'd3d11.dll', 'd3d9.dll', 'dinput8.dll') { if (Test-Path (Join-Path $gameDir $name)) { $name } }
if ($hooks) { Say ("Heads up: {0} in the game folder may fight the mod's view (ReShade or similar). Move it out if the headset shows nothing." -f ($hooks -join ', ')) }

# SteamVR first, always (the user, 2026-11-01): without it the mod's tries to reach the headset stalled the game
# every few seconds ("the game is running really slow").
# And streaming over the Steam Frame's dongle (Valve's USB Wi-Fi adapter), not the home Wi-Fi: SteamVR takes the
# dongle only if the dongle's link to the headset is up when SteamVR starts; started before that, it streams over
# the home network and stays on it. The user's fix by hand: close SteamVR, wait for the headset to show the dongle
# active, start SteamVR again. This does the same.
function SteamVrUp { (Get-Process vrserver -ErrorAction SilentlyContinue) -and (Get-Process vrcompositor -ErrorAction SilentlyContinue) }
function DongleNic { Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.InterfaceDescription -match 'for Valve' } | Select-Object -First 1 }
# The PC's address on the dongle's link to the headset, while it's up.
function DongleIp {
    $nic = DongleNic
    if (-not $nic -or $nic.Status -ne 'Up') { return $null }
    Get-NetIPAddress -InterfaceIndex $nic.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike '169.254.*' } | Select-Object -First 1 -ExpandProperty IPAddress
}
# The PC's address SteamVR streams to the headset from, the newest since $since in its link driver's log
# ("Found primary link 10.35.78.25"); none before the headset connects.
$steamDir = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
function SteamVrLink([datetime]$since) {
    $file = if ($steamDir) { Join-Path $steamDir 'logs\driver_vrlink.txt' }
    if (-not $file -or -not (Test-Path $file)) { return $null }
    $link = $null
    foreach ($line in (Get-Content $file -Tail 4000 -ErrorAction SilentlyContinue)) {
        if ($line -match '^\w{3} (\w{3} \d{2} \d{4} \d{2}:\d{2}:\d{2}\.\d{3}) .*Found primary link (\S+)') {
            $when = [datetime]::ParseExact($Matches[1], 'MMM dd yyyy HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
            if ($when -ge $since) { $link = $Matches[2] }
        }
    }
    $link
}
function StopSteamVr {
    Get-Process vrmonitor, vrdashboard, vrcompositor, vrserver, vrwebhelper, vrstartup -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    $t = 0
    while ((Get-Process vrserver, vrcompositor, vrmonitor -ErrorAction SilentlyContinue) -and $t -lt 20) { Start-Sleep 1; $t++ }
    Start-Sleep 3
}
function WaitForDongle {
    if (DongleIp) { return $true }
    Say 'Waiting for the Steam Frame''s dongle: turn the headset on and wait until it shows the adapter active...'
    $t = 0
    while (-not (DongleIp) -and $t -lt 300) { Start-Sleep 1; $t++ }
    [bool](DongleIp)
}

$dongle = DongleNic  # none plugged in (a cable, another headset): SteamVR's own choice
if ($dongle -and (SteamVrUp)) {
    $link = SteamVrLink (Get-Process vrserver | Select-Object -First 1).StartTime
    if ($link -and $link -ne (DongleIp)) {
        Say "SteamVR is streaming over the home network ($link), not the dongle: restarting it."
        StopSteamVr
    }
}
for ($try = 1; -not (SteamVrUp) -and $try -le 2; $try++) {
    if ($dongle -and -not (WaitForDongle)) {
        Say 'The dongle''s link to the headset didn''t come up in 5 minutes. Check the headset is on and the dongle plugged in, then run this again.'
        Read-Host 'Press Enter to close'
        exit 1
    }
    $since = Get-Date
    Start-Process 'steam://rungameid/250820'
    Say 'Starting SteamVR...'
    $t = 0
    while (-not (SteamVrUp) -and $t -lt 90) { Start-Sleep 1; $t++ }
    if (-not (SteamVrUp)) {
        Say 'SteamVR didn''t start. Start it from Steam (and turn the headset on), then run this again.'
        Read-Host 'Press Enter to close'
        exit 1
    }
    if ($dongle) {
        # Which way it streams once the headset connects (up to a minute; with the headset asleep, left be).
        $link = $null; $t = 0
        while (-not $link -and $t -lt 60) { Start-Sleep 1; $t++; $link = SteamVrLink $since }
        if ($link -and $link -ne (DongleIp) -and $try -lt 2) {
            Say "SteamVR took the home network ($link), not the dongle: restarting it."
            StopSteamVr
            continue
        }
    }
    Start-Sleep 5  # settled before the game asks for the headset
}
$link = if ($dongle) { SteamVrLink (Get-Process vrserver | Select-Object -First 1).StartTime }
if ($link -and $link -eq (DongleIp)) { Say 'SteamVR is running, streaming over the dongle.' }
elseif ($link) { Say "SteamVR is running, but streaming over the home network ($link). If the picture stutters: close the game and SteamVR, wait for the headset to show the dongle active, and run this again." }
else { Say 'SteamVR is running.' }

# The injector waits for bf3.exe and loads the mod into it as it starts. Any left over from before is stopped.
Get-Process injector -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$started = Get-Date
$inject = Start-Process -FilePath $injector -WorkingDirectory $root -WindowStyle Hidden -PassThru
Say 'The mod is waiting for the game.'

# Started through Steam, as the user starts it by hand (CircuitLord's VR installer does the same for Titanfall 2): Steam
# starts the EA app signed in through the account Steam is linked to, and the EA app opens Battlelog. Without
# Steam, Battlelog straight away.
$steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamExe
if ($steam -and (Test-Path $steam)) {
    Start-Process 'steam://rungameid/1238820'
    Say 'Starting Battlefield 3 through Steam: Steam signs you in to the EA app, which opens Battlelog.'
} else {
    Start-Process 'https://battlelog.battlefield.com/bf3/'
    Say 'Battlelog is opening in your browser.'
}
Say 'In Battlelog, click PLAY CAMPAIGN (the one click left: the game''s own launch).'
Say '(If it asks you to sign in, sign in there and tick "Remember me". This window never touches your account.)'

# The game, within 10 minutes.
$t = 0
while (-not (Get-Process bf3 -ErrorAction SilentlyContinue) -and $t -lt 600) { Start-Sleep 1; $t++ }
if (-not (Get-Process bf3 -ErrorAction SilentlyContinue)) {
    Say 'The game didn''t start within 10 minutes. Stopping the mod''s injector; run this again when you''re ready.'
    if (-not $inject.HasExited) { Stop-Process -Id $inject.Id -Force -ErrorAction SilentlyContinue }
    Read-Host 'Press Enter to close'
    exit 1
}
Say 'Battlefield 3 is starting.'

# The mod writes a new log as it loads: one newer than this launch means it's in.
$t = 0
$loaded = $false
while ($t -lt 60 -and -not $loaded) {
    Start-Sleep 1; $t++
    $loaded = (Test-Path $log) -and (Get-Item $log).LastWriteTime -gt $started -and
              ((Get-Content $log -TotalCount 1) -match 'bf3vr loaded')
}
if (-not $loaded) {
    Say 'The mod didn''t load into the game. Close Battlefield 3 and run this again.'
    Read-Host 'Press Enter to close'
    exit 1
}
Say 'The mod is in. It switches to the headset as soon as you''re in a level.'

if ($Menu) {
    Say 'Pick your mission in the campaign menu (Replay Missions). This window closes in 10 s.'
    Start-Sleep 10
    exit 0
}

Say 'Waiting for the menu, then resuming the campaign...'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'resume.ps1')
if ($LASTEXITCODE -ne 0) {
    Say 'The menu click didn''t take: click RESUME CAMPAIGN yourself in the game''s menu.'
    Start-Sleep 15
    exit 1
}
Say 'Campaign resumed. Put on the headset. This window closes in 10 s.'
Start-Sleep 10
