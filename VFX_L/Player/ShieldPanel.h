// ============================================================
// ShieldPanel.h
// シールドの調整用 ImGui（戦闘シーンの Player パネルの中に出す。2026-10-07）
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"

namespace ShieldPanel
{
    // player に ShieldComponent が無ければ何も出さない
    void Draw(Registry& reg, Entity player);
}
