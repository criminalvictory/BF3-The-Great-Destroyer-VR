#include "sounds.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "log.h"

namespace {

struct Clip {
    WAVEFORMATEX format{};
    std::shared_ptr<std::vector<BYTE>> data;  // kept by each voice playing it, past a reload
    FILETIME written{};
};

struct Voice {
    IXAudio2SourceVoice* voice;
    std::shared_ptr<std::vector<BYTE>> data;
};

std::mutex g_mutex;  // the queue
std::condition_variable g_wake;
std::deque<std::pair<std::string, float>> g_queue;  // what to play, and where
// The rest is the sounds' thread's own.
IXAudio2* g_audio = nullptr;
IXAudio2MasteringVoice* g_master = nullptr;
UINT32 g_outputs = 2;
bool g_failed = false;
std::map<std::string, Clip> g_clips;
std::vector<Voice> g_voices;  // playing or done, destroyed once done

std::wstring ClipPath(const char* name) {
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ClipPath), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p = path;
    p.resize(p.find_last_of(L'\\') + 1);
    p += L"sounds\\";
    for (const char* c = name; *c; ++c)
        p += static_cast<wchar_t>(*c);
    return p + L".wav";
}

// A plain PCM wave file (what capture cut writes).
bool Load(const std::wstring& path, Clip& clip) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") || !f)
        return false;
    std::vector<BYTE> all;
    BYTE buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
        all.insert(all.end(), buffer, buffer + n);
    fclose(f);
    if (all.size() < 12 || std::memcmp(all.data(), "RIFF", 4) || std::memcmp(all.data() + 8, "WAVE", 4))
        return false;
    bool haveFormat = false;
    for (size_t at = 12; at + 8 <= all.size();) {
        DWORD size = 0;
        std::memcpy(&size, all.data() + at + 4, 4);
        size = static_cast<DWORD>(std::min<size_t>(size, all.size() - (at + 8)));
        if (!std::memcmp(all.data() + at, "fmt ", 4) && size >= 16) {
            std::memcpy(&clip.format, all.data() + at + 8, 16);
            clip.format.cbSize = 0;
            haveFormat = clip.format.wFormatTag == WAVE_FORMAT_PCM;
        } else if (!std::memcmp(all.data() + at, "data", 4) && haveFormat) {
            clip.data = std::make_shared<std::vector<BYTE>>(all.data() + at + 8, all.data() + at + 8 + size);
            return !clip.data->empty();
        }
        at += 8 + size + (size & 1);
    }
    return false;
}

// On the sounds' own thread, which has COM (XAudio2 finds the output through it) in the
// multithreaded model: on a game thread, whatever model the game chose there, the voices started
// but nothing was heard (2026-10-30).
bool Start() {
    if (g_audio)
        return true;
    if (g_failed)
        return false;
    g_failed = true;  // one try
    HRESULT hr = XAudio2Create(&g_audio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        Log("Sounds: XAudio2 didn't start (%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    // The default output as Windows names it for games and media (eConsole), where the game plays.
    std::wstring device;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* endpoint = nullptr;
    LPWSTR id = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&enumerator))) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint)) && SUCCEEDED(endpoint->GetId(&id))) {
        device = id;
        CoTaskMemFree(id);
    }
    if (endpoint)
        endpoint->Release();
    if (enumerator)
        enumerator->Release();
    hr = g_audio->CreateMasteringVoice(&g_master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0,
                                       device.empty() ? nullptr : device.c_str());
    if (FAILED(hr)) {
        Log("Sounds: no output for XAudio2 (%08lX)", static_cast<unsigned long>(hr));
        g_audio->Release();
        g_audio = nullptr;
        return false;
    }
    XAUDIO2_VOICE_DETAILS details{};
    g_master->GetVoiceDetails(&details);
    g_outputs = details.InputChannels;
    g_failed = false;
    Log("Sounds: ready (%u output channels, %u Hz, %s)", g_outputs, details.InputSampleRate,
        device.empty() ? "XAudio2's default output" : "the default output for games");
    return true;
}

