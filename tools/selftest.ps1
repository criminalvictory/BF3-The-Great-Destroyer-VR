# The never-break checks, run after every build once the campaign is in stereo (Battlelog's PLAY
# CAMPAIGN, then tools\resume.ps1; the mod switches to stereo itself), before anyone is called to the headset: what broke on
# 2026-10-30 (the view turning with the gun, the stick turn reversed, the HUD in one eye only, the
# right eye smeared, the frame rate) checked from the game itself with nobody in the headset
# (weapon.txt "standin 1" holds the rifle while the controllers sleep). One line per check, PASS /
# FAIL / SKIP with its numbers; the exit code is the number of failures. The results also go to
# screenshots\selftest_<time>.txt. Fires nothing.
#   tools\selftest.ps1 [-NoTurn]   (-NoTurn: skip the 3 s stick-turn test, which turns the view)
param([switch]$NoTurn)
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$log = Join-Path $root 'build\bf3vr.log'
$weapon = Join-Path $root 'build\weapon.txt'
$depth = Join-Path $root 'build\depth3d.txt'
$expectFile = Join-Path $PSScriptRoot 'selftest.expect'
$results = New-Object System.Collections.Generic.List[object]
function Result([string]$name, [string]$status, [string]$detail) {
    $results.Add([pscustomobject]@{ Check = $name; Status = $status; Detail = $detail })
    Write-Host ('{0,-5} {1,-34} {2}' -f $status, $name, $detail)
}
function LogLines { return @(Get-Content $log) }
function Since([int]$from) { $all = LogLines; if ($all.Count -le $from) { return @() } return @($all[$from..($all.Count - 1)]) }
function WriteFile([string]$path, [string]$text) {
    for ($i = 0; $i -lt 20; $i++) { try { [IO.File]::WriteAllText($path, $text); return } catch { Start-Sleep -Milliseconds 30 } }
}
# The forearms row (6) writes a test arms.txt marked by its first line; a run killed in the middle left
# it (the forearms on, the stand-in's test lines): put back what was there, or nothing.
$arms = Join-Path $root 'build\arms.txt'
$armsBackup = Join-Path $root 'build\arms.selftest.bak'
$armsMarker = '# selftest: put back by tools\selftest.ps1'
if ((Test-Path $arms) -and ((Get-Content $arms -TotalCount 1) -eq $armsMarker)) {
    if (Test-Path $armsBackup) { WriteFile $arms ([IO.File]::ReadAllText($armsBackup)) } else { Remove-Item $arms -ErrorAction SilentlyContinue }
    Remove-Item $armsBackup -ErrorAction SilentlyContinue
}
function Stamp([string]$line) {
    if ($line -match '^\[(\d+):(\d+):(\d+)\.(\d+)\]') { return [int]$matches[1] * 3600.0 + [int]$matches[2] * 60 + [int]$matches[3] + [int]$matches[4] / 1000.0 }
    return -1.0
}
function Median([double[]]$v) { if (-not $v -or $v.Count -eq 0) { return [double]::NaN } $s = $v | Sort-Object; return $s[[int][Math]::Floor($s.Count / 2)] }
function Wrap([double]$deg) { while ($deg -gt 180) { $deg -= 360 } while ($deg -lt -180) { $deg += 360 } return $deg }
function SaveEyes {
    $bmp = Join-Path $root 'build\eye_both.bmp'
    $text = Get-Content $depth -Raw
    $n = [int]([regex]::Match($text, 'save (\d+)').Groups[1].Value) + 1
    $before = if (Test-Path $bmp) { (Get-Item $bmp).LastWriteTime } else { [datetime]::MinValue }
    WriteFile $depth ($text -replace 'save \d+', "save $n")
    for ($t = 0; $t -lt 60 -and (-not (Test-Path $bmp) -or (Get-Item $bmp).LastWriteTime -eq $before); $t++) { Start-Sleep -Milliseconds 100 }
    Start-Sleep -Milliseconds 400
    if ((Get-Item $bmp).LastWriteTime -eq $before) { return $null }
    return [Drawing.Bitmap]::FromFile($bmp)
}
# The HUD's light blue in a region of the picture: pixels clearly bluer than they are red.
function HudBlue($img, [int]$x0, [int]$y0, [int]$w, [int]$h) {
    $n = 0
    for ($y = $y0; $y -lt $y0 + $h; $y += 3) {
        for ($x = $x0; $x -lt $x0 + $w; $x += 3) {
            $c = $img.GetPixel($x, $y)
            if ($c.B -gt 150 -and $c.B -gt $c.R + 60 -and $c.G -gt 110) { $n++ }
        }
    }
    return $n
}
# Detail across against detail down: the right eye smeared into streaks had almost none across.
function Streaks($img, [int]$x0, [int]$w) {
    $dx = 0.0; $dy = 0.0; $n = 0
    for ($y = 60; $y -lt $img.Height - 60; $y += 61) {
        for ($x = $x0 + 60; $x -lt $x0 + $w - 60; $x += 61) {
            $c = $img.GetPixel($x, $y); $r = $img.GetPixel($x + 4, $y); $d = $img.GetPixel($x, $y + 4)
            $b = $c.R + $c.G + $c.B
            $dx += [Math]::Abs($b - ($r.R + $r.G + $r.B)); $dy += [Math]::Abs($b - ($d.R + $d.G + $d.B)); $n++
        }
    }
    return ($dx / $n) / [Math]::Max($dy / $n, 0.1)
}
$expect = @{}
if (Test-Path $expectFile) { Get-Content $expectFile | ForEach-Object { if ($_ -match '^(\w+)=(.*)$') { $expect[$matches[1]] = $matches[2] } } }

