#pragma once
#include "Particle.h"
#include <random>

class ParticleEmitter
{
public:
    // 位置・方向
    Vector3 position = { 0, 0, 0 };
    Vector3 direction = { 0, 1, 0 };     // デフォルト：上向き

    // 発射設定
    float emitRate = 10.0f;              // 毎秒発射数
    float spreadAngle = 30.0f;           // 拡散角度（度）

    // 粒子初期値（最小〜最大でランダム）
    float speedMin = 1.0f;
    float speedMax = 3.0f;
    float sizeMin = 0.1f;
    float sizeMax = 0.3f;
    float lifetimeMin = 0.5f;
    float lifetimeMax = 2.0f;

    // 色
    Vector4 startColor = { 1, 1, 0, 1 };  // 開始色（黄）
    Vector4 endColor = { 1, 0, 0, 0 };    // 終了色（赤→透明）

    // 物理
    Vector3 gravity = { 0, -9.8f, 0 };

public:
    ParticleEmitter();

    // 粒子を初期化
    void InitParticle(Particle& p);

private:
    std::mt19937 m_Rng;

    float RandomFloat(float min, float max);
    Vector3 RandomDirectionInCone(const Vector3& dir, float angle);
};