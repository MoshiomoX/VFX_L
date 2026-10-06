// ============================================================
// AudioSystem.cpp
// miniaudio の高水準 API（ma_engine）。効果音は resource manager に全部デコードして登録し、
// 鳴らす度に声（ma_sound）を 1 つ作る（同じデータを共有するので軽い）。声は 64 個の溜めから使い回す
// ============================================================
#include "Audio/AudioSystem.h"
#include "ThirdParty/miniaudio/miniaudio.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <random>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#endif

using json = nlohmann::json;

namespace
{
    constexpr const char* kCuePath = "Assets/Data/Audio/Sounds.json";
    constexpr const char* kSettingsPath = "Assets/Data/Audio/Settings.json";
    constexpr int kVoiceCount = 64;
    const char* const kBusName[] = { "master", "sfx", "music", "ui" };

    // 使っている曲（title / stage / final / boss）を先に、試聴用の alt_* は後ろに名前順
    bool MusicOrder(const std::string& a, const std::string& b)
    {
        const bool altA = a.rfind("alt_", 0) == 0, altB = b.rfind("alt_", 0) == 0;
        return (altA != altB) ? !altA : a < b;
    }

#ifdef _WIN32
    // TEMP-TEST: このプログラムの音声 session の音量計（DebugOutputPeak）。見つけたら持っておき、読めなくなったら探し直す
    Microsoft::WRL::ComPtr<IAudioMeterInformation> g_SessionMeter;

    bool FindSessionMeter()
    {
        using Microsoft::WRL::ComPtr;
        g_SessionMeter.Reset();
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // miniaudio が主スレッドで済ませている。違う型で済んでいても使える
        ComPtr<IMMDeviceEnumerator> en;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return false;
        ComPtr<IMMDevice> dev;
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return false;
        ComPtr<IAudioSessionManager2> mgr;
        if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)mgr.GetAddressOf()))) return false;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(mgr->GetSessionEnumerator(&sessions))) return false;
        int n = 0;
        sessions->GetCount(&n);
        const DWORD pid = GetCurrentProcessId();
        for (int i = 0; i < n; ++i)
        {
            ComPtr<IAudioSessionControl> ctl;
            ComPtr<IAudioSessionControl2> ctl2;
            DWORD p = 0;
            if (FAILED(sessions->GetSession(i, &ctl)) || FAILED(ctl.As(&ctl2)) || FAILED(ctl2->GetProcessId(&p)) || p != pid) continue;
            if (SUCCEEDED(ctl.As(&g_SessionMeter))) return true;
        }
        return false;
    }
#endif
}

struct AudioSystem::Impl
{
    struct Cue
    {
        std::vector<std::string> files;
        float volume = 1.0f;
        float pitchMin = 1.0f, pitchMax = 1.0f;
        float minInterval = 0.0f;   // 秒。これより短い間隔の呼び出しは鳴らさない
        int   maxVoices = 4;        // 同じ cue が同時に鳴る上限
        Bus   bus = Bus::Sfx;
        double lastPlay = -1.0e9;
        uint32_t plays = 0, skipped = 0;   // パネルの表示用
    };
    struct Track
    {
        std::string file;
        float volume = 1.0f;
        float loopStart = 0.0f;   // 秒。2 周目からはここへ戻る（1 周目は曲の頭から。Tools の loopify で作った曲）
    };

    // 出口のリミッター（2026-10-03）。乱戦で命中・撃破・爆発の音が何百も重なるとピーク値が 1.8 まで行き、割れていた。
    // 全部の音を混ぜた後（ma_engine の onProcess、音声スレッド）で、峰が kThreshold を超える分だけ音量を下げる
    // （立ち上がり 1 ms・戻り 250 ms）。それでも越えた分は柔らかく丸める（1.0 を超えない）
    struct Limiter
    {
        static constexpr float kThreshold = 0.89f;   // -1 dBFS
        static constexpr float kKnee = 0.9f;
        std::atomic<uint32_t> channels{ 0 };
        float attack = 0.0f, release = 0.0f;
        float gain = 1.0f;
        std::atomic<float> minGain{ 1.0f };          // 自動テスト用: 前に読んでからの一番下げた倍率
        std::atomic<uint32_t> softClipped{ 0 };      // 自動テスト用: 丸めたサンプル数
        std::atomic<float> inPeak{ 0.0f }, outPeak{ 0.0f };   // 自動テスト用: リミットの前 / 後の峰

