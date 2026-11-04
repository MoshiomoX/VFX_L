// ============================================
// ParticleCommon.hlsli
// GPU パーティクルシステムの共通定義：
// 構造体 + 乱数関数 + ユーティリティ
// ============================================

#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

// ============================================
// renderMode（GPUParticle / GPUEmitter）。GPUParticle.h の ParticleRenderMode
// および kParticleMesh* と一致させること
//   bits 0-7   : 0 = ビルボード / 1.. = メッシュ表の番号 + 1（0 = 組み込みの立方体）
//   bit  8     : 発光（加算、ライティングなし、色のアルファで消える）
//   bit  9     : 速度の向きを向く（前方軸が速度に沿う、rot3 は使わない）
//   bits 10-11 : 前方軸（0 = +X / 1 = +Y / 2 = +Z / 3 = -Z、モデルの Y は上のまま）
// メッシュパーティクルはバケット（= 表の番号 * 2 + 発光）に振り分ける。各
// バケットが生存メッシュリストの PARTICLE_MESH_BUCKET_CAP 個分を持つ
// ============================================
#define PARTICLE_MESH_SLOTS      16u
#define PARTICLE_MESH_BUCKET_CAP 16384u

// ビルボードのみ（bits 0-7 == 0）：加算の代わりにアルファブレンド。
// ParticleRenderMode::kBillboardAlpha と一致させること
#define PARTICLE_BILLBOARD_ALPHA (1u << 12)

// 全描画モード共通：発生源（GPU 弾 / 範囲 / 経験玉）の速度を
// 初速に加える。読むのは SwarmEmitCS だけ。
// ParticleRenderMode::kInheritSourceVelocity と一致させること
#define PARTICLE_INHERIT_SOURCE_VELOCITY (1u << 13)

// 発光メッシュのみ（2026-10-16）：裏面だけを描き、メッシュを不透明な本体の
// 後ろの縁として見せる（ストーンショットの紫の殻）。ビルボードのトゥーン
// フラグと同じビットだが、メッシュはトゥーンのビットを使わない。
// ParticleRenderMode::kMeshBehind と一致させること。ParticleMeshVS が発光 PS へ渡す
#define PARTICLE_MESH_BEHIND (1u << 14)

// 単体（2026-10-16）：GPU 弾の上で毎フレームちょうど 1 個のパーティクルを出し、
// 次のフレームまで生かす = 描かれるのは常に 1 個（岩、矢、
// 水晶）。エミッタの発生率 / 寿命は無視する。読むのは
// SwarmEmitCS だけ。ParticleRenderMode::kSingleBody と一致させること
#define PARTICLE_SINGLE_BODY (1u << 30)

// ビルボードのみ：トゥーンの見た目、bits 14-25（2026-10-04）。GPUParticlePS で
// 展開し（PARTICLE_TOON_*）、ParticleRenderMode::PackToon が詰める

// ビルボードのみ（2026-10-20、エルデンリング風の風の流線）：四角形を
// カメラに向けて立てる代わりに、パーティクルの速度に沿って引き伸ばす。
// bit 9（メッシュでは「速度の向きを向く」、ビルボードでは今まで未使用）
// = 流線、bits 16-23 = 引き伸ばし（PARTICLE_STRETCH_UNIT 秒単位）
//（長さ = 速度 x 引き伸ばし、最短はサイズ）。トゥーンのビット（16-25）と
// 排他なので、流線はトゥーンにならない。ParticleRenderMode::kStreak と一致させること
#define PARTICLE_STREAK (1u << 9)
#define PARTICLE_STRETCH_SHIFT 16u
#define PARTICLE_STRETCH_UNIT 0.004
bool ParticleIsStreak(int renderMode)
{
    uint m = (uint) renderMode;
    return (m & 0xFFu) == 0u && (m & PARTICLE_STREAK) != 0u;
}
float ParticleStretch(int renderMode)
{
    return (float) ((((uint) renderMode) >> PARTICLE_STRETCH_SHIFT) & 255u) * PARTICLE_STRETCH_UNIT;
}

