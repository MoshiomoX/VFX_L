// ============================================================
// SwarmOrbTypes.h
// 経験値オーブの本体と調整値の CB。
// HLSL 側は Shader/Common/SwarmOrbTypes.hlsli（SwarmOrb）と SwarmOrbCB.hlsli（SwarmOrbCB）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    struct Orb
    {
        Vector3  position;
        float    amount = 0.0f;
        Vector3  velocity;
        float    attractSpeed = 0.0f;   // 今の吸引の速さ。0 = まだ吸われていない（SwarmOrbMoveCS が書く）
    };
    static_assert(sizeof(Orb) == 32, "SwarmOrb layout mismatch");

    // ============================================================
    // 経験値オーブの調整値（b2）
    // HitCS（生成）と OrbMoveCS（吸引・取得）の両方が読む
    // ============================================================
    struct OrbCB
    {
        float attractRadius = 5.0f;   // ここに入ると吸い寄せ開始
        float pickupRadius = 0.6f;    // ここまで来たら取得
        float accel = 30.0f;          // 吸い寄せの加速度
        float maxSpeed = 18.0f;

        float amount = 10.0f;         // 1個あたりの経験値（今は全敵共通）
        float orbY = 0.5f;            // オーブの浮遊高さ
        float _pad[2] = {};
    };
    static_assert(sizeof(OrbCB) == 32, "SwarmOrbCB layout mismatch");
}
