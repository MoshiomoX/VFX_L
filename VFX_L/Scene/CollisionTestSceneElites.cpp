// ============================================================
// CollisionTestSceneElites.cpp
// 面の固定エリート（2026-10-25）のシーン側：
//   ・面に合わせる（EliteAttacks::Configure と皮・飾り EliteLooks::Apply。InitBossFx から）
//   ・技の当たりを被弾へ（TryApplyHit = 無敵時間・シールドは雑魚と同じ道、ノックバックは技の種類で）
//   ・倒されたら死んだ所に金色の宝箱（無料。RewardCrateSystem::SpawnFree）+ 光の粒
//   ・GPU の体への指示（BomberCB）・地面の予告（Boss の輪とまとめて SetWarnCircles）・HUD の HP 条
// 技・詞缀・予告そのものは Enemy/EliteAttacks（EliteMoves.cpp / EliteAffixes.cpp）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/TransformComponent.h"
#include "Core/Application.h"
#include "Enemy/EliteLooks.h"
#include "Player/PlayerControlSystem.h"
#include "Player/PlayerStateSystem.h"
#include "Player/PlayerStatsComponent.h"
#include "VFX_Editor/VFXId.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

void CollisionTestScene::InitElites()
{
    m_StageElites.Clear();
    m_StageElites.Init(&m_AreaVFX, &m_VFXContext);
    m_StageElites.Configure(m_StageIndex);
    m_Stage.elites = &m_StageElites;
    EliteLooks::Apply(m_StageIndex, Application::Get().GetGraphics().GetDevice(), m_Swarm);
    for (const auto& c : m_EliteChestFx)
        if (c.second) m_AreaVFX.StopInstance(c.second);
    m_EliteChestFx.clear();
}

void CollisionTestScene::UpdateElites(float dt, const Vector3& player)
{
    EliteAttacks::Input in;
    in.dt = dt;
    in.player = player;
    in.playerFeetY = player.y - 0.9f;
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        const auto& st = m_Registry.Get<PlayerStatsComponent>(m_Player);
        in.playerFeetY = player.y - (st.height * 0.5f + st.radius);
        in.playerRadius = st.radius;
    }
    if (m_Registry.Has<TransformComponent>(m_Player))
        in.playerYaw = DirectX::XMConvertToRadians(m_Registry.Get<TransformComponent>(m_Player).rotation.y);
    in.playerAlive = !IsPlayerDead();
    in.damageMul = m_Mobs.GetDamageMul();
    m_StageElites.Update(in, m_Grid, m_Swarm);
    m_StageElites.WriteGpu(m_Swarm.GetBomberParams());

    // ---- 当たり：無敵時間・シールドは TryApplyHit、吹き飛ばしは技の種類（殴り / 爆発級 / トゲ）----
    for (const EliteAttacks::Hit& h : m_StageElites.Hits())
    {
        if (IsPlayerDead()) break;
        if (!PlayerStateSystem::TryApplyHit(m_Registry, m_Player, h.damage)) continue;
        ++m_EliteHits;
        Vector2 dir(player.x - h.from.x, player.z - h.from.z);
        dir = (dir.LengthSquared() > 1e-4f) ? dir / dir.Length() : Vector2(0.0f, 1.0f);
        const auto kind = (h.knock == 1) ? PlayerControlSystem::KnockKind::Blast
                        : (h.knock == 2) ? PlayerControlSystem::KnockKind::Spike : PlayerControlSystem::KnockKind::Melee;
        PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, dir, kind);
        if (h.knock == 1) OnExplosions(1);
    }

    // ---- 倒された：死んだ所に金色の宝箱（無料）。光の粒は箱が開けられるまで ----
    for (const EliteAttacks::Death& d : m_StageElites.Deaths())
    {
        const Entity chest = m_Crates.SpawnFree(m_Registry, m_Grid, d.pos);
        if (chest == EntityTraits::NULL_ENTITY) continue;
        const uint32_t fx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteChest), d.pos, 1.0e9f, false, m_VFXContext);
        m_EliteChestFx.push_back({ chest, fx });
        AudioSystem::Get().Play("elite_chest");
    }
    for (size_t i = 0; i < m_EliteChestFx.size();)
    {
        if (m_Registry.IsValid(m_EliteChestFx[i].first)) { ++i; continue; }
        if (m_EliteChestFx[i].second) m_AreaVFX.StopInstance(m_EliteChestFx[i].second);
        m_EliteChestFx.erase(m_EliteChestFx.begin() + i);
    }

    // ---- 地面の予告（Boss の輪の後ろに足して、まとめて GPU へ）----
    for (const EliteAttacks::Warn& w : m_StageElites.Warns())
    {
        if ((int)m_WarnCircles.size() >= SwarmSystem::kMaxWarnCircles) break;
        SwarmSystem::WarnCircle c;
        c.center = w.center;
        c.radius = w.radius;
        c.progress = w.progress;
        c.yaw = w.yaw;
        c.shape = w.shape;
        m_WarnCircles.push_back(c);
    }
    // ---- 砂嵐の術師の砂旋の足元の赤い輪（2026-10-26 ユーザー「砂旋の下にも赤い輪を」）：位置は CPU の柱（Enemy/SandTornadoFx）と同じ ----
    for (const SandTornadoFx::Inst& t : m_SandTornadoes.Instances())
    {
        if ((int)m_WarnCircles.size() >= SwarmSystem::kMaxWarnCircles) break;
        SwarmSystem::WarnCircle c;
        c.center = t.pos;
        c.radius = m_SandTornadoes.look.ringRadius * (0.3f + 0.7f * t.presence);
        c.progress = (m_SandTornadoes.look.ringFill > 0.0f) ? m_SandTornadoes.look.ringFill : -1.0f;
        m_WarnCircles.push_back(c);
    }
    m_Swarm.SetWarnCircles(m_WarnCircles.data(), (int)m_WarnCircles.size());

    // ---- HUD の HP 条 ----
    // bars は作業用（GetBars が毎回 clear して詰める）なので使い回す。hud は GameUI へ渡して持たせる（&& の受け口）ので毎回作る。
    // エリートが居ない時は空のまま = どちらも確保しない
    static std::vector<EliteAttacks::Bar> bars;
    m_StageElites.GetBars(bars);
    std::vector<HUDEliteBar> hud;
    hud.reserve(bars.size());
    for (const EliteAttacks::Bar& b : bars)
    {
        HUDEliteBar h;
        wcsncpy_s(h.name, b.name, _TRUNCATE);
        h.hp = b.hp;
        h.shield = b.shield;
        h.enraged = b.enraged;
        hud.push_back(h);
    }
    m_GameUI.SetEliteBars(std::move(hud));
}
