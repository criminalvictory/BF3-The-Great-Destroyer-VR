# Speaks a line out loud with Windows' "Mark" voice, so test instructions can be heard in the
# headset. Usage: tools\say.ps1 "Press Insert now."
param([Parameter(Mandatory)] [string]$Text)

Add-Type -AssemblyName System.Runtime.WindowsRuntime
[void][Windows.Media.SpeechSynthesis.SpeechSynthesizer, Windows.Media.SpeechSynthesis, ContentType = WindowsRuntime]
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
$synth = New-Object Windows.Media.SpeechSynthesis.SpeechSynthesizer
$mark = [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices | Where-Object { $_.DisplayName -like '*Mark*' } | Select-Object -First 1
if ($mark) { $synth.Voice = $mark }
$task = $asTask.MakeGenericMethod([Windows.Media.SpeechSynthesis.SpeechSynthesisStream]).Invoke($null, @($synth.SynthesizeTextToStreamAsync($Text)))
$stream = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($task.Result)
$player = New-Object System.Media.SoundPlayer $stream
$player.PlaySync()
