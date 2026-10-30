// What the PC plays, recorded (WASAPI loopback of the default output), to cut the game's own
// sounds from: the reload's magazine out, magazine in and bolt release, played again by the mod
// when the left hand does them (src/sounds.cpp).
//   capture record <seconds> <out.wav>         records, in the output's own format
//   capture loud <in.wav>                      how loud each 10 ms is, in dB, with the peaks
//   capture cut <in.wav> <start s> <length s> <out.wav> [gain]   a mono 16-bit clip, faded in and out
//   capture sessions                           each app's audio and its volume slider
#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Wave {
    int channels = 0, rate = 0;
    std::vector<float> samples;  // interleaved
};

bool WriteWav(const wchar_t* path, const WAVEFORMATEX* format, const std::vector<BYTE>& data) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") || !f)
        return false;
    const DWORD formatSize = sizeof(WAVEFORMATEX) + format->cbSize;
    const DWORD dataSize = static_cast<DWORD>(data.size());
    const DWORD riffSize = 4 + 8 + formatSize + 8 + dataSize;
    fwrite("RIFF", 1, 4, f), fwrite(&riffSize, 4, 1, f), fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f), fwrite(&formatSize, 4, 1, f), fwrite(format, 1, formatSize, f);
    fwrite("data", 1, 4, f), fwrite(&dataSize, 4, 1, f), fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

// 32-bit float or 16-bit PCM, plain or extensible.
bool ReadWav(const wchar_t* path, Wave& w) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") || !f)
        return false;
    std::vector<BYTE> all;
    BYTE buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
        all.insert(all.end(), buffer, buffer + n);
    fclose(f);
    if (all.size() < 12 || std::memcmp(all.data(), "RIFF", 4) || std::memcmp(all.data() + 8, "WAVE", 4))
        return false;
    WAVEFORMATEX fmt{};
    bool isFloat = false;
    size_t at = 12;
    while (at + 8 <= all.size()) {
        DWORD size = 0;
        std::memcpy(&size, all.data() + at + 4, 4);
        const BYTE* body = all.data() + at + 8;
        if (!std::memcmp(all.data() + at, "fmt ", 4)) {
            std::memcpy(&fmt, body, std::min<size_t>(size, sizeof(fmt)));
            isFloat = fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (fmt.wFormatTag == WAVE_FORMAT_EXTENSIBLE && size >= sizeof(WAVEFORMATEXTENSIBLE))
                isFloat = !std::memcmp(&reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(body)->SubFormat,
                                       &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID));
        } else if (!std::memcmp(all.data() + at, "data", 4)) {
            w.channels = fmt.nChannels;
            w.rate = static_cast<int>(fmt.nSamplesPerSec);
            const size_t count = std::min<size_t>(size, all.size() - (at + 8)) / (fmt.wBitsPerSample / 8);
            w.samples.resize(count);
            for (size_t i = 0; i < count; ++i) {
                if (isFloat && fmt.wBitsPerSample == 32)
                    std::memcpy(&w.samples[i], body + i * 4, 4);
                else if (fmt.wBitsPerSample == 16)
                    w.samples[i] = reinterpret_cast<const short*>(body)[i] / 32768.0f;
                else
                    return false;
            }
            return w.channels > 0;
        }
        at += 8 + size + (size & 1);
    }
    return false;
}

int Record(double seconds, const wchar_t* path) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) ||
        FAILED(client->GetMixFormat(&format)) ||
        FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, format, nullptr)) ||
        FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture)))) {
        std::printf("can't open the default output for recording\n");
        return 1;
    }
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
            std::wprintf(L"recording %ls: %d channels, %d Hz, %d bits\n", name.pwszVal, format->nChannels,
                         static_cast<int>(format->nSamplesPerSec), format->wBitsPerSample);
        PropVariantClear(&name);
        props->Release();
    }
    std::vector<BYTE> data;
    client->Start();
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000.0);
    const UINT64 framesWanted = static_cast<UINT64>(seconds * format->nSamplesPerSec);
    UINT64 frames = 0;
    while (GetTickCount64() < end) {
        Sleep(5);
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet) {
            BYTE* buffer = nullptr;
            UINT32 count = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&buffer, &count, &flags, nullptr, nullptr)))
                break;
            const size_t bytes = static_cast<size_t>(count) * format->nBlockAlign;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                data.insert(data.end(), bytes, 0);
            else
                data.insert(data.end(), buffer, buffer + bytes);
            frames += count;
            capture->ReleaseBuffer(count);
        }
    }
    client->Stop();
    std::printf("%llu of %llu frames (the output sends none while nothing plays)\n", frames, framesWanted);
    if (!WriteWav(path, format, data)) {
        std::printf("can't write the file\n");
        return 1;
    }
    std::printf("saved\n");
    return 0;
}

