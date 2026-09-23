// ============================================================
// VFXMeshVS.hlsl - VFX mesh. Same as the model VS; kept separate
// so vertex animation can be added here later without touching
// lit models.
// ============================================================
#include "../Common/ModelCommon.hlsli"

VS_OUTPUT main(VS_INPUT input)
{
    return ModelVS(input);
}