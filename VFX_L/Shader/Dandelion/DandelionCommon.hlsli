// ============================================================
// DandelionCommon.hlsli
// タンポポの綿毛（World/DandelionField、2026-10-22、ユーザー：「風で動く
// ものをもっと増やして」）。草原ではカメラの周りに、茎の先に白い
// 綿毛を付けたものが草より高く群生する。
// 局所的な風源（竜巻、渦、突風、砂旋）、強い天気の風、
// 草を踏み倒すもの（プレイヤー、爆発、光線、突風の扇）が当たると綿毛が散る。
// 種は風に乗って飛んでいき、
// 綿毛はまた生えてくる。
//   DandelionCB  フレームごとの cbuffer。CS / VS / PS で b0
//   Flower       茎 + 綿毛 1 本分。DandelionUpdateCS が書き、DandelionVS が読む
//   Seed         飛んでいる種 1 粒。1 株あたり g_SeedsPer 粒
// C++ 側の対応：DandelionField::DandelionCB / Flower / Seed
//
// 配置：地面の落ち葉（Shader/Debris/DebrisCommon.hlsli）と同じ、
// カメラの周りに巻き付けたワールド固定の格子。
// ============================================================
#ifndef DANDELION_COMMON_HLSLI
#define DANDELION_COMMON_HLSLI

#define DANDELION_HIDDEN 0x80000000u

cbuffer DandelionCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    float g_Dt;               // このステップの秒数（ポーズ中は 0）
    float2 g_MapOrigin;       // 高さマップの最小の角のワールド xz
    float2 g_MapSize;         // m
    float2 g_WindDir;         // 単位ベクトル xz
    float g_WindSpeed;        // m/s（天気の強さ込み）
    float g_WindGust;
    float g_WindWaveScale;
    float g_WindWaveSpeed;
    float g_WindTime;         // 草の風の時計
    float g_Window;           // 窓の半分の大きさ、m
    uint g_Side;              // 1 辺あたりの格子セル数
    float g_Spacing;          // 格子の間隔、m
    uint g_SeedsPer;          // 1 株あたりの種の数
    float g_Time;             // 秒。種のふらつき用
    float g_Density;          // 群生の中で花が生えるセルの割合
    float g_PatchScale;       // m：群生の大きさ（バリューノイズの波長）
    float g_PatchCover;       // 0..1 草原のうち群生が覆う割合
    float g_FadeWidth;        // m：窓の端でこの幅をかけて縮んで消える
    float g_StemMin;          // 茎の高さの範囲、m
    float g_StemMax;
    float g_PuffRadius;       // 綿毛の半径、m
    float g_SeedSize;         // 飛んでいる種の半分の大きさ、m
    float g_ReleaseWind;      // 綿毛が散り始める天気の風（m/s）（綿毛ごとに x 0.8 .. 1.3）
    float g_ReleaseLocal;     // 綿毛の所でこれ以上なら散る局所風源の風速（m/s）
    float g_ReleaseTrample;   // これ以上なら散る踏み倒しの強さ（0..1）
    float g_RegrowDelay;      // 裸の茎が新しい綿毛を生やし始めるまでの s
    float g_RegrowTime;       // 新しい綿毛が育ちきるまでの s
    float g_SeedDrag;         // 1/s：種はこの速さで空気の流れに合わせる
    float g_SeedGravity;      // m/s^2（終端の落下速度 = gravity / drag）
    float g_SeedLifeMin;      // s
    float g_SeedLifeMax;
    float g_SeedLift;         // 種への局所の上昇気流の倍率（軽いので竜巻に持ち上げられる）
    float g_BendGain;         // 風速 1 m/s あたりの茎の曲がり
    float g_MaxSlope;         // これより急な地面（高さ / 距離）には花が生えない
    float3 g_StemColor;
    uint g_Seed;              // 地形の seed
    float3 g_PuffColor;
    uint g_Reset;             // 1 = すべての花を置き直す
    float3 g_CamRight;
    float g_SizeMul;
    float3 g_CamUp;
    float _padCB;
    float3 g_PlayerPos;       // プレイヤーの体の中心（足 + 約 0.9 m）
    float g_PlayerRadius;     // プレイヤーにこれより近い綿毛は散る（0 = プレイヤーなし）
};

// 48 バイト
struct Flower
{
    float3 pos;     // 地面の茎の根元
    float puff;     // 0..1 綿毛がどれだけ残っているか（1 = 満開）
    float2 bend;    // 茎 1 m あたりの茎の先のずれ（空気の流れへなめらかに寄せる）
    float regrow;   // 裸の茎が新しい綿毛を生やし始めるまでの s
    uint cell;      // 格子セルを詰めたもの（x | z << 16）
    uint seed;      // ビット 0-23 はばらつき、ビット 31 = 非表示
    float3 _pad;
};

// 32 バイト。life <= 0 = 飛んでいない
struct Seed
{
    float3 pos;
    float age;
    float3 vel;
    float life;
};

uint DandelionHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float DandelionRand(inout uint s)
{
    s = DandelionHash(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

float DandelionStemHeight(uint seed)
{
    return lerp(g_StemMin, g_StemMax, (float) ((seed >> 4) & 0xFFu) / 255.0) * g_SizeMul;
}

// xz でのワールドの風（GrassVS の風の波の式）
float2 DandelionWindAt(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * 6.2831853 - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

#endif