        void Setup(uint32_t sampleRate, uint32_t ch)
        {
            const float sr = (float)(std::max)(sampleRate, 8000u);
            attack = 1.0f - std::exp(-1.0f / (0.001f * sr));
            release = 1.0f - std::exp(-1.0f / (0.25f * sr));
            channels = ch;
        }

        static void Process(void* user, float* frames, ma_uint64 count)
        {
            Limiter& L = *static_cast<Limiter*>(user);
            const uint32_t ch = L.channels;
            if (ch == 0) return;
            float low = 1.0f, inMax = 0.0f, outMax = 0.0f;
            uint32_t clipped = 0;
            for (ma_uint64 i = 0; i < count; ++i)
            {
                float* f = frames + i * ch;
                float pk = 0.0f;
                for (uint32_t c = 0; c < ch; ++c) pk = (std::max)(pk, std::fabs(f[c]));
                inMax = (std::max)(inMax, pk);
                const float want = (pk > kThreshold) ? kThreshold / pk : 1.0f;
                L.gain += (want - L.gain) * ((want < L.gain) ? L.attack : L.release);
                low = (std::min)(low, L.gain);
                for (uint32_t c = 0; c < ch; ++c)
                {
                    float y = f[c] * L.gain;
                    const float a = std::fabs(y);
                    if (a > kKnee)
                    {
                        y = std::copysign(kKnee + (1.0f - kKnee) * std::tanh((a - kKnee) / (1.0f - kKnee)), y);
                        ++clipped;
                    }
                    f[c] = y;
                    outMax = (std::max)(outMax, std::fabs(y));
                }
            }
            if (low < L.minGain.load()) L.minGain = low;
            if (clipped) L.softClipped += clipped;
            if (inMax > L.inPeak.load()) L.inPeak = inMax;
            if (outMax > L.outPeak.load()) L.outPeak = outMax;
        }
    };
    Limiter limiter;
    struct Voice { ma_sound sound{}; bool inUse = false; std::string cue; };

    ma_engine engine{};
    bool ok = false;
    ma_sound_group groups[3]{};   // Sfx / Music / Ui（Master は ma_engine の音量）
    bool groupOk[3] = {};
    float volumes[(int)Bus::Count] = { 1.0f, 0.8f, 0.6f, 0.8f };

    std::unordered_map<std::string, Cue> cues;
    std::unordered_map<std::string, Track> tracks;
    std::vector<std::string> registered;   // resource manager に登録したファイル（読み直す時に外す）
    std::vector<std::unique_ptr<Voice>> voices;

    std::unique_ptr<ma_sound> music, musicOld;
    std::string musicName;
    std::unique_ptr<ma_sound> ambient;   // 環境音のループ（SetAmbient）
    std::string ambientFile;
    float oldLeft = 0.0f;   // 前の曲がフェードし終わるまでの秒

    double time = 0.0;
    std::mt19937 rng{ 20261003u };

    ma_sound_group* Group(Bus b)
    {
        const int i = (b == Bus::Music) ? 1 : (b == Bus::Ui) ? 2 : 0;
        return groupOk[i] ? &groups[i] : nullptr;
    }

    void ApplyVolumes()
    {
        if (!ok) return;
        ma_engine_set_volume(&engine, volumes[(int)Bus::Master]);
        if (groupOk[0]) ma_sound_group_set_volume(&groups[0], volumes[(int)Bus::Sfx]);
        if (groupOk[1]) ma_sound_group_set_volume(&groups[1], volumes[(int)Bus::Music]);
        if (groupOk[2]) ma_sound_group_set_volume(&groups[2], volumes[(int)Bus::Ui]);
    }

