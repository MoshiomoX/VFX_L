// ============================================================
// MobSpawnerRuins.cpp
// 遺跡の敵（2026-10-25、ユーザー「遺跡の雑魚を作って」→ 呪いの鎧・石像ガーゴイル・死霊術師・鎖の番人）の CPU 側。
//   ・呪いの鎧：1 回目の死は GPU の分裂の環に「崩れた」が届く → armorReviveDelay 秒後に同じ所へ蘇った鎧（HP 半分、紫に脈打つ）
//   ・ガーゴイル：目覚め・急降下・着地が環に届く → シーンが石の欠片・土煙・音を出す（動きは全部 GPU）
//   ・死霊術師：詠唱の終わりが環に届く → 近くの最近の死体（シーンがダメージの数字の「倒した」から NoteKill で渡す）
//     から necroRaiseCount 体の骸骨を起こす。地面に印 → necroRaiseDelay 秒後に立つ（死体が無ければ術師の周り）
//   ・鎖の番人：投げの瞬間（溜めの終わり）が向き付きで届く → ChainThrow に積む（鉤を飛ばし、当たれば引き寄せるのはシーン）
// どれも同時上限は見ない（倒した結果・敵の技なので必ず出る。池が一杯なら GPU が捨てる）
// ============================================================
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "World/GridWorld.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"

using DirectX::SimpleMath::Vector3;

namespace
{
    float Rand01() { return (float)rand() / RAND_MAX; }
    constexpr size_t kMaxKills = 48;   // 覚えておく死体の数
}

void MobSpawner::NoteKill(const Vector3& groundPos)
{
    if (m_RecentKills.size() >= kMaxKills)
        m_RecentKills.erase(m_RecentKills.begin());
    m_RecentKills.push_back({ groundPos, m_RunTime });
}

void MobSpawner::QueueDebugRuins(uint32_t kind, int n)
{
    if (kind < Swarm::kEnemyKindArmor || kind > Swarm::kEnemyKindWarden) return;
    m_DebugRuins[kind - Swarm::kEnemyKindArmor] += n;
}

void MobSpawner::ClearRuinsPending()
{
    m_ArmorPending.clear();
    m_RaisePending.clear();
    m_RecentKills.clear();
    m_RuinsFx.clear();
    m_ChainThrows.clear();
}

Vector3 MobSpawner::GargoyleSpot(const Vector3& fallback, float groundY) const
{
    if (!m_Grid) return fallback;
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        const float ang = Rand01() * 6.2831853f;
        const float r = gargoyleSpawnMin + (gargoyleSpawnMax - gargoyleSpawnMin) * Rand01();
        const Vector3 q = m_Player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);
        int gx = 0, gz = 0;
        m_Grid->WorldToCell(q, gx, gz);
        if (!m_Grid->IsWalkable(gx, gz)) continue;
        return { q.x, m_Grid->SampleHeight(q.x, q.z) + groundY, q.z };
    }
    return fallback;
}

