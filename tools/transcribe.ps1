# Transcribes what's said in a video with Windows' own components (no downloads): its audio track
# turned into a 16 kHz mono WAV by Windows.Media.Transcoding, then System.Speech's dictation over it.
# Rough (the desktop dictation engine), but enough to follow what the user said in a recording.
#   tools\transcribe.ps1 -Video 'C:\Users\me\Videos\clip.mov' [-Wav out.wav]
param([Parameter(Mandatory)] [string]$Video,
      [string]$Wav = (Join-Path (Split-Path $PSScriptRoot) 'screenshots\transcribe.wav'))
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskOp = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
$asTaskOpProgress = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncActionWithProgress`1' })[0]
function Await($op, [type]$type) {
    $task = $asTaskOp.MakeGenericMethod($type).Invoke($null, @($op)); [void]$task.Wait(-1); $task.Result
}
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Media.Transcoding.MediaTranscoder, Windows.Media.Transcoding, ContentType = WindowsRuntime]
[void][Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
$src = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($Video)) ([Windows.Storage.StorageFile])
$dir = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync((Split-Path $Wav))) ([Windows.Storage.StorageFolder])
$dst = Await ($dir.CreateFileAsync((Split-Path $Wav -Leaf), [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
$profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateWav([Windows.Media.MediaProperties.AudioEncodingQuality]::Low)
$profile.Audio.SampleRate = 16000; $profile.Audio.ChannelCount = 1; $profile.Audio.BitsPerSample = 16
$profile.Video = $null
$transcoder = New-Object Windows.Media.Transcoding.MediaTranscoder
$prep = Await ($transcoder.PrepareFileTranscodeAsync($src, $dst, $profile)) ([Windows.Media.Transcoding.PrepareTranscodeResult])
if (-not $prep.CanTranscode) { Write-Host "can't take the audio out: $($prep.FailureReason)"; exit 1 }
$t = $asTaskOpProgress.MakeGenericMethod([double]).Invoke($null, @($prep.TranscodeAsync())); [void]$t.Wait(-1)
Write-Host "audio: $Wav ($([math]::Round((Get-Item $Wav).Length / 32000)) s)"
Add-Type -AssemblyName System.Speech
$engine = New-Object System.Speech.Recognition.SpeechRecognitionEngine ([Globalization.CultureInfo]'en-US')
$engine.LoadGrammar((New-Object System.Speech.Recognition.DictationGrammar))
# Its samples straight from the data chunk: the transcoder writes an extensible WAV header that
# SetInputToWaveFile turns down.
$bytes = [IO.File]::ReadAllBytes($Wav)
$at = 12; $rate = 16000; $channels = 1; $bits = 16
while ($at + 8 -le $bytes.Length) {
    $id = [Text.Encoding]::ASCII.GetString($bytes, $at, 4)
    if ($id -eq 'fmt ') {
        $channels = [BitConverter]::ToInt16($bytes, $at + 10); $rate = [BitConverter]::ToInt32($bytes, $at + 12)
        $bits = [BitConverter]::ToInt16($bytes, $at + 22)
    }
    if ($id -eq 'data') { break }
    $at += 8 + [BitConverter]::ToInt32($bytes, $at + 4)
}
Write-Host "format: $rate Hz, $channels channel(s), $bits bits"
$stream = New-Object IO.MemoryStream (, $bytes[($at + 8)..($bytes.Length - 1)])
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo $rate, ([System.Speech.AudioFormat.AudioBitsPerSample]$bits), ([System.Speech.AudioFormat.AudioChannel]$channels)
$engine.SetInputToAudioStream($stream, $format)
$engine.BabbleTimeout = [TimeSpan]::FromSeconds(0); $engine.EndSilenceTimeout = [TimeSpan]::FromSeconds(0.6)
# Until the stream is read to the end (a pause gives no result, not the end).
$total = [TimeSpan]::FromSeconds(($bytes.Length - $at - 8) / ($rate * $channels * $bits / 8))
$misses = 0
while ($engine.RecognizerAudioPosition -lt $total.Subtract([TimeSpan]::FromSeconds(0.5)) -and $misses -lt 200) {
    try { $r = $engine.Recognize() } catch { break }
    if (-not $r) { ++$misses; continue }
    '[{0:mm\:ss}] {1}' -f $engine.RecognizerAudioPosition, $r.Text
}
$engine.Dispose()
