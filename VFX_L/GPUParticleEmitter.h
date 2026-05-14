#pragma once
#include "GPUParticle.h"

// ============================================
// 発射器タイプ (CS側のswitch分岐と対応)
// ============================================
enum class EmitType
{
    Point = 0,
    Sphere = 1,
    Cone = 2,
    Box = 3,
    Ring = 4,
    Disc = 5,
    Mesh = 6,
};

// ============================================
// GPU粒子発射器
// パラメータ管理 → GPUEmitterへ変換 → SBにアップロード
// 形状計算・シミュレーションはCS側で行う
// ============================================
class GPUParticleEmitter
{
public:
    GPUParticleEmitter(int id = 0);
    ~GPUParticleEmitter() = default;

    void Update(float deltaTime);
    GPUEmitter ToGPU() const;

    void SetActive(bool active) { m_IsActive = active; }
    bool IsActive() const { return m_IsActive; }
    int  GetID() const { return m_ID; }
    int  GetPendingEmitCount() const { return m_PendingEmitCount; }

    // --- 形状 ---
    EmitType emitType = EmitType::Point;

    struct ShapeParams
    {
        float   spreadAngle = 0.0f;       // Point, Cone
        float   radius = 1.0f;       // Sphere, Cone, Ring, Disc
        float   innerRadius = 0.0f;       // Ring
        Vector3 boxExtents = { 1, 1, 1 };// Box
        int     meshVertexOffset = 0;        // Mesh
        int     meshVertexCount = 0;        // Mesh
    } shape;

    // --- 発射位置 ---
    Vector3  position = { 0, 0, 0 };
    Vector3  direction = { 0, 1, 0 };

    // --- 発射パラメータ ---
    float    emitRate = 10.0f;
    int      maxParticles = 1000;
    int      particleOffset = 0;

    // --- 速度 & 寿命 ---
    Vector2  speedRange = { 1.0f, 3.0f };
    Vector2  lifetimeRange = { 1.0f, 3.0f };

    // --- 大きさ ---
    Vector4  sizeRange = { 0.1f, 0.3f, 0.0f, 0.1f };

    // --- 色 ---
    Vector4  startColorMin = { 1, 1, 1, 1 };
    Vector4  startColorMax = { 1, 1, 1, 1 };
    Vector4  endColorMin = { 1, 1, 1, 0 };
    Vector4  endColorMax = { 1, 1, 1, 0 };

    // --- 物理 ---
    Vector3  gravity = { 0, -9.81f, 0 };
    float    dragCoeff = 0.0f;

    // --- 回転 ---
    Vector2  rotationRange = { 0, 0 };
    Vector2  angularVelRange = { 0, 0 };

    int atlasRows = 1;
    int atlasCols = 1;
    int atlasIndex = 0;       // -1 = アニメーション
    bool atlasAnimate = false;
    int textureIndex = 0;      // Texture Array内のインデックス

private:
    int   m_ID;
    bool  m_IsActive = true;
    float m_EmitAccumulator = 0.0f;
    int   m_PendingEmitCount = 0;
};