    void StopVoices()
    {
        for (auto& v : voices)
            if (v->inUse)
            {
                ma_sound_uninit(&v->sound);
                v->inUse = false;
            }
    }

    void Unregister()
    {
        if (!ok) return;
        ma_resource_manager* rm = ma_engine_get_resource_manager(&engine);
        for (const std::string& f : registered) ma_resource_manager_unregister_file(rm, f.c_str());
        registered.clear();
    }
};

AudioSystem& AudioSystem::Get()
{
    static AudioSystem s;
    return s;
}

AudioSystem::AudioSystem() : m(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Initialize()
{
    if (m->ok) return true;
    ma_engine_config cfg = ma_engine_config_init();
    cfg.onProcess = &Impl::Limiter::Process;   // 全部を混ぜた後のリミット（Setup までは channels = 0 で素通し）
    cfg.pProcessUserData = &m->limiter;
    if (ma_engine_init(&cfg, &m->engine) != MA_SUCCESS)
    {
        std::cout << "[Audio] no audio device (sound off)" << std::endl;
        return false;
    }
    m->limiter.Setup(ma_engine_get_sample_rate(&m->engine), ma_engine_get_channels(&m->engine));
    m->ok = true;
    for (int i = 0; i < 3; ++i)
        m->groupOk[i] = (ma_sound_group_init(&m->engine, 0, nullptr, &m->groups[i]) == MA_SUCCESS);
    m->voices.clear();
    for (int i = 0; i < kVoiceCount; ++i) m->voices.push_back(std::make_unique<Impl::Voice>());
    LoadSettings();
    // TEMP-TEST: VFXL_AUDIO_MUTE=1 なら主音量 0（自動テストでユーザーの机から音を出さない。鳴らす処理は通るので統計は取れる。保存はしない）
    {
        char* mute = nullptr;
        size_t len = 0;
        if (_dupenv_s(&mute, &len, "VFXL_AUDIO_MUTE") == 0 && mute)
        {
            if (mute[0] == '1') m->volumes[(int)Bus::Master] = 0.0f;
            free(mute);
        }
    }
    m->ApplyVolumes();
    ReloadCues();
    std::cout << "[Audio] miniaudio " << MA_VERSION_STRING << ", " << m->cues.size() << " cues, "
        << m->tracks.size() << " music tracks, device " << DeviceName() << std::endl;
    return true;
}

void AudioSystem::Shutdown()
{
    if (!m || !m->ok) return;
    m->StopVoices();
    if (m->musicOld) { ma_sound_uninit(m->musicOld.get()); m->musicOld.reset(); }
    if (m->music) { ma_sound_uninit(m->music.get()); m->music.reset(); }
    m->musicName.clear();
    if (m->ambient) { ma_sound_uninit(m->ambient.get()); m->ambient.reset(); }
    m->ambientFile.clear();
    m->Unregister();
    for (int i = 0; i < 3; ++i)
        if (m->groupOk[i]) { ma_sound_group_uninit(&m->groups[i]); m->groupOk[i] = false; }
    ma_engine_uninit(&m->engine);
    m->ok = false;
#ifdef _WIN32
    g_SessionMeter.Reset();
#endif
}

bool AudioSystem::IsAvailable() const { return m->ok; }

void AudioSystem::Update(float dt)
{
    if (!m->ok) return;
    m->time += dt;
    for (auto& v : m->voices)
        if (v->inUse && (ma_sound_at_end(&v->sound) || !ma_sound_is_playing(&v->sound)))
        {
            ma_sound_uninit(&v->sound);
            v->inUse = false;
        }
    if (m->musicOld)
    {
        m->oldLeft -= dt;
        if (m->oldLeft <= 0.0f)
        {
            ma_sound_uninit(m->musicOld.get());
            m->musicOld.reset();
        }
    }
}

bool AudioSystem::Play(const std::string& cue, float volume, float pitch)
{
    if (!m->ok) return false;
    auto it = m->cues.find(cue);
    if (it == m->cues.end() || it->second.files.empty()) return false;
    Impl::Cue& c = it->second;
    if (m->time - c.lastPlay < c.minInterval) { ++c.skipped; return false; }

    int active = 0;
    Impl::Voice* freeVoice = nullptr;
    for (auto& v : m->voices)
    {
        if (v->inUse) { if (v->cue == cue) ++active; }
        else if (!freeVoice) freeVoice = v.get();
    }
    if (active >= c.maxVoices || !freeVoice) { ++c.skipped; return false; }

    const std::string& file = c.files[std::uniform_int_distribution<size_t>(0, c.files.size() - 1)(m->rng)];
    if (ma_sound_init_from_file(&m->engine, file.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
            m->Group(c.bus), nullptr, &freeVoice->sound) != MA_SUCCESS)
        return false;
    ma_sound_set_volume(&freeVoice->sound, c.volume * volume);
    const float lo = (std::min)(c.pitchMin, c.pitchMax), hi = (std::max)(c.pitchMin, c.pitchMax);
    ma_sound_set_pitch(&freeVoice->sound, std::uniform_real_distribution<float>(lo, hi + 1e-6f)(m->rng) * pitch);
    ma_sound_start(&freeVoice->sound);
    freeVoice->inUse = true;
    freeVoice->cue = cue;
    c.lastPlay = m->time;
    ++c.plays;
    return true;
}

bool AudioSystem::PlayBurst(const std::string& cue, uint32_t count)
{
    if (count == 0) return false;
    const float vol = (std::min)(1.3f, 1.0f + 0.1f * std::log2((float)count));   // 多いほど少しだけ大きく（乱戦で割れないよう控えめ）
    return Play(cue, vol);
}

void AudioSystem::PlayMusic(const std::string& track, float fadeSec)
{
    if (!m->ok || track == m->musicName) return;
    const ma_uint64 fadeMs = (ma_uint64)((std::max)(fadeSec, 0.0f) * 1000.0f);

    // 前の前の曲（まだフェード中）は即座に止める。今の曲はフェードアウトへ
    if (m->musicOld) { ma_sound_uninit(m->musicOld.get()); m->musicOld.reset(); }
    if (m->music)
    {
        ma_sound_stop_with_fade_in_milliseconds(m->music.get(), fadeMs);
        m->musicOld = std::move(m->music);
        m->oldLeft = fadeSec + 0.2f;
    }
    m->musicName = track;
    if (track.empty()) return;

    auto it = m->tracks.find(track);
    if (it == m->tracks.end() || it->second.file.empty())
    {
        std::cout << "[Audio] music not found: " << track << std::endl;
        return;
    }
    auto s = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file(&m->engine, it->second.file.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
            m->Group(Bus::Music), nullptr, s.get()) != MA_SUCCESS)
    {
        std::cout << "[Audio] music load failed: " << it->second.file << std::endl;
        return;
    }
    ma_sound_set_looping(s.get(), MA_TRUE);
    if (it->second.loopStart > 0.0f)
    {
        // 2 周目からの戻り先（流し読みの decoder も stream の loop point を引き継ぐ）
        ma_uint32 rate = 0;
        ma_sound_get_data_format(s.get(), nullptr, nullptr, &rate, nullptr, 0);
        if (rate > 0)
            ma_data_source_set_loop_point_in_pcm_frames(ma_sound_get_data_source(s.get()),
                (ma_uint64)((double)it->second.loopStart * rate), ~(ma_uint64)0);   // 終わり = ファイルの終わり
    }
    ma_sound_set_volume(s.get(), it->second.volume);
    ma_sound_set_fade_in_milliseconds(s.get(), 0.0f, 1.0f, fadeMs);
    ma_sound_start(s.get());
    m->music = std::move(s);
}

