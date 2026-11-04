// ============================================================
// GrassCommon.hlsli
// 草のシェーダー共通（Graphics/Renderer/GrassRenderer）：
//   GrassCB     毎フレームの cbuffer、全ステージで b0
//   GrassBlade  草の葉 1 枚。GrassCullCS が書き、GrassVS が読む。
//               2026-10-23 からはカリング CS が葉ごとに 1 回アニメも
//               計算し（風、風源、踏みつけ、火）、葉の背骨を 3 次ベジェで
//               保存する。VS は 21 頂点を組むだけ
//   GrassHash   ワールドに揃えた格子に葉を置く（カメラが動いても
//               葉はその場に留まる）
// C++ 側の対応：GrassRenderer::GrassCB / GrassBlade
// ============================================================
#ifndef GRASS_COMMON_HLSLI
#define GRASS_COMMON_HLSLI

#define GRASS_MAX_GUSTS 4   // = GrassRenderer::kMaxGusts

cbuffer GrassCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float4 g_Frustum[6];    // 内向きの平面：内側 = dot(xyz, p) + w >= 0
    float3 g_CamPos;
    float g_Time;           // 秒（風）。ゲームのポーズ中は止まる
    float2 g_MapOrigin;     // マップの最小角のワールド xz
    float2 g_MapSize;       // マップの大きさ（m）
    int2 g_CellMin;         // このフレームの窓の最初の格子セル
    uint2 g_CellCount;      // 窓の大きさ（格子セル数）
    float g_Spacing;        // 格子の間隔（m）
    float g_MaxDist;        // カメラからこの距離より先には葉を出さない
    float g_FullDist;       // ここまでは最大密度
    float g_FarDensity;     // g_MaxDist での密度、0..1
    float g_HeightMin;
    float g_HeightMax;
    float g_WidthMin;
    float g_WidthMax;
    float g_CurlMax;        // 葉自身の曲がり：先端のずれ / 高さ
    float g_MaxSlope;       // これより急な地面（高さ / 水平距離）には生やさない（崖面）
    float g_WindStrength;   // 最大の突風での先端のずれ / 高さ
    float g_WindSpeed;      // 突風の波、rad/s
    float2 g_WindDir;       // 正規化した xz
    float g_WindScale;      // 1m あたりの突風の波の数
    float g_TrampleBend;    // 完全に踏まれた時の先端のずれ / 高さ
    float3 g_RootColor;     // 根元で地面の色に掛ける
    float g_TrampleRadius;  // プレイヤーの円、0 = 草に触れていない
    float3 g_TipColor;      // 先端で地面の色に掛ける
    float g_TrampleDecay;   // 古い踏みつけに掛けるこのフレームの係数、exp(-dt / recover)
    float3 g_Player;
    uint g_MaxBlades;       // 葉バッファの容量（GrassCullCS はこれ以上は残さない）
    // 焼けた草（GrassFireField、2026-10-12）：g_Fire = (温度, 燃料 0..1)
    float g_FireIgnite;     // 草が燃える温度（残り火の光は少し下から始まる）
    float g_FireShrink;     // 燃料が尽きた所で減る高さ
    float g_FireGlow;       // 先端の残り火の光の HDR の明るさ
    float g_FireOn;         // 0 = 火のマップが未バインド、参照を飛ばす
    // 突風（突風の魔法 2026-10-12、ボスの叩きつけ / 投石雨）：このフレームに踏みつけマップへ押す型、
    // 1 フレームに最大 GRASS_MAX_GUSTS 個（2026-10-25：同じフレームに落ちた岩がそれぞれ自分の円を押す）。
    // g_GustA = (root.x, root.z, dir.x, dir.z)、g_GustB = (length, radius, 0, 0)。radius 0 = 未使用の要素。
    // length 0 = 根元の周りに外向きの円。それ以外は根元から dir 方向へ長さ length m の扇（2026-10-13）
    float4 g_GustA[GRASS_MAX_GUSTS];
    float4 g_GustB[GRASS_MAX_GUSTS];
    // GPU の群れからの環境スタンプ（2026-10-13）：x = 爆発半径の倍率（範囲の半径 x これを
    // 外向きに倒す）、y = 光線の帯の半幅（光線の半径に掛ける）、z = 敵の足跡の強さ（0 = オフ）、
    // w = 敵の足跡の半径 m
    float4 g_TrampleExtra;
    // ---- 草の細部（2026-10-23、ユーザー：「もう少し細かく、風で巻き上がるように」） ----
    float4 g_Wave2;         // 2 つ目の小さい突風の波：xy = 方向、z = 1m あたりの波の数、w = rad/s
    float4 g_Sheen;         // rgb = 葉が裏返った所（突風 / 渦）で色に掛ける、a = 量（0 = オフ）
    float4 g_Vortex;        // 風源：x = 葉を流れに沿って寝かせる風速（m/s）、y = 先端を芯へ巻き上げる
                            // 上昇気流（m/s）、z = はためき（先端のずれ / 高さ）、w = 巻き上がった時の追加の捻り（rad）
    float4 g_BladeShape;    // x = 葉自身の捻り（+- rad）、y = 枯れた葉の割合 0..1、z = 2 つ目の波の重み 0..1、
                            // w = 踏みつけマップに付く風源の跡（強さ、0 = オフ）
    float4 g_DryColor;      // rgb = 枯れた葉に掛ける、a = 跡のスタンプが最大になる風速（m/s）
    float4 g_SwarmExtra;    // x = 敵の position.y から足元を引いた値（群れの AICB groundY、0.9）、yzw = 未使用（2026-10-28）
};

// 草の葉 1 枚（96 バイト）、GrassCullCS がアニメさせる。背骨は根元 (0) から c1、c2 を通って
// tip までの 3 次ベジェ、すべて根元からの相対
struct GrassBlade
{
    float3 pos;     // 根元、地面の上
    float height;   // m（距離による間引きと火による縮みの後）
    float3 c1;
    float width;    // 根元での幅（m）
    float3 c2;
    float twist;    // 幅の軸が根元から先端までにこの rad だけ回る
    float3 tip;
    float sheen;    // 0..1 明るい裏側が見える度合い（突風で裏返った、渦で巻き上がった）
    float2 side;    // 根元での幅の軸（単位 xz）
    float shade;    // 明るさのばらつき
    float dry;      // 0..1 枯れた黄色い葉
    float burn;     // 0..1 焦げ（火）
    float glow;     // 0..1 先端の残り火の光（火）
    float2 pad;
};

uint GrassHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// 進行中のシードから次の乱数 0..1
float GrassRand(inout uint s)
{
    s = GrassHash(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

#endif
