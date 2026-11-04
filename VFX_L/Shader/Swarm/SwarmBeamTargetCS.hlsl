// ============================================================
// SwarmBeamTargetCS.hlsl
// 光線ごとの標的（2026-10-01）。光線のチャンネル 1 つにつきスレッドグループ 1 つ。
//
// CPU（WeaponSystem）は、ここからリードバックした位置へ光線を
// 上限付きの速さで向ける。チャンネルごとに BeamTargetCB から：
//   cmd 0 = 待機（チャンネル未使用、何も書かない）
//   cmd 1 = 開始：シーク点（連鎖元の弾が消えた所）に最も近い敵を
//           kLockRadius 以内でロックする
//   cmd 2 = 追跡：ロックした敵が生きていて届く範囲にいる間は維持。
//           そうでなければ光線の長さ以内の生きた敵のうち、
//           方向が光線に最も近い（回す量が最小の）ものを選ぶ
//   cmd 3 = 連鎖（2026-10-20、アークボルトの連鎖のルーン）：シーク点
//           （稲妻が届いた所）に最も近い生きた敵を SWARM_CHAIN_RADIUS
//           以内で選ぶ。asuint(dir.xyz) のスロット（この連鎖が
//           既に届いた敵）は除く。
//           いなければ valid = 0
// 結果（slot, serial, valid, 位置）は beamTargets に残り、これが
// 次のフレームの状態（ロックしたスロット）にもなる。serial で CPU は
// どの光線への答えかを見分ける（チャンネルは使い回される）。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer BeamTargetCB : register(b4)
{
    float4 g_BTOrigin[SWARM_MAX_BEAMS]; // xyz = 杖口、w = asfloat(cmd)
    float4 g_BTDir[SWARM_MAX_BEAMS];    // xyz = 今の光線の向き、w = 光線の長さ
    float4 g_BTSeek[SWARM_MAX_BEAMS];   // xyz = シーク点（cmd 1）、w = asfloat(serial)
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2); // 潜っている砂潜りは標的にしない（2026-10-17）
// チャンネルごとに 32 バイト：slot, serial, valid, pad, pos.xyz, pad
RWByteAddressBuffer beamTargets : register(u0);

static const uint kNoSlot = 0xFFFFFFFFu;
static const float kLockRadius = 2.5;  // 開始：弾が当たった敵は、弾が消えた所からこの距離以内にいる
static const float kKeepReach = 1.15;  // 長さ x これより遠いロック中の敵は外す（遠くへ転送された）

groupshared uint gBest;

[numthreads(256, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    uint ch = gid.x;
    uint cmd = asuint(g_BTOrigin[ch].w);
    if (cmd == 0u)
        return; // グループ全体で同じ値（定数バッファ）

    uint base = ch * 32u;
    uint serial = asuint(g_BTSeek[ch].w);
    float3 origin = g_BTOrigin[ch].xyz;
    float3 dir = g_BTDir[ch].xyz;
    float len = g_BTDir[ch].w;
    float3 seek = g_BTSeek[ch].xyz;

    // ロック中の敵を維持するか？
    uint curSlot = beamTargets.Load(base + 0u);
    uint curSerial = beamTargets.Load(base + 4u);
    bool keep = false;
    if (cmd == 2u && curSerial == serial && curSlot < g_MaxEnemies && enemyStates[curSlot] != SWARM_DEAD)
    {
        float2 d = enemies[curSlot].position.xz - origin.xz;
        keep = dot(d, d) <= (len * kKeepReach) * (len * kKeepReach);
    }

    if (gi == 0u)
        gBest = kNoSlot;
    GroupMemoryBarrierWithGroupSync();

    if (!keep)
    {
        float2 fwd = dir.xz;
        float fl = length(fwd);
        fwd = (fl > 1e-4) ? fwd / fl : float2(0.0, 1.0);
        for (uint i = gi; i < g_MaxEnemies; i += 256u)
        {
            if (enemyStates[i] == SWARM_DEAD || SwarmUntargetable(enemyExtra[i]))
                continue;
            float3 p = enemies[i].position;
            float score;
            if (cmd == 1u)
            {
                // 開始：シーク点に最も近いもの
                score = length(p.xz - seek.xz);
                if (score > kLockRadius)
                    continue;
            }
            else if (cmd == 3u)
            {
                // 連鎖：今当たった敵に最も近いもの。ただしこの連鎖が
                // 既に届いた敵は除く（dir.xyz = スロット）
                if (i == asuint(dir.x) || i == asuint(dir.y) || i == asuint(dir.z))
                    continue;
                score = length(p.xz - seek.xz);
                if (score > SWARM_CHAIN_RADIUS)
                    continue;
            }
            else
            {
                // 標的の取り直し：光線の長さ以内で、光線からの角度が最小のもの
                float2 to = p.xz - origin.xz;
                float l = length(to);
                if (l > len || l < 1e-3)
                    continue;
                score = max(1.0 - dot(to / l, fwd), 0.0); // 0 = 真正面、2 = 真後ろ
            }
            uint prev;
            InterlockedMin(gBest, (asuint(score) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK), prev);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (gi == 0u)
    {
        uint slot = keep ? curSlot : ((gBest == kNoSlot) ? kNoSlot : (gBest & SWARM_SLOT_MASK));
        bool valid = (slot != kNoSlot);
        float3 pos = valid ? enemies[slot].position : float3(0, 0, 0);
        beamTargets.Store4(base, uint4(slot, serial, valid ? 1u : 0u, 0u));
        beamTargets.Store4(base + 16u, uint4(asuint(pos.x), asuint(pos.y), asuint(pos.z), 0u));
    }
}
