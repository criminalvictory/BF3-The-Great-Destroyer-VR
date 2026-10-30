# After the campaign is running (Battlelog's PLAY CAMPAIGN, then tools\resume.ps1): checks from the
# log that each part of the mod came up this run, and prints one line per part.
#   tools\selfcheck.ps1 [-Wait 50]   (50 s covers the checkpoint's opening ride)
param([int]$Wait = 50)
$log = Join-Path (Split-Path $PSScriptRoot) 'build\bf3vr.log'
if (-not (Get-Process bf3 -ErrorAction SilentlyContinue)) { Write-Output 'FAIL  BF3 is not running'; exit 1 }
Start-Sleep -Seconds $Wait
$text = Get-Content $log -Raw
$checks = @(
    @('Hooks installed', '(?s)^(?!.*(MH_CreateHook failed|hooking .* failed))'),
    @('OpenXR session started', 'Session state -> 2'),
    @('Wide field of view', 'FOV: source [\d.]+, render view 1\d\d'),
    @('God mode found the server soldier', "God mode: the server's soldier is at"),
    @('Bodies stay longer', 'Bodies: soldiers'' bodies at'),
    @('Depth buffer found for 3D', 'Depth 3D: depth buffer .* \(used\)')
)
$failed = 0
foreach ($c in $checks) {
    $ok = $text -match $c[1]
    if (-not $ok) { ++$failed }
    Write-Output ('{0}  {1}' -f $(if ($ok) { 'ok  ' } else { 'FAIL' }), $c[0])
}
$fps = Select-String -Path $log -Pattern 'Frame timing .*whole frame ([\d.]+)' | Select-Object -Last 1
if ($fps) { Write-Output ('      last frame time: {0} ms' -f $fps.Matches[0].Groups[1].Value) }
exit $failed
