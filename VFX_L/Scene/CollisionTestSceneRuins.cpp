// ============================================================
// CollisionTestSceneRuins.cpp
// 遺跡の敵（2026-10-25：呪いの鎧・石像ガーゴイル・死霊術師 + 骸骨・鎖の番人）のシーン側。
//   ・MobSpawner の出来事（鎧が崩れた / 蘇った、ガーゴイルの目覚め / 急降下 / 着地、詠唱、骸骨の印 / 起き上がり）に特効を出す
//   ・鎖の番人の投げ：鉤が固定した向きへ kChainSpeed で飛び、プレイヤーの体（横の距離 kChainHitRadius + 体の半径、
//     高さの差 kChainHitHeight）に触れたら接触ダメージ × wardenDamageMul を TryApplyHit へ（無敵中は外れ）、
//     通れば番人の前 kChainPullStop m まで kChainPullTime 秒で引き寄せる。壁・地面・届く長さで戻る。
//     見た目は ECS のモデル（EnemyLooks の鎖の区切り + 鉤。卡通・影・赤い縁取りが付く）を毎フレーム並べ直す
// 動き（GPU）は SwarmEnemyAICS、湧き・蘇り・起こしは Enemy/MobSpawnerRuins.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/ModelComponent.h"
#include "Component/TransformComponent.h"
#include "Core/Application.h"
#include "Enemy/EnemyLooks.h"
#include "Player/PlayerControlSystem.h"
#include "Player/PlayerStateSystem.h"
#include "Player/PlayerStatsComponent.h"
#include "VFX_Editor/VFXId.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kChainSpeed = 24.0f;      // 鉤が飛ぶ速さ m/s
    constexpr float kChainBackSpeed = 30.0f;  // 戻る速さ
    constexpr float kChainHitRadius = 0.55f;  // 鉤の当たりの太さ（+ プレイヤーの半径。予告の帯の半幅と同じ）
    constexpr float kChainHitHeight = 1.4f;   // 鉤とプレイヤーの中心の高さの差がこれ以内
    constexpr float kChainPullStop = 1.6f;    // 番人の前のここまで引き寄せる
    constexpr float kChainPullTime = 0.32f;
    constexpr float kChainHandUp = 0.25f;     // 番人の位置（体の中心）から鉤を出す高さ
    constexpr int   kMaxChains = 4;
    constexpr int   kSegsPerChain = 28;       // 0.5m × 28 = 14m（届く長さ 12m + 余り）

    // +Z をこの向きに向ける Transform の回転（度。Transform は CreateFromYawPitchRoll(yaw, pitch, roll)）
    Vector3 FaceRotation(const Vector3& d, float rollDeg)
    {
        const float yaw = std::atan2(d.x, d.z);
        const float pitch = -std::asin(std::clamp(d.y, -1.0f, 1.0f));
        return Vector3(DirectX::XMConvertToDegrees(pitch), DirectX::XMConvertToDegrees(yaw), rollDeg);
    }
}

void CollisionTestScene::ClearRuinsFoes()
{
    for (Entity e : m_ChainSegEntities)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    for (Entity e : m_ChainHookEntities)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    m_ChainSegEntities.clear();
    m_ChainHookEntities.clear();
    m_Chains.clear();
}

