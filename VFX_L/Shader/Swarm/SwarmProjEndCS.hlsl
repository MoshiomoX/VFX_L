// ============================================================
// SwarmProjEndCS.hlsl
// 固定ステップの第 4b 段（命中判定の直後）：このステップで消えた
// タグ付きの弾を報告する。
//
// 上級魔法（メテオ）を駆動する基本魔法（ファイアボール / ストーンショット）は
// 連鎖タグ付きで生成される（projTags、SwarmSpawnProjCS が書く）。
// そういう弾が消えたら -- 敵に当たった（HitCS）、寿命が尽きた、
// 壁に当たった（ProjMoveCS） -- このパスが消えた位置を連鎖リングに書き、
// タグを消す。
// CPU はリングをリードバックし、そこにメテオを落とす（WeaponSystem）。
//
// HitCS / ProjMoveCS の中で書かずに別パスにしている理由：
//   HitCS はすでに UAV を 8 本束縛していて（D3D11.0 の上限）、弾が
//   消える 3 か所すべてに同じコードが要る。
//   どちらのシェーダも消える弾の位置は動かさないので、
//   projectiles[i].position が消えた位置になる。
//
// 位置は地面（地形の高さ）に置く。CPU が普段のメテオの標的と同じく
// 敵の中心の高さまで持ち上げる。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
StructuredBuffer<float> terrainHeight : register(t2);

RWBuffer<uint> projTags : register(u0);
RWByteAddressBuffer triggerEvents : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    uint tag = projTags[i];
    if (tag == 0u)
        return;
    if (projStates[i] != SWARM_DEAD)
        return;

    projTags[i] = 0u;
    // 吸血の印（2026-10-25）は最上位ビットに乗っている。連鎖ではないので CPU には知らせない
    tag &= ~SWARM_TAG_LIFESTEAL;
    if (tag == 0u)
        return;

    float3 pos = projectiles[i].position;
    pos.y = SwarmTerrainHeight(terrainHeight, pos.xz);

    // [0] はこれまでに書いた全イベントの数（消さない）。CPU がどこまで
    // 読んだかを覚えているので、リードバックを逃したフレームがあっても取りこぼさない
    uint n;
    triggerEvents.InterlockedAdd(0, 1u, n);
    uint offset = 16u + (n % SWARM_MAX_TRIGGER_EVENTS) * 16u;
    triggerEvents.Store4(offset, uint4(asuint(pos), tag));
}