# 1. The game and the mod's parts.
if (-not (Get-Process bf3 -ErrorAction SilentlyContinue)) { Result 'BF3 running' 'FAIL' 'bf3.exe is not running'; exit 1 }
$all = LogLines
$text = $all -join "`n"
# The window as big as the game draws ("resizing to"), or fitted to the monitor with the game still drawing its size.
$stereoWindow = 'Window: resizing to \d+x\d+|Window: fitted to the monitor, .* the game still draws \d+x\d+'
$startup = @(
    @('Stereo, both eyes side by side', $stereoWindow),
    @('Wide field of view', 'FOV: source [\d.]+, render view 1\d\d'),
    @('God mode', "God mode: the server's soldier is at"),
    @('Look angles found', 'Aim: the look angles found')
)
foreach ($s in $startup) { if ($text -match $s[1]) { Result $s[0] 'PASS' '' } else { Result $s[0] 'FAIL' "no '$($s[1])' in the log" } }
# The headset's resolution never changes with the desktop window (the user, 2026-11-01: "dont ever change the headset
# resolution unless i absolutely ask"): the game's picture stays two [window] size squares side by side.
$ini = Join-Path $root 'build\bf3vr.ini'
$size = 0
if (Test-Path $ini) {
    $inWindow = $false
    foreach ($l in Get-Content $ini) {
        if ($l -match '^\s*\[(.+)\]') { $inWindow = $matches[1] -eq 'window' }
        elseif ($inWindow -and $l -match '^\s*size\s*=\s*(\d+)') { $size = [int]$matches[1] }
    }
}
$swap = $all | Select-String 'Swapchains (\d+)x(\d+)' | Select-Object -Last 1
if (-not $swap -or $size -le 0) { Result 'Headset resolution' 'SKIP' 'no swap chain line, or no [window] size in bf3vr.ini' }
else {
    $w = [int]$swap.Matches[0].Groups[1].Value; $h = [int]$swap.Matches[0].Groups[2].Value
    if ($w -eq 2 * $size -and $h -eq $size) { Result 'Headset resolution' 'PASS' "${w}x${h}: $size per eye ([window] size)" }
    else { Result 'Headset resolution' 'FAIL' "the game draws ${w}x${h}, not $(2 * $size)x$size ([window] size $size per eye)" }
}
if ($text -match '(MH_CreateHook failed|hooking .* failed)') { Result 'Hooks' 'FAIL' $matches[1] } else { Result 'Hooks' 'PASS' '' }
# Lost again: the search's own lines after the last find.
$lastFound = ($all | Select-String 'Aim: the look angles found' | Select-Object -Last 1).LineNumber
$after = if ($lastFound) { $all | Select-Object -Skip $lastFound } else { @() }
if ($after -match 'exactly level|too many to nudge|no look angles yet') { Result 'Look angles held' 'FAIL' 'the search is running again (lost them)' } else { Result 'Look angles held' 'PASS' '' }

