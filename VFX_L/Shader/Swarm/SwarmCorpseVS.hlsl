// ============================================================
// SwarmCorpseVS.hlsl
// 死亡時の砕け散り（2026-10-02）：死んだ敵の剛体パーツ（Kenney Blocky：
// 頭 / 胴 / 腕 / 脚、それぞれ 1 サブメッシュ）が飛び散り、回転し、地面で
// 1 回跳ねて、g_CorpseLife 秒かけて縮んで消える。
// 描き方は SwarmEnemyVS と同じ（同じメッシュ、同じ PS、1 回の描画で 1 サブメッシュ、
// 見た目ごとの死体リストを DrawIndexedInstancedIndirect で）だが、各
// パーツは (seed, part, age) の閉じた式による自分の軌道で動く：
//
//   発射   : プレイヤーから離れる向き（corpse.dir）x g_Fling、パーツ自身の側に
//            沿って体から外へ、ランダムな横向きの押し、そして
//            上向き x g_Up。エリート / ボスは少し強く飛ばす
//   飛行   : パーツの支点が静止時の高さに達するまで g_Gravity で放物線、
//            その後 1 回跳ねる（着地速度の g_Bounce 倍、
//            横の速度は半分）、その後はそこに横たわる
//   回転   : ランダムな軸のまわり、着地後は 3 分の 1 に遅くなる
//   終わり : g_FlashTime の間は白熱の閃光、g_ShrinkStart x life から
//            最後まで縮む
//
// パーツの支点（g_PartPivot）は焼き込んだポーズでのパーツのノード原点
//（関節：首、肩、腰）なので、パーツは付いていた所を中心に
// 転がる。
//
// b0 = MVPBuffer（World は未使用）、b2 = AICB（体の大きさ）、b4 = CorpseCB、
// b5 = BomberCB（エリート / ボスの倍率、幽霊の見た目）。C++: SwarmSystem::CorpseCB
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmCorpse> corpses : register(t0);
StructuredBuffer<uint> corpseList : register(t2);   // この見た目のリングのインデックス（SwarmCorpseListCS）

static const uint kMaxCorpseParts = 8u;

cbuffer SwarmCorpseCB : register(b4)
{
    uint g_CorpsePart;        // 描いているサブメッシュ
    uint g_CorpsePartCount;
    float g_CorpseTime;       // SwarmSystem の時計
    float g_CorpseLife;       // 死体を描く秒数

    float g_CorpseGravity;    // m/s^2
    float g_CorpseFling;      // プレイヤーから離れる向きの m/s
    float g_CorpseUp;         // 上向きの m/s
    float g_CorpseSpin;       // rad/s

    float g_CorpseBounce;     // 跳ね返りで残す着地速度の割合
    float g_CorpseFlash;      // 死んだ瞬間の頂点色の倍率（HDR、ブルーム）
    float g_CorpseFlashTime;  // 閃光が消えるまでの秒数
    float g_CorpseShrinkStart; // 縮み始める寿命の割合

    float4 g_PartPivot[kMaxCorpseParts]; // xyz = 焼き込んだメッシュ空間での支点
};

// エリート / ボス / 幽霊の色、SwarmEnemyVS と同じ
static const float3 kEliteTint = float3(1.4, 0.55, 0.5);
static const float3 kGhostTint = float3(0.55, 1.25, 1.6);

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

uint CorpseHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float CorpseRand(uint seed, uint k)
{
    return (float) (CorpseHash(seed ^ (k * 0x85EBCA6Bu)) & 0xFFFFFFu) / 16777216.0;
}

// yaw = atan2(v.x, v.z)：前方 (0,0,1) -> (sin, 0, cos)
float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

