// ============================================================
// SwarmDropRingVS.hlsl
// 落下中のメテオ（DROP の弾道）がすべて落ちる地面に出す予告の輪。
// 見た目は自爆兵の輪と同じ：縁はメテオが残す範囲（その弾道の
// hitArea）の半径を示し、落ちるにつれて（pathT）中心から円が広がり、
// 着弾の瞬間に縁に届く。
// ピクセルシェーダーは SwarmBomberRingPS（b0 の並びが同じ）。違うのは
// 色だけ（SwarmSystem::dropRing）。
//
// 弾のスロット 1 つにつきインスタンス 1 つ（DrawInstanced、一覧なし）：死んだ
// スロットや DROP でないスロットはニアプレーンの手前に潰す。地面の四角形の
// 6 頂点を SV_VertexID から作る。頂点バッファなし。
//
// 着弾点 p3 は生成時の敵の位置（体の中心）で、地形から g_CenterAboveFeet 上にある
// （SwarmEnemyMoveCS 参照）。なので四角形は
// p3.y - g_CenterAboveFeet + g_RingLift に置く。
//
// cbuffer は SwarmGroundMarkCB.hlsli（C++ 側: SwarmSystem::GroundMarkCB）
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

#include "SwarmGroundMarkCB.hlsli"

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
StructuredBuffer<SwarmProjPath> paths : register(t2);
StructuredBuffer<SwarmMotion> motions : register(t3);
StructuredBuffer<SwarmAreaDef> areaDefs : register(t4);

struct RingOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;       // 範囲の半径にわたって -1..1
    float progress : TEXCOORD1;  // 落下 0..1 = 塗られた円の半径（uv 単位）
    float edgeWidth : TEXCOORD2; // 縁の太さ（uv 単位）
    float2 shapeFill : TEXCOORD3; // ここでは常に 0：丸い輪（SwarmBomberRingPS。設定するのは予告の扇 / 帯だけ）
};

static const float2 kCorner[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

RingOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    RingOut o = (RingOut) 0;
    o.pos = float4(0, 0, -1, 1); // このスロットが落下中のメテオでなければクリップされる

    if (projStates[iid] == SWARM_DEAD)
        return o;
    SwarmProjectile p = projectiles[iid];
    SwarmMotion m = motions[p.motion & SWARM_MOTION_INDEX_MASK];
    if (m.mode != SWARM_MOTION_DROP)
        return o;
    // ほとんど高さの無い落下（次のステップで着弾する）には予告を出さない。
    // 出すと輪が 1 フレーム光るだけになる。c1.x = 落下の高さ
    if (m.c1.x < 1.0)
        return o;

    // 爆発はメテオと一緒に拡大される（SwarmSpawnAreaFromDef）ので、輪も同じく拡大する
    float radius = (m.hitArea != 0u) ? areaDefs[m.hitArea].radius * SwarmProjScale(p) : 1.0;
    radius = max(radius, 0.01);
    float3 c3 = paths[iid].p3;
    float2 c = kCorner[vid];
    float3 world = float3(c3.x + c.x * radius,
                          c3.y - g_CenterAboveFeet + g_RingLift,
                          c3.z + c.y * radius);

    o.pos = mul(mul(float4(world, 1.0), g_RingView), g_RingProj);
    o.uv = c;
    o.progress = saturate(p.pathT);
    o.edgeWidth = g_RingEdgeWidth / radius;
    return o;
}
