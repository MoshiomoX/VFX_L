// ============================================================
// SwarmDebugProjVS.hlsl
// GPU の弾の芯を表示するデバッグ用のワイヤーフレーム。
//
// リードバックなし: VS が弾のバッファを直接読み、生きている
// スロットを 2 つの輪（XZ + XY）のラインリストに展開する。
// 死んだスロットはニアプレーンの後ろへ潰す。
//
// インスタンス 1 つに 64 頂点 = 輪 2 つ x 16 区間 x 端点 2 つ。
// ============================================================

// SwarmCommon は既定で b0 を使うので退ける（ここでは使わない）
#define SWARM_FRAME_CB_REG b1
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmDebugCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
};

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

static const uint SEGS = 16;
static const float TWO_PI = 6.28318530718;

VSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;

    if (projStates[iid] == SWARM_DEAD)
    {
        o.position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmProjectile p = projectiles[iid];

    // どの輪の、どの区間の、どの端点か
    uint ring = vid / (SEGS * 2); // 0 = XZ, 1 = XY
    uint seg = (vid % (SEGS * 2)) / 2;
    uint endp = vid & 1;
    float a = (float) (seg + endp) * TWO_PI / (float) SEGS;

    float3 offset;
    if (ring == 0)
        offset = float3(cos(a), 0.0, sin(a)) * p.radius;
    else
        offset = float3(cos(a), sin(a), 0.0) * p.radius;

    float3 worldPos = p.position + offset;

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    // 普段は緑、寿命の最後の 0.3s で赤になる
    o.color = (p.lifetime < 0.3) ? float4(1.0, 0.3, 0.3, 1.0)
                                 : float4(0.4, 1.0, 0.4, 1.0);
    return o;
}