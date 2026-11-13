// ============================================================
// WeaponSystemBats.cpp
// 吸血コウモリ（AreaProfile::Kind::Bats）。
//   放つと batCount 匹が扇に散って飛び立ち、左右にふらつきながら一番近い敵（1 匹ずつ周りにずらした所）へ。
//   寄ったら噛む = GPU の範囲を 1 回（radius・威力）。噛んだらプレイヤーの所へ戻り、着いたら
//   噛んだ威力 × batHeal を HP に返す（ConsumeBatHeal。上限なし）。噛めないまま maxOut 秒過ぎたら何も持たずに戻る
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "Audio/AudioSystem.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    float Hash01(uint32_t n)
    {
        n = (n << 13u) ^ n;
        n = n * (n * n * 15731u + 789221u) + 1376312589u;
        return (float)(n & 0x7fffffffu) / (float)0x7fffffff;
    }
}

void WeaponSystem::StartBats(const AreaStats& a, const Vector3& playerPos, float durationMul)
{
    if (!m_Swarm || a.profile <= 0) return;
    const AreaProfile& ap = AreaProfileDB::At(a.profile);
    const int n = (std::max)(ap.batCount, 1);
    const float ground = groundHeight ? groundHeight(playerPos.x, playerPos.z) : playerPos.y - m_Swarm->GetAIParams().groundY;

    // 敵の方を真ん中にして扇に飛び立つ
    Vector3 to = m_HasClose ? m_ClosePos - playerPos : Vector3::UnitZ;
    to.y = 0.0f;
    if (to.LengthSquared() < 1e-4f) to = Vector3::UnitZ;
    to.Normalize();
    const float base = std::atan2(to.x, to.z);
    for (int i = 0; i < n; ++i)
    {
        const uint32_t s = ++m_BatSerial;
        Bat b;
        b.id = a.id;
        b.profile = a.profile;
        b.seed = Hash01(s * 7919u) * 100.0f;
        const float fan = (n == 1) ? 0.0f : ((float)i / (float)(n - 1) * 2.0f - 1.0f) * 1.1f;   // ±63 度
        const float ang = base + fan;
        const Vector3 dir(std::sin(ang), 0.0f, std::cos(ang));
        b.pos = Vector3(playerPos.x, ground + batLook.flyHeight, playerPos.z) + dir * 0.4f;
        b.speed = ap.batSpeed * (0.9f + 0.2f * Hash01(s * 31u));
        b.vel = dir * b.speed * 0.8f + Vector3(0.0f, 2.5f, 0.0f);
        const float oa = Hash01(s * 131u) * DirectX::XM_2PI;
        b.offset = Vector2(std::sin(oa), std::cos(oa)) * batLook.spread * Hash01(s * 17u);
        b.life = a.duration * durationMul;
        b.damage = a.damagePerTick;
        b.radius = a.radius;
        b.lifesteal = a.lifesteal > 0.0f;
        m_Bats.push_back(b);
    }
    AudioSystem::Get().Play(ap.sound);
}

void WeaponSystem::UpdateBats(float dt, const Vector3& playerPos)
{
    if (m_Bats.empty()) return;
    const float lift = m_Swarm ? m_Swarm->GetAIParams().groundY : 0.9f;

    for (size_t i = 0; i < m_Bats.size();)
    {
        Bat& b = m_Bats[i];
        b.age += dt;
        const float ground = groundHeight ? groundHeight(b.pos.x, b.pos.z) : b.pos.y - batLook.flyHeight;
        const float fly = ground + batLook.flyHeight + batLook.bob * std::sin(b.age * 6.0f + b.seed);

        // ---- 行き先：行き = 一番近い敵の周り、戻り = プレイヤーの胸 ----
        if (!b.returning && (b.age > batLook.maxOut || b.age > b.life || !m_HasClose))
        {
            b.returning = true;
            b.returnAt = b.age;
        }
        Vector3 goal;
        if (b.returning) goal = playerPos + Vector3(0.0f, 0.4f, 0.0f);
        else goal = Vector3(m_ClosePos.x + b.offset.x, fly, m_ClosePos.z + b.offset.y);

        Vector3 to = goal - b.pos;
        const float dist = to.Length();

        // ---- 着いた ----
        if (!b.returning && Vector2(to.x, to.z).Length() < batLook.biteReach)
        {
            // 噛む：GPU の範囲を 1 回（体の高さ）。吸血のルーン・枠が付いていれば印も
            if (m_Swarm && b.damage > 0.0f)
            {
                const AreaProfile& ap = AreaProfileDB::At(b.profile);
                Swarm::Area area = ap.MakeArea(Vector3(b.pos.x, ground + lift, b.pos.z), false);
                area.radius = b.radius;
                area.damage = b.damage;
                area.tickInterval = 1.0e9f;
                area.timeLeft = 0.01f;
                if (b.lifesteal) area.tickNow |= Swarm::kAreaExtraLifesteal;
                m_Swarm->SpawnArea(area);
                b.heal = b.damage * ap.batHeal;
                ++m_BatBites;
                AudioSystem::Get().Play("hit_homing");
            }
            b.returning = true;
            b.returnAt = b.age;
            b.vel = -b.vel * 0.3f + Vector3(0.0f, 3.0f, 0.0f);   // 噛んだ反動で跳ね上がる
        }
        else if (b.returning && dist < batLook.homeReach)
        {
            m_BatHeal += b.heal;
            m_Bats.erase(m_Bats.begin() + (ptrdiff_t)i);
            continue;
        }
        else if (b.age > b.life + 3.0f)   // 迷子の保険
        {
            m_Bats.erase(m_Bats.begin() + (ptrdiff_t)i);
            continue;
        }

        // ---- 飛ぶ：行き先へ向かいつつ左右にふらつく（羽ばたく蝙蝠の不規則な軌道）----
        if (dist > 1e-3f) to /= dist;
        Vector3 side(to.z, 0.0f, -to.x);
        if (side.LengthSquared() > 1e-4f) side.Normalize();
        const float wob = std::sin(b.age * batLook.wobbleFreq + b.seed) + 0.5f * std::sin(b.age * batLook.wobbleFreq * 2.3f + b.seed * 1.7f);
        const float speed = b.speed * (b.returning ? batLook.returnSpeedMul : 1.0f);
        Vector3 want = (to + side * (wob * batLook.wobble * (b.returning ? 0.5f : 1.0f))) ;
        want.Normalize();
        want *= speed;
        if (!b.returning) want.y += (fly - b.pos.y) * 3.0f;   // 飛ぶ高さへ寄せる
        const float k = (std::min)(1.0f, batLook.steer * dt);
        b.vel += (want - b.vel) * k;
        b.pos += b.vel * dt;
        if (b.pos.y < ground + 0.3f) { b.pos.y = ground + 0.3f; if (b.vel.y < 0.0f) b.vel.y = 0.0f; }

        // ---- 見た目の尾：0.02 秒ごとに今の位置を先頭へ ----
        b.trailTimer -= dt;
        if (b.trailTimer <= 0.0f)
        {
            b.trailTimer += 0.02f;
            for (int t = Bat::kTrail - 1; t > 0; --t) b.trail[t] = b.trail[t - 1];
            b.trail[0] = b.pos;
            b.trailN = (std::min)(b.trailN + 1, Bat::kTrail);
        }
        ++i;
    }
}
