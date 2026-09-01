// ============================================================
// ProjectileSystem.h
// 投射物の飛行・寿命・命中処理
// 命中は CollisionSystem の衝突ペアから拾う（レイヤーで絞り込み済み）。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "ECS/System/HitEvent.h"
#include <vector>

class Registry;
class CollisionSystem;

class ProjectileSystem
{
public:
    void Update(Registry& reg, float dt, const CollisionSystem& collision);

    const std::vector<HitEvent>& GetHitEvents() const { return m_HitEvents; }

private:
    std::vector<HitEvent> m_HitEvents;
    std::vector<Entity>   m_ToDestroy;
};