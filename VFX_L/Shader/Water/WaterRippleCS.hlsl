// ============================================================
// WaterRippleCS.hlsl
// 池の波紋を 1 ステップ進める（World/WaterSurface、2026-10-27）。
// マップ全体の高さ場、1024^2（1 テクセル 0.2 m）：
// x = 現在の高さ、y = 1 ステップ前の高さ。踏み跡マップと同じくピンポン。
// zw = 大波だけについて同じもの（爆発、ボスのスラムと投石）：
// 波は足し合わせなので、zw は xy のうちの大波の分。WaterPS は白い
// 波頭の線を zw から描くので、プレイヤー自身の足音や着地では出ない
//（ユーザー：「まだ小さい輪が出る」）。
// 動くのは池の中（g_Mask）のテクセルだけ。乾いたテクセルは 0 のままで、
// 隣の波にとっての壁になる（水側の自分の高さを読み返す
// = 波が岸で跳ね返る）。
//
// 1 ステップごと（減衰つきの波動方程式）：
//   next = cur + (cur - prev) * keep + courant2 * laplacian - cur * restore
//（restore はごく小さい。押しは水量を変えないので、丸め誤差のずれを消すだけ）
// 水を押すものは、フレームの最初のステップでだけ加える
//（g_Sources）：
//   CPU の押し   足音 / 着地 / 水の中を歩く（WaterSurface::AddRipple。
//                ボスのスラムと投石は大波）
//   GPU 範囲     水がまだ見ていない範囲（g_SeenIn）：
//                爆発（SWARM_AREA_SHAKE）は大波として強く押し、それ以外は
//                少しだけ。カプセル（光線）は飛ばす。グループが自分のタイルに
//                かかる新しい範囲を共有メモリに詰める
//   敵           水の中を歩く敵（WaterStirCS が毎フレームリストを書き、
//                範囲と同じやり方で集める）
//   雨           雨セルごとに、このステップ確率 g_RainProb で 1 滴
// それ以外に水を動かすものはない：原因がなければ平らなまま。
// ============================================================

// WaterSurface::RippleCB と一致させること
cbuffer WaterRippleCB : register(b0)
{
    float2 g_MapOrigin;     // マップの最小角のワールド xz
    float2 g_MapSize;       // メートル
    float g_Courant2;       //（波の速さ x dt / テクセル）^2
    float g_Keep;           // 1 ステップで残す速度（減衰）
    float g_Restore;        // 1 ステップごとに 0 へ引き戻す量
    float g_Texel;          // 1 テクセルあたりのメートル
    uint g_Step;            // ステップのカウンタ（雨のハッシュ用）
    uint g_Sources;         // 1 = このステップで押しを加える
    float g_RainProb;       // このステップで雨セルごとに 1 滴落ちる確率
    float g_RainCell;       // メートル
    float g_RainStrength;   // m
    float g_RainRadius;     // m
    float g_BlastStrength;  // m（爆発の範囲）
    float g_AreaStrength;   // m（その他の範囲）
    uint g_ImpulseCount;
    float3 _ripplePad;
    float4 g_Impulse[16];   // x, z, 半径（負 = 大波）, 強さ（m、+ = 中心を押し下げる）
};

// = SwarmArea（Shader/Common/SwarmCommon.hlsli）、48 バイト
struct RippleArea
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

Texture2D<float4> g_In : register(t0);          // xy = 全部の波（現在、前ステップ）、zw = 大波のみ
Texture2D<float> g_Mask : register(t1);          // 1 = 池
StructuredBuffer<RippleArea> g_Areas : register(t2);
Buffer<uint> g_AreaStates : register(t3);        // 0 = 死亡
Buffer<float> g_SeenIn : register(t4);           // 範囲スロットごと：水が前回見たときの timeLeft（0 = 空）
StructuredBuffer<float4> g_Stirs : register(t5); // 水の中を歩く敵（WaterStirCS）：x, z, 半径, 強さ
Buffer<uint> g_StirCount : register(t6);         // [0] = WaterStirCS が書いた数（リストを超えることがある）
Texture2D<float> g_Ice : register(t7);           // 池の上の氷（WaterIceCS）、この 4 分の 1 のサイズ
RWTexture2D<float4> g_Out : register(u0);
RWBuffer<float> g_SeenOut : register(u1);        // 次のフレーム用の同じもの（最初のステップでグループ 0 が書く）

