// ============================================================
// PBR_VS.hlsl - same as VS.hlsl; kept as a separate name so
// Material can still pair it with PBR_PS
// ============================================================
#include "Common/ModelCommon.hlsli"

VS_OUTPUT main(VS_INPUT input)
{
    return ModelVS(input);
}