const std::string& AudioSystem::CurrentMusic() const { return m->musicName; }

void AudioSystem::SetAmbient(const std::string& file, float volume, float pitch)
{
    if (!m->ok) return;
    if (file != m->ambientFile)
    {
        if (m->ambient) { ma_sound_uninit(m->ambient.get()); m->ambient.reset(); }
        m->ambientFile = file;
        if (!file.empty())
        {
            auto s = std::make_unique<ma_sound>();
            if (ma_sound_init_from_file(&m->engine, file.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                    m->Group(Bus::Sfx), nullptr, s.get()) != MA_SUCCESS)
            {
                std::cout << "[Audio] ambient load failed: " << file << std::endl;
                m->ambientFile.clear();
                return;
            }
            ma_sound_set_looping(s.get(), MA_TRUE);
            ma_sound_set_pitch(s.get(), pitch);
            ma_sound_set_volume(s.get(), 0.0f);
            ma_sound_start(s.get());
            m->ambient = std::move(s);
        }
    }
    if (m->ambient) ma_sound_set_volume(m->ambient.get(), (std::max)(volume, 0.0f));
}

float AudioSystem::GetVolume(Bus b) const { return m->volumes[(int)b]; }

void AudioSystem::SetVolume(Bus b, float v)
{
    m->volumes[(int)b] = std::clamp(v, 0.0f, 1.0f);
    m->ApplyVolumes();
}

