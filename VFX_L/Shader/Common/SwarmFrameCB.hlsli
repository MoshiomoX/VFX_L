// ============================================================
// SwarmFrameCB.hlsli
// フレームごとの定数（SwarmFrameCB）と、敵 AI の調整値（SwarmAICB）。
// ============================================================

#ifndef SWARM_FRAME_CB_HLSLI
#define SWARM_FRAME_CB_HLSLI

// ============================================================
// フレームごとの定数。デフォルトのレジスタは b0。ParticleCommon.hlsli
// （b0/b1 を使う）も include する CS は、このファイルを include する前に
// SWARM_FRAME_CB_REG を #define すること。
// ============================================================
#ifndef SWARM_FRAME_CB_REG
#define SWARM_FRAME_CB_REG b0
#endif

cbuffer SwarmFrameCB : register(SWARM_FRAME_CB_REG)
{
    float3 g_PlayerPos;
    float g_PlayerRadius;

    float g_Step;
    uint g_MaxEnemies;
    uint g_MaxProjectiles;
    uint g_MaxOrbs;

    float3 g_GridOrigin; // セル (0,0) のワールド位置
    float g_CellSize;

    uint g_GridW;
    uint g_GridD;
    uint g_Seed;
    uint g_PlayerAlive; // 0 = 死亡、敵は追うのをやめる
};

// ============================================================
// 敵 AI の調整値。デフォルトのレジスタは b1。すでに b1 を使っている CS は
// このファイルを include する前に SWARM_AI_CB_REG を #define すること。
// SwarmTypes.h の Swarm::AICB と一致させること
// ============================================================
#ifndef SWARM_AI_CB_REG
#define SWARM_AI_CB_REG b1
#endif

cbuffer SwarmAICB : register(SWARM_AI_CB_REG)
{
    float g_SeparationRadius;
    float g_SeparationPower;
    float g_AvoidPower;
    float g_LookAhead;

    float g_CenterAboveFeet; // カプセルの中心から足元までの高さ（position.y = 足の高さ + これ）
    float g_EnemyRadius;
    float g_EnemyCapsuleHalf;
    float g_VelocityLag;

    float g_MaxSpeedMul;
    float g_TurnSpeed;
    float g_PlayerPushOut;
    float g_ContactDamage;

    float g_AttackInterval;
    float g_PlayerCapsuleHalf;
    float g_HitStun; // 被弾後に敵が止まる秒（HitCS が animIndex = SWARM_ANIM_STUN を立てる）
    float g_HitFlash; // 硬直開始時の頂点色の倍率。1 まで減衰する
};

#endif
