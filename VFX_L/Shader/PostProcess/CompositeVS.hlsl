// ============================================================
// CompositeVS.hlsl
// SV_VertexID から作る全画面三角形。頂点バッファなし。
// ============================================================
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vid : SV_VertexID)
{
    VSOutput o;
    // uv で 0:(0,0) 1:(2,0) 2:(0,2) -> 三角形 1 枚で画面を覆う
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}