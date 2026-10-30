# Finds where bf3.exe's code mentions a 4-byte value (an absolute address: a global, a vtable, a
# function), in the image saved by dumpimage.ps1, and prints each place's address; with -Disasm,
# the instructions around it (dumpbin). Read-only.
#   tools\findcode.ps1 -Value 022E8D14 -Disasm
param([Parameter(Mandatory)] [string]$Value, [string]$Image = (Join-Path (Split-Path $PSScriptRoot) 'build\bf3_mem.exe'),
      [int]$Max = 60, [switch]$Disasm, [int]$Around = 0x30)
$bytes = [IO.File]::ReadAllBytes($Image)
$v = [Convert]::ToUInt32($Value, 16)
$pattern = [BitConverter]::GetBytes($v)
# .text: 0x401000-0x1EEFC50 in this build (dumpimage.ps1 lists the sections).
$textStart = 0x1000; $textEnd = 0x1AEFC50
$hits = New-Object System.Collections.Generic.List[int]
for ($i = $textStart; $i -lt $textEnd - 3 -and $hits.Count -lt $Max; ++$i) {
    if ($bytes[$i] -eq $pattern[0] -and $bytes[$i + 1] -eq $pattern[1] -and $bytes[$i + 2] -eq $pattern[2] -and $bytes[$i + 3] -eq $pattern[3]) { $hits.Add($i) }
}
"{0} places in the code mention 0x{1:X8}" -f $hits.Count, $v
if (-not $Disasm) { $hits | ForEach-Object { '0x{0:X8}' -f (0x400000 + $_) }; return }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x86\dumpbin.exe" | Select-Object -First 1 -ExpandProperty FullName
foreach ($h in $hits) {
    $at = 0x400000 + $h
    "--- 0x{0:X8}" -f $at
    & $dumpbin /nologo /disasm:nobytes ('/range:0x{0:X},0x{1:X}' -f ($at - $Around), ($at + $Around)) $Image |
        Where-Object { $_ -match '^\s+[0-9A-F]{8}:' } | Select-Object -Skip 1
}
