// ============================================================
// MiasmaStepCS.hlsl
// 遺跡の瘴気の 1 ステップ（World/MiasmaField、2026-11-03）。
// プレイヤー周りの 3D の濃度の箱: 128 x 32 x 128 セル、横 0.5 m、
// 縦 0.3 m。縦軸は「地面からの高さ」（箱は地形に沿うので、
// どの段の台地の上でも層は同じ厚さで乗る）。
// 箱は 2 つでピンポン。このパスは前の箱を読み（箱はプレイヤーに
// セル単位でついて動くので、南西の角がずれていることがある）、
// 次の箱に書く。
// セルごと:
//   速度 = 世界の風 x 割合 + 風源（WindSourceAir: 渦、
//              吸い込み、上昇気流、突風の押し）- 沈降
//              + 爆発（SWARM_AREA_SHAKE）は外へ押し、火は持ち上げ、
//              プレイヤーは足元から霧を押しのける
//   セミラグランジュ移流: 速度 x dt だけ遡り、そこにある前の箱の値を
//              取る（トライリニア）。前の箱の外
//              （箱が今そこへ動いた）= 周囲の量
//   周囲の量へ近づける: 少なければ g_Regrow で増やし（霧が地面から
//              染み出し直す）、多ければ g_Decay で減らす（巻き上がった柱が消える）
//   火（レシピの heat > 0）は薄く燃やし、爆発は穴を空ける。
//   砂漠の砂塵（MiasmaField::Kind::Dust）: 周囲の量はほぼ 0。舞う砂
//              （SandField の状態 w）と爆発が低い所に濃度を「足し」、沈んで
//              消えていく（同じ箱を逆向きに使う）
//   壁のセル（歩けない所）は g_WallHeight より下に霧を持たない
// ============================================================

#include "../Common/WindSources.hlsli"

// MiasmaField::StepCB（160 バイト）と一致させること
cbuffer MiasmaStepCB : register(b0)
{
    float2 g_Origin;        // このステップの箱の南西の角（ワールド xz）
    float2 g_PrevOrigin;    // 読む方の箱の南西の角
    float2 g_MapOrigin;     // フィールド全体（高さマップ / 壁マップ）
    float2 g_MapSize;
    float2 g_Wind;          // プレイヤーの位置の世界の風（m/s、xz）
    float g_Dt;
    float g_Reset;          // 1 = 周囲の量で埋める（新しいマップ）
    float2 g_NoiseOffset;   // 斑は風と一緒に流れる
    float g_NoiseScale;     // 1/m
    float g_LowBias;        // くぼみほど濃く
    float g_Base;           // 地面での周囲の濃度
    float g_LayerH;         // m: 周囲の層の厚さ
    float g_Regrow;         // 1/s
    float g_Decay;          // 1/s
    float g_Settle;         // m/s（下向き）
    float g_WindShare;
    float g_SourceMul;      // 範囲の半径 x これ
    float g_BurnRate;       // 熱 1 あたりの 1/s
    float g_BlastPush;      // 爆発の中心での m/s
    float g_BlastClear;     // 爆発の中での 1/s
    float g_WallHeight;     // 地面からの m
    float g_PlayerPush;     // m/s
    float3 g_Player;
    float g_PlayerRadius;
    float4 g_Clear;         // CPU からの消去: xz、半径、1 = 有効
    float g_LiftRate;       // 砂漠の砂塵: 砂が舞う所（g_Lift.w = 1）で 1 秒あたりに足す濃度
    float g_BlastDust;      // 砂漠の砂塵: 爆発が 1 秒あたりに足す濃度（低い所）
    float g_LiftHeight;     // m: これより下で砂が舞い上がる
    float _padStep;
};

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト
struct MiasmaArea
{
    float3 center;
    float radius;
    float damage;
    float timeLeft;
    float tickInterval;
    float tickTimer;
    float halfHeight;
    uint flags;
    uint vfxType;
    uint tickNow;
};

Texture3D<float> g_Prev : register(t0);
Texture2D<float> g_Height : register(t1);               // GridWorld の高さ（テクセル中心 = 高さセルの中心）
Texture2D<float> g_Solid : register(t2);                // 1 = 壁のセル（GridWorld のセル）
StructuredBuffer<MiasmaArea> g_Areas : register(t3);
Buffer<uint> g_AreaStates : register(t4);
StructuredBuffer<float> g_RecipeHeat : register(t5);    // エフェクトのレシピごと（GrassFireField）
StructuredBuffer<WindSource> g_WindSources : register(t6);
Texture2D<float4> g_Lift : register(t7);                // 砂漠: SandField の状態（w = 砂の舞う強さ）。瘴気では null
RWTexture3D<float> g_Out : register(u0);

