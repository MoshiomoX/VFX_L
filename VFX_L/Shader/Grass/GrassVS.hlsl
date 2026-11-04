// ============================================================
// GrassVS.hlsl
// インスタンス 1 つが草の葉 1 枚（GrassCullCS の append バッファを
// DrawInstancedIndirect で描く）。SV_VertexID から 21 頂点、頂点バッファ
// なし（2026-10-23。以前は 9 頂点 = 四角形 1 枚 + 先端の三角形）。
//
// GrassCullCS が葉を 1 回だけアニメさせ、中心線を 3 次ベジェ
// （root, c1, c2, tip）で保存する。t = 0, 1/4, 2/4, 3/4 の行で四角形を
// 3 枚作り、先端を三角形で閉じる。幅は先端へ向かって細くなり、
// 幅の軸は葉のねじれと一緒に回るので、竜巻に巻かれた葉は
// ねじれたリボンに見える。
// ============================================================
#include "GrassCommon.hlsli"

StructuredBuffer<GrassBlade> g_Blades : register(t0);
Texture2D<float4> g_Ground : register(t1);
SamplerState s_Linear : register(s0);

struct GrassOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 color : TEXCOORD2;  // 葉の下の地面の色 × 葉の陰影
    float t : TEXCOORD3;       // 0 根元 .. 1 先端
    float glow : TEXCOORD4;    // 燃えさしの光 0..1（延焼）
    float sheen : TEXCOORD5;   // 0..1 明るい葉裏が見える量
    float dry : TEXCOORD6;     // 0..1 枯れた黄色い葉
};

GrassOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    GrassBlade b = g_Blades[iid];

    // 葉のどの点か：行 0..3 の間に四角形 3 枚（各 6 頂点）、その後に先端の三角形。
    // across: -1 / +1 = 両側の縁、0 = 先端
    float t;
    float across;
    if (vid < 18u)
    {
        uint q = vid / 6u;
        uint c = vid % 6u;
        uint row = q + ((c == 2u || c == 3u || c == 5u) ? 1u : 0u);
        t = (float) row * 0.25;
        across = (c == 1u || c == 4u || c == 5u) ? 1.0 : -1.0;
    }
    else
    {
        uint c = vid - 18u;
        t = (c == 2u) ? 1.0 : 0.75;
        across = (c == 0u) ? -1.0 : ((c == 1u) ? 1.0 : 0.0);
    }

    float u = 1.0 - t;
    float3 spine = b.c1 * (3.0 * u * u * t) + b.c2 * (3.0 * u * t * t) + b.tip * (t * t * t);
    // 幅の軸：根元の軸を、そこまでのねじれの分だけ鉛直軸まわりに回したもの
    float a = b.twist * t;
    float2 w2 = b.side * cos(a) + float2(-b.side.y, b.side.x) * sin(a);
    float halfW = b.width * 0.5 * (1.0 - 0.8 * pow(t, 1.5));
    float3 p = b.pos + spine + float3(w2.x, 0.0, w2.y) * (across * halfW);

    GrassOut o;
    o.world = p;
    o.pos = mul(float4(p, 1.0), g_ViewProj);
    // ほぼ真上向き（草原が下の地面と同じように陰影が付く）、葉の面の向きへ少し傾ける
    float2 face = float2(-w2.y, w2.x);
    o.normal = normalize(float3(0.0, 1.0, 0.0) + float3(face.x, 0.0, face.y) * 0.25);
    // 焦げ：地面の色を煤の色へ暗くする（色相は保つ）
    float2 uv = (b.pos.xz - g_MapOrigin) / g_MapSize;
    o.color = g_Ground.SampleLevel(s_Linear, uv, 0).rgb * b.shade * lerp(1.0, 0.06, pow(b.burn, 0.6));
    o.t = t;
    o.glow = b.glow;
    o.sheen = b.sheen;
    o.dry = b.dry;
    return o;
}
