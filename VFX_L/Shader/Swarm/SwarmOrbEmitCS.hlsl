// ============================================================
// SwarmOrbEmitCS.hlsl
// プレイヤーへ吸い寄せられている経験玉の尾のパーティクル。
// 発射の本体は SwarmEmitCS と同じ。発生源は吸引の速さが g_OrbTrailMinSpeed を
// 超えている生きた経験玉で、全部 1 つのレシピを使う
// （VFXId::ExpOrbTrail、Assets/Data/VFXData/ExpOrbTrail.json）。
// 待機中の経験玉は何も出さないので、経験玉だらけのフィールドでもパーティクルを使わない。
// ============================================================
#define SWARM_EMIT_ORBS
#include "SwarmEmitCS.hlsl"