bool MobSpawner::HandleRuinsEvent(const Swarm::SplitEvent& ev, const GridWorld& grid, const Vector3& player, SwarmSystem& swarm)
{
    const uint32_t kind = ev.kind & Swarm::kSplitKindMask;
    const uint32_t payload = ev.kind >> 16;
    const Vector3 ground(ev.position.x, grid.SampleHeight(ev.position.x, ev.position.z), ev.position.z);
    auto& audio = AudioSystem::Get();
    switch (kind)
    {
    case Swarm::kEnemyKindArmor:
        // 崩れた：鎧の山（シーンの特効）→ 蘇り待ち
        m_ArmorPending.push_back({ ground, armorReviveDelay, m_ArmorHp * m_HpMul * armorReviveHpFrac });
        m_RuinsFx.push_back({ RuinsFxKind::ArmorFall, ground });
        audio.Play("armor_fall");
        return true;

    case Swarm::kEnemyKindGargoyle:
        if (payload == Swarm::kGargoyleEventWake)
        {
            ++m_GargoyleWakes;
            m_RuinsFx.push_back({ RuinsFxKind::GargoyleWake, ground });
            audio.Play("gargoyle_wake");
        }
        else if (payload == Swarm::kGargoyleEventDive)
        {
            ++m_GargoyleDives;
            m_RuinsFx.push_back({ RuinsFxKind::GargoyleDive, ev.position });
            audio.Play("gargoyle_dive");
        }
        else
        {
            m_RuinsFx.push_back({ RuinsFxKind::GargoyleLand, ground });
            audio.Play("gargoyle_land");
        }
        return true;

    case Swarm::kEnemyKindNecro:
    {
        // 近くの最近の死体（新しい順）から起こす。使った死体は消す（同じ死体から二度起こさない）
        m_RuinsFx.push_back({ RuinsFxKind::NecroCast, ground });
        audio.Play("necro_cast");
        int raised = 0;
        const float r2 = necroRaiseRadius * necroRaiseRadius;
        for (int i = (int)m_RecentKills.size() - 1; i >= 0 && raised < necroRaiseCount; --i)
        {
            const Vector3 d = m_RecentKills[i].pos - ground;
            if (d.x * d.x + d.z * d.z > r2 || m_RunTime - m_RecentKills[i].time > corpseMemory) continue;
            m_RaisePending.push_back({ m_RecentKills[i].pos, necroRaiseDelay + 0.12f * (float)raised, m_SkeletonHp * m_HpMul });
            m_RuinsFx.push_back({ RuinsFxKind::RaiseMark, m_RecentKills[i].pos });
            m_RecentKills.erase(m_RecentKills.begin() + i);
            ++raised;
        }
        // 足りなければ術師の周り（1.5〜3.5m の歩けるマス）
        for (int attempt = 0; raised < necroRaiseCount && attempt < 16; ++attempt)
        {
            const float ang = Rand01() * 6.2831853f;
            const float r = 1.5f + 2.0f * Rand01();
            Vector3 q = ground + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);
            int gx = 0, gz = 0;
            grid.WorldToCell(q, gx, gz);
            if (!grid.IsWalkable(gx, gz)) continue;
            q.y = grid.SampleHeight(q.x, q.z);
            m_RaisePending.push_back({ q, necroRaiseDelay + 0.12f * (float)raised, m_SkeletonHp * m_HpMul });
            m_RuinsFx.push_back({ RuinsFxKind::RaiseMark, q });
            ++raised;
        }
        return true;
    }

    case Swarm::kEnemyKindWarden:
    {
        ChainThrow t;
        t.pos = ev.position;
        t.yaw = (float)payload / 65535.0f * 6.2831853f;
        m_ChainThrows.push_back(t);
        ++m_ChainThrowsSeen;
        audio.Play("chain_throw");
        return true;
    }
    default:
        (void)player;
        return false;
    }
}

void MobSpawner::UpdateRuins(const GridWorld& grid, const Vector3& player, float dt, SwarmSystem& swarm)
{
    const float groundY = swarm.GetAIParams().groundY;
    auto faceYaw = [&](const Vector3& p) { return std::atan2(player.x - p.x, player.z - p.z); };

    // 蘇った鎧
    for (size_t i = 0; i < m_ArmorPending.size();)
    {
        Pending& p = m_ArmorPending[i];
        p.timer -= dt;
        if (p.timer > 0.0f) { ++i; continue; }
        swarm.SpawnEnemy({ p.pos.x, p.pos.y + groundY, p.pos.z }, p.hp, m_ArmorSpeed * finalSpeedMul, Swarm::kEnemyKindArmorRevived, faceYaw(p.pos));
        m_RuinsFx.push_back({ RuinsFxKind::ArmorRise, p.pos });
        AudioSystem::Get().Play("armor_rise");
        ++m_ArmorsRevived;
        m_ArmorPending.erase(m_ArmorPending.begin() + i);
    }
    // 起き上がる骸骨
    uint32_t risen = 0;
    for (size_t i = 0; i < m_RaisePending.size();)
    {
        Pending& p = m_RaisePending[i];
        p.timer -= dt;
        if (p.timer > 0.0f) { ++i; continue; }
        swarm.SpawnEnemy({ p.pos.x, p.pos.y + groundY, p.pos.z }, p.hp, m_SkeletonSpeed * finalSpeedMul, Swarm::kEnemyKindSkeleton, faceYaw(p.pos));
        m_RuinsFx.push_back({ RuinsFxKind::SkeletonRise, p.pos });
        ++m_SkeletonsRaised;
        ++risen;
        m_RaisePending.erase(m_RaisePending.begin() + i);
    }
    if (risen > 0)
        AudioSystem::Get().PlayBurst("skeleton_rise", risen);
    // 古い死体は忘れる
    while (!m_RecentKills.empty() && m_RunTime - m_RecentKills.front().time > corpseMemory)
        m_RecentKills.erase(m_RecentKills.begin());

    // パネルのボタンの分（プレイヤーの周り 8〜12m）
    const float hps[6] = { m_ArmorHp, m_ArmorHp * armorReviveHpFrac, m_GargoyleHp, m_NecroHp, m_SkeletonHp, m_WardenHp };
    const float spd[6] = { m_ArmorSpeed, m_ArmorSpeed, m_GargoyleSpeed, m_NecroSpeed, m_SkeletonSpeed, m_WardenSpeed };
    for (int k = 0; k < 6; ++k)
        if (m_DebugRuins[k] > 0)
            SpawnDebugKind(grid, player, swarm, m_DebugRuins[k], hps[k] * m_HpMul, spd[k], Swarm::kEnemyKindArmor + (uint32_t)k);
}

