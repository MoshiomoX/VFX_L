// ============================================================
// BattleAudio.cpp
// ============================================================
#include "Audio/BattleAudio.h"
#include "Audio/AudioSystem.h"
#include "ECS/Registry.h"
#include "Component/RigidbodyComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/SwarmVFXTable.h"
#include <iostream>

void BattleAudio::Init(const SwarmSystem& swarm, int stage)
{
    m_Stage = stage;
    for (auto& c : m_RecipeCue) c.clear();
    m_Births = {};
    m_Prev = State{};
    m_Started = false;
    m_PrevGrounded = true;
    m_PrevSlide = false;
    m_PrevAirJumps = 0;
    m_AirTime = 0.0f;

    // 範囲の profile の sound を、GPU がその範囲に付ける VFX レシピ番号（AreaProfileDB::BuildDefs と同じ引き方）へ。
    // 同じ vfxFile の範囲（Explosion と BomberBlast）は同じ音になる（先に見つけた方）
    const SwarmVFXTable& table = swarm.GetVFXTable();
    for (int i = 1; i < AreaProfileDB::Count(); ++i)
    {
        const AreaProfile& p = AreaProfileDB::At(i);
        if (p.sound.empty()) continue;
        const uint32_t r = table.IndexOf(AreaProfileDB::FindVFXId(p.vfxFile));
        if (r == 0 || r >= Swarm::kAreaBirthKinds) continue;   // 0 = GPU で見た目を出さない（CPU が出す範囲）
        if (m_RecipeCue[r].empty()) m_RecipeCue[r] = p.sound;
        else if (m_RecipeCue[r] != p.sound)
            std::cout << "[Audio] area " << p.name << " shares VFX with another sound (" << m_RecipeCue[r] << ")" << std::endl;
    }
}

void BattleAudio::Update(float dt, SwarmSystem& swarm, Registry& reg, Entity player, const State& st)
{
    AudioSystem& audio = AudioSystem::Get();

    // ---- GPU で生まれた範囲（命中・爆発・撃破・毒の池）----
    m_Births = {};
    swarm.ConsumeAreaBirths(m_Births);
    for (uint32_t r = 1; r < Swarm::kAreaBirthKinds; ++r)
        if (m_Births[r] > 0 && !m_RecipeCue[r].empty())
            audio.PlayBurst(m_RecipeCue[r], m_Births[r]);

    // ---- プレイヤーの跳び・着地・滑り込み ----
    if (reg.IsValid(player) && reg.Has<RigidbodyComponent>(player) && reg.Has<PlayerStateComponent>(player))
    {
        const auto& rb = reg.Get<RigidbodyComponent>(player);
        const auto& ps = reg.Get<PlayerStateComponent>(player);
        if (!rb.isGrounded) m_AirTime += dt;
        if (m_PrevGrounded && !rb.isGrounded && rb.velocity.y > 2.0f) audio.Play("player_jump");
        if (ps.airJumpsUsed > m_PrevAirJumps) audio.Play("player_jump", 1.0f, 1.12f);   // 空中跳びは少し高く
        if (!m_PrevGrounded && rb.isGrounded)
        {
            if (m_AirTime > 0.3f) audio.Play("player_land", (std::min)(1.0f, 0.5f + m_AirTime));
            m_AirTime = 0.0f;
        }
        if (ps.slideActive && !m_PrevSlide) audio.Play("player_slide");
        m_PrevGrounded = rb.isGrounded;
        m_PrevSlide = ps.slideActive;
        m_PrevAirJumps = ps.airJumpsUsed;
    }

    // ---- 一回物 ----
    if (m_Started)
    {
        if (st.bossAlive && !m_Prev.bossAlive)
        {
            // Boss の登場（2026-10-03 ユーザー「もっと激しく」）: 咆哮 + 低い衝撃音、曲は下で交差フェードせず切り替える
            audio.Play("boss_summon");
            audio.Play("boss_impact");
        }
        if (st.finalWave && !m_Prev.finalWave) audio.Play("final_wave");
        if (st.playerDead && !m_Prev.playerDead) audio.Play("player_death");
        if (st.cleared && !m_Prev.cleared) audio.Play("stage_clear");
    }
    m_Prev = st;
    m_Started = true;

    // ---- BGM ----
    if (!musicAuto) return;
    std::string track;
    if (st.playerDead || st.cleared) track.clear();
    else if (st.bossAlive) track = "boss";
    else if (st.finalWave) track = "final";
    else track = "stage" + std::to_string(m_Stage);
    // 切り替えの交差フェード: 死んだら短め、Boss は 0.05 秒（衝撃音と同時に Boss 曲の頭が鳴る）、他は 1.5 秒
    const float fade = st.playerDead ? 0.6f : (track == "boss") ? 0.05f : 1.5f;
    audio.PlayMusic(track, fade);
}

void BattleAudio::StopMusic()
{
    AudioSystem::Get().PlayMusic("", 0.5f);
}
