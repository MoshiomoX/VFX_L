// ============================================================
// WindGridDiffuseCS.hlsl
// 風の格子（World/WindGrid）の拡散 1 回：上下左右前後 6 マスの平均へ g_Diffuse だけ寄せる（= ぼかす）。
// 既定 3 回（WindGrid::Settings::diffuseIters）。格子の外は自分の値（端で風が消えない）
// ============================================================

cbuffer WindDiffuseCB : register(b0)
{
    int3 g_Dims;
    float g_Diffuse;   // 0〜0.5（1 回で 6 マスの平均へ寄せる割合）
};

Texture3D<float4> g_In : register(t0);
RWTexture3D<float4> g_Out : register(u0);

float3 At(int3 c, float3 self)
{
    float3 v = self;
    if (all(c >= 0) && all(c < g_Dims))
        v = g_In.Load(int4(c, 0)).xyz;
    return v;
}

[numthreads(8, 4, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= (uint3)g_Dims))
        return;
    int3 c = (int3)id;
    float3 self = g_In.Load(int4(c, 0)).xyz;
    float3 sum = At(c + int3(1, 0, 0), self) + At(c - int3(1, 0, 0), self)
               + At(c + int3(0, 1, 0), self) + At(c - int3(0, 1, 0), self)
               + At(c + int3(0, 0, 1), self) + At(c - int3(0, 0, 1), self);
    g_Out[id] = float4(lerp(self, sum / 6.0, g_Diffuse), 0.0);
}
