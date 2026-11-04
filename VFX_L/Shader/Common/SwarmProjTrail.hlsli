// ============================================================
// SwarmProjTrail.hlsli
// SwarmProjTrailCS / SwarmProjTrailVS で共用。弾ごとのトレイル状態のうち、
// ParticleTrail.hlsli の head / count パックに追加で載せるビット
// （2026-10-15）。
// ============================================================
#ifndef SWARM_PROJ_TRAIL_HLSLI
#define SWARM_PROJ_TRAIL_HLSLI

// bit 16: 弾が消えた。帯は着弾点へ吸い込まれている途中
#define PROJ_TRAIL_DRAINING (1u << 16)
// bits 17-24: 消えてから押し込んだ点の数（TRAIL_POINTS = 吸い込み完了）
#define PROJ_TRAIL_DRAIN_SHIFT 17u

#endif
