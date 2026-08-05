// ============================================================
// ProjectileVS.hlsl
// 投射物の芯をビルボードで描く（インスタンシング）。
// 頂点バッファを使わず、SV_VertexID(0~5) と SV_InstanceID で quad を展開する。
// ※パーティクルとは別経路。CPU が毎フレーム投射物配列をアップロードする。
// ============================================================

cbuffer ProjectileRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    float _pad0;
};

// C++ 側の ProjectileInstance と一致させる
struct ProjectileInstance
{
    float3 position;
    float size;
    float4 color;
    float3 velocity;
    float stretch;
};

StructuredBuffer<ProjectileInstance> instances : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output = (VSOutput) 0;

    ProjectileInstance p = instances[instanceID];

    static const float2 corners[6] =
    {
        float2(-1, 1), float2(1, 1), float2(-1, -1),
        float2(-1, -1), float2(1, 1), float2(1, -1),
    };
    static const float2 uvs[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };

    float2 corner = corners[vertexID];
    output.uv = uvs[vertexID];

    // カメラの右・上ベクトル（ビュー行列から取り出す）
    float3 camRight = float3(g_View[0][0], g_View[1][0], g_View[2][0]);
    float3 camUp = float3(g_View[0][1], g_View[1][1], g_View[2][1]);

    float halfSize = p.size * 0.5;
    float2 local = corner * halfSize;

    // --- 速度方向への引き伸ばし（尾を引く見た目、追加の描画コストゼロ）---
    if (p.stretch > 0.001)
    {
        // 速度をカメラ平面へ射影して画面上の進行方向を得る
        float2 dir2 = float2(dot(p.velocity, camRight), dot(p.velocity, camUp));
        float len = length(dir2);
        if (len > 0.0001)
        {
            dir2 /= len;
            float2 perp = float2(-dir2.y, dir2.x);
            local = perp * (corner.x * halfSize)
                  + dir2 * (corner.y * halfSize * (1.0 + p.stretch));
        }
    }

    float3 worldPos = p.position + camRight * local.x + camUp * local.y;

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    output.position = mul(viewPos, g_Projection);
    output.color = p.color;

    return output;
}