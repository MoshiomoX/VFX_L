#pragma once
#include "Particle/GPUParticle.h"
#include <string>

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
    void SetColorKeyOffset(int offset) { m_ColorKeyOffset = offset; }
    void ClearPendingEmitCount() { m_PendingEmitCount = 0; }

    // --- 形状 ---
    EmitType emitType = EmitType::Point;

    struct ShapeParams
    {
        float   spreadAngle = 0.0f;       // Point, Cone
        float   radius = 1.0f;       // Sphere, Cone, Ring, Disc
        float   innerRadius = 0.0f;       // Ring
        Vector3 boxExtents = { 1, 1, 1 };// Box
        // Mesh: GPUParticleSystem::RegisterEmitSource が返す番号 (-1 = 未設定) と頂点数
        int     sourceId = -1;
        int     sourceCount = 0;
        int     edgeMode = 0;                // 0 = 全頂点 / 1 = 溶解の縁だけ
    } shape;

    // --- 発射位置 ---
    Vector3  position = { 0, 0, 0 };
    Vector3  direction = { 0, 1, 0 };

    // Mesh 発射のみ使う。発射源モデルの世界行列（回転・縮尺込み）。
    // 発射位置 = mul(頂点, world) + position
    Matrix   world = Matrix::Identity;

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

    // --- 描き方 ---
    int renderMode = 0;       // 0 = ビルボード / 1 = メッシュ（既定は組み込みの立方体）
    // メッシュの時だけ使う（ToGPU が ParticleRenderMode::Pack で GPUEmitter::renderMode へ詰める）
    std::string meshPath;             // モデルのファイル（"" = 立方体）。json "mesh"
    bool meshGlow = false;            // 発光（加算・光を受けない）。false = 不透明・光を受ける
    bool meshFaceVelocity = false;    // 前方の軸を進行方向へ向ける（回転の範囲は使わない）
    int  meshForwardAxis = 2;         // モデルの前方の軸（0 = +X / 1 = +Y / 2 = +Z）
    int  meshSlot = 0;                // 実行時：GPUParticleSystem のモデル表の番号（登録は entry 側。0 = 立方体）
    // 発射元（GPU の弾など）の速度を初速に足す。弾と一緒に飛ぶ見た目用（GPU の弾の上でだけ効く）。json "inheritVelocity"
    bool inheritVelocity = false;

    // --- テクスチャ（ビルボードだけ）---
    // textureIndex = ParticleSheets の番号。0 番（旧 6x6）は下の行列を使い、
    // 1 番以降はテクスチャの説明 json の行列を ToGPU が入れる
    int atlasRows = 6;
    int atlasCols = 6;
    int atlasIndex = 0;       // 最初のコマ
    bool atlasAnimate = false; // frameMode == Legacy の時だけ見る（true = 格子の全コマを再生）
    int textureIndex = 0;      // ParticleSheets の番号（json "sheet"）
    int frameMode = 0;         // ParticleFrameMode（0 = 旧式）。json "frameMode"
    int frameCount = 1;        // Animate / Random のコマ数。json "frameCount"
    bool alphaBlend = false;   // 半透明で描く（false = 加算で光る）。json "blend"

    // --- トゥーン（ビルボードだけ。2026-10-04）---
    // テクスチャの alpha を境で切って縁をくっきりさせ（色の alpha の減り = 形が痩せて消える）、
    // 内側から外側へ色を段に分け、外周に濃い線。煙・土・毒霧向け（光る火花・光の玉は切ると紙っぽくなる）。
    // ToGPU が renderMode の 14 bit 以降へ詰める（ParticleRenderMode::PackToon、GPUParticlePS）。json "toon" {...}
    bool  toon = false;
    float toonCut = 0.30f;       // alpha の境（0.05〜0.80。大きいほど塊が小さく締まる）
    int   toonBands = 2;         // 色の段（1〜3）
    float toonShade = 0.60f;     // 一番外の段の明るさ（内側 = 1）
    bool  toonOutline = true;    // 外周の濃い線

    // --- Color over Lifetime ---
    static const int MAX_COLOR_KEYS = 8;
    ColorKey colorKeys[MAX_COLOR_KEYS];
    int colorKeyCount = 0;

private:
    int   m_ID;
    bool  m_IsActive = true;
    float m_EmitAccumulator = 0.0f;
    int   m_PendingEmitCount = 0;
	int   m_ColorKeyOffset = 0; // GPU側ColorKeyBuffer内の開始位置
};