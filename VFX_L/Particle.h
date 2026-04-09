#pragma once
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

struct Particle
{
    Vector3 position = { 0, 0, 0 };      // 位置
    Vector3 velocity = { 0, 0, 0 };      // 速度
    Vector4 color = { 1, 1, 1, 1 };      // 颜色
    float size = 1.0f;                   // 大小
    float rotation = 0.0f;               // 旋转角度
    float lifetime = 1.0f;               // 总寿命
    float age = 0.0f;                    // 已存活时间
    bool isAlive = false;                // 是否存活

    float GetLifeRatio() const
    {
        return (lifetime > 0) ? (age / lifetime) : 1.0f;
    }
    
};