bool AudioSystem::SaveSettings() const
{
    json j;
    for (int i = 0; i < (int)Bus::Count; ++i) j[kBusName[i]] = m->volumes[i];
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(kSettingsPath).parent_path(), ec);
    std::ofstream out(kSettingsPath);
    if (!out.is_open()) return false;
    out << j.dump(4);
    return true;
}

bool AudioSystem::LoadSettings()
{
    std::ifstream in(kSettingsPath);
    if (!in.is_open()) return false;
    try
    {
        json j;
        in >> j;
        for (int i = 0; i < (int)Bus::Count; ++i)
            m->volumes[i] = std::clamp(j.value(kBusName[i], m->volumes[i]), 0.0f, 1.0f);
    }
    catch (const std::exception& e)
    {
        std::cout << "[Audio] settings parse error: " << e.what() << std::endl;
        return false;
    }
    m->ApplyVolumes();
    return true;
}

bool AudioSystem::ReloadCues()
{
    if (!m->ok) return false;
    m->StopVoices();
    m->Unregister();
    m->cues.clear();
    m->tracks.clear();

    std::ifstream in(kCuePath);
    if (!in.is_open())
    {
        std::cout << "[Audio] " << kCuePath << " not found" << std::endl;
        return false;
    }
    json j;
    try { in >> j; }
    catch (const std::exception& e)
    {
        std::cout << "[Audio] " << kCuePath << " parse error: " << e.what() << std::endl;
        return false;
    }

    namespace fs = std::filesystem;
    ma_resource_manager* rm = ma_engine_get_resource_manager(&m->engine);
    int missing = 0;
    if (j.contains("cues") && j["cues"].is_object())
        for (auto& [name, cj] : j["cues"].items())
        {
            Impl::Cue c;
            c.volume = cj.value("volume", 1.0f);
            c.pitchMin = cj.value("pitchMin", 1.0f);
            c.pitchMax = cj.value("pitchMax", c.pitchMin);
            c.minInterval = cj.value("minInterval", 0.0f);
            c.maxVoices = (std::max)(1, cj.value("maxVoices", 4));
            const std::string bus = cj.value("bus", std::string("sfx"));
            c.bus = (bus == "ui") ? Bus::Ui : (bus == "music") ? Bus::Music : Bus::Sfx;
            if (cj.contains("files") && cj["files"].is_array())
                for (const auto& f : cj["files"])
                {
                    const std::string path = f.get<std::string>();
                    std::error_code ec;
                    if (!fs::exists(path, ec)) { ++missing; std::cout << "[Audio] missing " << path << std::endl; continue; }
                    // 全部デコードしておく（鳴らす瞬間にファイルを読まない）。同じファイルは 1 回だけ
                    if (std::find(m->registered.begin(), m->registered.end(), path) == m->registered.end()
                        && ma_resource_manager_register_file(rm, path.c_str(), MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE) == MA_SUCCESS)
                        m->registered.push_back(path);
                    c.files.push_back(path);
                }
            m->cues[name] = std::move(c);
        }
    if (j.contains("music") && j["music"].is_object())
        for (auto& [name, tj] : j["music"].items())
        {
            Impl::Track t;
            t.file = tj.value("file", std::string());
            t.volume = tj.value("volume", 1.0f);
            t.loopStart = (std::max)(0.0f, tj.value("loopStart", 0.0f));
            std::error_code ec;
            if (!t.file.empty() && !fs::exists(t.file, ec)) { ++missing; std::cout << "[Audio] missing " << t.file << std::endl; }
            m->tracks[name] = t;
        }
    if (missing > 0) std::cout << "[Audio] " << missing << " sound files missing" << std::endl;
    return true;
}