// ビルボードのみ（2026-10-20）：画面の歪み（屈折）パス。
// bits 10-11 = 強さ 0..3（メッシュでは前方軸に使う）、
// bit 15 = 歪みのみ（色のパスでは見えない。トゥーンのアウトラインの
// ビットで、トゥーンの bit 14 が無ければ意味がない）。GPUParticleVS の g_Pass 3 は
// 強さ > 0 のパーティクルだけをオフセットのターゲットに描く
//（ParticleDistortPS）。その後 Graphics::ApplyDistortion がそれでシーンを歪ませる。
// 3D シーンの後、UI の前（なので HUD は歪まない）。
// UpdateCS がこのパーティクルの数を数える（GPUParticleSystem::HasDistortion）。
// ParticleRenderMode::kDistortShift / kDistortOnly と一致させること
#define PARTICLE_DISTORT_SHIFT 10u
#define PARTICLE_DISTORT_ONLY (1u << 15)
uint ParticleDistortLevel(int renderMode)
{
    uint m = (uint) renderMode;
    if ((m & 0xFFu) != 0u)
        return 0u;
    return (m >> PARTICLE_DISTORT_SHIFT) & 3u;
}
bool ParticleDistortOnly(int renderMode)
{
    uint m = (uint) renderMode;
    return (m & 0xFFu) == 0u && (m & (1u << 14)) == 0u && (m & PARTICLE_DISTORT_ONLY) != 0u;
}

// 全描画モード共通：風への反応、bits 26-29 = q（2026-10-12）。UpdateCS は
// 水平速度を毎秒 q / 15 * PARTICLE_WIND_RATE の割合で
// ワールドの風に近づける。0 = 風を無視。
// ParticleRenderMode::kWindShift / PackWind と一致させること
#define PARTICLE_WIND_SHIFT 26u
#define PARTICLE_WIND_RATE  3.0
float ParticleWindResponse(int renderMode)
{
    return (float) ((((uint) renderMode) >> PARTICLE_WIND_SHIFT) & 15u) / 15.0;
}

// ============================================
// パーティクルがアトラスのどのセルを表示するか（GPUEmitter.frameMode）。
// GPUParticle.h の ParticleFrameMode と一致させること。
//   atlasIndex = 最初のセル、frameCount = セル数
// アニメ中は particle.atlasAnimate = (first << 12) | count で、
// UpdateCS が寿命に合わせて uvFrame を進める。0 = uvFrame は動かない。
// ============================================
#define PARTICLE_FRAME_LEGACY  0 // atlasIndex >= 0 は固定、-1 = 寿命の間にグリッドの全セルを再生
#define PARTICLE_FRAME_FIXED   1
#define PARTICLE_FRAME_ANIMATE 2
#define PARTICLE_FRAME_RANDOM  3

uint ParticleMeshSlotPlusOne(int renderMode)
{
    return ((uint) renderMode) & 0xFFu;
}

uint ParticleMeshBucket(int renderMode)
{
    uint slot = ParticleMeshSlotPlusOne(renderMode) - 1u;
    if (slot >= PARTICLE_MESH_SLOTS)
        slot = 0u; // 不明な表の番号：立方体として描く
    return slot * 2u + ((((uint) renderMode) >> 8u) & 1u);
}

// ============================================
// GPU パーティクル（C++ の GPUParticle と一致）
// 192 バイト（実際の保証は GPUParticle.h の static_assert）
// ============================================
struct GPUParticle
{
    float3 position;
    float size;
    float3 velocity;
    float age;
    float3 acceleration;
    float drag;
    float4 color;
    float4 startColor;
    float4 endColor;
    float startSize;
    float endSize;
    float rotation;
    float angularVel;
    float lifetime;
    float isAlive;
    int uvFrame;
    uint seed;
    
    int textureIndex;
    int atlasRows;
    int atlasCols;
    int atlasAnimate;
    
