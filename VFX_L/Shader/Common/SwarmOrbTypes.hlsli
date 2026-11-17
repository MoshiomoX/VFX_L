// ============================================================
// SwarmOrbTypes.hlsli
// 経験玉の構造体（SwarmOrb）。
// ============================================================

#ifndef SWARM_ORB_TYPES_HLSLI
#define SWARM_ORB_TYPES_HLSLI

// ============================================================
// 経験玉：32 バイト。SwarmTypes.h の Swarm::Orb と一致させること
// ============================================================
struct SwarmOrb
{
    float3 position;
    float amount;

    float3 velocity;
    float attractSpeed; // 今の吸引の速さ。0 = まだ吸われていない（SwarmOrbMoveCS が書く）
};

#endif