#define MIASMA_NX 128
#define MIASMA_NY 32
#define MIASMA_NZ 128
#define MIASMA_CELL 0.5
#define MIASMA_CELL_Y 0.3
#define MIASMA_MAX_AREAS 256u   // = SWARM_MAX_AREAS
#define MIASMA_MAX_SOURCES 64u
#define MIASMA_SHAKE 8u         // = SWARM_AREA_SHAKE

groupshared float4 gs_Src[MIASMA_MAX_SOURCES];   // xz、半径、熱
groupshared float gs_Blast[MIASMA_MAX_SOURCES];  // 1 = 爆発
groupshared uint gs_Count;

float GroundAt(float2 xz)
{
    uint w, h;
    g_Height.GetDimensions(w, h);
    float2 t = (xz - g_MapOrigin) / g_MapSize * float2(w, h) - 0.5;
    int2 i = int2(floor(t));
    float2 f = t - float2(i);
    int2 m = int2(w - 1, h - 1);
    float a = g_Height.Load(int3(clamp(i, int2(0, 0), m), 0));
    float b = g_Height.Load(int3(clamp(i + int2(1, 0), int2(0, 0), m), 0));
    float c = g_Height.Load(int3(clamp(i + int2(0, 1), int2(0, 0), m), 0));
    float d = g_Height.Load(int3(clamp(i + int2(1, 1), int2(0, 0), m), 0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float SolidAt(float2 xz)
{
    uint w, h;
    g_Solid.GetDimensions(w, h);
    int2 c = int2(floor((xz - g_MapOrigin) / g_MapSize * float2(w, h)));
    if (any(c < 0) || c.x >= (int)w || c.y >= (int)h)
        return 1.0;
    return g_Solid.Load(int3(c, 0));
}

float Hash2(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash2(i), b = Hash2(i + float2(1, 0));
    float c = Hash2(i + float2(0, 1)), d = Hash2(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// ここで地面から出る瘴気の量（霧が染み出して戻っていく量）
float Ambient(float2 xz, float h, float ground)
{
    float2 q = (xz - g_NoiseOffset) * g_NoiseScale;
    float n = (ValueNoise(q) + 0.5 * ValueNoise(q * 2.3 + 17.0)) / 1.5;
    float patch = 0.08 + 0.92 * smoothstep(0.35, 0.7, n);   // 霧の塊の間の隙間（石畳が見える）
    // くぼみ: 周囲 4 m の地面より低い -> 濃く
    float around = 0.25 * (GroundAt(xz + float2(4, 0)) + GroundAt(xz - float2(4, 0))
                         + GroundAt(xz + float2(0, 4)) + GroundAt(xz - float2(0, 4)));
    float low = 1.0 + g_LowBias * saturate((around - ground) * 0.8);
    float prof = saturate(1.0 - h / max(g_LayerH, 0.1));
    prof = prof * prof * (3.0 - 2.0 * prof);
    return g_Base * patch * low * prof;
}

float LoadPrev(int3 c)
{
    c = clamp(c, int3(0, 0, 0), int3(MIASMA_NX - 1, MIASMA_NY - 1, MIASMA_NZ - 1));
    return g_Prev.Load(int4(c, 0));
}

// ワールド位置（x、地面からの高さ、z）での前の箱の値。false = 前の箱の外
bool SamplePrev(float3 q, out float v)
{
    float3 t = float3((q.x - g_PrevOrigin.x) / MIASMA_CELL, q.y / MIASMA_CELL_Y, (q.z - g_PrevOrigin.y) / MIASMA_CELL) - 0.5;
    v = 0.0;
    if (t.x < -0.5 || t.z < -0.5 || t.x > MIASMA_NX - 0.5 || t.z > MIASMA_NZ - 0.5)
        return false;
    t.y = clamp(t.y, 0.0, MIASMA_NY - 1.0);   // 地面より下 = 一番下のセル、天井より上 = 一番上のセル
    int3 i = int3(floor(t));
    float3 f = t - float3(i);
    float c000 = LoadPrev(i), c100 = LoadPrev(i + int3(1, 0, 0));
    float c010 = LoadPrev(i + int3(0, 1, 0)), c110 = LoadPrev(i + int3(1, 1, 0));
    float c001 = LoadPrev(i + int3(0, 0, 1)), c101 = LoadPrev(i + int3(1, 0, 1));
    float c011 = LoadPrev(i + int3(0, 1, 1)), c111 = LoadPrev(i + int3(1, 1, 1));
    float x00 = lerp(c000, c100, f.x), x10 = lerp(c010, c110, f.x);
    float x01 = lerp(c001, c101, f.x), x11 = lerp(c011, c111, f.x);
    v = lerp(lerp(x00, x10, f.y), lerp(x01, x11, f.y), f.z);
    return true;
}

[numthreads(8, 4, 8)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // ---- 霧に作用する生きた範囲（火、爆発）を詰めて集める ----
    if (gi == 0u)
        gs_Count = 0u;
    GroupMemoryBarrierWithGroupSync();
    uint heatCount, heatStride;
    g_RecipeHeat.GetDimensions(heatCount, heatStride);
    // 範囲のスロット 1 つにつき 1 スレッド（グループの 256 スレッド = MIASMA_MAX_AREAS）
    if (gi < MIASMA_MAX_AREAS && g_AreaStates[gi] != 0u)
    {
        MiasmaArea ar = g_Areas[gi];
        float heat = (ar.vfxType < heatCount) ? g_RecipeHeat[ar.vfxType] : 0.0;
        float blast = ((ar.flags & MIASMA_SHAKE) != 0u) ? 1.0 : 0.0;
        if (heat > 0.0 || blast > 0.0)
        {
            uint slot;
            InterlockedAdd(gs_Count, 1u, slot);
            if (slot < MIASMA_MAX_SOURCES)
            {
                gs_Src[slot] = float4(ar.center.xz, ar.radius * g_SourceMul, max(heat, 0.0));
                gs_Blast[slot] = blast;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x >= MIASMA_NX || id.y >= MIASMA_NY || id.z >= MIASMA_NZ)
        return;
    const float2 xz = g_Origin + (float2(id.xz) + 0.5) * MIASMA_CELL;
    const float h = (id.y + 0.5) * MIASMA_CELL_Y;
    const float ground = GroundAt(xz);
    const float target = Ambient(xz, h, ground);

    if (g_Reset > 0.5)
    {
        g_Out[id] = target;
        return;
    }

    // ---- 速度 ----
    float lift;
    float3 air = WindSourceAir(g_WindSources, xz, lift);
    float3 v = float3(g_Wind.x, 0.0, g_Wind.y) * g_WindShare + air;
    v.y = air.y - g_Settle;
    float burn = 0.0, clearK = 0.0, dustK = 0.0;
    const uint n = min(gs_Count, MIASMA_MAX_SOURCES);
    for (uint i = 0u; i < n; ++i)
    {
        float4 s = gs_Src[i];
        float2 d = xz - s.xy;
        float dist = length(d);
        float r = dist / max(s.z, 1e-3);
        if (r >= 1.4)
            continue;
        float k = saturate(1.4 - r);
        float2 outw = dist > 1e-3 ? d / dist : float2(0.0, 1.0);
        if (gs_Blast[i] > 0.5)
        {
            v.xz += outw * (g_BlastPush * k);
            v.y += 2.0 * k;
            clearK = max(clearK, saturate(1.0 - r));
            dustK = max(dustK, saturate(1.0 - r));
        }
        if (s.w > 0.0)
        {
            burn = max(burn, s.w * saturate((1.2 - r) * 2.0));
            v.y += 1.5 * k;   // 熱い空気は昇り、霧を一緒に持ち上げる
        }
    }
    {
        float2 d = xz - g_Player.xz;
        float dist = length(d);
        float r = dist / max(g_PlayerRadius, 0.1);
        if (r < 1.0 && h < 2.2)
            v.xz += (dist > 1e-3 ? d / dist : float2(0.0, 1.0)) * (g_PlayerPush * (1.0 - r));
    }

    // ---- 移流 ----
    float dens;
    if (!SamplePrev(float3(xz.x, h, xz.y) - v * g_Dt, dens))   // （x、高さ、z）
        dens = target;   // 箱が今ここへ動いた: 地面から出る量

    // ---- 染み出し / 消散 ----
    if (dens < target)
        dens += (target - dens) * saturate(g_Regrow * g_Dt);
    else
        dens -= (dens - target) * saturate(g_Decay * g_Dt);
    dens *= exp(-g_BurnRate * burn * g_Dt);
    dens *= exp(-g_BlastClear * clearK * g_Dt);
    // ---- 砂漠の砂塵: 舞う砂と爆発が砂塵を巻き上げる（瘴気ではどちらも 0） ----
    float low = saturate(1.0 - h / max(g_LiftHeight, 0.1));
    if (g_LiftRate > 0.0)
    {
        uint lw, lh;
        g_Lift.GetDimensions(lw, lh);
        int2 lc = clamp(int2(floor((xz - g_MapOrigin) / g_MapSize * float2(lw, lh))), int2(0, 0), int2(lw - 1, lh - 1));
        dens += g_LiftRate * g_Lift.Load(int3(lc, 0)).w * low * g_Dt;
    }
    dens += g_BlastDust * dustK * saturate(1.0 - h / (2.0 * max(g_LiftHeight, 0.1))) * g_Dt;
    if (g_Clear.w > 0.5 && distance(xz, g_Clear.xy) < g_Clear.z)
        dens = 0.0;
    if (h < g_WallHeight && SolidAt(xz) > 0.5)
        dens = 0.0;
    g_Out[id] = clamp(dens, 0.0, 8.0);
}
