# Maps the first-person draws (the gun's parts and the arms): hides each alone in turn (hunt.txt
# "first person only N", then F15 twice) and saves a clean eye dump of the gun area for each, as
# screenshots\fp_<N>_right_crop.png. Stereo mode, research hooks counting (F15 pressed once
# before). The log lists which draw each number is.
param([int]$From = 0, [int]$To = 17, [string]$Crop = '560,760,900,840')
$root = Split-Path $PSScriptRoot
$hunt = Join-Path $PSScriptRoot 'drawhunt.ps1'
for ($n = $From; $n -le $To; ++$n) {
    Set-Content (Join-Path $root 'build\hunt.txt') "first person only $n" -Encoding ascii
    & $hunt -Action start -Name huntkey | Out-Null   # stops the previous one (shows everything)
    & $hunt -Action start -Name huntkey | Out-Null   # hides draw n alone
    & (Join-Path $PSScriptRoot 'huntstep.ps1') -Answer none -Name "fp_$n" -Crop $Crop | Out-Null
    $line = Select-String -Path (Join-Path $root 'build\bf3vr.log') -Pattern 'hiding only first-person draw' | Select-Object -Last 1
    Write-Output "$n : $($line.Line)"
}
