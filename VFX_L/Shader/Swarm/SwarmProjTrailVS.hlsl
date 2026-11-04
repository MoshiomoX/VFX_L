// ============================================================
// SwarmProjTrailVS.hlsl
// GPU の弾ごとにカメラを向いたリボンを、スロットの履歴リング
// （SwarmProjTrailCS）から直接作る。形は ParticleTrailVS と
// 同じ：インスタンス 1 つ = 弾のスロット 1 つ、SV_VertexID が
// (TRAIL_POINTS + 1) 組のトライアングルストリップをたどる。
//
//   組 0      : 生きた先頭 = 弾の現在位置
//   組 1..N   : リングのサンプル、新しい順
//
// スタイルごとに 1 回描く（テクスチャ / ブレンドが違う）。他のスタイルと死んだ
// スロットはクリップされる 1 点に潰す。ParticleTrailPS と組で使う。
// ============================================================
#include "../Particle/Common/ParticleTrail.hlsli"
#include "../Common/SwarmProjTrail.hlsli"

// SwarmProjectile（SwarmCommon.hlsli、48 バイト）。SwarmCommon の
// フレーム cbuffer に b0 を取られないよう、ここで宣言する
struct ProjLite
{
    float3 position;
    float damage;
    float3 velocity;
    float lifetime;
    float radius;
    uint vfxType;
    uint motion;
    float pathT;
};

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot; // スタイル番号 + 1（recipeTrail と同じ表し方）
    uint g_Premultiply;
    float g_Time;
    float _padT;
};

StructuredBuffer<ProjLite> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
StructuredBuffer<uint> recipeTrail : register(t2);
StructuredBuffer<TrailStyle> trailStyles : register(t3);
StructuredBuffer<float3> trailPoints : register(t4);
StructuredBuffer<uint> trailState : register(t5);
StructuredBuffer<float> trailAge : register(t6);
// 出口魔法のリボンは運んでいる魔法の色になる（2026-10-31、SwarmEmitCS 参照）
Buffer<uint> projTags : register(t7);
cbuffer SwarmCarryCB : register(b2)
{
    float4 g_CarryColor[32];
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float softEdge : TEXCOORD1;
};

// ストリップの点 k の位置と、正規化した経過 t（0 先頭 .. 1 尾）
void TrailPointAt(float3 headPos, uint slot, uint head, uint count,
                  float f, float interval, float lifetime, uint k,
                  out float3 pos, out float t)
{
    pos = headPos;
    t = 0.0;

    if (k > 0u)
    {
        uint j = min(k - 1u, count - 1u); // 0 = 最新のサンプル
        uint base = slot * TRAIL_POINTS;
        uint ring = (head + TRAIL_POINTS - j) % TRAIL_POINTS;
        pos = trailPoints[base + ring];
        t = saturate(((float) j + f) * interval / lifetime);

        // リングが満杯：最古のサンプルを隣へ寄せ、尾が 1 サンプルずつ
        // 跳ぶのではなくなめらかに縮むようにする
        if (count == TRAIL_POINTS && j == count - 1u)
        {
            uint ringNext = (head + TRAIL_POINTS - (j - 1u)) % TRAIL_POINTS;
            pos = lerp(pos, trailPoints[base + ringNext], f);
            t = 1.0;
        }
    }
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;
    o.position = float4(0, 0, -1, 1); // 最後までたどり着かなければ捨てられる

    uint slot = instanceID;
    uint state = trailState[slot];
    bool draining = (state & PROJ_TRAIL_DRAINING) != 0u;
    // 死んだスロットも、リボンが着弾点へ吸い込まれている間は描く
    if (projStates[slot] == 0u && !draining) // SWARM_DEAD
        return o;
    ProjLite p = projectiles[slot];
    if (recipeTrail[p.vfxType] != g_StyleSlot)
        return o;

    uint head = TrailHead(state);
    uint count = TrailCount(state);
    if (count == 0u)
        return o;
    // 吸い込み中：沈むにつれて細くする（吸い込みの間に 1 -> 0.4）
    float drainWidth = draining ? lerp(1.0, 0.4, (float) (state >> PROJ_TRAIL_DRAIN_SHIFT) / (float) TRAIL_POINTS) : 1.0;

    TrailStyle s = trailStyles[g_StyleSlot - 1u];
    float lifetime = max(s.lifetime, 0.01);
    float interval = TrailInterval(s.lifetime);
    float f = frac(trailAge[slot] / interval); // 最新サンプルからの経過時間（interval 単位）

    uint k = vertexID / 2u;
    float sideSign = ((vertexID & 1u) == 0u) ? 1.0 : -1.0;

    float3 pos, posPrev, posNext;
    float t, tDummy;
    TrailPointAt(p.position, slot, head, count, f, interval, lifetime, k, pos, t);
    TrailPointAt(p.position, slot, head, count, f, interval, lifetime, (k > 0u) ? k - 1u : 0u, posPrev, tDummy);
    TrailPointAt(p.position, slot, head, count, f, interval, lifetime, min(k + 1u, TRAIL_POINTS), posNext, tDummy);

    // 進行方向（尾 -> 先頭）。サンプルを記録した直後は先頭で両隣が
    // 重なるので、速度で代用する
    float3 tangent = posPrev - posNext;
    if (dot(tangent, tangent) < 1e-10)
        tangent = p.velocity;

    float3 toCam = g_CameraPosition - pos;
    float3 side = cross(tangent, toCam);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-12) ? side * rsqrt(sideLenSq) : float3(0, 0, 0);

    float width = lerp(s.widthHead, s.widthTail, t) * drainWidth;
    float3 worldPos = pos + side * (sideSign * width * 0.5);

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    float4 c = lerp(s.colorHead, s.colorTail, t);
    uint carryTag = projTags[slot] & 0x7FFFFFFFu;   // 最上位ビット = 吸血の印
    if (carryTag != 0u)
    {
        float4 carry = g_CarryColor[firstbitlow(carryTag)];
        c.rgb = lerp(c.rgb, carry.rgb * max(max(c.r, c.g), c.b), carry.a);
    }
    c.rgb *= s.intensity;
    o.color = c;

    o.uv = float2(t * s.uvRepeat + g_Time * s.uvScroll, (sideSign > 0.0) ? 0.0 : 1.0);
    o.softEdge = s.softEdge;
    return o;
}
