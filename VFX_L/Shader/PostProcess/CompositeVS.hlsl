// ============================================================
// CompositeVS.hlsl
// Full-screen triangle from SV_VertexID. No vertex buffer.
// ============================================================
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vid : SV_VertexID)
{
    VSOutput o;
    // 0:(0,0) 1:(2,0) 2:(0,2) in uv -> covers the screen with one triangle
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}