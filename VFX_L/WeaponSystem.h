// ============================================================
// WeaponSystem.h
// 杖の自動発射。出力源（spells）ごとに独立して処理する。
//   分裂       = 同フレーム内で扇状に複数発（空間展開）
//   二重釈放   = pendingCasts を残し、delayTimer 後に次を撃つ（時間展開）
// マナは杖で共有なので、出力源が多いと奪い合う。
// ============================================================
#pragma once
#include "Entity.h"
#include "SpellID.h"
#include <memory>
#include <vector>
#include <utility>
#include <SimpleMath.h>

class Registry;
class CollisionSystem;
class Model;
struct SpellStats;

class WeaponSystem
{
public:
    void Update(Registry& reg, float dt, const CollisionSystem& collision);

    // 投射物モデル（種類ごとに使い回す）
    void SetProjectileModel(ItemID id, std::shared_ptr<Model> m);

    // ---- デバッグ可視化用：今フレームの照準情報 ----
    struct AimDebug
    {
        bool    hasTarget = false;
        Entity  target = 0;
        DirectX::SimpleMath::Vector3 muzzle = { 0, 0, 0 };
        DirectX::SimpleMath::Vector3 dir = { 0, 0, 1 };
        float   range = 0.0f;
    };
    const AimDebug& GetAimDebug() const { return m_AimDebug; }

private:
    // View 走査中に Entity を作れないので、発射要求を溜めてから生成する
    struct CastRequest
    {
        ItemID  id;
        DirectX::SimpleMath::Vector3 muzzle;
        DirectX::SimpleMath::Vector3 dir;
        float speed, radius, damage, lifetime;
    };

    // 1回の施法ぶんの発射要求を積む（分裂の扇状展開もここで行う）
    void QueueOneCast(const SpellStats& s,
        const DirectX::SimpleMath::Vector3& muzzle,
        const DirectX::SimpleMath::Vector3& dir);

    std::shared_ptr<Model> GetModel(ItemID id) const;

    std::vector<CastRequest> m_Requests;
    std::vector<std::pair<ItemID, std::shared_ptr<Model>>> m_Models;

    AimDebug m_AimDebug;   // 可視化用
};