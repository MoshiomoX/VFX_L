// ============================================================
// BossAttacksKnight.cpp
// 遺跡 1 面目の Boss「墓守の騎士」だけの技（2026-10-26）。重撃 / 衝撃波 / 召喚（呪いの鎧）は共通の物、復活は StageDirector::bossLives。
//   双鎖：向きを固めて 2 本の帯の予告（± chainFanDeg）→ 溜めが終わった瞬間に鎖を 2 本投げる（Output::chainThrows。鉤の飛び・
//         当たり・引き寄せはシーンの鎖 = 鎖の番人と同じ物）→ chainRest 秒その場で待つ。掛かったら（OnChainCaught）終わり次第
//         間を空けずに重撃（引き寄せた足元に輪）
// 流れ（StartMove → UpdateMove → 間）は BossAttacks.cpp と同じ。ここは技毎の中身だけ
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    Vector2 FlatDir(const Vector3& from, const Vector3& to, const Vector2& fallback)
    {
        Vector2 d(to.x - from.x, to.z - from.z);
        const float l = d.Length();
        return (l > 1e-3f) ? d / l : fallback;
    }
    Vector2 Rotate(const Vector2& v, float rad)
    {
        const float c = std::cos(rad), s = std::sin(rad);
        return Vector2(v.x * c - v.y * s, v.x * s + v.y * c);
    }
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }   // +Z が 0（SwarmSystem::WarnCircle と同じ）
}

void BossAttacks::StartChainPull(const Input& in, const GridWorld& grid)
{
    m_ChainStart = Vector3(in.bossPos.x, grid.SampleHeight(in.bossPos.x, in.bossPos.z), in.bossPos.z);
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_ChainCaught = false;
}

bool BossAttacks::UpdateChainPull(const Input& in, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;   // 向きは始めた瞬間に固める（帯の間 / 外へ逃げられる）
    if (m_MoveTime < chainWindup) return false;
    if (prevTime < chainWindup)
    {
        const float fan = DirectX::XMConvertToRadians(chainFanDeg);
        for (int k = -1; k <= 1; k += 2)
        {
            Output::ChainThrow t;
            t.pos = in.bossPos;
            t.yaw = YawOf(Rotate(m_ChargeDir, (float)k * fan));
            out.chainThrows.push_back(t);
        }
    }
    if (m_MoveTime < chainWindup + chainRest) return false;
    if (m_ChainCaught)
    {
        m_ChainCaught = false;
        m_DebugMove = BossMove::Slam;   // 次の Update で間を空けずに重撃（引き寄せた所の足元に輪）
    }
    return true;
}

// 予告の見た目：2 本の帯（Boss の足元から ± chainFanDeg の向きへ chainRange）
void BossAttacks::BuildKnightVisuals(const GridWorld& grid)
{
    (void)grid;
    if (m_Move != BossMove::ChainPull || m_MoveTime >= chainWindup) return;
    const float fan = DirectX::XMConvertToRadians(chainFanDeg);
    for (int k = -1; k <= 1; k += 2)
    {
        Visual v;
        v.center = m_ChainStart;
        v.radius = chainRange;
        v.progress = (chainWindup > 0.0f) ? m_MoveTime / chainWindup : 1.0f;
        v.yaw = YawOf(Rotate(m_ChargeDir, (float)k * fan));
        v.shape = -chainWidth;
        m_Visuals.push_back(v);
    }
}
