// ============================================================
// AudioSystem.h
// 音（2026-10-03、ユーザー：効果音 + BGM。音の庫は miniaudio を選んだ）。
//
// 効果音は「cue」の名前で鳴らす。cue は Assets/Data/Audio/Sounds.json に書く:
//   素材の候補（毎回どれかを選ぶ）・音量・音程の揺れ・同時に鳴らす上限・最短間隔・バス。
//   同じ cue を短い間に何度も呼んでも minInterval / maxVoices で間引く
//   （GPU の雑魚が一度に何十体死んでも、命中が毎秒何十回あっても音が潰れない）。
//   素材は読み込み時に全部デコードしておく（鳴らす瞬間にファイルを読まない）。
// BGM は流しながら読み（stream）、繰り返し、曲を変える時は交差フェード。
//   music の loopStart（秒）があれば 2 周目からそこへ戻る（1 周目は曲の頭から）。
// 全部を混ぜた後にリミッター（峰 -1 dBFS で音量を下げ、越えた分は柔らかく丸める）。乱戦で何百もの音が重なっても割れない。
// バス（Master / Sfx / Music / Ui）の音量は Assets/Data/Audio/Settings.json に保存。
// 音の装置が無い・初期化に失敗した時は全部何もしない（ゲームは止めない）
// ============================================================
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AudioSystem
{
public:
    enum class Bus { Master = 0, Sfx, Music, Ui, Count };

    static AudioSystem& Get();

    bool Initialize();
    void Shutdown();
    void Update(float dt);   // 鳴り終わった声の回収・BGM のフェード（毎フレーム）
    bool IsAvailable() const;

    // 効果音。volume / pitch は cue の値に掛ける。戻り値 = 鳴らしたか（間引き・未登録なら false）
    bool Play(const std::string& cue, float volume = 1.0f, float pitch = 1.0f);
    // 同じフレームに count 回分のイベントをまとめて受けた時（GPU の命中・撃破）。1 回だけ、数が多いほど少し大きく鳴らす
    bool PlayBurst(const std::string& cue, uint32_t count);

    // BGM。track = Sounds.json の music の名前。同じ曲なら何もしない。空 = フェードして止める
    void PlayMusic(const std::string& track, float fadeSec = 1.5f);
    const std::string& CurrentMusic() const;

    float GetVolume(Bus b) const;
    void  SetVolume(Bus b, float v);   // 0..1
    bool  SaveSettings() const;
    bool  LoadSettings();

    bool  ReloadCues();                // Sounds.json を読み直す（編集しながら試す）
    bool  HasCue(const std::string& cue) const;
    void  DrawImGui();
    // 自動テストの記録用: "cues N files F music M now <曲> | cue played/skipped ..."（鳴らした物だけ）
    std::string DebugStats() const;

    // ---- 自動テスト用（TEMP-TEST、VFXL_BATTLE_AUTOTEST=music）----
    struct MusicProbe
    {
        bool  playing = false, looping = false;
        float cursor = 0.0f, length = 0.0f;   // 秒
        float fade = 0.0f;                    // フェードの今の倍率（0..1）
        float loopBeg = -1.0f;                // 流し読みの decoder が実際に持っている繰り返しの戻り先（秒。-1 = 不明）
    };
    MusicProbe DebugMusic() const;           // 今の曲（無ければ playing = false）
    void  DebugSeekMusic(float seconds);     // 今の曲の再生位置を動かす（繰り返しの継ぎ目を試す）
    std::vector<std::string> MusicNames() const;   // Sounds.json の music の名前（使う枠が先、alt_* は後ろ）
    std::vector<std::string> CueNames() const;     // cue の名前（名前順）
    // このプログラムが Windows の音声 session に実際に出している音のピーク値（WASAPI の音量計、0..1）。取れない時は -1
    float DebugOutputPeak();
    // 出口のリミッター: 前に呼んでからの一番下げた倍率（1 = 下げていない）、柔らかく丸めたサンプル数、
    // リミットの前 / 後の峰（後 = このプログラムが実際に出した数字の峰。読むと戻る）
    void  DebugLimiter(float& minGain, uint32_t& softClipped, float* inPeak = nullptr, float* outPeak = nullptr);
    std::string DeviceName() const;

    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

private:
    AudioSystem();
    struct Impl;
    std::unique_ptr<Impl> m;
};
