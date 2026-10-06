// ============================================================
// WeaponSystemOrbs.cpp
// 光球（水晶玉の召喚物。2026-10-06、ユーザー：「魔法を貯蔵する単位」）。
//   WandComponent::orbs[unit]（集約が作る）の設定で、プレイヤーの周りを回る光球が出る：
//   orbInterval 秒毎に 1 個、同時に maxOrbs 個まで、各 orbLife 秒。何も貯蔵していない水晶玉は光球を出さない。
//   貯蔵された魔法（SpellStats / AreaStats の storeUnit == unit）は杖からは撃たず、
//   光球ごとの計時で光球の位置から撃つ（分裂・二重詠唱・発動間隔は魔法自身のルーンの修飾どおり。
//   消費 MP は集約で既に倍率を掛けてある）。上級魔法も光球が直接撃つ（誘発を待たない）。
//   見た目は AreaVFXPlayer のインスタンス（OrbUnitStats::vfxFile）を貯蔵した魔法の混色で染め、毎フレーム動かす
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "Component/WandComponent.h"
#include "Component/ManaComponent.h"
#include "Swarm/AreaVFXPlayer.h"
#include "Swarm/AreaProfile.h"
#include <algorithm>
#include <cmath>

using DirectX::SimpleMath::Vector3;

bool WeaponSystem::FindOrb(uint32_t serial, Vector3& pos) const
{
    for (const Orb& o : m_Orbs)
        if (o.serial == serial) { pos = o.pos; return true; }
    return false;
}

bool WeaponSystem::FirstOrbOf(int unit, Vector3& pos, uint32_t& serial) const
{
    for (const Orb& o : m_Orbs)
        if (o.unit == unit && o.age < o.life) { pos = o.pos; serial = o.serial; return true; }
    return false;
}

