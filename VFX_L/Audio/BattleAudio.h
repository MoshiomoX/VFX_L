// ============================================================
// BattleAudio.h
// 戦闘の音（2026-10-03）。シーンが持ち、gameplay の毎フレーム呼ぶ。
//   ・GPU で生まれた範囲（命中の火花・爆発・死んだ時の土煙・毒の池）: 範囲の profile の sound を
//     VFX レシピ番号へ引いた表で、SwarmSystem::ConsumeAreaBirths の数だけ鳴らす（1 フレーム 1 回にまとめる）
//   ・プレイヤーの跳び / 着地 / 滑り込み（状態の変わり目）
//   ・BGM: 面の曲（環境のテーマ曲）→ 最終ウェーブ → Boss、死んだ / クリアで止める
//   ・Boss の登場（咆哮 + 低い衝撃音、Boss 曲へ 0.05 秒で切り替え）・最終ウェーブが始まった・死んだ・クリアの一回物
// 撃った音（WeaponSystem）、レベルアップ・開箱・被弾・経験値・魔力解放（FeedbackVFXSystem）は各所で直接鳴らす
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "Swarm/SwarmTypes.h"
#include <array>
#include <cstdint>
#include <string>

class SwarmSystem;
class Registry;

class BattleAudio
{
public:
    // 範囲の音の表を作る（AreaProfileDB と SwarmSystem の VFX 表を読んだ後）。stage = 1..3（曲の選択）
    void Init(const SwarmSystem& swarm, int stage);

    struct State
    {
        bool bossAlive = false;
        bool finalWave = false;
        bool playerDead = false;
        bool cleared = false;
    };
    // gameplay の毎フレーム（一時停止・三択の間は呼ばない）
    void Update(float dt, SwarmSystem& swarm, Registry& reg, Entity player, const State& st);
    void StopMusic();   // シーンを出る時

    bool musicAuto = true;   // false = 曲を状態で選ばない（TEMP-TEST: music 自動テストが曲を順に掛ける間）

private:
    std::array<std::string, Swarm::kAreaBirthKinds> m_RecipeCue;   // VFX レシピ番号 → cue
    std::array<uint32_t, Swarm::kAreaBirthKinds> m_Births = {};
    int  m_Stage = 1;
    bool m_PrevGrounded = true;
    bool m_PrevSlide = false;
    int  m_PrevAirJumps = 0;
    float m_AirTime = 0.0f;
    State m_Prev;
    bool m_Started = false;
};