    int colorKeyOffset;
    int colorKeyCount;
    int ownerID;
    int renderMode; // 上の PARTICLE_MESH_* 参照（0 = ビルボード）
    // メッシュのみ：3 軸の回転（度）。ビルボードは rotation / angularVel を使う
    float3 rot3;
    uint trailStyle; // 0 = トレイルなし、それ以外はスタイル番号 + 1（ParticleTrail.hlsli 参照）
    float3 angVel3;
    uint trailState; // bits 0-7 リングの書き込み位置 / bits 8-15 有効なサンプル数
};
// ============================================
// GPU エミッタ（C++ の GPUEmitter と一致、336 バイト）。
// maxParticles / particleOffset はどのシェーダーも読まない（レイアウト用のみ）
// ============================================
struct GPUEmitter
{
    float3 position;
    int emitType;
    float3 direction;
    float spreadAngle;
    float3 shapeSize;
    float _pad0;
    int emitCount;
    int maxParticles;
    int particleOffset;
    float emitRate;
    float2 speedRange;
    float2 lifetimeRange;
    float4 sizeRange;
    float4 startColorMin;
    float4 startColorMax;
    float4 endColorMin;
    float4 endColorMax;
    float3 gravity;
    float dragCoeff;
    float2 rotationRange;
    float2 angularVelRange;
    int sourceId;
    int sourceCount;
    float isActive;
    int emitterID;
    int atlasRows;
    int atlasCols;
    int atlasIndex;
    int textureIndex;
    int colorKeyoffset;
    int colorKeyCount;
    int ownerID;
    int frameCount; // PARTICLE_FRAME_ANIMATE / _RANDOM が使うフレーム数（旧 _pad2）
    // メッシュ発生：モデルのワールド行列。row_major = C++ の Matrix と同じメモリ順
    row_major float4x4 world;
    int edgeMode; // 0 全頂点 / 1 ディゾルブの縁のみ
    int renderMode; // パーティクルへコピー（PARTICLE_MESH_* 参照）
    int trailStyle; // 0 = トレイルなし、それ以外はスタイル番号 + 1。パーティクルへコピー
    int frameMode; // PARTICLE_FRAME_*（旧 _padS2）
    // スイープ発生：このフレームの発生位置から前フレームの発生位置へ
    // 戻るベクトル。EmitCS がこれに沿ってばらまくので、速いエミッタでも隙間が出ない。
    // 0 = 従来どおり 1 点から発生
    float3 sweep;
    float _padT;
};

// ============================================
// メッシュ発生元の頂点レイアウト（C++ の EmitSourceLayout）
// raw 頂点バッファ内のバイトオフセット
// ============================================
struct EmitSourceLayout
{
    uint stride;
    uint posOffset;
    uint normalOffset;
    uint uvOffset;
    uint triangleCount; // 0 = 頂点サンプリングのみ
    uint indexBytes; // 2 か 4
    uint _pad0;
    uint _pad1;
};
// ============================================
// ColorKey（寿命に沿った色のキーフレーム）
// ============================================
struct ColorKey
{
    float4 color;
    float time;
    float _pad0;
    float _pad1;
    float _pad2;
};
// ============================================
// GPU コライダー（C++ の GPUCollider は削除済み。この構造体はどこも使っていない）
// 48 バイト
// ============================================
struct GPUCollider
{
    float3 center; // Sphere: 中心 / AABB: min / Capsule: 始点
    float radius; // Sphere / Capsule: 半径

    float3 extent; // AABB: max / Capsule: 終点
    int type; // 0=Sphere, 1=AABB, 2=Capsule

    float _pad0;
    float _pad1;
    float _pad2;
    float _pad3;
};
// ============================================
// 定数バッファ（バッファ本体は各 .hlsl で宣言する）
// ============================================
cbuffer GlobalCB : register(b0)
{
    float g_DeltaTime;
    float g_TotalTime;
    uint g_BaseSeed;
    int g_EmitterCount;
};

