// ============================================================
// TrailDrawCB.hlsli
// 帯（トレイル）を描くときのスタイルごとの cbuffer（b1）。ParticleTrailVS / PS、
// EffectTrailVS、SwarmProjTrailVS で共用する。C++ 側：TrailDrawCB（Particle/GPUParticle.h）
// ============================================================
#ifndef TRAIL_DRAW_CB_HLSLI
#define TRAIL_DRAW_CB_HLSLI

cbuffer TrailDrawCB : register(b1)
{
    uint g_StyleSlot;    // スタイルの番号 + 1（GPUParticle.trailStyle / recipeTrail と同じ表し方）
    uint g_Premultiply;  // アルファ合成のとき 1
    float g_Time;        // 秒（EffectTrailCB.g_Now と同じ時計）
    float _padT;
};

#endif
