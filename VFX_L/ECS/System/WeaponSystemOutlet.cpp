// ============================================================
// WeaponSystemOutlet.cpp
// 出口魔法（2026-10-31）：出口魔法の弾が消えた所で、出口の先の魔法を撃ち出す。
// 弾が消えた所は上級魔法の誘発と同じ環で届く（SwarmProjEndCS → m_TriggerEvents）。
// 違いは撃ち出す所：上級魔法は手元や空から、出口の先は弾が消えたその場所から
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "Component/WandComponent.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/VFXId.h"
#include "Audio/AudioSystem.h"
#include "Item/ItemDatabase.h"

using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kNextEnemyMin = 1.5f;   // 次の敵とみなす距離（今当たった敵に向け直さない）
    constexpr float kBurstInterval = 0.10f; // 発動の光の最短の間隔（速い魔法の連射で画面が白くならないように）
    constexpr float kBurstGrow = 0.55f;     // 段が 1 つ深くなる毎に大きくする割合
    constexpr float kBurstMaxScale = 2.6f;
    constexpr float kCarryMix = 0.7f;       // 弾の色に運ぶ魔法の色を混ぜる割合

    // 道具の色を一番明るい成分が 1 になるように（暗い色の魔法でも光が沈まない）
    DirectX::SimpleMath::Vector3 BrightColor(ItemID id)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        DirectX::SimpleMath::Vector3 v = c ? DirectX::SimpleMath::Vector3(c->color.x, c->color.y, c->color.z) : DirectX::SimpleMath::Vector3(1, 1, 1);
        const float m = (std::max)({ v.x, v.y, v.z, 0.01f });
        return v / m;
    }
}

void WeaponSystem::QueueOutletCast(const SpellStats& s, const Vector3& impact, const Vector3& muzzle, float durationMul)
{
    // 向き：次の敵。今の敵しか居なければ弾が飛んでいた向きのまま
    Vector3 dir = impact - muzzle;
    dir.y = 0.0f;
    if (m_HasClose)
    {
        Vector3 to = m_ClosePos - impact;
        to.y = 0.0f;
        if (to.Length() > kNextEnemyMin) dir = to;
    }
    if (dir.LengthSquared() < 1e-4f) dir = Vector3::UnitZ;
    dir.Normalize();

    if (s.triggerLine)
        QueueTriggeredLine(s, impact + dir * 2.0f, impact);   // アイススパイク：足元ではなくこの場所から線を走らせる
    else
        QueueTriggeredCast(s, impact, impact - dir, durationMul, false, impact);

    PlayOutletBurst(s.id, impact, s.outletDepth);
}

void WeaponSystem::PlayOutletBurst(ItemID id, const Vector3& at, int depth)
{
    if (m_OutletFxTimer > 0.0f) return;
    m_OutletFxTimer = kBurstInterval;
    const float scale = (std::min)(1.0f + kBurstGrow * (float)((std::max)(depth, 1) - 1), kBurstMaxScale);
    if (m_AreaVFX && m_AreaVFXCtx)
    {
        const uint32_t h = m_AreaVFX->Play(VFXDatabase::GetFile(VFXId::OutletBurst), at, 0.7f, false, *m_AreaVFXCtx, BrightColor(id));
        if (h != 0) m_AreaVFX->ScaleInstance(h, scale);
    }
    AudioSystem::Get().Play("outlet_burst");
    m_OutletShake = (std::max)(m_OutletShake, (std::min)(0.14f + 0.08f * (float)depth, 0.34f));
}

void WeaponSystem::QueueLarpa(const SpellStats& s, const Vector3& at, float durationMul)
{
    // 分身は下流の魔法も分身の印も持たない = 増え続けず、弾 1 発で下流 1 回の決まりも変わらない
    constexpr float kLarpaDamage = 0.4f;
    constexpr float kLarpaLifetime = 0.5f;
    m_LarpaAngle += 0.7f;
    for (int i = 0; i < s.larpaCount; ++i)
    {
        const float a = m_LarpaAngle + DirectX::XM_2PI * (float)i / (float)s.larpaCount;
        CastRequest req = { s.id, s.profile, at, Vector3(std::sin(a), 0.0f, std::cos(a)),
            s.speed, s.radius, s.damage * kLarpaDamage, s.lifetime * kLarpaLifetime, s.lifesteal > 0.0f ? Swarm::kTagLifesteal : 0u };
        req.atPos = true;   // 手元の閃光を出さない（Drop 型でなければ着弾点の意味は持たない）
        req.areaDamageMul = s.areaDamageMul * kLarpaDamage;
        req.areaDurationMul = durationMul;
        req.fanIndex = i + 1;   // 追う型の魔法も自分の向きの敵を選ぶ
        m_Requests.push_back(req);
    }
}