bool AudioSystem::HasCue(const std::string& cue) const { return m->cues.count(cue) > 0; }

std::string AudioSystem::DebugStats() const
{
    if (!m->ok) return "audio off";
    size_t files = 0;
    std::vector<std::string> names;
    for (const auto& [name, c] : m->cues) { files += c.files.size(); if (c.plays + c.skipped > 0) names.push_back(name); }
    std::sort(names.begin(), names.end());
    std::string s = "cues " + std::to_string(m->cues.size()) + " files " + std::to_string(files)
        + " music " + std::to_string(m->tracks.size()) + " now " + (m->musicName.empty() ? "-" : m->musicName) + " |";
    for (const std::string& n : names)
    {
        const Impl::Cue& c = m->cues.at(n);
        s += " " + n + " " + std::to_string(c.plays) + "/" + std::to_string(c.skipped);
    }
    return s;
}

AudioSystem::MusicProbe AudioSystem::DebugMusic() const
{
    MusicProbe r;
    if (!m->ok || !m->music) return r;
    ma_sound* s = m->music.get();
    r.playing = ma_sound_is_playing(s) != MA_FALSE;
    r.looping = ma_sound_is_looping(s) != MA_FALSE;
    ma_sound_get_cursor_in_seconds(s, &r.cursor);
    ma_sound_get_length_in_seconds(s, &r.length);
    r.fade = ma_sound_get_current_fade_volume(s);
    {
        // loop point が stream から中の decoder へ渡っているか（decoder は読み込みスレッドの物。自動テストで覗くだけ）
        auto* ds = static_cast<ma_resource_manager_data_source*>(ma_sound_get_data_source(s));
        ma_uint32 rate = 0;
        ma_sound_get_data_format(s, nullptr, nullptr, &rate, nullptr, 0);
        if (ds && (ds->flags & MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_STREAM) && ds->backend.stream.isDecoderInitialized && rate > 0)
        {
            ma_uint64 beg = 0, end = 0;
            ma_data_source_get_loop_point_in_pcm_frames(&ds->backend.stream.decoder, &beg, &end);
            r.loopBeg = (float)((double)beg / rate);
        }
    }
    if (r.length <= 0.0f)
    {
        // 流し読みの OGG は stb_vorbis が push 方式なので長さが分からない（0）。ファイルを別に開いて測る（曲毎に 1 回）
        static std::unordered_map<std::string, float> s_Len;
        const auto it = m->tracks.find(m->musicName);
        if (it != m->tracks.end())
        {
            auto [lit, added] = s_Len.try_emplace(it->second.file, 0.0f);
            ma_decoder dec;
            if (added && ma_decoder_init_file(it->second.file.c_str(), nullptr, &dec) == MA_SUCCESS)
            {
                ma_uint64 frames = 0;
                if (ma_decoder_get_length_in_pcm_frames(&dec, &frames) == MA_SUCCESS && dec.outputSampleRate > 0)
                    lit->second = (float)((double)frames / dec.outputSampleRate);
                ma_decoder_uninit(&dec);
            }
            r.length = lit->second;
        }
    }
    return r;
}

