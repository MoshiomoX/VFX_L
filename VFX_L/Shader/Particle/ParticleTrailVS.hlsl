// ============================================================
// ParticleTrailVS.hlsl
// 履歴リングから直接、パーティクルごとにカメラを向いたリボンを作る。
// 頂点バッファなし：インスタンス 1 つ = パーティクル 1 つ、
// SV_VertexID が (TRAIL_POINTS + 1) 組のトライアングルストリップをたどる。
//
//   組 0            : 生きた先頭 = パーティクルの現在位置
//   組 1..N         : リングのサンプル、新しい順
//   最後の有効サンプルより後ろの組はそこに潰れる（縮退
//   三角形になり、ラスタライザが捨てる）
//
// スタイルごとに 1 回描く（テクスチャとブレンドが違う）ので、
// 他のスタイルのインスタンスはここで捨てる。
// ============================================================
#include "Common/ParticleCommon.hlsli"
#include "Common/ParticleTrail.hlsli"

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot; // スタイル番号 + 1。GPUParticle.trailStyle と同じ表し方
    uint g_Premultiply;
    float g_Time;
    float _padT;
};

StructuredBuffer<GPUParticle> particles : register(t0);
StructuredBuffer<uint> trailAlive : register(t1);
StructuredBuffer<TrailStyle> trailStyles : register(t2);
StructuredBuffer<float3> trailPoints : register(t3);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation float softEdge : TEXCOORD1;
};

// ストリップの点 k の位置と、正規化した経過 t（0 先頭 .. 1 尾）
void TrailPoint(GPUParticle p, uint particleIndex, uint head, uint count,
                float f, float interval, float lifetime, uint k,
                out float3 pos, out float t)
{
    // k == 0 は生きた先頭。早期 return を使わずに書く：そうしないと fxc が
    // out 引数を未初期化の可能性ありと警告する（X4000）
    pos = p.position;
    t = 0.0;

    if (k > 0u)
    {
        uint j = min(k - 1u, count - 1u); // 0 = 最新のサンプル
        uint base = particleIndex * TRAIL_POINTS;
        uint slot = (head + TRAIL_POINTS - j) % TRAIL_POINTS;
        pos = trailPoints[base + slot];
        t = saturate(((float) j + f) * interval / lifetime);

        // リングが満杯：最古のサンプルは lifetime より f * interval だけ古い。
        // 隣のサンプルへ寄せて、尾が 1 サンプルずつ跳ぶのではなく
        // なめらかに縮むようにする。
        if (count == TRAIL_POINTS && j == count - 1u)
        {
            uint slotNext = (head + TRAIL_POINTS - (j - 1u)) % TRAIL_POINTS;
            pos = lerp(pos, trailPoints[base + slotNext], f);
            t = 1.0;
        }
    }
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;
    o.position = float4(0, 0, -1, 1); // 最後までたどり着かなければ捨てられる

    uint particleIndex = trailAlive[instanceID];
    GPUParticle p = particles[particleIndex];
    if (p.isAlive < 0.5 || p.trailStyle != g_StyleSlot)
        return o;

    uint head = TrailHead(p.trailState);
    uint count = TrailCount(p.trailState);
    if (count == 0u)
        return o;

    TrailStyle s = trailStyles[p.trailStyle - 1u];
    float lifetime = max(s.lifetime, 0.01);
    float interval = TrailInterval(s.lifetime);
    float f = frac(p.age / interval); // 最新サンプルからの経過時間（interval 単位）

    uint k = vertexID / 2u;
    float sideSign = ((vertexID & 1u) == 0u) ? 1.0 : -1.0;

    float3 pos, posPrev, posNext;
    float t, tDummy;
    TrailPoint(p, particleIndex, head, count, f, interval, lifetime, k, pos, t);
    TrailPoint(p, particleIndex, head, count, f, interval, lifetime, (k > 0u) ? k - 1u : 0u, posPrev, tDummy);
    TrailPoint(p, particleIndex, head, count, f, interval, lifetime, min(k + 1u, TRAIL_POINTS), posNext, tDummy);

    // 進行方向（尾 -> 先頭）。サンプルを記録した直後の先頭では両隣が
    // 重なるので、速度で代用する
    float3 tangent = posPrev - posNext;
    if (dot(tangent, tangent) < 1e-10)
        tangent = p.velocity;

    float3 toCam = g_CameraPosition - pos;
    float3 side = cross(tangent, toCam);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-12) ? side * rsqrt(sideLenSq) : float3(0, 0, 0);

    float width = lerp(s.widthHead, s.widthTail, t);
    if ((s.flags & TRAIL_FLAG_INHERIT_SIZE) != 0u)
        width *= p.size;

    float3 worldPos = pos + side * (sideSign * width * 0.5);

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    float4 c = lerp(s.colorHead, s.colorTail, t);
    if ((s.flags & TRAIL_FLAG_INHERIT_COLOR) != 0u)
        c *= p.color;
    c.rgb *= s.intensity;
    o.color = c;

    o.uv = float2(t * s.uvRepeat + g_Time * s.uvScroll, (sideSign > 0.0) ? 0.0 : 1.0);
    o.softEdge = s.softEdge;
    return o;
}
