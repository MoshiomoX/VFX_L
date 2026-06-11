struct PSIn
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 L = normalize(float3(0.5, 1.0, -0.5)); // 固定ライト
    float ndl = saturate(dot(N, L)) * 0.8 + 0.2; // 簡易Lambert + ambient
    float3 albedo = float3(0.80, 0.80, 0.85); // フラットグレー（形が見えればOK）
    return float4(albedo * ndl, 1.0);
}