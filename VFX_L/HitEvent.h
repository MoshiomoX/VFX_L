// ============================================================
// HitEvent.h
// 命中イベント。ProjectileSystem が生成し、戦闘側が消費する。
// position を持たせているのは、将来コンボ（命中点で新しい法術を生成）
// を挿し込めるようにするため。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include "Entity.h"

struct HitEvent
{
    Entity projectile;
    Entity target;
    DirectX::SimpleMath::Vector3 position;   // 命中位置
    float  damage;
};