cbuffer DeadListCB : register(b1)
{
    uint g_DeadCount;
    uint g_MaxParticles;
    uint g_DLPad0;
    uint g_DLPad1;
};


// ============================================
// 乱数（Wang hash）
// ============================================
uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

// シードを進めて 0.0 .. 1.0 の乱数を返す
float Random(inout uint seed)
{
    seed = WangHash(seed);
    return float(seed) / 4294967295.0;
}

// min .. max の乱数
float RandomRange(inout uint seed, float minVal, float maxVal)
{
    return lerp(minVal, maxVal, Random(seed));
}

// ============================================
// 新しいパーティクルのアトラスセルを決める（両方の発生経路から呼ぶ）。
// 専用のシードを使うので、呼び出し側の属性の乱数列は変わらない
// ============================================
void InitParticleFrame(int frameMode, int atlasIndex, int frameCount,
                       int atlasRows, int atlasCols, uint seed,
                       inout int uvFrame, inout int atlasAnimate)
{
    int first = max(atlasIndex, 0);
    int count = clamp(frameCount, 1, 4095);
    uvFrame = first;
    atlasAnimate = 0;

    if (frameMode == PARTICLE_FRAME_ANIMATE)
    {
        atlasAnimate = (first << 12) | count;
    }
    else if (frameMode == PARTICLE_FRAME_RANDOM)
    {
        uint frameSeed = seed ^ 0x85EBCA6Bu;
        uvFrame = first + min((int) (Random(frameSeed) * count), count - 1);
    }
    else if (frameMode == PARTICLE_FRAME_LEGACY && atlasIndex < 0)
    {
        uvFrame = 0;
        atlasAnimate = clamp(atlasRows * atlasCols, 1, 4095);
    }
}

// 各成分が -1 .. 1 のランダムな float3
float3 RandomFloat3(inout uint seed)
{
    float x = Random(seed) * 2.0 - 1.0;
    float y = Random(seed) * 2.0 - 1.0;
    float z = Random(seed) * 2.0 - 1.0;
    return float3(x, y, z);
}

// 単位球面上のランダムな方向
float3 RandomOnSphere(inout uint seed)
{
    float z = Random(seed) * 2.0 - 1.0;
    float phi = Random(seed) * 6.28318530718;
    float r = sqrt(1.0 - z * z);
    return float3(r * cos(phi), r * sin(phi), z);
}

// 単位球内のランダムな点
float3 RandomInSphere(inout uint seed)
{
    float3 dir = RandomOnSphere(seed);
    float t = pow(Random(seed), 1.0 / 3.0);
    return dir * t;
}

// dir を中心に拡散角以内のランダムな方向
float3 RandomInCone(inout uint seed, float3 dir, float angleInDegrees)
{
    float angleRad = radians(angleInDegrees);
    float cosAngle = cos(angleRad);

    float z = lerp(cosAngle, 1.0, Random(seed));
    float phi = Random(seed) * 6.28318530718;
    float r = sqrt(1.0 - z * z);
    float3 localDir = float3(r * cos(phi), r * sin(phi), z);

    float3 up = abs(dir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, dir));
    up = cross(dir, right);

    return right * localDir.x + up * localDir.y + dir * localDir.z;
}

// min と max の間のランダムな float4 の色（チャンネルごと）
float4 RandomColor(inout uint seed, float4 minColor, float4 maxColor)
{
    float4 c;
    c.r = RandomRange(seed, minColor.r, maxColor.r);
    c.g = RandomRange(seed, minColor.g, maxColor.g);
    c.b = RandomRange(seed, minColor.b, maxColor.b);
    c.a = RandomRange(seed, minColor.a, maxColor.a);
    return c;
}

// age / lifetime（0.0 .. 1.0）
float GetLifeRatio(GPUParticle p)
{
    return (p.lifetime > 0.0) ? saturate(p.age / p.lifetime) : 1.0;
}

#endif