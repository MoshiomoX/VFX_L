// ============================================================
// ManaSystem.cpp
// ============================================================
#include "ECS/System/ManaSystem.h"
#include "ECS/Registry.h"
#include "Component/ManaComponent.h"
#include "ECS/View.h"

void ManaSystem::Update(Registry& reg, float dt)
{
    reg.CreateView<ManaComponent>()
        .Each([&](Entity, ManaComponent& m)
            {
                // ---- 1) 予約された消費を引き落とす ----
                m.current -= m.pendingSpend;
                m.pendingSpend = 0.0f;

                // ---- 2) 回復 ----
                m.current += m.regen * dt;

                // ---- 3) 範囲に収める ----
                //   予約は CanAfford を通っているので負にはならないはずだが、
                //   ImGui で max を下げられた時のために両側で丸める
                if (m.current > m.max) m.current = m.max;
                if (m.current < 0.0f) m.current = 0.0f;
            });
}