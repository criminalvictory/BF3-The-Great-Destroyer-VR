# Pulls still frames out of a video with Windows' own media components (no ffmpeg needed) and
# puts them side by side in one sheet, to look at what a recording shows.
#   tools\videoframes.ps1 -Video 'C:\Users\me\Videos\clip.mov' [-From 0 -To 10 -Count 16] [-Out sheet.png]
param([Parameter(Mandatory)] [string]$Video, [double]$From = 0, [double]$To = -1, [int]$Count = 16,
      [string]$Out = (Join-Path (Split-Path $PSScriptRoot) 'screenshots\video_sheet.png'), [int]$Width = 480)
Add-Type -AssemblyName System.Runtime.WindowsRuntime
Add-Type -AssemblyName System.Drawing
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($op, [type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($op))
    [void]$task.Wait(-1)
    $task.Result
}
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Media.Editing.MediaComposition, Windows.Media.Editing, ContentType = WindowsRuntime]
[void][Windows.Graphics.Imaging.ImageStream, Windows.Graphics.Imaging, ContentType = WindowsRuntime]

$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Resolve-Path $Video).Path)) ([Windows.Storage.StorageFile])
$clip = Await ([Windows.Media.Editing.MediaClip]::CreateFromFileAsync($file)) ([Windows.Media.Editing.MediaClip])
$composition = New-Object Windows.Media.Editing.MediaComposition
# The clip list is a WinRT vector: its .NET Add isn't reachable from PowerShell directly.
[System.Collections.Generic.ICollection[Windows.Media.Editing.MediaClip]].GetMethod('Add').Invoke($composition.Clips, @($clip))
$length = $clip.OriginalDuration.TotalSeconds
if ($To -lt 0 -or $To -gt $length) { $To = $length }
Write-Output ('{0:F1} s long; frames from {1:F1} to {2:F1} s' -f $length, $From, $To)

$frames = @()
for ($i = 0; $i -lt $Count; ++$i) {
    $t = $From + ($To - $From) * $i / [Math]::Max(1, $Count - 1)
    $t = [Math]::Min($t, $length - 0.05)
    # A request now and then fails; a second try usually works.
    $stream = $null
    for ($try = 0; $try -lt 3 -and -not $stream; ++$try) {
        try {
            $op = $composition.GetThumbnailAsync([TimeSpan]::FromSeconds($t), $Width, 0, [Windows.Media.Editing.VideoFramePrecision]::NearestFrame)
            $stream = Await $op ([Windows.Graphics.Imaging.ImageStream])
        } catch { Start-Sleep -Milliseconds 200 }
    }
    if (-not $stream) { Write-Output ('no frame at {0:F2} s' -f $t); continue }
    $net = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream)
    $memory = New-Object System.IO.MemoryStream
    $net.CopyTo($memory); $net.Dispose()
    $memory.Position = 0
    $frames += , @($t, [System.Drawing.Image]::FromStream($memory))
}
if (-not $frames) { Write-Output 'no frames'; exit 1 }
$w = $frames[0][1].Width; $h = $frames[0][1].Height
$cols = 4; $rows = [Math]::Ceiling($frames.Count / $cols)
$sheet = New-Object System.Drawing.Bitmap ($cols * $w), ($rows * $h)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$font = New-Object System.Drawing.Font 'Arial', 12
for ($i = 0; $i -lt $frames.Count; ++$i) {
    $x = ($i % $cols) * $w; $y = [Math]::Floor($i / $cols) * $h
    $g.DrawImage($frames[$i][1], $x, $y, $w, $h)
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 70, 20)
    $g.DrawString(('{0:F2} s' -f $frames[$i][0]), $font, [System.Drawing.Brushes]::Yellow, $x + 3, $y + 2)
    $frames[$i][1].Dispose()
}
$g.Dispose()
$sheet.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
Write-Output "sheet $Out"