#define RIPPLE_SIZE 1024
#define RIPPLE_MAX_AREAS 256u    // = SWARM_MAX_AREAS
#define RIPPLE_MAX_SOURCES 32u
#define RIPPLE_MAX_STIRS 128u    // = WaterSurface::kMaxStirs
#define RIPPLE_AREA_CAPSULE 4u   // = SWARM_AREA_CAPSULE
#define RIPPLE_AREA_SHAKE 8u     // = SWARM_AREA_SHAKE
#define RIPPLE_ICE_SCALE 4u      // 氷 1 テクセルあたりの波紋テクセル数（WaterSurface::kIceSize）
#define RIPPLE_ICE_SOLID 0.05    // これ以上で凍結：波は止まり、岸と同じように跳ね返る

groupshared float4 gs_Source[RIPPLE_MAX_SOURCES];   // xz, 半径, 強さ
groupshared uint gs_SourceCount;

uint RippleHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float RippleRand(uint x)
{
    return (float) (RippleHash(x) & 0xFFFFFFu) / 16777216.0;
}

// なめらかな山：中心で 1、r で 0
float Bump(float d, float r)
{
    float t = saturate(1.0 - (d * d) / max(r * r, 1e-6));
    return t * t;
}

// 水量を変えない押し：中心を 0.75 押し下げ、その周りを 2r まで持ち上げる
//（山の体積は pi r^2 / 3 なので、2 倍サイズの山の 4 分の 1 で打ち消せる）。
// 持ち上がった縁は輪になって外へ走り、穴は横から埋まる。単純なくぼみだと
// 池全体が沈み、それを戻すための 0 への強い引き戻しが長い波をその場に留めた：
// 広がらずにその場で上下するだけ（輪ではなく縁取りの塊）になっていた
float Splash(float d, float r)
{
    return Bump(d, r) - 0.25 * Bump(d, 2.0 * r);
}

