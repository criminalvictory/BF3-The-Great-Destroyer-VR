# Rings the user when a test is ready and the headset is off: loud alternating beeps, then a spoken
# line in the "Mark" voice, played on every speaker at once (with SteamVR running Windows' default
# output is the headset, which may be lying on the desk). Usage: tools\alarm.ps1 "Ready to test."
param([string]$Text = 'Ready for you to test.', [int]$Rings = 3)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public static class Alarm {
    [StructLayout(LayoutKind.Sequential)]
    struct WaveFormat { public short tag, channels; public int rate, bytesPerSecond; public short blockAlign, bits, extra; }
    [StructLayout(LayoutKind.Sequential)]
    struct WaveHeader { public IntPtr data; public int length, recorded; public IntPtr user; public int flags, loops; public IntPtr next, reserved; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WaveOutCaps {
        public short mid, pid; public int driverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string name;
        public int formats; public short channels, reserved; public int support;
    }
    [DllImport("winmm.dll")] static extern int waveOutGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern int waveOutGetDevCapsW(IntPtr id, ref WaveOutCaps caps, int size);
    [DllImport("winmm.dll")] static extern int waveOutOpen(out IntPtr handle, IntPtr device, ref WaveFormat format, IntPtr callback, IntPtr instance, int flags);
    [DllImport("winmm.dll")] static extern int waveOutPrepareHeader(IntPtr handle, IntPtr header, int size);
    [DllImport("winmm.dll")] static extern int waveOutWrite(IntPtr handle, IntPtr header, int size);
    [DllImport("winmm.dll")] static extern int waveOutUnprepareHeader(IntPtr handle, IntPtr header, int size);
    [DllImport("winmm.dll")] static extern int waveOutReset(IntPtr handle);
    [DllImport("winmm.dll")] static extern int waveOutClose(IntPtr handle);

    // Alternating 880 / 1320 Hz bursts, 16-bit mono at 44.1 kHz, near full scale.
    public static byte[] Beeps(int rings) {
        const int rate = 44100;
        var samples = new List<short>();
        for (int r = 0; r < rings; ++r) {
            for (int b = 0; b < 6; ++b) {
                double f = b % 2 == 0 ? 880 : 1320;
                int n = rate * 15 / 100;
                for (int i = 0; i < n; ++i) {
                    double fade = Math.Min(1.0, Math.Min(i, n - i) / 200.0);
                    samples.Add((short)(Math.Sin(2 * Math.PI * f * i / rate) * 30000 * fade));
                }
            }
            for (int i = 0; i < rate * 4 / 10; ++i) samples.Add(0);
        }
        var bytes = new byte[samples.Count * 2];
        Buffer.BlockCopy(samples.ToArray(), 0, bytes, 0, bytes.Length);
        return bytes;
    }

    // Plays PCM on every output device at once; returns the devices' names.
    public static string PlayEverywhere(byte[] pcm, int rate, int channels, int bits) {
        var format = new WaveFormat { tag = 1, channels = (short)channels, rate = rate, bits = (short)bits };
        format.blockAlign = (short)(channels * bits / 8);
        format.bytesPerSecond = rate * format.blockAlign;
        IntPtr data = Marshal.AllocHGlobal(pcm.Length);
        Marshal.Copy(pcm, 0, data, pcm.Length);
        var open = new List<KeyValuePair<IntPtr, IntPtr>>();
        var names = new List<string>();
        int headerSize = Marshal.SizeOf(typeof(WaveHeader));
        for (int d = 0; d < waveOutGetNumDevs(); ++d) {
            var caps = new WaveOutCaps();
            waveOutGetDevCapsW((IntPtr)d, ref caps, Marshal.SizeOf(typeof(WaveOutCaps)));
            IntPtr handle;
            if (waveOutOpen(out handle, (IntPtr)d, ref format, IntPtr.Zero, IntPtr.Zero, 0) != 0) continue;
            IntPtr header = Marshal.AllocHGlobal(headerSize);
            Marshal.StructureToPtr(new WaveHeader { data = data, length = pcm.Length }, header, false);
            waveOutPrepareHeader(handle, header, headerSize);
            waveOutWrite(handle, header, headerSize);
            open.Add(new KeyValuePair<IntPtr, IntPtr>(handle, header));
            names.Add(caps.name);
        }
        int waitMs = (int)(1000L * pcm.Length / format.bytesPerSecond) + 3000;
        for (int t = 0; t < waitMs; t += 50) {
            bool done = true;
            foreach (var o in open)
                if ((((WaveHeader)Marshal.PtrToStructure(o.Value, typeof(WaveHeader))).flags & 1) == 0) done = false;
            if (done) break;
            Thread.Sleep(50);
        }
        foreach (var o in open) {
            waveOutReset(o.Key);
            waveOutUnprepareHeader(o.Key, o.Value, headerSize);
            waveOutClose(o.Key);
            Marshal.FreeHGlobal(o.Value);
        }
        Marshal.FreeHGlobal(data);
        return string.Join(", ", names);
    }

    // The PCM in a RIFF/WAVE file: (data, rate, channels, bits).
    public static Tuple<byte[], int, int, int> ReadWave(byte[] wav) {
        int rate = 0, channels = 0, bits = 0, p = 12;
        while (p + 8 <= wav.Length) {
            string id = System.Text.Encoding.ASCII.GetString(wav, p, 4);
            int size = BitConverter.ToInt32(wav, p + 4);
            if (id == "fmt ") { channels = BitConverter.ToInt16(wav, p + 10); rate = BitConverter.ToInt32(wav, p + 12); bits = BitConverter.ToInt16(wav, p + 22); }
            if (id == "data") {
                var pcm = new byte[Math.Min(size, wav.Length - p - 8)];
                Array.Copy(wav, p + 8, pcm, 0, pcm.Length);
                return Tuple.Create(pcm, rate, channels, bits);
            }
            p += 8 + size + (size & 1);
        }
        return null;
    }
}
'@

$devices = [Alarm]::PlayEverywhere([Alarm]::Beeps($Rings), 44100, 1, 16)

# The spoken line, synthesised as in say.ps1 and played everywhere too.
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
$memory = New-Object System.IO.MemoryStream
$stream.CopyTo($memory)
$wave = [Alarm]::ReadWave($memory.ToArray())
if ($wave) { [void][Alarm]::PlayEverywhere($wave.Item1, $wave.Item2, $wave.Item3, $wave.Item4) }
"Rang on: $devices"
