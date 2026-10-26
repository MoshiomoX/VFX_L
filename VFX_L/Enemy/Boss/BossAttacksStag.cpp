// ============================================================
// BossAttacksStag.cpp
// 草原 3 面目の Boss「雷鳴の雄鹿」だけの中身（2026-10-26）：
//   ・突進の姿勢（溜めで頭を下げ脚を開く、走りで前傾、当てた瞬間に頭を振り上げる、終わると頭が上がる）→ Output::chargePose
//     → BomberCB::bossChargePose → SwarmEnemyVS が首（mode 3）・脚（mode 4）・全身を動かす
//   ・突進で通り過ぎた道筋の輪に落ちる雷（stagThunderWarn 秒の予告 → Ring{ thunder }。見た目・音はシーン）
// 突進そのもの（溜め・走り・当たり）は BossAttacks.cpp の Charge。同日に BossAttacks.cpp（550 行）から分けた
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector3;

void BossAttacks::UpdateStagPose(const Input& in, Output& out)
{
    float pose = 0.0f;
    if (m_Move == BossMove::Charge)
    {
        pose = (chargeWindup > 0.0f) ? (std::min)(1.0f, m_MoveTime / chargeWindup) : 1.0f;
        if (m_ChargeTossT >= 0.0f)
        {
            // 当てた瞬間：頭を振り上げて戻す（1 → 1.3 → 1）
            m_ChargeTossT += in.dt;
            const float u = (std::min)(1.0f, m_ChargeTossT / (std::max)(stagTossTime, 0.02f));
            pose = 1.0f + 0.3f * std::sin(u * 3.14159265f);
            if (u >= 1.0f) m_ChargeTossT = -1.0f;
        }
    }
    else if (m_ChargeRecover > 0.0f)
    {
        // 突進が終わった：頭が上がる（1 → 0）
        m_ChargeRecover -= in.dt;
        pose = (std::max)(0.0f, m_ChargeRecover / (std::max)(stagRecoverTime, 0.02f));
    }
    out.chargePose = pose;
}

// 突進の走りの間に毎フレーム：通り過ぎた道筋の輪に雷の予告を置く。回読みの位置は 2〜3 フレーム遅れるので、
// 走った秒 × 速さ（CPU の予測）で「何個目まで通ったか」を判る
void BossAttacks::StagThunderLane(const Input& in, const GridWorld& grid, Output& out)
{
    if (!stagThunder) return;
    const float ran = (m_MoveTime - chargeWindup) * chargeSpeed;
    const int n = (std::max)(1, (int)(chargeLength / (std::max)(chargeLaneSpacing, 0.5f)));
    while (m_ChargeLanesPassed < n)
    {
        const float s = chargeLaneSpacing * ((float)m_ChargeLanesPassed + 0.5f);
        if (ran < s) break;
        const float x = m_ChargeStart.x + m_ChargeDir.x * s, z = m_ChargeStart.z + m_ChargeDir.y * s;
        Ring r;
        r.center = Vector3(x, grid.SampleHeight(x, z), z);
        r.radius = stagThunderRadius;
        r.warn = (std::max)(stagThunderWarn, 0.05f);
        r.damage = stagThunderDamage * in.damageMul;
        r.kind = BossMove::Charge;
        r.thunder = true;
        m_Rings.push_back(r);
        out.thunderStruck = true;
        ++m_ChargeLanesPassed;
    }
}
