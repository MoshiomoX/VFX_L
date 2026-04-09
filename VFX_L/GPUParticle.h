#pragma once
#include <SimpleMath.h>
#include <cstdint>

using namespace DirectX::SimpleMath;

// ============================================
// GPU粒子構造体 (StructuredBuffer用)
// 128字節 / 每行16字節対齐
// ============================================
struct GPUParticle
{
    Vector3  position;       // 位置
    float    size;           // 現在の大きさ
    Vector3  velocity;       // 速度
    float    age;            // 経過時間
    Vector3  acceleration;   // 加速度
    float    drag;           // 空気抵抗
    Vector4  color;          // 現在の色
    Vector4  startColor;     // 初期色
    Vector4  endColor;       // 最終色
    float    startSize;      // 初期サイズ
    float    endSize;        // 最終サイズ
    float    rotation;       // 回転角度
    float    angularVel;     // 角速度
    float    lifetime;       // 総寿命
    float    isAlive;        // 生存状態 (0.0 / 1.0)
    int      uvFrame;        // UVフレーム
    uint32_t seed;           // 乱数シード
};

// ============================================
// Mesh発射用頂点 (StructuredBuffer用)
// 32字節
// ============================================
struct EmitMeshVertex
{
    Vector3  position;       // 頂点位置
    float    area;           // 三角形面積 (加重サンプリング用)
    Vector3  normal;         // 法線 (発射方向)
    float    _pad0;
};

// ============================================
// GPU発射器構造体 (StructuredBuffer用)
// 多発射器対応
// ============================================
struct GPUEmitter
{
    Vector3  position;       // 発射位置
    int      emitType;       // 発射器タイプ
    Vector3  direction;      // 発射方向
    float    spreadAngle;    // 拡散角度
    Vector3  shapeSize;      // 形状パラメータ
    float    _pad0;
    int      emitCount;      // 今回の発射数
    int      maxParticles;   // 最大粒子数
    int      particleOffset; // 粒子Buffer内開始位置
    float    emitRate;       // 毎秒発射数
    Vector2  speedRange;     // x=min, y=max
    Vector2  lifetimeRange;  // x=min, y=max
    Vector4  sizeRange;      // x=startMin, y=startMax, z=endMin, w=endMax
    Vector4  startColorMin;
    Vector4  startColorMax;
    Vector4  endColorMin;
    Vector4  endColorMax;
    Vector3  gravity;        // 重力
    float    dragCoeff;      // 抵抗係数
    Vector2  rotationRange;  // x=min, y=max
    Vector2  angularVelRange;// x=min, y=max
    int      meshVertexOffset; // EmitMeshVertex Bufferの開始位置
    int      meshVertexCount;  // 頂点数
    float    isActive;       // 有効/無効 (0.0 / 1.0)
    int      emitterID;      // 発射器ID
};

// ============================================
// グローバル定数バッファ (ConstantBuffer用)
// CS / VS 共用 b0
// ============================================
struct GlobalCB
{
    float    deltaTime;
    float    totalTime;
    uint32_t baseSeed;
    int      emitterCount;   // アクティブな発射器数
};

// ============================================
// Dead List用定数バッファ (ConstantBuffer用)
// EmitCS b1 — 現在の空き数をCSに渡す
// ============================================
struct DeadListCB
{
    uint32_t deadCount;      // 現在のDead List内の空きインデックス数
    uint32_t maxParticles;   // 粒子プール全体のサイズ
    uint32_t _pad0;
    uint32_t _pad1;
};

// ============================================
// 描画用定数バッファ (ConstantBuffer用)
// VS b1
// ============================================
struct ParticleRenderCB
{
    Matrix   view;
    Matrix   projection;
    Vector3  cameraPosition; // Billboard用
    float    _pad0;
};