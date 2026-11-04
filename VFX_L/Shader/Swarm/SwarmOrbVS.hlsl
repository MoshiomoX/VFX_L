// ============================================================
// SwarmOrbVS.hlsl
// インスタンス描画する経験玉の宝石（PrimitiveBuilder::CreateBipyramid）。
// 全スロットを描き、死んだものは潰す。
//
// 待機   : 上下に浮き沈みし、長軸（Y）まわりに回り、脈打つ
//          （大きさの脈動）。スロットごとに位相が違うので、
//          積もった経験玉が揃って動くことはない。
// 吸引中 : 長軸をプレイヤーへ傾け、引かれる向きに伸び（体積は
//          保つ）、浮き沈みをやめ、吸引中の色に変わって
//          明るくなる。度合い = 吸引速度 / g_FullPullSpeed。
//
// SwarmOrb.velocity は SwarmOrbMoveCS が書く水平の吸引速度
// （待機中は 0）。Color.rgb = 本体の色、Color.a = 発光の倍率。
// シェーディングは SwarmOrbPS が行う。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmOrb> orbs : register(t0);
Buffer<uint> orbStates : register(t1);

// SwarmSystem::OrbLookCB と一致させること
cbuffer SwarmOrbLookCB : register(b4)
{
    float g_LookTime;       // 秒
    float g_LookScale;      // メッシュの拡大率
    float g_BobHeight;      // m
    float g_BobSpeed;       // rad/s

    float g_SpinSpeed;      // rad/s
    float g_PulseAmount;    // 大きさ 1 +- これ
    float g_PulseSpeed;     // rad/s
    float g_FullPullSpeed;  // 吸引中の見た目が完全になる m/s

    float g_StretchPerSpeed; // 吸引 1 m/s あたりに伸びる長さ
    float g_StretchMax;      // 伸びる長さの上限
    float g_TiltMax;         // 長軸がプレイヤーへ傾くラジアン
    float g_PullGlow;        // 吸引しきった時の発光倍率

    float4 g_IdleColor;     // 線形 rgb
    float4 g_PullColor;
};

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

// ロドリゲス：v を単位軸 a まわりに回す
float3 RotateAxis(float3 v, float3 a, float s, float c)
{
    return v * c + cross(a, v) * s + a * dot(a, v) * (1.0 - c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    if (orbStates[input.InstanceID] == SWARM_DEAD)
    {
        o.Position = float4(0, 0, -1, 1);
        return o;
    }

    SwarmOrb b = orbs[input.InstanceID];

    // スロットごとの位相
    uint h = input.InstanceID * 2654435761u;
    h ^= h >> 15;
    float phase = (float) (h & 0xFFFFu) * (6.2831853 / 65535.0);

    float attract = saturate(b._pad / max(g_FullPullSpeed, 1e-3));

    // ---- 大きさ：脈動 ----
    float pulse = 1.0 + g_PulseAmount * sin(g_LookTime * g_PulseSpeed + phase * 1.7);
    float3 p = input.Position * (g_LookScale * pulse);
    float3 n = input.Normal;

    // ---- 長軸まわりの回転 ----
    float ss, sc;
    sincos(g_LookTime * g_SpinSpeed + phase, ss, sc);
    p = float3(sc * p.x + ss * p.z, p.y, -ss * p.x + sc * p.z);
    n = float3(sc * n.x + ss * n.z, n.y, -ss * n.x + sc * n.z);

    // ---- 吸引中：プレイヤーへ傾き、引かれる向きに伸びる ----
    float2 v = b.velocity.xz;
    float vlen = length(v);
    if (vlen > 1e-3)
    {
        float3 d = float3(v.x, 0.0, v.y) / vlen;

        // axis = up x d。これで +Y が d の方へ向く
        float3 axis = float3(d.z, 0.0, -d.x);
        float ts, tc;
        sincos(g_TiltMax * attract, ts, tc);
        p = RotateAxis(p, axis, ts, tc);
        n = RotateAxis(n, axis, ts, tc);

        // d 方向に k 倍、それと直交する方向に 1/sqrt(k) 倍（体積を保つ）。
        // 法線には逆の倍率をかける
        float k = 1.0 + min(vlen * g_StretchPerSpeed, g_StretchMax);
        float kp = rsqrt(k);
        float pd = dot(p, d);
        p = d * (pd * k) + (p - d * pd) * kp;
        float nd = dot(n, d);
        n = d * (nd / k) + (n - d * nd) / kp;
    }

    // ---- 浮き沈み（待機中のみ） ----
    float bob = sin(g_LookTime * g_BobSpeed + phase) * g_BobHeight * (1.0 - attract);

    float3 worldPos = b.position + p + float3(0.0, bob, 0.0);
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normalize(n);
    o.Tangent = float3(1, 0, 0);
    o.UV = input.UV;
    o.Color = float4(lerp(g_IdleColor.rgb, g_PullColor.rgb, attract),
                     lerp(1.0, g_PullGlow, attract));
    return o;
}
