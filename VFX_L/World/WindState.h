// ============================================================
// WindState.h
// 世界の風（2026-10-12）。草（GrassRenderer）・マント（ClothChainSystem）・粒子（ParticleUpdateCS）が
// 全部これ 1 つを読む。以前は 3 か所がそれぞれ別の向き・別の強さを持っていて、雨の中で草だけが
// 強くなびき、煙は無風のまま、という食い違いがあった。
//   作るのは WeatherSystem（面の基本の風 + 天候の出来事で強く + 向きがゆっくり揺れる）。
//   シーンが毎フレーム各システムへ渡す（CollisionTestScene::PushWind）。
//
// 突風の波は GrassVS / ParticleUpdateCS の式と同じ：
//   phase = dot(xz, dir) * waveScale * 2π - time * waveSpeed、w = (sin(phase) * 0.5 + 0.5)^2
//   風速 = dir * (speed + gust * w) * strength
// 単位は m と秒。dir は xz の単位ベクトル
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cmath>

struct WindState
{
    DirectX::SimpleMath::Vector2 dir = { 0.8660254f, 0.5f };   // 吹いていく向き（xz、単位）。既定 = 30 度
    float speed = 1.2f;        // m/s（基本）
    float gust = 0.8f;         // m/s（突風の振れ幅）
    float waveScale = 0.06f;   // 突風の波の細かさ（1m あたりの波の数）
    float waveSpeed = 1.6f;    // 波の進む速さ（rad/s）
    float strength = 1.0f;     // 天候の倍率（1 = 普段の日）。speed と gust に掛かる

    void SetYawDeg(float deg)
    {
        const float r = DirectX::XMConvertToRadians(deg);
        dir = { std::cos(r), std::sin(r) };
    }
    float YawDeg() const { return DirectX::XMConvertToDegrees(std::atan2(dir.y, dir.x)); }

    // 位置 (x, z)・時刻 time の風速ベクトル（GPU と同じ波）
    DirectX::SimpleMath::Vector3 Velocity(float time, float x, float z) const
    {
        const float phase = (x * dir.x + z * dir.y) * waveScale * 6.2831853f - time * waveSpeed;
        float w = std::sin(phase) * 0.5f + 0.5f;
        w *= w;
        const float v = (speed + gust * w) * strength;
        return { dir.x * v, 0.0f, dir.y * v };
    }
};
