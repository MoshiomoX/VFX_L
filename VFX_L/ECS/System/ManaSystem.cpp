// ============================================================
// ManaSystem.cpp
// ============================================================
#include "ECS/System/ManaSystem.h"
#include "ECS/Registry.h"
#include "Component/ManaComponent.h"
#include "Player/LevelComponent.h"
#include "ECS/View.h"

void ManaSystem::Update(Registry& reg, float dt)
{
    reg.CreateView<ManaComponent>()
        .Each([&](Entity e, ManaComponent& m)
            {
                // ---- 1) 予約された消費を引き落とす ----
                m.current -= m.pendingSpend;
                m.pendingSpend = 0.0f;

                // ---- 2) 回復（レベルで伸びる）----
                const int level = reg.Has<LevelComponent>(e) ? reg.Get<LevelComponent>(e).level : 1;
                m.current += m.EffectiveRegen(level) * dt;

                // ---- 魔力解放の残りと再使用待ち ----
                if (m.surgeTime > 0.0f) m.surgeTime = (m.surgeTime > dt) ? m.surgeTime - dt : 0.0f;
                if (m.surgeCooldownLeft > 0.0f) m.surgeCooldownLeft = (m.surgeCooldownLeft > dt) ? m.surgeCooldownLeft - dt : 0.0f;

                // ---- 3) 範囲に収める ----
                //   予約は CanAfford を通っているので負にはならないはずだが、
                //   ImGui で max を下げられた時のために両側で丸める
                if (m.current > m.max) m.current = m.max;
                if (m.current < 0.0f) m.current = 0.0f;
            });
}