// ============================================================
// AutoTestMusic.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=music
// VFXL_BATTLE_AUTOTEST=music：BGM を音を出したまま全曲掛け、再生位置・繰り返しの継ぎ目・実際の出力のピーク値、
// 面の曲 → 最終ウェーブ → Boss → クリアの切り替えを記録
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestMusic final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: BGM の通し検査（VFXL_BATTLE_AUTOTEST=music、2026-10-03）
// ユーザー：自動テストでも音を消さず、曲も確かめる。VFXL_AUDIO_MUTE を付けずに回す（実際に鳴る）。
// 1 秒で湧きを止め・全消去・無敵・詠唱停止、曲の自動選択を止める（BattleAudio::musicAuto）。
// Sounds.json の全曲を順に掛け、3 秒で `music track <名> playing loop cursor a->b len peakMax`
//   （0.5 秒の時点から 2.5 秒ほど進むか、WASAPI のピーク値 = 実際に出ている音の最大）、
//   次に終わりの 1.5 秒前へ飛ばし、継ぎ目の前後 0.4 秒のピーク値の最小（途切れていれば 0 近く）と
//   継ぎ目 2 秒後の cursor（≒2 = 頭へ戻った）を `music wrap <名> cursor playing peakMin peakMax`。
// 全曲の後、自動選択に戻して 4 秒（面の曲）→ 計時を時間切れへ 4 秒（最終ウェーブ）→ 門の前で F 6.5 秒（Boss）→
// KillAll 2.5 秒（クリア、曲は止まる）。各段の終わりに `music state <段> now <曲> ...`、最後に `music done`
// ============================================================
void AutoTestMusic::Run()
{
    AudioSystem& audio = AudioSystem::Get();
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    static std::vector<std::string> s_Names;
    static size_t s_Idx = 0;
    static int    s_Phase = -1;     // 曲毎: -1 次を掛ける / 0 掛けた / 1 cursor を取った / 2 終わり前へ飛ばした
    static float  s_T0 = 0.0f;      // 今の曲・段が始まった時刻
    static float  s_C0 = 0.0f, s_PeakMax = 0.0f, s_WrapMin = 1.0f, s_WrapMax = 0.0f, s_WrapAt = 0.0f;
    static int    s_WrapSamples = 0, s_Frames = 0, s_Over90 = 0, s_Clip = 0;
    static float  s_NextLog = 0.0f, s_NextSpawn = 0.0f;
    static float  s_LimMin = 1.0f, s_MixIn = 0.0f, s_MixOut = 0.0f;   // 乱戦中のリミットの倍率の最小・リミットの前 / 後の峰
    static uint32_t s_LimClip = 0;
    static float  s_BossEnter = -1.0f;   // 曲が boss に変わった時刻
    char line[400];
    const float peak = audio.DebugOutputPeak();
    const float t = m_AutoTime - s_T0;
    const AudioSystem::MusicProbe mp = audio.DebugMusic();
    // 最終ウェーブへ飛ぶまで計時を 0 に（3:00 のエリートが来て被弾の音が混ざらないように）
    if (m_AutoStep < 3 || m_AutoStep >= 10) m_RunTime = 0.0f;

    auto logState = [&](const char* label)
    {
        snprintf(line, sizeof(line), "music state %s now %s playing %d fade %.2f cursor %.1f peakMax %.3f",
            label, audio.CurrentMusic().empty() ? "-" : audio.CurrentMusic().c_str(), mp.playing ? 1 : 0, mp.fade, mp.cursor, s_PeakMax);
        AutoTestLog(line);
        s_T0 = m_AutoTime;
        s_PeakMax = 0.0f;
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Audio.musicAuto = false;
        s_Names = audio.MusicNames();
        snprintf(line, sizeof(line), "music start device %s tracks %zu now %s volumes master %.2f music %.2f sfx %.2f ui %.2f peak %.3f",
            audio.DeviceName().c_str(), s_Names.size(), audio.CurrentMusic().c_str(),
            audio.GetVolume(AudioSystem::Bus::Master), audio.GetVolume(AudioSystem::Bus::Music),
            audio.GetVolume(AudioSystem::Bus::Sfx), audio.GetVolume(AudioSystem::Bus::Ui), peak);
        AutoTestLog(line);
        s_Idx = 0;
        s_Phase = -1;
        m_AutoStep = 1;
        // VFXL_MUSIC_QUICK=1: 全曲・効果音の一巡を飛ばし、面の曲 → 乱戦 → 最終ウェーブ → Boss だけ
        char quick[8] = {};
        if (GetEnvironmentVariableA("VFXL_MUSIC_QUICK", quick, sizeof(quick)) > 0 && quick[0] == '1')
        {
            m_Audio.musicAuto = true;
            s_T0 = m_AutoTime;
            s_PeakMax = 0.0f;
            m_AutoStep = 2;
        }
    }
    else if (m_AutoStep == 1)   // 全曲
    {
        if (s_Phase == -1)
        {
            if (s_Idx >= s_Names.size())
            {
                audio.PlayMusic("", 0.3f);   // 次は効果音を 1 つずつ（曲を止めて）
                s_Names = audio.CueNames();
                s_Idx = 0;
                s_T0 = m_AutoTime + 0.7f;    // フェードが消えるまで待つ
                s_PeakMax = 0.0f;
                m_AutoStep = 10;
                return;
            }
            audio.PlayMusic(s_Names[s_Idx], 0.2f);
            s_T0 = m_AutoTime;
            s_PeakMax = 0.0f;
            s_Phase = 0;
            return;
        }
        const char* name = s_Names[s_Idx].c_str();
        if (s_Phase == 0 && t >= 0.5f) { s_C0 = mp.cursor; s_Phase = 1; }
        else if (s_Phase == 1)
        {
            s_PeakMax = (std::max)(s_PeakMax, peak);
            if (t >= 3.0f)
            {
                snprintf(line, sizeof(line), "music track %s playing %d loop %d cursor %.2f->%.2f len %.1f peakMax %.3f",
                    name, mp.playing ? 1 : 0, mp.looping ? 1 : 0, s_C0, mp.cursor, mp.length, s_PeakMax);
                AutoTestLog(line);
                audio.DebugSeekMusic(mp.length - 1.5f);
                s_WrapAt = m_AutoTime + 1.5f;
                s_WrapMin = 1.0f;
                s_WrapMax = 0.0f;
                s_WrapSamples = 0;
                s_Phase = 2;
            }
        }
        else if (s_Phase == 2)
        {
            // 継ぎ目の前後 0.3 秒（cursor で見る。流し読みの OGG は長さが分からないので cursor は折り返さず増え続ける。
            // 流し読みの seek は頭から読み直すので遅れる = 時刻では継ぎ目を外す）
            if (mp.cursor >= mp.length - 0.3f && mp.cursor <= mp.length + 0.3f && peak >= 0.0f)
            {
                s_WrapMin = (std::min)(s_WrapMin, peak);
                s_WrapMax = (std::max)(s_WrapMax, peak);
                ++s_WrapSamples;
            }
            if (m_AutoTime >= s_WrapAt + 2.0f)
            {
                snprintf(line, sizeof(line), "music wrap %s cursor %.2f len %.2f loopBeg %.2f playing %d samples %d peakMin %.3f peakMax %.3f",
                    name, mp.cursor, mp.length, mp.loopBeg, mp.playing ? 1 : 0, s_WrapSamples, s_WrapMin, s_WrapMax);
                AutoTestLog(line);
                ++s_Idx;
                s_Phase = -1;
            }
        }
    }
    else if (m_AutoStep == 10)   // 効果音を 1 つずつ: 鳴らして 1.4 秒のピーク値（音量の釣り合い・割れ）
    {
        if (t < 0.0f) return;
        if (s_Idx >= s_Names.size())
        {
            m_Audio.musicAuto = true;
            s_T0 = m_AutoTime;
            s_PeakMax = 0.0f;
            m_AutoStep = 2;
            return;
        }
        static int s_Played = -1;   // -1 = まだ鳴らしていない / 0 = 間引かれた / 1 = 鳴った
        if (s_Played < 0) s_Played = audio.Play(s_Names[s_Idx]) ? 1 : 0;
        s_PeakMax = (std::max)(s_PeakMax, peak);
        if (t >= 1.4f)
        {
            snprintf(line, sizeof(line), "music cue %s played %d peak %.3f", s_Names[s_Idx].c_str(), s_Played, s_PeakMax);
            AutoTestLog(line);
            ++s_Idx;
            s_T0 = m_AutoTime;
            s_PeakMax = 0.0f;
            s_Played = -1;
        }
    }
    else if (m_AutoStep == 2)   // 自動選択: 面の曲
    {
        s_PeakMax = (std::max)(s_PeakMax, peak);
        if (t >= 4.0f)
        {
            logState("stage");
            // 次は乱戦: 3x3 に隕石（中）+ 火球・石弾・追尾・弧（四隅）、MP 満タン、0.5 秒毎に周りへ雑魚 25 + 自爆兵 2
            if (m_Registry.Has<BackpackComponent>(m_Player))
            {
                auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
                const int lo = BackpackComponent::GRID / 2 - 1;
                ClearBackpackItems(bp);
                BackpackLogic::Place(bp, ItemID::Meteor, lo + 1, lo + 1, 0);
                BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
                BackpackLogic::Place(bp, ItemID::StoneShot, lo + 2, lo, 0);
                BackpackLogic::Place(bp, ItemID::HomingBolt, lo, lo + 2, 0);
                BackpackLogic::Place(bp, ItemID::ArcBolt, lo + 2, lo + 2, 0);
                bp.dirty = true;
            }
            if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
            s_Frames = s_Over90 = s_Clip = 0;
            s_LimMin = 1.0f;
            s_MixIn = s_MixOut = 0.0f;
            s_LimClip = 0;
            { float g; uint32_t c; audio.DebugLimiter(g, c); }   // ここまでの分は捨てる
            s_NextLog = m_AutoTime + 1.0f;
            s_NextSpawn = m_AutoTime;
            m_AutoStep = 11;
        }
    }
    else if (m_AutoStep == 11)   // 乱戦: 効果音が重なった時のピーク値（1.0 を超えると割れる）
    {
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mana = m_Registry.Get<ManaComponent>(m_Player);
            mana.current = mana.max;
        }
        if (m_AutoTime >= s_NextSpawn && t < 9.0f)
        {
            s_NextSpawn += 0.5f;
            const Vector3 pp = tf.position;
            const float gy = m_Swarm.GetAIParams().groundY;
            for (int i = 0; i < 27; ++i)
            {
                const float a = (float)i * 2.39996f + m_AutoTime * 1.7f;   // 黄金角で散らす
                const float r = 7.0f + 6.0f * std::fmod((float)i * 0.618034f, 1.0f);
                const float x = pp.x + std::sin(a) * r, z = pp.z + std::cos(a) * r;
                m_Swarm.SpawnEnemy({ x, m_Grid.SampleHeight(x, z) + gy, z }, 8.0f, 3.5f,
                    i < 25 ? Swarm::kEnemyKindMob : Swarm::kEnemyKindBomber);
            }
        }
        {
            float g = 1.0f, in = 0.0f, out = 0.0f;
            uint32_t c = 0;
            audio.DebugLimiter(g, c, &in, &out);   // 出口のリミッターがどこまで下げたか・丸めた数・前後の峰
            s_LimMin = (std::min)(s_LimMin, g);
            s_LimClip += c;
            s_MixIn = (std::max)(s_MixIn, in);
            s_MixOut = (std::max)(s_MixOut, out);
        }
        const float limGain = s_LimMin;
        const uint32_t limClip = s_LimClip;
        if (peak >= 0.0f)
        {
            ++s_Frames;
            if (peak >= 0.9f) ++s_Over90;
            if (peak >= 0.99f) ++s_Clip;
            s_PeakMax = (std::max)(s_PeakMax, peak);
        }
        if (m_AutoTime >= s_NextLog)
        {
            s_NextLog += 1.0f;
            snprintf(line, sizeof(line), "music combat t %.0f kills %u peakMax %.3f over90 %d clip %d frames %d limitGain %.2f softClip %u mixIn %.3f mixOut %.3f",
                t, m_Swarm.GetCounters().killCount, s_PeakMax, s_Over90, s_Clip, s_Frames, limGain, limClip, s_MixIn, s_MixOut);
            AutoTestLog(line);
        }
        if (t >= 10.0f)
        {
            m_Swarm.KillAll();
            if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
            logState("combat");
            m_RunTime = (std::max)(m_RunTime, m_Stage.stageTime + 0.5f);
            m_AutoStep = 3;
        }
    }
    else if (m_AutoStep == 3)   // 最終ウェーブ
    {
        s_PeakMax = (std::max)(s_PeakMax, peak);
        if (t >= 4.0f)
        {
            logState("final");
            const float yaw = DirectX::XMConvertToRadians(m_Stage.GetPortalYaw());
            const Vector3 p = m_Stage.GetPortalCenter() + Vector3(std::sin(yaw), 0.0f, std::cos(yaw)) * 2.0f;
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            m_AutoStep = 4;
        }
    }
    else if (m_AutoStep == 4)   // 門の前で F → Boss
    {
        s_PeakMax = (std::max)(s_PeakMax, peak);
        if (t < 0.1f) s_BossEnter = -1.0f;
        if (t >= 0.5f && t < 0.6f) m_AutoInteract = true;
        // 登場: 曲が boss に変わった瞬間と、その後 0.1 / 0.3 / 1 秒の様子（前の曲が 0.05 秒で消え、Boss 曲が頭から鳴るか）
        if (s_BossEnter < 0.0f && audio.CurrentMusic() == "boss")
        {
            s_BossEnter = m_AutoTime;
            snprintf(line, sizeof(line), "music boss enter t %.2f bossAlive %d cursor %.2f fade %.2f peak %.3f",
                t, m_Stage.IsBossAlive() ? 1 : 0, mp.cursor, mp.fade, peak);
            AutoTestLog(line);
        }
        static int s_EnterLog = 0;
        if (s_BossEnter < 0.0f) s_EnterLog = 0;
        else
        {
            const float since = m_AutoTime - s_BossEnter;
            const float marks[3] = { 0.1f, 0.3f, 1.0f };
            if (s_EnterLog < 3 && since >= marks[s_EnterLog])
            {
                snprintf(line, sizeof(line), "music boss +%.1f s now %s cursor %.2f fade %.2f peak %.3f",
                    marks[s_EnterLog], audio.CurrentMusic().c_str(), mp.cursor, mp.fade, peak);
                AutoTestLog(line);
                ++s_EnterLog;
            }
        }
        if (t >= 6.5f)
        {
            logState("boss");
            m_Swarm.KillAll();   // Boss も倒れる → クリア（曲は止まり、stage_clear が鳴る）
            m_AutoStep = 5;
        }
    }
    else if (m_AutoStep == 5)
    {
        s_PeakMax = (std::max)(s_PeakMax, peak);
        if (t >= 2.5f)   // kDeathToResult（3 秒）でリザルトへ移る前に
        {
            logState(m_Stage.IsCleared() ? "cleared" : "notCleared");
            AutoTestLog("music done");
            m_AutoStep = 6;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("music", AutoTestMusic)