void CollisionTestScene::UpdateRuinsFoes(float dt, const Vector3& player)
{
    // ---- 出来事の見た目 ----
    static std::vector<MobSpawner::RuinsFx> s_Fx;
    s_Fx.clear();
    m_Mobs.ConsumeRuinsFx(s_Fx);
    for (const MobSpawner::RuinsFx& f : s_Fx)
    {
        VFXId id = VFXId::None;
        float dur = 1.0f;
        switch (f.kind)
        {
        case MobSpawner::RuinsFxKind::ArmorFall:    id = VFXId::RuinsArmorFall; dur = m_Mobs.armorReviveDelay + 0.1f; break;
        case MobSpawner::RuinsFxKind::ArmorRise:    id = VFXId::RuinsArmorRise; dur = 0.8f; break;
        case MobSpawner::RuinsFxKind::GargoyleWake: id = VFXId::RuinsGargoyleWake; dur = 1.2f; break;
        case MobSpawner::RuinsFxKind::GargoyleDive: id = VFXId::RuinsGargoyleDive; dur = 0.5f; break;
        case MobSpawner::RuinsFxKind::GargoyleLand: id = VFXId::RuinsGargoyleLand; dur = 1.0f; m_Camera.OnShakeAreas(1); break;
        case MobSpawner::RuinsFxKind::NecroCast:    id = VFXId::RuinsNecroCast; dur = 1.0f; break;
        case MobSpawner::RuinsFxKind::RaiseMark:    id = VFXId::RuinsRaiseMark; dur = m_Mobs.necroRaiseDelay + 0.3f; break;
        case MobSpawner::RuinsFxKind::SkeletonRise: id = VFXId::RuinsSkeletonRise; dur = 1.0f; break;
        }
        if (id != VFXId::None)
            m_AreaVFX.Play(VFXDatabase::GetFile(id), f.pos, dur, false, m_VFXContext);
    }

    // ---- 鎖の投げ ----
    const auto& bomb = m_Swarm.GetBomberParams();
    static std::vector<MobSpawner::ChainThrow> s_Throws;
    s_Throws.clear();
    m_Mobs.ConsumeChainThrows(s_Throws);
    for (const MobSpawner::ChainThrow& t : s_Throws)
    {
        if ((int)m_Chains.size() >= kMaxChains) break;
        ChainHook c;
        c.origin = t.pos + Vector3(0.0f, kChainHandUp, 0.0f);
        c.dir = Vector3(std::sin(t.yaw), 0.0f, std::cos(t.yaw));
        c.reach = bomb.wardenRange;
        m_Chains.push_back(c);
    }

    const bool havePlayer = m_Registry.IsValid(m_Player) && m_Registry.Has<PlayerStatsComponent>(m_Player);
    const float playerR = havePlayer ? m_Registry.Get<PlayerStatsComponent>(m_Player).radius : 0.4f;
    for (size_t i = 0; i < m_Chains.size();)
    {
        ChainHook& c = m_Chains[i];
        if (c.state == 0)
        {
            const float prev = c.out;
            c.out = (std::min)(c.out + kChainSpeed * dt, c.reach);
            // 当たり：今ステップで鉤が通った区間 [prev, out] とプレイヤー（水平の横の距離・高さ）
            const Vector3 rel = player - c.origin;
            const float along = rel.x * c.dir.x + rel.z * c.dir.z;
            const Vector2 side(rel.x - c.dir.x * along, rel.z - c.dir.z * along);
            const bool inSpan = along >= prev - playerR && along <= c.out + playerR;
            if (havePlayer && inSpan && side.Length() <= kChainHitRadius + playerR && std::fabs(rel.y) <= kChainHitHeight)
            {
                const float dmg = m_Swarm.GetAIParams().contactDamage * bomb.wardenDamageMul;
                if (PlayerStateSystem::TryApplyHit(m_Registry, m_Player, dmg))
                {
                    // 番人の前まで引き寄せる
                    const Vector3 stop = c.origin + c.dir * kChainPullStop;
                    Vector2 pull(stop.x - player.x, stop.z - player.z);
                    const float dist = pull.Length();
                    if (dist > 0.05f)
                        PlayerControlSystem::ApplyPull(m_Registry, m_Player, pull, dist, kChainPullTime);
                    m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::RuinsChainHit), player, 0.4f, false, m_VFXContext);
                    AudioSystem::Get().Play("chain_hit");
                    ++m_ChainHits;
                    c.state = 1;
                    c.timer = kChainPullTime;
                }
                else
                    c.state = 2;   // 無敵中：外れて戻る
            }
            else
            {
                // 壁（歩けないマス）・地面（坂・崖）・届く長さで戻る
                const Vector3 tip = c.origin + c.dir * c.out;
                int gx = 0, gz = 0;
                m_Grid.WorldToCell(tip, gx, gz);
                const bool wall = !m_Grid.IsWalkable(gx, gz) || m_Grid.SampleHeight(tip.x, tip.z) > tip.y - 0.3f;
                if (wall || c.out >= c.reach)
                    c.state = 2;
            }
        }
        else if (c.state == 1)
        {
            // 引っ掛けて引く：鉤はプレイヤーに付いて来る
            const Vector3 rel = player - c.origin;
            c.out = (std::max)(0.3f, std::sqrt(rel.x * rel.x + rel.z * rel.z));
            c.timer -= dt;
            if (c.timer <= 0.0f)
                c.state = 2;
        }
        else
        {
            c.out -= kChainBackSpeed * dt;
            if (c.out <= 0.0f)
            {
                m_Chains.erase(m_Chains.begin() + (std::ptrdiff_t)i);
                continue;
            }
        }
        ++i;
    }
    LayoutChains();
}