// ロドリゲス：単位軸 k のまわりに v を角度 a だけ回す
float3 RotateAxis(float3 v, float3 k, float a)
{
    float s, c;
    sincos(a, s, c);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    o.Position = float4(0.0, 0.0, -1.0, 1.0);   // 砕け散り中でなければクリップされる

    SwarmCorpse cp = corpses[corpseList[input.InstanceID]];
    float age = g_CorpseTime - cp.birth;
    if (cp.birth <= 0.0 || age < 0.0 || age >= g_CorpseLife)
        return o;

    float ys, yc;
    sincos(cp.yaw, ys, yc);
    float kindScale = SwarmKindScale(cp.kind);
    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);

    // ---- 焼き込んだメッシュ空間のパーツ、エリート / ボスは足元を基点に拡大 ----
    float3 local = input.Position;
    float3 pivot = g_PartPivot[min(g_CorpsePart, kMaxCorpseParts - 1u)].xyz;
    local.xz *= kindScale;
    local.y = footY + (local.y - footY) * kindScale;
    pivot.xz *= kindScale;
    pivot.y = footY + (pivot.y - footY) * kindScale;

    // ---- 発射（ワールド） ----
    uint seed = CorpseHash(cp.seed ^ (g_CorpsePart * 0x9E3779B9u));
    float r0 = CorpseRand(seed, 0u), r1 = CorpseRand(seed, 1u), r2 = CorpseRand(seed, 2u);
    float r3 = CorpseRand(seed, 3u), r4 = CorpseRand(seed, 4u), r5 = CorpseRand(seed, 5u);
    float3 away = float3(cp.dir.x, 0.0, cp.dir.y);
    float3 side = float3(-cp.dir.y, 0.0, cp.dir.x);
    float3 pivotW = RotateY(pivot, ys, yc);
    float outLenSq = dot(pivotW.xz, pivotW.xz);
    float3 outward = (outLenSq > 1e-4) ? float3(pivotW.x, 0.0, pivotW.z) * rsqrt(outLenSq) : float3(0.0, 0.0, 0.0);
    float throwMul = lerp(1.0, kindScale, 0.3);
    float3 vh = (away * (g_CorpseFling * (0.6 + 0.8 * r0))
               + outward * (1.0 + 1.5 * r1)
               + side * ((r2 - 0.5) * 2.0)) * throwMul;
    float vy = g_CorpseUp * (0.7 + 0.6 * r3) * throwMul;

    // ---- 飛行：支点が静止時の高さに戻るまで放物線、1 回跳ねる ----
    float g = max(g_CorpseGravity, 0.1);
    float restH = 0.12 * kindScale;                   // 地面に横たわるパーツ：支点はこの高さ
    float h0 = max((pivot.y - footY) - restH, 0.0);   // 死んだ瞬間の、それより上の支点の高さ
    float t1 = (vy + sqrt(vy * vy + 2.0 * g * h0)) / g;   // 最初の着地
    float dy, hx, spinT;
    if (age < t1)
    {
        dy = vy * age - 0.5 * g * age * age;
        hx = age;
        spinT = age;
    }
    else
    {
        float vb = (g * t1 - vy) * g_CorpseBounce;    // 着地速度 x 跳ね返り、ここから上向き
        float tb = age - t1;
        float t2 = 2.0 * vb / g;                       // 跳ねたときの滞空時間
        float tt = min(tb, t2);
        dy = -h0 + vb * tt - 0.5 * g * tt * tt;
        hx = t1 + 0.5 * tt;                            // 跳ねている間は横の速度が半分、その後は横たわる
        spinT = t1 + (age - t1) * 0.3;
    }
    float3 offset = vh * hx + float3(0.0, dy, 0.0);

    // ---- パーツの支点を中心に、ランダムな軸で回転 ----
    float3 axis = normalize(float3(r4 - 0.5, 0.35, r5 - 0.5) + float3(1e-3, 0.0, 0.0));
    float angle = g_CorpseSpin * (0.5 + r0) * spinT * ((r1 < 0.5) ? -1.0 : 1.0);
    float shrink = 1.0 - smoothstep(g_CorpseShrinkStart * g_CorpseLife, g_CorpseLife, age);

    float3 rel = RotateAxis(local - pivot, axis, angle) * shrink;
    float3 worldPos = RotateY(pivot + rel, ys, yc) + cp.position + offset;
    float3 normal = RotateY(RotateAxis(input.Normal, axis, angle), ys, yc);
    float3 tangent = RotateY(RotateAxis(input.Tangent, axis, angle), ys, yc);

    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normalize(normal);
    o.Tangent = tangent;
    o.UV = input.UV;
    o.Color = input.Color;
    if (cp.kind == SWARM_KIND_ELITE)
        o.Color.rgb *= kEliteTint;
    else if (cp.kind == SWARM_KIND_BOSS)
        o.Color.rgb *= g_BossTint;
    else if (cp.kind == SWARM_KIND_GHOST)
    {
        o.Color.rgb *= kGhostTint * g_GhostGlow;
        o.Color.a = g_GhostAlpha * shrink;
    }
    // 死んだ瞬間：白熱して、元の色に戻っていく（PS はアルベドに Color を掛ける。> 1 = ブルーム）
    o.Color.rgb *= lerp(g_CorpseFlash, 1.0, saturate(age / max(g_CorpseFlashTime, 1e-3)));
    return o;
}