// 隣の現在の高さ（x = 全部の波、y = 大波）。隣が乾いていれば自分の高さを返す
float2 LoadH(int2 p, float2 self)
{
    p = clamp(p, int2(0, 0), int2(RIPPLE_SIZE - 1, RIPPLE_SIZE - 1));
    bool water = g_Mask.Load(int3(p, 0)) > 0.5 && g_Ice.Load(int3(int2(uint2(p) / RIPPLE_ICE_SCALE), 0)) < RIPPLE_ICE_SOLID;
    return water ? g_In.Load(int3(p, 0)).xz : self;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    // ---- この 8x8 タイルにかかる、生きている新しい範囲 ----
    if (gi == 0)
        gs_SourceCount = 0;
    GroupMemoryBarrierWithGroupSync();
    if (g_Sources != 0u)
    {
        float2 tileMin = g_MapOrigin + float2(gid.xy * 8u) * g_Texel;
        float2 tileMax = tileMin + 8.0 * g_Texel;
        bool firstGroup = all(gid.xy == uint2(0, 0));
        for (uint k = gi; k < RIPPLE_MAX_AREAS; k += 64u)
        {
            bool alive = g_AreaStates[k] != 0u;
            RippleArea a = g_Areas[k];
            // 新しい = 前回はスロットが空だった、または時間が増えた（再利用スロットに入った新しい範囲）。
            // tickNow の「初回 tick」は使わない：それは群れの 1 ステップしか続かず、群れが 1 フレームで
            // 2 ステップ進んだとき（重いフレーム）は水が見る前に消える = 波紋の出ない爆発になる
            float seen = g_SeenIn[k];
            if (firstGroup)
                g_SeenOut[k] = alive ? max(a.timeLeft, 1e-4) : 0.0;
            if (!alive || (seen > 0.0 && a.timeLeft <= seen + 1e-4))
                continue;
            if ((a.flags & RIPPLE_AREA_CAPSULE) != 0u || a.radius <= 0.0)
                continue;
            // 押しは半径の 1.2 倍まで届く（0.6 R の Splash は 2 x 0.6 R まで持ち上げる）
            float2 nearest = clamp(a.center.xz, tileMin, tileMax);
            if (distance(nearest, a.center.xz) > a.radius * 1.2)
                continue;
            bool blast = (a.flags & RIPPLE_AREA_SHAKE) != 0u;
            float s = blast ? g_BlastStrength : g_AreaStrength;
            uint slot;
            InterlockedAdd(gs_SourceCount, 1u, slot);
            if (slot < RIPPLE_MAX_SOURCES)
                gs_Source[slot] = float4(a.center.xz, blast ? -a.radius : a.radius, s);   // 負の半径 = 大波
        }
        // 水の中を歩く敵（WaterStirCS）。半径 r / 0.6 の範囲として入れるので、下の共通の式が
        // Splash(d, r) になる
        uint stirs = min(g_StirCount[0], RIPPLE_MAX_STIRS);
        for (uint m = gi; m < stirs; m += 64u)
        {
            float4 w = g_Stirs[m];
            float2 nearest = clamp(w.xy, tileMin, tileMax);
            if (distance(nearest, w.xy) > w.z * 2.0)
                continue;
            uint slot;
            InterlockedAdd(gs_SourceCount, 1u, slot);
            if (slot < RIPPLE_MAX_SOURCES)
                gs_Source[slot] = float4(w.xy, w.z / 0.6, w.w);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (any(id.xy >= uint2(RIPPLE_SIZE, RIPPLE_SIZE)))
        return;
    int2 p = int2(id.xy);
    if (g_Mask.Load(int3(p, 0)) < 0.5 || g_Ice.Load(int3(int2(uint2(p) / RIPPLE_ICE_SCALE), 0)) >= RIPPLE_ICE_SOLID)
    {
        g_Out[p] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // x = 全部の波、y = 大波のみ（両方に同じ式）
    float4 s = g_In.Load(int3(p, 0));
    float2 cur = s.xz;
    float2 prev = s.yw;
    float2 lap = LoadH(p + int2(1, 0), cur) + LoadH(p - int2(1, 0), cur)
               + LoadH(p + int2(0, 1), cur) + LoadH(p - int2(0, 1), cur) - 4.0 * cur;
    float2 next = cur + (cur - prev) * g_Keep + g_Courant2 * lap - cur * g_Restore;

    if (g_Sources != 0u)
    {
        float2 xz = g_MapOrigin + (float2(p) + 0.5) * g_Texel;
        // 押しはこのステップと前ステップの高さを一緒に動かす：変えるのは水面の形で、
        // 速度ではない（速度を蹴ると約 1/(1-keep) ステップの間、水が沈み続ける）
        float push = 0.0;
        float bigPush = 0.0;   // そのうち大波（負の半径）の分
        // CPU の押し（足、着地。ボスのスラムと投石は大波）
        for (uint i = 0u; i < g_ImpulseCount; ++i)
        {
            float4 im = g_Impulse[i];
            float v = im.w * Splash(distance(xz, im.xy), abs(im.z));
            push += v;
            bigPush += (im.z < 0.0) ? v : 0.0;
        }
        // 出たばかりの範囲（爆発 = 大波、薬瓶の飛沫）
        uint n = min(gs_SourceCount, RIPPLE_MAX_SOURCES);
        for (uint j = 0u; j < n; ++j)
        {
            float4 a = gs_Source[j];
            // 半径の内側 0.6 に穴、1.2 まで持ち上がった縁：波が爆発から外へ走る
            float v = a.w * Splash(distance(xz, a.xy), abs(a.z) * 0.6);
            push += v;
            bigPush += (a.z < 0.0) ? v : 0.0;
        }
        // 雨：雨セルごとに最大 1 滴、セル内のランダムな位置に
        if (g_RainProb > 0.0)
        {
            int2 cell = int2(floor(xz / g_RainCell));
            [unroll]
            for (int dz = -1; dz <= 1; ++dz)
            {
                [unroll]
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int2 c = cell + int2(dx, dz);
                    uint seed = RippleHash((uint) c.x * 73856093u ^ (uint) c.y * 19349663u ^ g_Step * 83492791u);
                    if (RippleRand(seed) >= g_RainProb)
                        continue;
                    float2 drop = (float2(c) + float2(RippleRand(seed + 1u), RippleRand(seed + 2u))) * g_RainCell;
                    push += g_RainStrength * Splash(distance(xz, drop), g_RainRadius);
                }
            }
        }
        next -= float2(push, bigPush);
        cur -= float2(push, bigPush);
    }
    g_Out[p] = float4(next.x, cur.x, next.y, cur.y);
}
