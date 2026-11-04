// ============================================================
// PBR_VS.hlsl - 中身は VS.hlsl と同じ。Material が PBR_PS と
// 組にできるよう、別名のまま残している
// ============================================================
#include "Common/ModelCommon.hlsli"

VS_OUTPUT main(VS_INPUT input)
{
    return ModelVS(input);
}