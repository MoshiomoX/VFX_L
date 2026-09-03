// ============================================================
// GridWorld.cpp
// ============================================================
#include "World/GridWorld.h"
#include "Manager/InputManager.h"
#include "Debug/DebugManager.h"
#include <algorithm>

using DirectX::SimpleMath::Vector3;

void GridWorld::DrawDebug(const Vector3& center, int viewRadius)
{
    auto& input = InputManager::Get();
    if (input.GetKeyPress(VK_CONTROL) && input.GetKeyTrigger(VK_F3))
        m_DebugVisible = !m_DebugVisible;

    if (!m_DebugVisible) return;
    if (m_GridW <= 0 || m_GridD <= 0) return;

    auto& dbg = DebugManager::Get();
    const float cs = kCellSize;
    const float y = 0.05f;
    const float yBlock = 3.2f;   // 障害物より上（中に描くと隠れる）

    int cx = 0, cz = 0;
    WorldToCell(center, cx, cz);

    const int x0 = (std::max)(0, cx - viewRadius);
    const int x1 = (std::min)(m_GridW - 1, cx + viewRadius);
    const int z0 = (std::max)(0, cz - viewRadius);
    const int z1 = (std::min)(m_GridD - 1, cz + viewRadius);

    const Color walkCol(0.3f, 0.5f, 0.3f, 1.0f);
    const Color blockCol(1.0f, 0.25f, 0.25f, 1.0f);

    // マス番号 → ワールド座標（原点オフセット込み）
    auto wx = [&](int gx) { return m_OriginX + gx * cs; };
    auto wz = [&](int gz) { return m_OriginZ + gz * cs; };

    // ---- 格子線 ----
    for (int x = x0; x <= x1 + 1; ++x)
        dbg.AddDebugLine({ wx(x), y, wz(z0) }, { wx(x), y, wz(z1 + 1) }, walkCol);

    for (int z = z0; z <= z1 + 1; ++z)
        dbg.AddDebugLine({ wx(x0), y, wz(z) }, { wx(x1 + 1), y, wz(z) }, walkCol);

    // ---- 塞がったマスに赤い X（上空に浮かせる）----
    for (int z = z0; z <= z1; ++z)
    {
        for (int x = x0; x <= x1; ++x)
        {
            if (m_Walkable[(size_t)z * m_GridW + x]) continue;

            dbg.AddDebugLine({ wx(x),      yBlock, wz(z) },
                { wx(x) + cs, yBlock, wz(z) + cs }, blockCol);
            dbg.AddDebugLine({ wx(x) + cs, yBlock, wz(z) },
                { wx(x),      yBlock, wz(z) + cs }, blockCol);
        }
    }
}