# 2. Frame rate (the last ten seconds' timing lines).
$timing = @($all | Select-String 'Frame timing \(ms per frame\): whole frame ([\d.]+)' | Select-Object -Last 10 | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
if ($timing.Count -ge 3) {
    $ms = Median $timing; $fps = 1000.0 / $ms
    $status = if ($fps -ge 70) { 'PASS' } elseif ($fps -ge 60) { 'WARN' } else { 'FAIL' }
    Result 'Frame rate' $status ('{0:F1} fps (median {1:F1} ms; worst {2:F1} ms)' -f $fps, $ms, ($timing | Measure-Object -Maximum).Maximum)
} else { Result 'Frame rate' 'SKIP' 'no timing lines yet' }

# 3. The view mustn't turn with the gun; the game must aim where it points (the camera follows
# the written look yaw). aimtest turns the hand 20 degrees from 1 s to 3 s, viewlog logs each frame.
$from = (LogLines).Count
WriteFile $weapon "standin 1`nviewlog 5`naimtest 20"
Start-Sleep -Seconds 6
WriteFile $weapon 'standin 1'
$lines = Since $from
$t0 = ($lines | Select-String 'Aim: test: the hand' | Select-Object -First 1)
if (-not $t0) { Result 'View still when the gun turns' 'SKIP' 'the aim test did not start' }
else {
    $t0 = Stamp $t0.Line
    $before = @(); $during = @()
    foreach ($l in $lines) {
        if ($l -match 'View: hand ([-\d.]+) written ([-\d.]+) \| camera ([-\d.]+) at .* \| head ([-\d.]+) shown ([-\d.]+)') {
            $t = (Stamp $l) - $t0
            $row = [pscustomobject]@{ written = [double]$matches[2]; camera = [double]$matches[3]; offset = Wrap ([double]$matches[5] - [double]$matches[4]) }
            if ($t -gt 0.1 -and $t -lt 0.9) { $before += $row } elseif ($t -gt 1.3 -and $t -lt 2.8) { $during += $row }
        }
    }
    if ($before.Count -lt 5 -or $during.Count -lt 5) { Result 'View still when the gun turns' 'SKIP' "too few view lines ($($before.Count) before, $($during.Count) during): is the right hand (or the stand-in) aiming?" }
    else {
        $dw = Wrap ((Median ($during | ForEach-Object written)) - (Median ($before | ForEach-Object written)))
        $do = Wrap ((Median ($during | ForEach-Object offset)) - (Median ($before | ForEach-Object offset)))
        $ok = [Math]::Abs([Math]::Abs($dw) - 20) -lt 3 -and [Math]::Abs($do) -lt 1.5
        Result 'View still when the gun turns' $(if ($ok) { 'PASS' } else { 'FAIL' }) ('look yaw written {0:+0.0;-0.0} deg, view {1:+0.00;-0.00} deg (must stay ~0)' -f $dw, $do)
    }
    # Where the game fires: its weapon camera, built from the look angles, against the yaw written
    # (the "camera" of the view lines is the render camera before the mod's turn, which carries the
    # hand's turn by design). The test's own lines (aim.cpp, from 2026-10-31) or the once-a-second ones.
    $pairs = @()
    foreach ($l in $lines) {
        if ($l -match 'Aim test: written ([-\d.]+), weapon camera ([-\d.]+)') { $pairs += , @([double]$matches[1], [double]$matches[2], ((Stamp $l) - $t0)) }
    }
    if ($pairs.Count -eq 0) {
        $written = $null
        foreach ($l in $lines) {
            if ($l -match 'Aim: yaw at \S+ = ([-\d.]+)') { $written = [double]$matches[1] }
            elseif ($l -match 'weapon camera as the game wrote it: yaw ([-\d.]+)' -and $null -ne $written) { $pairs += , @($written, [double]$matches[1], ((Stamp $l) - $t0)) }
        }
    }
    $inTest = @($pairs | Where-Object { $_[2] -gt 1.2 -and $_[2] -lt 2.9 })
    if ($inTest.Count -eq 0) { Result 'Game aims where the gun points' 'SKIP' 'no weapon camera reading during the test' }
    else {
        $best = ($inTest | ForEach-Object { [Math]::Abs((Wrap (($_[1] - $_[0]) * 57.29578))) } | Measure-Object -Minimum).Minimum
        $ok = $best -lt 1.0
        Result 'Game aims where the gun points' $(if ($ok) { 'PASS' } else { 'FAIL' }) ('weapon camera within {0:F2} deg of the yaw written while the gun was turned ({1} readings)' -f $best, $inTest.Count)
    }
}

# 4. The stick's turn direction (F17: 3 s of the stick held right). The first run records the
# direction the user confirmed (2026-10-30 22:00: right turns right) in tools\selftest.expect.
if ($NoTurn) { Result 'Stick turn direction' 'SKIP' '-NoTurn' }
else {
    $from = (LogLines).Count
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'drawhunt.ps1') -Action f17 | Out-Null
    Start-Sleep -Seconds 5
    $nets = @(Since $from | Select-String 'Turn \(shown\): .* net ([-+\d.]+) degrees' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
    $net = ($nets | Measure-Object -Sum).Sum
    if ($nets.Count -eq 0 -or [Math]::Abs($net) -lt 20) { Result 'Stick turn direction' 'SKIP' ('no turn measured (net {0:F1} deg): game not in front?' -f $net) }
    else {
        $sign = [Math]::Sign($net)
        if (-not $expect.ContainsKey('turn_right_sign')) {
            Add-Content $expectFile "turn_right_sign=$sign"
            Result 'Stick turn direction' 'PASS' ('recorded: right turns the shown yaw {0:+0;-0} ({1:F0} deg in 3 s)' -f $sign, $net)
        } else {
            $ok = [int]$expect['turn_right_sign'] -eq $sign
            Result 'Stick turn direction' $(if ($ok) { 'PASS' } else { 'FAIL' }) ('{0:F0} deg in 3 s (expected sign {1})' -f $net, $expect['turn_right_sign'])
        }
    }
}

# 5. Both eyes alike: the HUD's corners in each (twice, 4 s apart: a weapon switch's animation
# draws the gun differently in the two eyes for a few seconds, 2026-10-30), and no streaks.
$img = SaveEyes
if (-not $img) { Result 'Both eyes alike' 'SKIP' 'no picture saved' }
else {
    $w = $img.Width / 2; $h = $img.Height
    $corners = @(@('HUD bottom left', 0), @('HUD bottom right', ($w - 600)))
    $first = @{}
    foreach ($corner in $corners) {
        $l = HudBlue $img $corner[1] ($h - 300) 600 300
        $r = HudBlue $img ($w + $corner[1]) ($h - 300) 600 300
        $first[$corner[0]] = @($l, $r)
    }
    $again = $null
    foreach ($corner in $corners) {
        $l, $r = $first[$corner[0]]
        $ok = [Math]::Abs($l - $r) -lt [Math]::Max(60, 0.4 * [Math]::Max($l, $r))
        if (-not $ok) {
            if (-not $again) { $img.Dispose(); Start-Sleep -Seconds 4; $again = SaveEyes; $img = $again }
            if ($again) {
                $l = HudBlue $again $corner[1] ($h - 300) 600 300
                $r = HudBlue $again ($w + $corner[1]) ($h - 300) 600 300
                $ok = [Math]::Abs($l - $r) -lt [Math]::Max(60, 0.4 * [Math]::Max($l, $r))
            }
        }
        # A warning only: the HUD is see-through, and something dark close behind it in one eye (the
        # gun, with the eyes' parallax) darkens it there (2026-10-31). The HUD check that counts is
        # the overlay's own (below).
        Result $corner[0] $(if ($ok) { 'PASS' } else { 'WARN' }) "blue HUD pixels: left eye $l, right eye $r"
    }
    $hud = $all | Select-String 'Overlay: last second (\d+) HUD draws in the left eye, (\d+) copied into the right' | Select-Object -Last 1
    if (-not $hud) { Result 'HUD in both eyes' 'SKIP' 'no overlay count in the log (an older build?)' }
    else {
        $d = [int]$hud.Matches[0].Groups[1].Value; $c = [int]$hud.Matches[0].Groups[2].Value
        Result 'HUD in both eyes' $(if ($d -gt 0 -and $d -eq $c) { 'PASS' } else { 'FAIL' }) "last second: $d HUD draws in the left eye, $c copied into the right"
    }
    $sl = Streaks $img 0 $w; $sr = Streaks $img $w $w
    $ok = $sl -gt 0.45 -and $sr -gt 0.45
    Result 'No streaks in either eye' $(if ($ok) { 'PASS' } else { 'FAIL' }) ('detail across/down: left {0:F2}, right {1:F2} (streaks ~0.03)' -f $sl, $sr)
    $img.Dispose()
}

# 6. Wrist movement (build\arms.txt, off until seen in the headset): the stand-in's left hand moved
# into reach, its wrist turned straight along the forearm solved for it, then bent 30 degrees of
# flexion about the wrist joint; the once-a-second readings must say so. arms.txt is put back as it
# was (no file: none again, the forearms off).
if (-not (Get-Process bf3 -ErrorAction SilentlyContinue)) { Result 'Forearms' 'SKIP' 'bf3.exe is not running' }
elseif ($text -notmatch $stereoWindow) { Result 'Forearms' 'SKIP' 'stereo is not on' }
else {
    $hadArms = Test-Path $arms
    $armsBefore = if ($hadArms) { [IO.File]::ReadAllText($arms) } else { $null }
    $from = (LogLines).Count
    try {
        # The marker line lets the next run put a file left by a killed run back (top of this script).
        if ($hadArms) { WriteFile $armsBackup $armsBefore }
        WriteFile $arms "$armsMarker`non`nlog 1`ntest move -0.25 0.05 0.35`ntest neutral`ntest bend 30 0 0`n"
        Start-Sleep -Seconds 4
    } finally {
        if ($hadArms) { WriteFile $arms $armsBefore } else { Remove-Item $arms -ErrorAction SilentlyContinue }
        Remove-Item $armsBackup -ErrorAction SilentlyContinue
    }
    $lines = @(Since $from)
    $readings = @(); $unread = 0
    # Case-sensitive: The Great Destroyer's "Bare arms: left wrist bent ..." lines aren't the gloves' readings.
    foreach ($l in @($lines | Where-Object { $_ -cmatch 'Arms: left' })) {
        if ($l -match 'Arms: left reach ([\d.]+)/[\d.]+ bend ([\d.]+) \(flexion ([-+]?[\d.]+) ulnar ([-+]?[\d.]+)\) twist ([-+]?[\d.]+)') {
            $readings += [pscustomobject]@{ reach = [double]$matches[1]; flexion = [double]$matches[3]; ulnar = [double]$matches[4]; standIn = $l -match "the stand-in's hand" }
        } else { $unread++ }  # not numbers (nan, inf)
    }
    $ours = @($readings | Where-Object standIn)
    if (-not ($lines -match 'Arms: ')) { Result 'Forearms' 'SKIP' 'no Arms lines in the log after writing arms.txt (an older build?)' }
    elseif ($readings.Count -eq 0 -and $unread -eq 0) { Result 'Forearms' 'FAIL' "no 'Arms: left' lines: is the stand-in's left hand drawn (weapon.txt standin 1, the left controller asleep)?" }
    elseif ($readings.Count -gt 0 -and $ours.Count -eq 0 -and $unread -eq 0) { Result 'Forearms' 'SKIP' "the left hand is a real controller's (the test moves the stand-in's)" }
    else {
        $off = @($ours | Where-Object { [Math]::Abs($_.flexion - 30) -gt 1 })
        $ok = $ours.Count -gt 0 -and $unread -eq 0 -and $off.Count -eq 0
        $flex = ($ours | ForEach-Object { '{0:+0.0;-0.0}' -f $_.flexion }) -join ' '
        Result 'Forearms' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("{0} readings of the stand-in's hand, flexion {1} (30 +-1 wanted), {2} not numbers; reach {3:F2} m" -f $ours.Count, $flex, $unread, $(if ($ours.Count) { $ours[-1].reach } else { 0 }))
    }
}

# 7. Errors in the log since the game started.
$errors = @($all | Select-String -Pattern 'fail|error|exception' | Where-Object { $_.Line -notmatch "couldn't|didn't fire|without|no rounds" })
Result 'Log errors' $(if ($errors.Count -eq 0) { 'PASS' } else { 'WARN' }) $(if ($errors.Count) { "$($errors.Count): $($errors[-1].Line)" } else { '' })

# 8. The glove against the gun, offline (tools/collidetest.cpp), when it's built.
$collide = Join-Path $root 'build\collidetest.exe'
if (Test-Path $collide) {
    $out = & $collide 2>&1
    Result 'Glove collision (offline)' $(if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }) ("$LASTEXITCODE failures; " + ($out | Select-Object -Last 1))
} else { Result 'Glove collision (offline)' 'SKIP' 'build\collidetest.exe not built' }