void WeaponSystem::UpdateOrbs(float dt, float castDt, float castSpeed, float durationMul,
    const Vector3& playerPos, WandComponent& wand, ManaComponent& mana,
    bool allowNewCast, bool ignoreCooldown, bool hasTarget,
    const Vector3& targetPos, const Vector3& targetVel)
{
    // ---- バックパックを組み替えて水晶玉が減った / 貯蔵が空になった光球は消す ----
    for (Orb& o : m_Orbs)
    {
        const bool gone = o.unit >= (int)wand.orbs.size() || wand.orbs[o.unit].storedCount <= 0;
        if (gone) o.age = o.life;   // 下の消滅処理に任せる
    }

    // ---- 出現 ----
    for (int u = 0; u < (int)wand.orbs.size(); ++u)
    {
        OrbUnitStats& unit = wand.orbs[u];
        if (unit.storedCount <= 0) continue;
        unit.spawnTimer -= castDt;   // 魔力解放中は光球も速く出る
        if (unit.spawnTimer > 0.0f) continue;

        int alive = 0;
        for (const Orb& o : m_Orbs) if (o.unit == u && o.age < o.life) ++alive;
        if (alive >= unit.maxOrbs) continue;   // 上限に達している間は次が出せるようになるまで待つ（タイマーは 0 のまま）

        Orb o;
        o.unit = u;
        o.serial = ++m_OrbSerial;
        if (m_OrbSerial == 0) o.serial = ++m_OrbSerial;
        o.life = unit.orbLife;
        // 同じ水晶玉の光球は等間隔に並ぶ（出た順に 1/maxOrbs 周ずつずらす）
        o.phase = DirectX::XM_2PI * (float)(m_OrbSerial % (uint32_t)(std::max)(1, unit.maxOrbs)) / (float)(std::max)(1, unit.maxOrbs);
        o.pos = playerPos + Vector3(std::sin(o.phase) * unit.orbitRadius, unit.orbitHeight, std::cos(o.phase) * unit.orbitRadius);
        if (m_AreaVFX && m_AreaVFXCtx && unit.vfxFile && *unit.vfxFile)
            o.vfxHandle = m_AreaVFX->Play(unit.vfxFile, o.pos, o.life, false, *m_AreaVFXCtx, unit.color);
        m_Orbs.push_back(o);
        unit.spawnTimer = unit.orbInterval;
    }

    // ---- 移動・発射・消滅 ----
    for (Orb& o : m_Orbs)
    {
        o.age += dt;
        if (o.age >= o.life)
        {
            if (m_AreaVFX && o.vfxHandle) m_AreaVFX->StopInstance(o.vfxHandle);
            o.vfxHandle = 0;
            continue;
        }
        const OrbUnitStats& unit = wand.orbs[o.unit];
        o.phase += unit.orbitSpeed * dt;
        o.pos = playerPos + Vector3(std::sin(o.phase) * unit.orbitRadius, unit.orbitHeight, std::cos(o.phase) * unit.orbitRadius);
        if (m_AreaVFX && o.vfxHandle) m_AreaVFX->SetInstance(o.vfxHandle, o.pos, o.pos);

        // 光球の位置からの照準（WeaponSystem::Update の aimFor と同じ先読み）
        auto aimFrom = [&](float projSpeed) -> Vector3
            {
                Vector3 aimPos = targetPos;
                if (projSpeed > 0.01f)
                    aimPos += targetVel * ((targetPos - o.pos).Length() / projSpeed);
                Vector3 dir = aimPos - o.pos;
                if (dir.LengthSquared() < 1e-6f) return Vector3(0, 0, 1);
                dir.Normalize();
                return dir;
            };

        // --- 飛行物 ---
        o.spellTimers.resize(wand.spells.size());
        for (size_t k = 0; k < wand.spells.size(); ++k)
        {
            const SpellStats& s = wand.spells[k];
            if (s.storeUnit != o.unit) continue;
            if (s.triggered) continue;   // 上級魔法は誘発で撃つ（WeaponSystem::Update の「誘発」。起点だけ光球）
            Orb::Timer& t = o.spellTimers[k];

            if (t.pendingCasts > 0)
            {
                t.delayTimer -= castDt;
                if (t.delayTimer <= 0.0f)
                {
                    if (hasTarget && mana.CanAfford(s.manaCost))
                    {
                        QueueOneCast(s, o.pos, aimFrom(s.speed), durationMul);
                        mana.Reserve(s.manaCost);
                        ++m_OrbCasts;
                    }
                    --t.pendingCasts;
                    t.delayTimer = s.castDelay;
                }
                continue;
            }

            t.castTimer -= castDt;
            if (!ignoreCooldown && t.castTimer > 0.0f) continue;
            if (!allowNewCast || !hasTarget) continue;
            if (!mana.CanAfford(s.manaCost)) continue;

            QueueOneCast(s, o.pos, aimFrom(s.speed), durationMul);
            mana.Reserve(s.manaCost);
            ++m_OrbCasts;
            t.pendingCasts = (std::max)(0, s.castCount - 1);
            t.delayTimer = s.castDelay;
            t.castTimer = s.castInterval;
        }

        // --- 範囲（光線）: 光球から標的へ向けて撃つ。光線以外の範囲型は今のところ無い ---
        o.areaTimers.resize(wand.areas.size(), 0.0f);
        for (size_t j = 0; j < wand.areas.size(); ++j)
        {
            AreaStats& a = wand.areas[j];
            if (a.storeUnit != o.unit) continue;
            if (a.triggered) continue;   // 上級の光線は誘発で撃つ（起点だけ光球）
            float& timer = o.areaTimers[j];
            timer -= castDt;
            if (!ignoreCooldown && timer > 0.0f) continue;
            if (!allowNewCast || !hasTarget) continue;
            if (!mana.CanAfford(a.manaCost)) continue;
            const bool beam = a.profile > 0 && AreaProfileDB::At(a.profile).IsBeam();
            if (!beam) continue;
            if (!StartBeam(a, o.pos, targetPos, castSpeed, durationMul, o.serial)) continue;
            mana.Reserve(a.manaCost);
            ++m_OrbCasts;
            timer = a.castInterval;
        }
    }

    m_Orbs.erase(std::remove_if(m_Orbs.begin(), m_Orbs.end(),
        [](const Orb& o) { return o.age >= o.life; }), m_Orbs.end());
}
