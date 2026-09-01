#pragma once
#include <SimpleMath.h>
#include <cstdint>

using namespace DirectX::SimpleMath;

// ============================================
// GPU粒子構造体 (StructuredBuffer用)
// 160バイト (4バイト x 40) / 16バイト境界に整列
// ※ サイズの真実は sizeof()。この注釈は参考値。
//    ファイル末尾の static_assert が唯一の保証。
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
    // --- Texture ---
    int      textureIndex;
    int      atlasRows;
    int      atlasCols;
    int      atlasAnimate;
    // --- Color over Lifetime ---
    int      colorKeyOffset;
    int      colorKeyCount;
    // 所有者ID (0 = 無主)。第5段階の生存数集計で使う。
    // 現時点では EmitCS で 0 に初期化されるだけで、誰も読まない。
    int      ownerID;
    float    _pad1;
};

// ============================================
// Mesh発射用頂点 (StructuredBuffer用)
// 32バイト
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
// 240バイト (4バイト x 60)
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
    // --- Atlas & Texture ---
    int      atlasRows;        // アトラス行数
    int      atlasCols;        // アトラス列数
    int      atlasIndex;       // 固定コマ (-1ならアニメーション)
    int      textureIndex;     // Texture Array内のインデックス

    int      colorKeyOffset;   // ColorKeyBuffer内の開始位置
    int      colorKeyCount;    // キー数 (0=startColor/endColorで線形補間)
    // 所有者ID (0 = 無主)。発射した粒子へ引き継がれる。
    int      ownerID;
    int      _pad2;
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
// b1
//
// ※ deadCount は現在未使用。
//    空き数は CopyStructureCount で GPU 上のバッファへ渡し、
//    EmitCS が SRV から直接読む方式に変えた。
//    毎フレームの Map READ (GPU 待ち) を廃止するため。
//
// ※ maxParticles は削除不可。
//    UpdateCS の "if (id.x >= g_MaxParticles) return;" が使っている。
//    ここを消すと粒子が一切更新されなくなる。
// ============================================
struct DeadListCB
{
    uint32_t deadCount;      // 未使用 (0 を入れる)
    uint32_t maxParticles;   // 粒子プール全体のサイズ
    uint32_t _pad0;
    uint32_t _pad1;
};

// ============================================
// 描画用定数バッファ (ConstantBuffer用)
// VS b0
// ============================================
struct ParticleRenderCB
{
    Matrix   view;
    Matrix   projection;
    Vector3  cameraPosition; // Billboard用
    float    _pad0;
};

// ============================================
// カラーキー (Color over Lifetime用)
// StructuredBuffer用、32バイト
// ============================================
struct ColorKey
{
    Vector4 color;       // RGBA
    float   time;        // 0.0 ~ 1.0 (寿命比率)
    float   _pad0;
    float   _pad1;
    float   _pad2;
};

// ============================================
// HLSL 側構造体との一致を保証する
//
// ※ 手書きミラーの構造体は、使われていない期間に静かに腐る。
//    注釈では防げない。実際 GPUParticle は長い間 128 と
//    書かれ続けていたが、本当は 160 バイトだった。
//    片側だけ変更した瞬間にコンパイルを止めるのが唯一の防御。
// ============================================
static_assert(sizeof(GPUParticle) == 160, "GPUParticle: HLSL側と不一致");
static_assert(sizeof(GPUEmitter) == 240, "GPUEmitter: HLSL側と不一致");
static_assert(sizeof(EmitMeshVertex) == 32, "EmitMeshVertex: HLSL側と不一致");
static_assert(sizeof(ColorKey) == 32, "ColorKey: HLSL側と不一致");

// StructuredBuffer は 16バイト境界を要求する
static_assert(sizeof(GPUParticle) % 16 == 0, "GPUParticle: 16バイト境界違反");
static_assert(sizeof(GPUEmitter) % 16 == 0, "GPUEmitter: 16バイト境界違反");