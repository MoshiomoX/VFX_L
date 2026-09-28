// ============================================================
// SkyVS.hlsl
// Full-screen triangle from SV_VertexID (no vertex buffer). Passes the
// NDC position on so SkyPS can rebuild the view ray per pixel.
// ============================================================
struct SkyOut
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

SkyOut main(uint vid : SV_VertexID)
{
    SkyOut o;
    // 0:(0,0) 1:(2,0) 2:(0,2) in uv -> covers the screen with one triangle
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.position = float4(o.ndc, 0.0, 1.0);
    return o;
}
