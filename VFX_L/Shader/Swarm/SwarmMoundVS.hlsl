// ============================================================
// SwarmMoundVS.hlsl
// 砂潜りの砂山（SWARM_KIND_BURROWER、2026-10-17）: 敵が地中を進む間、
// 箱型の体は地面の下に沈め（SwarmEnemyVS）、代わりにこの低い
// ドームが地表を動いていく。
// 砂潜り 1 体に 1 インスタンス（砂潜りの描画リスト、数は
// DrawIndexedInstancedIndirect）。メッシュはコードで作った単位球で、
// (半径, 高さ, 半径) x 体格に押しつぶし、足元を中心にするので、
// 下半分は地形の下に入り、ドームだけが見える。
// 敵が地上に出るのに合わせて（fuse）盛り上がる / 平らになり、移動中は上下に揺れる。
// 敵のピクセルシェーダーと既定の白テクスチャで描くので、
// 色は頂点カラー x g_ShieldTint になる。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> burrowerList : register(t2);
// （t4 の enemySlow はここで宣言していたが読んでいなかった: コンパイラが削り、C++ のバインドが毎フレーム警告していた。2026-10-20 に削除）
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);

// C++ 側: SwarmSystem::PropMeshCB（SwarmShieldVS と同じブロック）
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor; // 使わない
    float g_MeshScale;   // 体格 1 のときの砂山の半径（m）
    float3 g_HoldOffset; // y = 体格 1 のときの砂山の高さ（m）
    float g_MeshYaw;     // 使わない
    float4 g_ShieldTint; // 頂点カラーに掛ける
    float g_BobAmount;   // 移動中の上下の揺れ（m）
    float g_PropAlpha;   // 使わない
    float2 _propPad;
};

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    uint slot = burrowerList[input.InstanceID];
    SwarmEnemyExtra ex = enemyExtra[slot];
    // 体がどれだけ地面の下にあるか: 1 = 完全に埋まっている（砂山が最大）、0 = 出ている（砂山なし）
    float down = (ex.fuse > 0.0) ? 1.0 - saturate((g_BurrowSurfaceTime - ex.fuse) / SWARM_BURROW_RISE_TIME)
                                 : saturate(-ex.fuse / SWARM_BURROW_RISE_TIME);
    if (enemyStates[slot] == SWARM_DEAD || ex.kind != SWARM_KIND_BURROWER || down < 0.02)
    {
        o.Position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float k = SwarmKindScale(ex.kind);
    float r = g_MeshScale * k;
    float h = g_HoldOffset.y * k * down;
    bool moving = dot(e.velocity.xz, e.velocity.xz) > 0.01;
    float bob = moving ? abs(sin(e.animTime * 7.0 + (float) slot * 0.37)) * g_BobAmount : 0.0;

    float3 local = input.Position * float3(r, h, r);
    // 押しつぶした球の法線: 逆数で拡大縮小する
    float3 normal = normalize(input.Normal * float3(1.0 / max(r, 1e-3), 1.0 / max(h, 1e-3), 1.0 / max(r, 1e-3)));

    float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
    float3 worldPos = e.position + float3(0.0, footY + bob - 0.05, 0.0) + local;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normal;
    o.Tangent = float3(1, 0, 0);
    o.UV = input.UV;
    o.Color = input.Color * g_ShieldTint;
    return o;
}
