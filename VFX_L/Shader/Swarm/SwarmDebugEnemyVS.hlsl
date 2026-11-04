// ============================================================
// SwarmDebugEnemyVS.hlsl
// GPU の敵のデバッグ用ワイヤーフレーム：インスタンスごとに線分 64 本
// （128 頂点）で組んだ縦のカプセル。
//   [  0, 64)  上の輪 + 下の輪、各 16 分割
//   [ 64, 72)  縦線 4 本
//   [ 72,120)  半円の弧 4 本（上 / 下 x 2 面）、各 6 分割
//   [120,128)  縮退した埋め草
//
// 半径と線分の半分の長さは SwarmAICB から取るので、枠は常に
// HitCS / ContactCS が実際に判定しているものと一致する。
// ============================================================

#define SWARM_FRAME_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmDebugCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

static const float PI = 3.14159265;
static const float TWO_PI = 6.28318531;
static const uint RING_SEGS = 16;
static const uint ARC_SEGS = 6;

// 半径 r、直線部が y 軸の -h から +h までのカプセル上の、
// 頂点 vid のローカル空間でのオフセット
float3 CapsulePoint(uint vid, float r, float h)
{
    // ---- 輪 ----
    if (vid < 64)
    {
        uint ring = vid / 32; // 0 = 上、1 = 下
        uint k = vid % 32;
        uint seg = k / 2;
        uint endp = k & 1;
        float a = (float) (seg + endp) * TWO_PI / (float) RING_SEGS;
        float y = (ring == 0) ? h : -h;
        return float3(cos(a) * r, y, sin(a) * r);
    }

    // ---- 縦線 ----
    if (vid < 72)
    {
        uint k = vid - 64;
        uint idx = k / 2; // 0..3
        uint endp = k & 1;
        float a = (float) idx * PI * 0.5;
        float y = (endp == 0) ? h : -h;
        return float3(cos(a) * r, y, sin(a) * r);
    }

    // ---- 端の弧 ----
    if (vid < 120)
    {
        uint k = vid - 72; // 0..47
        uint arc = k / 12; // 0 topXY, 1 topZY, 2 botXY, 3 botZY
        uint m = k % 12;
        uint seg = m / 2;
        uint endp = m & 1;
        float t = (float) (seg + endp) * PI / (float) ARC_SEGS; // 0..PI
        float c = cos(t);
        float s = sin(t);

        bool top = (arc < 2);
        bool planeXY = ((arc & 1) == 0);

        float y = top ? (h + s * r) : (-h - s * r);
        return planeXY ? float3(c * r, y, 0.0) : float3(0.0, y, c * r);
    }

    // ---- 埋め草：1 点に潰す ----
    return float3(0.0, h, 0.0);
}

VSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;

    if (enemyStates[iid] == SWARM_DEAD)
    {
        o.position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[iid];

    float3 worldPos = e.position
                    + CapsulePoint(vid, g_EnemyRadius, g_EnemyCapsuleHalf);

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    o.position = mul(viewPos, g_Projection);

    o.color = float4(0.4, 0.6, 1.0, 1.0);
    return o;
}