void CollisionTestScene::LayoutChains()
{
    ID3D11Device* device = Application::Get().GetGraphics().GetDevice();
    if (!m_ChainSegModel && device) m_ChainSegModel = EnemyLooks::BuildChainSegment(device);
    if (!m_ChainHookModel && device) m_ChainHookModel = EnemyLooks::BuildChainHook(device);
    if (!m_ChainSegModel || !m_ChainHookModel) return;
    // 実体は使い回す（足りなければ作る、余りは見えなくする）。MobTag（赤い縁取り）は付けない：細い鎖が赤い光線に見えた
    auto make = [&](const std::shared_ptr<Model>& model)
        {
            const Entity e = m_Registry.Create();
            m_Registry.Add<TransformComponent>(e, TransformComponent{});
            ModelComponent mc;
            mc.model = model;
            mc.visible = false;
            m_Registry.Add<ModelComponent>(e, mc);
            return e;
        };
    size_t seg = 0, hook = 0;
    const Vector3 player = PlayerPos() ? *PlayerPos() : Vector3::Zero;
    for (const ChainHook& c : m_Chains)
    {
        // 引いている間は鉤の先 = プレイヤーの胸（高さも合わせる）、それ以外は水平
        Vector3 dir = c.dir;
        if (c.state == 1)
        {
            Vector3 to = player + Vector3(0.0f, 0.2f, 0.0f) - c.origin;
            if (to.LengthSquared() > 1e-4f) { to.Normalize(); dir = to; }
        }
        const int n = (std::min)(kSegsPerChain, (int)(c.out / EnemyLooks::kChainSegLen));
        for (int k = 0; k < n; ++k)
        {
            if (seg >= m_ChainSegEntities.size()) m_ChainSegEntities.push_back(make(m_ChainSegModel));
            const Entity e = m_ChainSegEntities[seg++];
            auto& tf = m_Registry.Get<TransformComponent>(e);
            tf.position = c.origin + dir * ((float)k * EnemyLooks::kChainSegLen);
            tf.rotation = FaceRotation(dir, 0.0f);
            m_Registry.Get<ModelComponent>(e).visible = true;
        }
        if (hook >= m_ChainHookEntities.size()) m_ChainHookEntities.push_back(make(m_ChainHookModel));
        const Entity h = m_ChainHookEntities[hook++];
        auto& tf = m_Registry.Get<TransformComponent>(h);
        tf.position = c.origin + dir * ((float)n * EnemyLooks::kChainSegLen);
        tf.rotation = FaceRotation(dir, 0.0f);
        tf.scale = Vector3(1.6f, 1.6f, 1.6f);   // 鉤は遠くからも分かる大きさに
        m_Registry.Get<ModelComponent>(h).visible = true;
    }
    for (; seg < m_ChainSegEntities.size(); ++seg)
        m_Registry.Get<ModelComponent>(m_ChainSegEntities[seg]).visible = false;
    for (; hook < m_ChainHookEntities.size(); ++hook)
        m_Registry.Get<ModelComponent>(m_ChainHookEntities[hook]).visible = false;
}
