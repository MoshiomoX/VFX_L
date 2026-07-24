// ============================================================
// WeaponSystem.h
// 杖の自動発射：マナ回復 → 間隔判定 → 最近の敵を索敵 → 投射物生成
// ※索敵はプレイヤー中心の全方位（カメラ方向は関係しない）
// ============================================================
#pragma once
#include "Entity.h"
#include <memory>
#include <vector>
#include <SimpleMath.h>

class Registry;
class CollisionSystem;
class Model;

class WeaponSystem
{
public:
    void Update(Registry& reg, float dt, const CollisionSystem& collision);

    // 全投射物で共有するモデル（毎回メッシュ生成すると重いので使い回す）
    void SetProjectileModel(std::shared_ptr<Model> m) { m_ProjectileModel = m; }

private:
    // View 走査中に Entity を作れないので、発射要求を溜めてから処理する
    struct CastRequest
    {
        DirectX::SimpleMath::Vector3 muzzle;
        DirectX::SimpleMath::Vector3 dir;
        float speed, radius, damage, lifetime;
    };

    std::shared_ptr<Model> m_ProjectileModel;
    std::vector<CastRequest> m_Requests;
};