void WeaponSystem::UpdateCarryColors(const WandComponent& wand)
{
    if (!m_Swarm) return;
    DirectX::SimpleMath::Vector4 colors[32] = {};
    for (uint32_t k = 0; k < (uint32_t)wand.spells.size() && k < TriggerBits::kSpellCount; ++k)
    {
        const Vector3 c = BrightColor(wand.spells[k].id);
        colors[k] = { c.x, c.y, c.z, kCarryMix };
    }
    for (uint32_t j = 0; j < (uint32_t)wand.areas.size() && j < TriggerBits::kAreaCount; ++j)
    {
        const Vector3 c = BrightColor(wand.areas[j].id);
        colors[TriggerBits::kAreaShift + j] = { c.x, c.y, c.z, kCarryMix };
    }
    m_Swarm->SetCarryColors(colors);
}

bool WeaponSystem::FireOutletArea(const AreaStats& a, const Vector3& impact, const Vector3& muzzle, const Vector3& playerPos,
    float castSpeed, float durationMul)
{
    const AreaProfile& ap = AreaProfileDB::At(a.profile);
    bool fired = false;
    if (a.profile > 0 && ap.IsRoam())
    {
        StartTwister(a, impact, playerPos, durationMul);   // 竜巻：この場所に起きて外へ進む
        fired = true;
    }
    else if (a.profile > 0 && ap.IsBeam())
        fired = CastArea(a, muzzle, playerPos, false, true, impact, castSpeed, durationMul, 0);   // 光線・稲妻：手元からこの場所へ
    else
    {
        // 突風など：この場所から、弾が飛んでいた向きへ吹く。
        // CastArea は前へ吹く物の狙いに m_ClosePos を使うので、この 1 回だけこの場所の先へ向ける
        Vector3 fwd = impact - playerPos;
        fwd.y = 0.0f;
        if (fwd.LengthSquared() < 1e-4f) fwd = Vector3::UnitZ;
        fwd.Normalize();
        const bool savedHas = m_HasClose;
        const Vector3 savedPos = m_ClosePos;
        m_HasClose = true;
        m_ClosePos = impact + fwd * 1.5f;
        fired = CastArea(a, impact, impact, false, true, m_ClosePos, castSpeed, durationMul, 0);
        m_HasClose = savedHas;
        m_ClosePos = savedPos;
    }
    if (fired) PlayOutletBurst(a.id, impact, a.outletDepth);
    return fired;
}

void WeaponSystem::SplitDelayedTriggers(const WandComponent& wand, float dt)
{
    m_OutletFxTimer = (std::max)(m_OutletFxTimer - dt, 0.0f);

    // 待たせる bit（triggerDelay のある魔法）
    uint32_t delayed = 0;
    for (uint32_t k = 0; k < (uint32_t)wand.spells.size() && k < TriggerBits::kSpellCount; ++k)
        if (wand.spells[k].triggerDelay > 0.0f) delayed |= 1u << k;
    for (uint32_t j = 0; j < (uint32_t)wand.areas.size() && j < TriggerBits::kAreaCount; ++j)
        if (wand.areas[j].triggerDelay > 0.0f) delayed |= 1u << (TriggerBits::kAreaShift + j);

    if (delayed != 0)
        for (auto& ev : m_TriggerEvents)
        {
            const uint32_t bits = ev.tag & delayed;
            if (bits == 0) continue;
            ev.tag &= ~bits;
            for (uint32_t b = 0; b < TriggerBits::kAreaShift + TriggerBits::kAreaCount; ++b)
            {
                const uint32_t bit = 1u << b;
                if ((bits & bit) == 0) continue;
                const float wait = (b < TriggerBits::kAreaShift)
                    ? wand.spells[b].triggerDelay : wand.areas[b - TriggerBits::kAreaShift].triggerDelay;
                DelayedTrigger d;
                d.ev.position = ev.position;
                d.ev.tag = bit;
                d.left = wait;
                m_DelayedTriggers.push_back(d);
            }
        }

    // 時間が来た物を今の誘発へ戻す
    for (size_t i = 0; i < m_DelayedTriggers.size();)
    {
        m_DelayedTriggers[i].left -= dt;
        if (m_DelayedTriggers[i].left > 0.0f) { ++i; continue; }
        m_TriggerEvents.push_back(m_DelayedTriggers[i].ev);
        m_DelayedTriggers.erase(m_DelayedTriggers.begin() + (std::ptrdiff_t)i);
    }
}
