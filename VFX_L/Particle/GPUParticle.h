#pragma once
#include <SimpleMath.h>
#include <cstdint>
#include <memory>

class Texture;

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
    int      renderMode;     // 0 = ビルボード / 1 = 立方体
    // --- 立方体用の 3 軸回転（度）。ビルボードは上の rotation / angularVel を使う ---
    Vector3  rot3;
    // --- 軌跡（帯）。ParticleTrail.hlsli を参照 ---
    uint32_t trailStyle;     // 0 = 帯なし。それ以外は RegisterTrailStyle の id + 1
    Vector3  angVel3;
    uint32_t trailState;     // 下位 8bit = 環の書き込み位置 / 次の 8bit = 記録済みの点数
};

// ============================================
// Mesh 発射源の頂点レイアウト (StructuredBuffer用)
// 16バイト
//
// 発射源は「GPU 上に既にある頂点 buffer」を raw view で読む。
// 頂点形式が統一されていない（VERTEX_3D と SkinnedVertexOut）ので、
// 位置・法線・uv のバイトオフセットをここで教える。
// EmitCS は ByteAddressBuffer を stride * idx + offset で Load3 する。
// ============================================
struct EmitSourceLayout
{
    uint32_t stride;         // 1 頂点のバイト数
    uint32_t posOffset;      // 位置 (float3) のオフセット
    uint32_t normalOffset;   // 法線 (float3) のオフセット
    uint32_t uvOffset;       // uv (float2) のオフセット（溶解の縁判定用）
    // ---- 三角形発射（index buffer の raw view があれば面上の点から出す）----
    // triangleCount == 0 なら頂点だけから出す（胶囊の円柱部のように頂点が無い面には出ない）
    uint32_t triangleCount;  // index 数 / 3。RegisterEmitSource が埋める
    uint32_t indexBytes;     // 2 (R16) か 4 (R32)
    uint32_t _pad0;
    uint32_t _pad1;
};

// ============================================
// GPU発射器構造体 (StructuredBuffer用)
// 336バイト (4バイト x 84)
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
    // Mesh 発射: RegisterEmitSource が返す番号 (-1 = 未設定) と頂点数
    int      sourceId;
    int      sourceCount;
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

    // --- Mesh 発射 ---
    // モデルの世界行列（回転・縮尺込み）。HLSL 側は row_major で受けるので転置しない。
    // 発射位置 = mul(頂点, world) + position
    Matrix   world;
    int      edgeMode;         // 0 = 全頂点 / 1 = 溶解の縁の頂点だけ（EdgeFilterCS の表から選ぶ）
    int      renderMode;       // 0 = ビルボード / 1 = 立方体
    int      trailStyle;       // 0 = 帯なし。それ以外は RegisterTrailStyle の id + 1（粒子へ引き継ぐ）
    int      _padS2;

    // --- 掃引発射（軌跡の隙間埋め）---
    // 今フレームの発射位置 → 前フレームの発射位置 のベクトル。
    // EmitCS が pos += sweep * 乱数[0,1) で線分上にばら撒くので、
    // 速く動く発射器でも軌跡が点々にならない。0 = 従来どおり一点から出す
    Vector3  sweep;
    float    _padT;
};