void PlayNow(const char* name, float pan) {
    if (!Start())
        return;
    // Done voices go first.
    for (auto it = g_voices.begin(); it != g_voices.end();) {
        XAUDIO2_VOICE_STATE state{};
        it->voice->GetState(&state);
        if (state.BuffersQueued == 0) {
            it->voice->DestroyVoice();
            it = g_voices.erase(it);
        } else {
            ++it;
        }
    }
    const std::wstring path = ClipPath(name);
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        return;
    Clip& clip = g_clips[name];
    if (CompareFileTime(&info.ftLastWriteTime, &clip.written) != 0) {
        clip = Clip{};
        if (!Load(path, clip)) {
            Log("Sounds: %s.wav can't be read (16-bit PCM wanted)", name);
            return;
        }
        clip.written = info.ftLastWriteTime;
        Log("Sounds: %s.wav loaded, %.2f s", name,
            static_cast<double>(clip.data->size()) / clip.format.nAvgBytesPerSec);
    }
    if (!clip.data || clip.data->empty() || g_voices.size() > 16)
        return;
    IXAudio2SourceVoice* voice = nullptr;
    if (HRESULT hr = g_audio->CreateSourceVoice(&voice, &clip.format); FAILED(hr)) {
        Log("Sounds: %s can't play (%08lX)", name, static_cast<unsigned long>(hr));
        return;
    }
    // Equal-power pan of the clip (mono or each channel the same) between the first two outputs,
    // as loud in the middle as the game played it (the clip is its two channels averaged).
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    constexpr float kMiddle = 1.41421356f;
    const UINT32 inputs = clip.format.nChannels;
    std::vector<float> matrix(inputs * g_outputs, 0.0f);
    for (UINT32 i = 0; i < inputs; ++i) {
        matrix[0 * inputs + i] = kMiddle * std::cos(angle) / inputs;
        if (g_outputs > 1)
            matrix[1 * inputs + i] = kMiddle * std::sin(angle) / inputs;
    }
    voice->SetOutputMatrix(g_master, inputs, g_outputs, matrix.data());
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(clip.data->size());
    buffer.pAudioData = clip.data->data();
    if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start())) {
        Log("Sounds: %s didn't start", name);
        voice->DestroyVoice();
        return;
    }
    g_voices.push_back({voice, clip.data});
    // The first few plays: is the engine running it? (In the game nothing was heard, 2026-10-30.)
    static int s_checks = 6;
    if (s_checks > 0) {
        --s_checks;
        Sleep(60);
        XAUDIO2_VOICE_STATE state{};
        voice->GetState(&state);
        XAUDIO2_PERFORMANCE_DATA perf{};
        g_audio->GetPerformanceData(&perf);
        Log("Sounds: %s after 60 ms: %llu samples played, %u voices active, latency %u samples, %u glitches", name,
            static_cast<unsigned long long>(state.SamplesPlayed), perf.ActiveSourceVoiceCount,
            perf.CurrentLatencyInSamples, perf.GlitchesSinceEngineStarted);
    }
}

// The sounds' thread: plays what Play asked for, in order.
void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        std::pair<std::string, float> next;
        {
            std::unique_lock lock(g_mutex);
            g_wake.wait(lock, [] { return !g_queue.empty(); });
            next = std::move(g_queue.front());
            g_queue.pop_front();
        }
        PlayNow(next.first.c_str(), next.second);
    }
}

}

namespace sounds {

void Play(const char* name, float pan) {
    static std::once_flag started;
    std::call_once(started, [] { std::thread(Worker).detach(); });
    {
        std::lock_guard lock(g_mutex);
        if (g_queue.size() < 16)
            g_queue.emplace_back(name, pan);
    }
    g_wake.notify_one();
}

}
