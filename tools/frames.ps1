# Pulls evenly spaced frames out of a video with Windows' own media player (no ffmpeg) and
# saves them as one labelled contact sheet.
# Usage: powershell -STA -ExecutionPolicy Bypass -File tools\frames.ps1 -Video <file> [-Count 20] [-Start 0] [-End 0]
param(
    [Parameter(Mandatory)] [string]$Video,
    [int]$Count = 20,
    [double]$Start = 0,
    [double]$End = 0,  # 0 = end of the video
    [int]$ThumbWidth = 400,
    [int]$Columns = 4
)

Add-Type -AssemblyName PresentationCore, WindowsBase, System.Drawing

# WPF's MediaPlayer only raises events and decodes frames while its dispatcher runs.
function Pump([int]$ms) {
    $frame = New-Object System.Windows.Threading.DispatcherFrame
    $timer = New-Object System.Windows.Threading.DispatcherTimer
    $timer.Interval = [TimeSpan]::FromMilliseconds($ms)
    $timer.Add_Tick({ $frame.Continue = $false; $timer.Stop() }.GetNewClosure())
    $timer.Start()
    [System.Windows.Threading.Dispatcher]::PushFrame($frame)
}

$path = (Resolve-Path $Video).Path
$player = New-Object System.Windows.Media.MediaPlayer
$player.ScrubbingEnabled = $true
$player.IsMuted = $true
$player.Open([Uri]$path)
for ($i = 0; $i -lt 100 -and -not $player.NaturalDuration.HasTimeSpan; $i++) { Pump 100 }
if (-not $player.NaturalDuration.HasTimeSpan) { Write-Host 'Could not open the video.'; exit 1 }
$player.Pause()

$duration = $player.NaturalDuration.TimeSpan.TotalSeconds
$vw = $player.NaturalVideoWidth
$vh = $player.NaturalVideoHeight
if ($End -le 0 -or $End -gt $duration) { $End = $duration }
Write-Host ('Video {0}x{1}, {2:N1}s; taking {3} frames from {4:N1}s to {5:N1}s' -f $vw, $vh, $duration, $Count, $Start, $End)

$cols = $Columns
$tw = $ThumbWidth
$th = [int]($vh * $tw / $vw)
$rows = [math]::Ceiling($Count / $cols)
$sheet = New-Object System.Drawing.Bitmap ($cols * $tw), ($rows * $th)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$font = New-Object System.Drawing.Font 'Arial', 14, ([System.Drawing.FontStyle]::Bold)

for ($i = 0; $i -lt $Count; $i++) {
    $t = $Start + ($End - $Start) * $i / [math]::Max(1, $Count - 1)
    if ($t -ge $duration) { $t = $duration - 0.1 }
    $player.Position = [TimeSpan]::FromSeconds($t)
    Pump 400

    # Render the current frame at thumbnail size and hand it to System.Drawing via PNG.
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen()
    $dc.DrawVideo($player, (New-Object System.Windows.Rect 0, 0, $tw, $th))
    $dc.Close()
    $rtb = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $tw, $th, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $rtb.Render($visual)
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($rtb))
    $ms = New-Object System.IO.MemoryStream
    $encoder.Save($ms)
    $img = [System.Drawing.Image]::FromStream($ms)

    $x = ($i % $cols) * $tw
    $y = [math]::Floor($i / $cols) * $th
    $g.DrawImage($img, $x, $y, $tw, $th)
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, 70, 24)
    $g.DrawString(('{0:N1}s' -f $t), $font, [System.Drawing.Brushes]::Yellow, ($x + 4), ($y + 2))
    $img.Dispose()
    $ms.Dispose()
}
$g.Dispose()
$player.Close()

$dir = Join-Path (Split-Path $PSScriptRoot) 'screenshots'
New-Item -ItemType Directory -Force $dir | Out-Null
$out = Join-Path $dir ('frames_{0}.png' -f [IO.Path]::GetFileNameWithoutExtension($path).Replace(' ', '_'))
$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "Saved $out"
