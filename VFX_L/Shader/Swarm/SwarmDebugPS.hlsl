// ============================================================
// SwarmDebugPS.hlsl
// Pass-through color for debug lines.
// ============================================================
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PSInput i) : SV_TARGET
{
    return i.color;
}