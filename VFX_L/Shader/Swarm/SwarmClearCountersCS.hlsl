// ============================================================
// SwarmClearCountersCS.hlsl
// サブステップごとに、その前にステップ単位のカウンタを 0 にする。
//
// 生存数は毎ステップ一から数え直すので、ここで
// クリアする。
//
// 撃破数とプレイヤーへのダメージはずっと累積する。CPU は
// 最後に見た値を覚えておいて差分を取る。これは意図的:
// リードバックは DO_NOT_WAIT でどのフレームでも失敗しうるので、
// フレームごとにリセットするとその分の出来事が黙って失われる。uint が
// 一周するには 40 億体倒す必要がある。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

RWByteAddressBuffer counters : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    counters.Store(SWARM_CNT_ALIVE_ORBS, 0u);
    counters.Store(SWARM_CNT_ALIVE_ENEMIES, 0u);
    counters.Store(SWARM_CNT_ALIVE_PROJ, 0u);
    counters.Store(SWARM_CNT_ALIVE_AREAS, 0u);
    counters.Store(SWARM_CNT_TICKING_AREAS, 0u);
    counters.Store(SWARM_CNT_NEAREST_KEY, SWARM_NO_TARGET_KEY);
   // counters.Store(SWARM_CNT_KILLS, 12345u); // TEMP: リードバックの確認用
    // SWARM_CNT_KILLS / SWARM_CNT_PLAYER_DAMAGE には触らないこと
}