// ============================================
// 溶解の縁の頂点表を作る CS 用定数バッファ
// EdgeFilterCS 専用 b2（b0/b1 は ParticleCommon の物）
// VFXMeshPS の溶解と同じ式で noise を引き、
// threshold <= n < threshold + edge の頂点だけ表に積む
// ============================================
struct EdgeFilterCB
{
    Vector2  noiseTiling;
    Vector2  noiseScroll;
    float    threshold;
    float    edge;
    uint32_t vertexCount;
    uint32_t sourceId;
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
// Emit の pass 用定数バッファ (ConstantBuffer用)
// EmitCS 専用 b2
//
// 1 回の Dispatch で結べる発射源 buffer は 1 本なので、
// Mesh 発射器は sourceId ごとに Dispatch を分ける。
// activeSource < 0 = Mesh 以外の発射器の pass。
// GlobalCB(b0) は SwarmEmitCS と共用なので、ここには足さない
// ============================================
struct EmitPassCB
{
    int      activeSource;
    int      _pad0;
    int      _pad1;
    int      _pad2;
};

// ============================================
// 粒子の軌跡（帯）
//
// 粒子 1 個ごとに kTrailPoints 個の位置の環を GPU 上に持つ（固定対応なので
// 割り当ても解放も無い）。記録は ParticleTrailCS、帯への展開は ParticleTrailVS。
// CPU は見た目の表（style）を上げるだけで、位置には一切触らない
// ============================================
constexpr uint32_t kTrailPoints = 16;   // ParticleTrail.hlsli の TRAIL_POINTS と一致させる

// GPU へ上げる形（StructuredBuffer、64B）。HLSL の TrailStyle と同じ並び
struct ParticleTrailStyleGPU
{
    Vector4  colorHead;
    Vector4  colorTail;
    float    widthHead;
    float    widthTail;
    float    lifetime;
    float    intensity;
    uint32_t flags;          // 1 = 粒子の色を掛ける / 2 = 粒子の大きさを幅に掛ける
    float    softEdge;
    float    uvRepeat;
    float    uvScroll;
};

// CPU 側で扱う形。GPUParticleSystem::RegisterTrailStyle に渡す
struct ParticleTrailStyle
{
    Vector4 colorHead = { 1, 1, 1, 1 };
    Vector4 colorTail = { 1, 1, 1, 0 };
    float   widthHead = 0.2f;        // 先頭（粒子の位置）の幅
    float   widthTail = 0.0f;        // 末尾の幅
    float   lifetime = 0.5f;         // 帯が覆う秒数。長さ = 粒子の速さ × これ
    float   intensity = 1.0f;        // HDR。>1 で bloom
    bool    inheritColor = true;     // 粒子の今の色（alpha 込み）を掛ける。粒子と一緒に薄れる
    bool    inheritSize = false;     // 幅に粒子の大きさを掛ける
    float   softEdge = 0.6f;         // 0 = 縁が硬い / 1 = 中心線から薄れる
    float   uvRepeat = 1.0f;         // 長さ方向に貼图を何回繰り返すか
    float   uvScroll = 0.0f;         // U/秒
    int     blend = 0;               // 0 additive / 1 alpha
    std::shared_ptr<Texture> texture;   // null なら白

    ParticleTrailStyleGPU ToGPU() const
    {
        ParticleTrailStyleGPU g = {};
        g.colorHead = colorHead;
        g.colorTail = colorTail;
        g.widthHead = widthHead;
        g.widthTail = widthTail;
        g.lifetime = lifetime;
        g.intensity = intensity;
        g.flags = (inheritColor ? 1u : 0u) | (inheritSize ? 2u : 0u);
        g.softEdge = softEdge;
        g.uvRepeat = uvRepeat;
        g.uvScroll = uvScroll;
        return g;
    }
};

// ParticleTrailVS / PS の b1
struct TrailDrawCB
{
    uint32_t styleSlot;      // style id + 1（GPUParticle::trailStyle と同じ表し方）
    uint32_t premultiply;    // alpha 合成の時 1
    float    time;
    float    _pad;
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
static_assert(sizeof(GPUParticle) == 192, "GPUParticle: HLSL側と不一致");
static_assert(sizeof(GPUEmitter) == 336, "GPUEmitter: HLSL側と不一致");
static_assert(sizeof(EmitSourceLayout) == 32, "EmitSourceLayout: HLSL側と不一致");
static_assert(sizeof(EmitPassCB) == 16, "EmitPassCB: HLSL側と不一致");
static_assert(sizeof(EdgeFilterCB) == 32, "EdgeFilterCB: HLSL側と不一致");
static_assert(sizeof(ColorKey) == 32, "ColorKey: HLSL側と不一致");
static_assert(sizeof(ParticleTrailStyleGPU) == 64, "TrailStyle: HLSL側と不一致");
static_assert(sizeof(TrailDrawCB) == 16, "TrailDrawCB: HLSL側と不一致");

// StructuredBuffer は 16バイト境界を要求する
static_assert(sizeof(GPUParticle) % 16 == 0, "GPUParticle: 16バイト境界違反");
static_assert(sizeof(GPUEmitter) % 16 == 0, "GPUEmitter: 16バイト境界違反");