int Loud(const wchar_t* path) {
    Wave w;
    if (!ReadWav(path, w)) {
        std::printf("can't read it\n");
        return 1;
    }
    const size_t window = static_cast<size_t>(w.rate / 100);  // 10 ms
    const size_t frames = w.samples.size() / w.channels;
    std::printf("%d channels, %d Hz, %.2f s\n", w.channels, w.rate, static_cast<double>(frames) / w.rate);
    for (size_t start = 0; start + window <= frames; start += window) {
        double sum = 0.0, peak = 0.0;
        for (size_t i = start; i < start + window; ++i)
            for (int c = 0; c < w.channels; ++c) {
                const double v = w.samples[i * w.channels + c];
                sum += v * v;
                peak = std::max(peak, std::fabs(v));
            }
        const double rms = std::sqrt(sum / (window * w.channels));
        const double db = 20.0 * std::log10(std::max(rms, 1e-6));
        const int bar = std::clamp(static_cast<int>((db + 60.0) / 1.5), 0, 40);
        std::printf("%7.2f s %6.1f dB peak %.3f %s\n", static_cast<double>(start) / w.rate, db, peak,
                    std::string(bar, '#').c_str());
    }
    return 0;
}

// Each app's audio on the default output and its volume (the mixer's slider), read-only.
int Sessions() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioSessionManager2* manager = nullptr;
    IAudioSessionEnumerator* sessions = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&manager))) ||
        FAILED(manager->GetSessionEnumerator(&sessions))) {
        std::printf("can't list the sessions\n");
        return 1;
    }
    int count = 0;
    sessions->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* control = nullptr;
        IAudioSessionControl2* control2 = nullptr;
        ISimpleAudioVolume* volume = nullptr;
        if (FAILED(sessions->GetSession(i, &control)))
            continue;
        DWORD pid = 0;
        float level = -1.0f;
        BOOL mute = FALSE;
        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&control2)))) {
            control2->GetProcessId(&pid);
            control2->Release();
        }
        if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&volume)))) {
            volume->GetMasterVolume(&level);
            volume->GetMute(&mute);
            volume->Release();
        }
        wchar_t name[MAX_PATH] = L"?";
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            DWORD size = MAX_PATH;
            QueryFullProcessImageNameW(p, 0, name, &size);
            CloseHandle(p);
        }
        std::wprintf(L"  pid %5lu volume %.2f%s  %ls\n", pid, level, mute ? L" muted" : L"", name);
        control->Release();
    }
    return 0;
}

// gain: undoes the game's volume slider at recording time (capture sessions shows it), so the clip
// is as loud as the game made it, and the slider then sets the mod's sounds as it does the game's.
int Cut(const wchar_t* in, double start, double length, const wchar_t* out, double gain) {
    Wave w;
    if (!ReadWav(in, w)) {
        std::printf("can't read it\n");
        return 1;
    }
    const size_t frames = w.samples.size() / w.channels;
    const size_t first = std::min(frames, static_cast<size_t>(start * w.rate));
    const size_t count = std::min(frames - first, static_cast<size_t>(length * w.rate));
    const size_t fade = static_cast<size_t>(0.004 * w.rate);  // 4 ms each end, no clicks
    std::vector<BYTE> data(count * 2);
    double peak = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double v = 0.0;
        for (int c = 0; c < w.channels; ++c)
            v += w.samples[(first + i) * w.channels + c];
        peak = std::max(peak, std::fabs(v / w.channels));
    }
    for (size_t i = 0; i < count; ++i) {
        double v = 0.0;
        for (int c = 0; c < w.channels; ++c)
            v += w.samples[(first + i) * w.channels + c];
        v /= w.channels;
        double fadeGain = 1.0;
        if (i < fade)
            fadeGain = static_cast<double>(i) / fade;
        else if (count - i < fade)
            fadeGain = static_cast<double>(count - i) / fade;
        const short s = static_cast<short>(std::clamp(v * fadeGain * gain * 32767.0, -32768.0, 32767.0));
        std::memcpy(&data[i * 2], &s, 2);
    }
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, static_cast<DWORD>(w.rate), static_cast<DWORD>(w.rate * 2), 2, 16, 0};
    if (!WriteWav(out, &format, data)) {
        std::printf("can't write the clip\n");
        return 1;
    }
    std::printf("clip %.3f s from %.3f s, peak %.3f, x%.2f: %.3f\n", static_cast<double>(count) / w.rate, start, peak,
                gain, peak * gain);
    return 0;
}

}

int wmain(int argc, wchar_t** argv) {
    const std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"record" && argc > 3)
        return Record(_wtof(argv[2]), argv[3]);
    if (mode == L"loud" && argc > 2)
        return Loud(argv[2]);
    if (mode == L"cut" && argc > 5)
        return Cut(argv[2], _wtof(argv[3]), _wtof(argv[4]), argv[5], argc > 6 ? _wtof(argv[6]) : 1.0);
    if (mode == L"sessions")
        return Sessions();
    std::printf("capture record <seconds> <out.wav> | loud <in.wav> | cut <in.wav> <start> <length> <out.wav>\n");
    return 1;
}
