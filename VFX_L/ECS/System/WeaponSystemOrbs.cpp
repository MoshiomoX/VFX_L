// ============================================================
// WeaponSystemOrbs.cpp
// 光球（水晶玉の召喚物。2026-10-06、ユーザー：「魔法を貯蔵する単位」）。
//   WandComponent::orbs[unit]（集約が作る）の設定で、プレイヤーの横に光球が出る（以後その場に留まり、ゆっくり昇るだけ。
//   プレイヤーには付いて来ない）：orbInterval 秒毎に 1 個、同時に maxOrbs 個まで、各 orbLife 秒。
//   何も貯蔵していない水晶玉は光球を出さない。
//   貯蔵された魔法（SpellStats / AreaStats の storeUnit == unit）は杖からは撃たず、
//   光球ごとの計時で光球の位置から撃つ（分裂・二重詠唱・発動間隔は魔法自身のルーンの修飾どおり。
//   消費 MP は集約で既に倍率を掛けてある）。範囲魔法（アークボルト・突風）は杖と同じ WeaponSystem::CastArea で撃つ。
//   上級魔法は光球が直接は撃たない：杖と同じく前提の基本魔法の誘発で撃ち、起点だけ光球にする（WeaponSystem::Update）。
//   見た目は AreaVFXPlayer のインスタンス（OrbUnitStats::vfxFile）を貯蔵した魔法の混色で染め、毎フレーム動かす
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "Component/WandComponent.h"
#include "Component/ManaComponent.h"
#include "Swarm/AreaVFXPlayer.h"
#include "Swarm/AreaProfile.h"

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

// ============================================================
// 光球と水晶玉の対応（2026-10-25）。wand.orbs の添字は組み替えでずれるので、
// 光球は「どの水晶玉か」（道具 ID と同じ道具の何個目か）を持ち、毎フレーム添字を引き直す
// ============================================================
void WeaponSystem::SyncOrbUnits(const WandComponent& wand)
{
    for (Orb& o : m_Orbs)
    {
        if (o.age >= o.life) continue;
        int found = -1;
        for (int u = 0; u < (int)wand.orbs.size(); ++u)
            if (wand.orbs[u].id == o.unitId && wand.orbs[u].dupIndex == o.unitDup) { found = u; break; }
        // バックパックを組み替えて水晶玉が無くなった / 貯蔵が空になった光球は消す（UpdateOrbs の消滅処理に任せる）
        if (found < 0 || wand.orbs[found].storedCount <= 0) { o.age = o.life; continue; }
        o.unit = found;
    }
}

void WeaponSystem::UpdateOrbs(float dt, float castDt, float castSpeed, float durationMul,
    const Vector3& playerPos, WandComponent& wand, ManaComponent& mana,
    bool allowNewCast, bool allowPending, bool ignoreCooldown, bool hasTarget,
    const Vector3& targetPos, const Vector3& targetVel)
{
    // 光球ごと・魔法ごとの計時（無ければ作る）。返した参照は次に探すまでの間だけ使う（push_back で無効になる）
    auto spellTimer = [](Orb& o, const SpellStats& s) -> Orb::Timer&
        {
            for (Orb::Timer& t : o.spellTimers)
                if (t.id == s.id && t.dup == s.dupIndex) return t;
            Orb::Timer t;
            t.id = s.id;
            t.dup = s.dupIndex;
            o.spellTimers.push_back(t);
            return o.spellTimers.back();
        };
    auto areaTimer = [](Orb& o, const AreaStats& a) -> float&
        {
            for (Orb::AreaTimer& t : o.areaTimers)
                if (t.id == a.id && t.dup == a.dupIndex) return t.timer;
            Orb::AreaTimer t;
            t.id = a.id;
            t.dup = a.dupIndex;
            o.areaTimers.push_back(t);
            return o.areaTimers.back().timer;
        };

    // 光球の真下の地面に、プレイヤーと同じ高さ（地面からの）で範囲を出す（突風の中心・前へ吹く根元）
    const float playerGround = groundHeight ? groundHeight(playerPos.x, playerPos.z) : 0.0f;
    auto casterUnder = [&](const Vector3& p) -> Vector3
        {
            if (!groundHeight) return Vector3(p.x, playerPos.y, p.z);
            return Vector3(p.x, groundHeight(p.x, p.z) + (playerPos.y - playerGround), p.z);
        };

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
        o.unitId = unit.id;
        o.unitDup = unit.dupIndex;
        o.serial = ++m_OrbSerial;
        if (m_OrbSerial == 0) o.serial = ++m_OrbSerial;
        o.life = unit.orbLife;
        // 出る位置：プレイヤーの横（出た順に 1/maxOrbs 周ずつ向きをずらす）。以後その場に留まり、ゆっくり昇るだけ（付いて来ない。10-06 ユーザー）
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
        o.pos.y += unit.riseSpeed * dt;   // その場でとてもゆっくり昇る（プレイヤーには付いて来ない）
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
        for (size_t k = 0; k < wand.spells.size(); ++k)
        {
            const SpellStats& s = wand.spells[k];
            if (s.storeUnit != o.unit) continue;
            if (s.triggered) continue;   // 上級魔法は誘発で撃つ（WeaponSystem::Update の「誘発」。起点だけ光球）
            Orb::Timer& t = spellTimer(o, s);

            if (t.pendingCasts > 0)
            {
                if (!allowPending) { t.pendingCasts = 0; continue; }   // 死んだら連発の残りは撃たない
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

        // --- 範囲（アークボルトの稲妻・突風など）: 杖と同じ CastArea で、起点を光球にして撃つ ---
        // （稲妻の射程は標的の体で止め、終点の火花・上級魔法への誘発も杖と同じ。足元に出す範囲は光球の真下）
        for (size_t j = 0; j < wand.areas.size(); ++j)
        {
            AreaStats& a = wand.areas[j];
            if (a.storeUnit != o.unit) continue;
            if (a.triggered) continue;   // 上級の光線・竜巻は誘発で撃つ（起点だけ光球）
            float& timer = areaTimer(o, a);
            timer -= castDt;
            if (!ignoreCooldown && timer > 0.0f) continue;
            if (!allowNewCast) continue;
            if (!mana.CanAfford(a.manaCost)) continue;
            if (!CastArea(a, o.pos, casterUnder(o.pos), false, hasTarget, targetPos, castSpeed, durationMul, o.serial)) continue;
            mana.Reserve(a.manaCost);
            ++m_OrbCasts;
            timer = a.castInterval;
        }
    }

    m_Orbs.erase(std::remove_if(m_Orbs.begin(), m_Orbs.end(),
        [](const Orb& o) { return o.age >= o.life; }), m_Orbs.end());
}
