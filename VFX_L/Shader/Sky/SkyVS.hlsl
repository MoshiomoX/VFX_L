// ============================================================
// SkyVS.hlsl
// SV_VertexID から全画面三角形を作る（頂点バッファなし）。NDC 座標を
// 渡し、SkyPS がピクセルごとに視線を組み立て直せるようにする。
// ============================================================
struct SkyOut
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

SkyOut main(uint vid : SV_VertexID)
{
    SkyOut o;
    // uv で 0:(0,0) 1:(2,0) 2:(0,2) -> 三角形 1 枚で画面を覆う
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.position = float4(o.ndc, 0.0, 1.0);
    return o;
}
