// ============================================================
// DebrisCommon.hlsli
// 地面の落ち葉（World/GroundDebris、2026-10-13）：カメラの周りの地面に落ちている
// 軽いゴミ（落ち葉、草屑、小枝）。風の場そのものは見えない。
// プレイヤーに見えるのは風が動かすものなので、
// 突風の魔法、爆発、天気の強風がこれを拾う。
//   DebrisCB  毎フレームの cbuffer、CS / VS / PS で b0
//   Debris    1 片、DebrisUpdateCS が書き、DebrisVS が読む
// C++ 側の対応：GroundDebris::DebrisCB / Debris
//
// 配置：ワールドに揃えた格子（g_Spacing m）をカメラの周りに
// 巻き付けたもの（g_Side x g_Side セル、トーラス）。片 i は格子の
// 列 i % side、行 i / side を受け持ち、ワールドのセルは今の窓の中の
// その列 / 行のセル。カメラが動くと、ある行が片側で窓を出て
// 反対側に置き直される
// （静止、地面の上、距離でフェードするので急に出てこない）。
// ============================================================
#ifndef DEBRIS_COMMON_HLSLI
#define DEBRIS_COMMON_HLSLI

#define DEBRIS_MAX_GUSTS 4u
#define DEBRIS_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define DEBRIS_AREA_SHAKE 8u    // Swarm::kAreaShake：爆発
#define DEBRIS_HIDDEN 0x80000000u
#define DEBRIS_KIND_MASK 3u

cbuffer DebrisCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    float g_Dt;             // このステップの秒（ポーズ中は 0）
    float2 g_MapOrigin;     // 高さマップの最小角のワールド xz
    float2 g_MapSize;       // m
    float2 g_WindDir;       // 単位 xz、風の吹く向き
    float g_WindSpeed;      // m/s（天気の強さ込み）
    float g_WindGust;       // m/s 突風の振幅（同上）
    float g_WindWaveScale;  // 1m あたりの突風の波の数
    float g_WindWaveSpeed;  // rad/s
    float g_WindTime;       // 草の風の時計（草の葉と同じ波）
    float g_Window;         // 窓の半分の大きさ、m（片はカメラ +- これの範囲にいる）
    uint g_Side;            // 窓の 1 辺あたりの格子セル数
    float g_Spacing;        // 格子の間隔、m
    float g_LiftWind;       // その場の風がこれ（m/s）を超えると静止中の片が転がり始める
    float g_Gravity;        // m/s^2（葉は g_Drag のせいでゆっくり落ちる）
    float g_Drag;           // 1/s：速度がこの割合で空気の速度に近づく
    float g_Density;        // 0..1 片を置くセルの割合
    float g_FadeWidth;      // m：窓の端のこの幅で片が縮んで消える
    float g_SizeMul;        // 全部の片の大きさに掛ける
    float3 g_ColorLeaf;     // 種類 0
    float g_BlastAccel;     // 爆発の範囲内での外向きの m/s^2（x (1 - d / r)）
    float3 g_ColorClip;     // 種類 1（草屑）
    float g_GustSpeed;      // 強さ 1 の突風の扇の中の風速 m/s
    float3 g_ColorTwig;     // 種類 2
    float g_Time;           // 秒、静止中のはためき用
    float4 g_GustA[DEBRIS_MAX_GUSTS];   // (root.x, root.z, dir.x, dir.z)
    float4 g_GustB[DEBRIS_MAX_GUSTS];   // (length, radius, strength 0..1, 0)。length 0 = 根元の周りの円
    uint g_GustCount;
    uint g_Seed;            // 地形のシード：同じ格子セルには常に同じ片が生える
    uint g_Reset;           // 1 = 全部の片を置き直す（最初のフレーム / 地形を作り直した時）
    float g_MaxSlope;       // これより急な地面（高さ / 水平距離）には置かない（崖面）
};

// 64 バイト
struct Debris
{
    float3 pos;
    float restT;        // 静止している秒（>= 0）。空中の片も数える（空中にいる時間）
    float3 vel;
    uint cell;          // 詰めた格子セル（x | z << 16）、変わったら置き直す
    float3 rot;         // オイラー角 rad：x = ピッチ、y = ヨー、z = ロール
    float air;          // 1 = 空中、0 = 静止
    float3 angVel;      // rad/s（空中での回転）
    uint seed;          // bit 0-1 種類、bit 2-15 大きさ / 色のばらつき、bit 31 = 非表示（崖 / 間引き）
};

uint DebrisHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float DebrisRand(inout uint s)
{
    s = DebrisHash(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

// 片の板の大きさ（m）：x = 長さ方向、y = 幅方向。種類 0 落ち葉、1 草屑、2 小枝
float2 DebrisSize(uint seed)
{
    uint kind = seed & DEBRIS_KIND_MASK;
    float v = 0.8 + 0.4 * (float) ((seed >> 2) & 0xFu) / 15.0;   // 0.8 .. 1.2
    float2 s = float2(0.22, 0.12);                         // 落ち葉
    if (kind == 1u) s = float2(0.26, 0.035);               // 草屑
    else if (kind == 2u) s = float2(0.34, 0.03);           // 小枝
    return s * v * g_SizeMul;
}

// オイラー角からの回転行列（Y 軸でヨー、次に板の幅方向の軸でピッチ、長さ方向の軸でロール）
float3x3 DebrisRotation(float3 r)
{
    float cy = cos(r.y), sy = sin(r.y);
    float cp = cos(r.x), sp = sin(r.x);
    float cr = cos(r.z), sr = sin(r.z);
    float3x3 Ry = float3x3(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
    float3x3 Rx = float3x3(1, 0, 0, 0, cp, -sp, 0, sp, cp);
    float3x3 Rz = float3x3(cr, -sr, 0, sr, cr, 0, 0, 0, 1);
    return mul(Ry, mul(Rx, Rz));
}

#endif
