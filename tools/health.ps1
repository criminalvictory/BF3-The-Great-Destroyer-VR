# Prints the player's health as the screen shows it (the copies the first health finder saved,
# bf3.exe+1EE8688 -> 158 -> 4D0 -> 778), to time the health finder's steps: full, damaged, full.
# Run with the 32-bit PowerShell (C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe).
$out = & "$PSScriptRoot\peek.ps1" -Chain 1EE8688,158,4D0,778 -Start 0 -Length 0x10 2>&1
$line = $out | Where-Object { $_ -match '^\+0000' } | Select-Object -First 1
if (-not $line) { Write-Host 'health unreadable'; exit 1 }
$v = ($line -replace '^\+0000\s+', '') -split '\s+' | Where-Object { $_ }
Write-Host ('health {0}' -f $v[0])
