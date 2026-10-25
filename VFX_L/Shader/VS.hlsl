// ============================================================
// VS.hlsl - the standard model VS (ModelVS in ModelCommon.hlsli).
// PBR_VS.hlsl is the same code under another name for PBR_PS
// ============================================================
#include "Common/ModelCommon.hlsli"

VS_OUTPUT main(VS_INPUT input)
{
    return ModelVS(input);
}