void AudioSystem::DebugSeekMusic(float seconds)
{
    if (m->ok && m->music) ma_sound_seek_to_second(m->music.get(), (std::max)(seconds, 0.0f));
}

std::vector<std::string> AudioSystem::MusicNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, t] : m->tracks) names.push_back(name);
    std::sort(names.begin(), names.end(), MusicOrder);
    return names;
}

std::vector<std::string> AudioSystem::CueNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, c] : m->cues) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

float AudioSystem::DebugOutputPeak()
{
#ifdef _WIN32
    if (!m->ok) return -1.0f;
    float v = 0.0f;
    if (g_SessionMeter && SUCCEEDED(g_SessionMeter->GetPeakValue(&v))) return v;
    if (FindSessionMeter() && SUCCEEDED(g_SessionMeter->GetPeakValue(&v))) return v;
#endif
    return -1.0f;
}

void AudioSystem::DebugLimiter(float& minGain, uint32_t& softClipped, float* inPeak, float* outPeak)
{
    minGain = m->limiter.minGain.exchange(1.0f);
    softClipped = m->limiter.softClipped.exchange(0u);
    const float in = m->limiter.inPeak.exchange(0.0f), out = m->limiter.outPeak.exchange(0.0f);
    if (inPeak) *inPeak = in;
    if (outPeak) *outPeak = out;
}

std::string AudioSystem::DeviceName() const
{
    if (!m->ok) return "(none)";
    ma_device* d = ma_engine_get_device(&m->engine);
    return d ? std::string(d->playback.name) : std::string("(unknown)");
}

void AudioSystem::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Audio")) return;
    if (!m->ok) { ImGui::TextDisabled("no audio device"); return; }

    bool changed = false;
    changed |= ImGui::SliderFloat("Master", &m->volumes[(int)Bus::Master], 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("SFX", &m->volumes[(int)Bus::Sfx], 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Music", &m->volumes[(int)Bus::Music], 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("UI", &m->volumes[(int)Bus::Ui], 0.0f, 1.0f);
    if (changed) m->ApplyVolumes();
    if (ImGui::Button("Save volumes")) SaveSettings();
    ImGui::SameLine();
    if (ImGui::Button("Reload Sounds.json")) ReloadCues();

    int inUse = 0;
    for (auto& v : m->voices) if (v->inUse) ++inUse;
    ImGui::Text("voices %d / %d   music: %s", inUse, kVoiceCount, m->musicName.empty() ? "(none)" : m->musicName.c_str());

    if (ImGui::TreeNode("Music"))
    {
        if (ImGui::SmallButton("Stop")) PlayMusic("");
        ImGui::SameLine();
        ImGui::TextDisabled("(alt_* = candidates to audition; Sounds.json decides which slot uses which file)");
        for (const std::string& name : MusicNames())
        {
            const Impl::Track& t = m->tracks[name];
            ImGui::PushID(name.c_str());
            if (ImGui::SmallButton(name == m->musicName ? "> Playing" : "Play")) PlayMusic(name);
            ImGui::SameLine();
            ImGui::Text("%s  (%s)", name.c_str(), t.file.c_str());
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Cues"))
    {
        for (const std::string& name : CueNames())
        {
            const Impl::Cue& c = m->cues[name];
            ImGui::PushID(name.c_str());
            if (ImGui::SmallButton("Play")) Play(name);
            ImGui::SameLine();
            ImGui::Text("%-18s files %zu  played %u  skipped %u", name.c_str(), c.files.size(), c.plays, c.skipped);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}
