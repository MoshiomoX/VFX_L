// ============================================================
// VS.hlsl - 標準のモデルの VS（ModelCommon.hlsli の ModelVS）。
// PBR_VS.hlsl は PBR_PS 用に名前だけ変えた同じコード
// ============================================================
#include "Common/ModelCommon.hlsli"

VS_OUTPUT main(VS_INPUT input)
{
    return ModelVS(input);
}