// ============================================================
// Enemies パネルの「Ruins (GPU)」の段
// ============================================================
void MobSpawner::DrawRuinsImGui(SwarmSystem& swarm)
{
    auto& bomb = swarm.GetBomberParams();
    auto drawRamp = [](const char* label, KindRamp& r)
        {
            ImGui::PushID(label);
            ImGui::DragFloat("Start (s)", &r.start, 5.0f, 0.0f, 1.0e9f);
            ImGui::DragFloat("Ratio Start", &r.ratioStart, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Ratio End", &r.ratioEnd, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Ramp End (s)", &r.rampEnd, 5.0f, 0.0f, 3600.0f);
            ImGui::PopID();
        };
    if (!ImGui::CollapsingHeader("Ruins enemies (GPU)")) return;
    ImGui::Text("ratio now  armor %.2f  gargoyle %.2f  necro %.2f  warden %.2f", m_ArmorRatio, m_GargoyleRatio, m_NecroRatio, m_WardenRatio);
    ImGui::Text("revived %u  wakes %u  dives %u  skeletons %u  chains %u  (pending: armor %zu, raise %zu, corpses %zu)",
        m_ArmorsRevived, m_GargoyleWakes, m_GargoyleDives, m_SkeletonsRaised, m_ChainThrowsSeen,
        m_ArmorPending.size(), m_RaisePending.size(), m_RecentKills.size());

    if (ImGui::TreeNode("Cursed Armor"))
    {
        drawRamp("armor", armorMix);
        ImGui::DragFloat("HP", &m_ArmorHp, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("Speed", &m_ArmorSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Revive Delay (s)", &armorReviveDelay, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Revive HP x", &armorReviveHpFrac, 0.05f, 0.05f, 2.0f);
        ImGui::DragFloat("Scale", &bomb.armorScale, 0.01f, 0.2f, 3.0f);
        ImGui::DragFloat("Damage x", &bomb.armorDamageMul, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Exp x (1st life)", &bomb.armorExpMul, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("Exp x (revived)", &bomb.armorRevivedExpMul, 0.05f, 0.0f, 20.0f);
        if (ImGui::Button("Spawn 5 Armors Nearby")) QueueDebugRuins(Swarm::kEnemyKindArmor, 5);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Stone Gargoyle"))
    {
        drawRamp("gargoyle", gargoyleMix);
        ImGui::DragFloat("HP", &m_GargoyleHp, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("Fly Speed", &m_GargoyleSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Statue Min (m)", &gargoyleSpawnMin, 0.5f, 0.0f, 60.0f);
        ImGui::DragFloat("Statue Max (m)", &gargoyleSpawnMax, 0.5f, 0.0f, 60.0f);
        ImGui::DragFloat("Scale", &bomb.gargoyleScale, 0.01f, 0.2f, 3.0f);
        ImGui::DragFloat("Damage x (landed)", &bomb.gargoyleDamageMul, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Dive Damage x", &bomb.gargoyleDiveDamageMul, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Exp x", &bomb.gargoyleExpMul, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("Wake Dist (m)", &bomb.gargoyleWakeDist, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Auto Wake (s)", &bomb.gargoyleAutoWake, 0.5f, 0.0f, 120.0f);
        ImGui::DragFloat("Wake Time (s)", &bomb.gargoyleWakeTime, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Hover (m)", &bomb.gargoyleHover, 0.05f, 0.0f, 8.0f);
        ImGui::DragFloat("Dive Range (m)", &bomb.gargoyleDiveRange, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Dive Wind-up (s)", &bomb.gargoyleDiveWindup, 0.05f, 0.0f, 3.0f);
        ImGui::DragFloat("Dive Speed", &bomb.gargoyleDiveSpeed, 0.5f, 1.0f, 60.0f);
        ImGui::DragFloat("Dive Time (s)", &bomb.gargoyleDiveTime, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Land Time (s)", &bomb.gargoyleLandTime, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Fly Min (s)", &bomb.gargoyleFlyMin, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Wing Flap", &bomb.gargoyleFlap, 0.02f, 0.0f, 2.0f);
        if (ImGui::Button("Spawn 5 Gargoyles Nearby")) QueueDebugRuins(Swarm::kEnemyKindGargoyle, 5);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Necromancer + Skeleton"))
    {
        drawRamp("necro", necroMix);
        ImGui::DragFloat("HP", &m_NecroHp, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("Speed", &m_NecroSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::DragInt("Raise Count", &necroRaiseCount, 1, 0, 12);
        ImGui::DragFloat("Raise Radius (m)", &necroRaiseRadius, 0.5f, 0.0f, 60.0f);
        ImGui::DragFloat("Raise Delay (s)", &necroRaiseDelay, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Corpse Memory (s)", &corpseMemory, 0.5f, 0.0f, 120.0f);
        ImGui::DragFloat("Scale", &bomb.necroScale, 0.01f, 0.2f, 3.0f);
        ImGui::DragFloat("Exp x", &bomb.necroExpMul, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("Keep Min (m)", &bomb.necroKeepMin, 0.5f, 0.0f, 40.0f);
        ImGui::DragFloat("Keep Max (m)", &bomb.necroKeepMax, 0.5f, 0.0f, 40.0f);
        ImGui::DragFloat("Cast Range (m)", &bomb.necroCastRange, 0.5f, 0.0f, 60.0f);
        ImGui::DragFloat("Wind-up (s)", &bomb.necroWindup, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Cooldown (s)", &bomb.necroCooldown, 0.1f, 0.0f, 60.0f);
        ImGui::DragFloat("Skeleton HP", &m_SkeletonHp, 0.5f, 1.0f, 500.0f);
        ImGui::DragFloat("Skeleton Speed", &m_SkeletonSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Skeleton Scale", &bomb.skeletonScale, 0.01f, 0.2f, 3.0f);
        ImGui::DragFloat("Skeleton Damage x", &bomb.skeletonDamageMul, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Skeleton Exp x", &bomb.skeletonExpMul, 0.05f, 0.0f, 20.0f);
        if (ImGui::Button("Spawn 3 Necromancers Nearby")) QueueDebugRuins(Swarm::kEnemyKindNecro, 3);
        ImGui::SameLine();
        if (ImGui::Button("Spawn 5 Skeletons")) QueueDebugRuins(Swarm::kEnemyKindSkeleton, 5);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Chain Warden"))
    {
        drawRamp("warden", wardenMix);
        ImGui::DragFloat("HP", &m_WardenHp, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("Speed", &m_WardenSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Scale", &bomb.wardenScale, 0.01f, 0.2f, 3.0f);
        ImGui::DragFloat("Damage x (melee, chain)", &bomb.wardenDamageMul, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Exp x", &bomb.wardenExpMul, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("Keep Min (m)", &bomb.wardenKeepMin, 0.5f, 0.0f, 40.0f);
        ImGui::DragFloat("Keep Max (m)", &bomb.wardenKeepMax, 0.5f, 0.0f, 40.0f);
        ImGui::DragFloat("Chain Range (m)", &bomb.wardenRange, 0.5f, 1.0f, 40.0f);
        ImGui::DragFloat("Wind-up (s)", &bomb.wardenWindup, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Hold (s)", &bomb.wardenHold, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Cooldown (s)", &bomb.wardenCooldown, 0.1f, 0.0f, 60.0f);
        if (ImGui::Button("Spawn 3 Wardens Nearby")) QueueDebugRuins(Swarm::kEnemyKindWarden, 3);
        ImGui::TreePop();
    }
}