# 9. The magazine from the pouch at the pelvis pushed into the well, offline (tools/magtest.cpp).
$magtest = Join-Path $root 'build\magtest.exe'
if (Test-Path $magtest) {
    $out = & $magtest 2>&1
    Result 'Magazine by hand (offline)' $(if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }) ("$LASTEXITCODE failures; " + ($out | Select-Object -Last 1))
} else { Result 'Magazine by hand (offline)' 'SKIP' 'build\magtest.exe not built' }

# 10. The forearm placed from its hand, offline (tools/armtest.cpp): src/armik.h's solver.
$armtest = Join-Path $root 'build\armtest.exe'
if (Test-Path $armtest) {
    $out = & $armtest 2>&1
    Result 'Arm IK (offline)' $(if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }) ("$LASTEXITCODE failures; " + ($out | Select-Object -Last 1))
} else { Result 'Arm IK (offline)' 'SKIP' 'build\armtest.exe not built' }

# 11. The left hand steering the gun while it holds the handguard, offline (tools/steertest.cpp):
# src/steer.h on scripted hands (the rifle had followed a left hand beside or behind the right, 2026-10-31).
$steertest = Join-Path $root 'build\steertest.exe'
if (Test-Path $steertest) {
    $out = & $steertest 2>&1
    Result 'Two-hand steer (offline)' $(if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }) ("$LASTEXITCODE failures; " + ($out | Select-Object -Last 1))
} else { Result 'Two-hand steer (offline)' 'SKIP' 'build\steertest.exe not built' }

$failed = @($results | Where-Object Status -eq 'FAIL').Count
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$report = Join-Path $root "screenshots\selftest_$stamp.txt"
$results | Format-Table -AutoSize | Out-String -Width 220 | Set-Content $report -Encoding utf8
Write-Host "$failed failed; written to $report"
exit $failed
