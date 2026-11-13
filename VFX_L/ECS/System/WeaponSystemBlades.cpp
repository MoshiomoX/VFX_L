// ============================================================
// WeaponSystemBlades.cpp
// 回る剣（魔剣の輪・回旋斬、AreaProfile::Kind::Blades）。
//   プレイヤーの周りを count 本の剣が回る。GPU の範囲は動かせないので、剣が前のフレームから通った弧に
//   hitSpacing 毎の小さな範囲（1 ステップだけ生きる）を出す。同じ敵が 1 回の通過で何度も当たらないよう、
//   範囲に kAreaExtraBlade とガード秒（次の剣が来るまでより少し短い）を付ける（SwarmAreaDamageCS）
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "Audio/AudioSystem.h"

using DirectX::SimpleMath::Vector3;

void WeaponSystem::StartBlades(const AreaStats& a, const Vector3& playerPos, float durationMul)
{
    if (!m_Swarm || a.profile <= 0) return;
    const AreaProfile& ap = AreaProfileDB::At(a.profile);

    BladeSet b;
    b.id = a.id;
    b.profile = a.profile;
    b.center = playerPos;
    b.center.y = groundHeight ? groundHeight(playerPos.x, playerPos.z) : playerPos.y - m_Swarm->GetAIParams().groundY;
    b.count = (std::max)(ap.bladeCount, 1);
    b.radius = a.radius;   // 拡大鏡・分裂で広がる
    // 剣の長さも同じ倍率で（柄はプレイヤー寄りに残す）
    b.length = (std::min)(ap.bladeLength * (ap.radius > 0.0f ? a.radius / ap.radius : 1.0f), a.radius - 0.3f);
    b.spin = DirectX::XMConvertToRadians(ap.spinSpeed);
    b.life = a.duration * durationMul;
    b.intro = (std::min)(bladeLook.introTime, b.life * 0.25f);
    b.outro = (std::min)(bladeLook.outroTime, (std::max)(b.life, 0.1f) * 0.5f);
    b.damage = a.damagePerTick;
    b.lifesteal = a.lifesteal > 0.0f;
    // 同じ所を次の剣が通るまでの秒。ガードはそれより少し短く（1 回の通過で 1 回、次の剣ではまた当たる）。
    // 1 本（回旋斬）は一回り分：出た時に剣の横に居た敵が、一回りして戻った剣にもう一度当たらないように
    const float pass = DirectX::XM_2PI / (std::max)(std::abs(b.spin) * (float)b.count, 1e-3f);
    b.guard = std::clamp(pass * (b.count == 1 ? 0.98f : bladeLook.guardShare), 0.05f, 2.55f);
    // 出る向きは毎回ずらす（同じ所から出ないように）
    b.angle = (float)(m_Blades.size() % 7) * 0.9f + (float)m_TriggeredCasts * 0.37f;
    AudioSystem::Get().Play(ap.sound);
    // 回旋斬：足元の草・落ち葉を外へ倒す（突風と同じ円盤）
    if (b.count == 1 && bladeLook.slashGrassBurst)
        m_GustFx.push_back({ b.center, b.radius, DirectX::SimpleMath::Vector2::Zero, 0.0f, true });
    m_Blades.push_back(b);
}

void WeaponSystem::UpdateBlades(float dt, const Vector3& playerPos)
{
    if (m_Blades.empty()) return;
    const float lift = m_Swarm ? m_Swarm->GetAIParams().groundY : 0.9f;   // 範囲の中心は敵の体の高さ

    for (size_t i = 0; i < m_Blades.size();)
    {
        BladeSet& b = m_Blades[i];
        b.age += dt;
        if (b.age >= b.life + b.outro)
        {
            m_Blades.erase(m_Blades.begin() + (ptrdiff_t)i);
            continue;
        }
        b.center = playerPos;
        b.center.y = groundHeight ? groundHeight(playerPos.x, playerPos.z) : playerPos.y - lift;

        const bool active = b.age < b.life;
        if (b.age < b.intro) b.presence = b.age / (std::max)(b.intro, 1e-3f);
        else if (active) b.presence = 1.0f;
        else b.presence = 1.0f - (b.age - b.life) / (std::max)(b.outro, 1e-3f);

        const float prev = b.angle;
        b.angle += b.spin * dt;

        // ---- 風：剣と同じ向きに回り、少し外へ吹く（風源の渦は上から見て反時計回りが正、剣の角度とは逆）----
        if (m_Swarm && b.presence > 0.0f)
        {
            const bool slash = b.count == 1;
            Swarm::WindSource w = {};
            w.pos[0] = b.center.x; w.pos[1] = b.center.y; w.pos[2] = b.center.z;
            w.radius = b.radius * bladeLook.windRadiusMul;
            w.swirl = (b.spin >= 0.0f ? -1.0f : 1.0f) * (slash ? bladeLook.windSwirlSlash : bladeLook.windSwirlRing);
            w.updraft = bladeLook.windUpdraft;
            w.inflow = -(slash ? bladeLook.windOutSlash : bladeLook.windOutRing);
            w.fade = b.presence;
            w.self = 1.0f;   // 自分の魔法の風はマントを吹かない（突風と同じ）
            m_Swarm->AddFrameWindSource(w);
        }

        // ---- 判定：剣の真ん中が通った弧に、hitSpacing 毎の小さな範囲 ----
        if (!active || !m_Swarm || b.damage <= 0.0f) { ++i; continue; }
        const float mid = (std::max)(b.radius - b.length * 0.5f, 0.2f);
        const float arc = std::abs(b.angle - prev) * mid;
        const int samples = std::clamp((int)std::ceil(arc / (std::max)(bladeLook.hitSpacing, 0.1f)), 1, 6);
        const AreaProfile& ap = AreaProfileDB::At(b.profile);
        const uint32_t guardQ = (uint32_t)std::lround(std::clamp(b.guard, 0.0f, 2.55f) * 100.0f);
        for (int k = 0; k < b.count; ++k)
        {
            const float offset = DirectX::XM_2PI * (float)k / (float)b.count;
            for (int s = 1; s <= samples; ++s)
            {
                const float ang = prev + (b.angle - prev) * (float)s / (float)samples + offset;
                const Vector3 p = b.center + Vector3(std::sin(ang) * mid, lift, std::cos(ang) * mid);
                Swarm::Area area = ap.MakeArea(p, false);
                area.radius = b.length * 0.5f + bladeLook.hitPad;
                area.damage = b.damage;
                area.tickInterval = 1.0e9f;
                area.timeLeft = 0.01f;   // 最初のステップに 1 回だけ当たって消える
                area.tickNow |= Swarm::kAreaExtraBlade | (guardQ << Swarm::kAreaExtraGuardShift);
                if (b.lifesteal) area.tickNow |= Swarm::kAreaExtraLifesteal;
                m_Swarm->SpawnArea(area);
            }
        }
        ++i;
    }
}
