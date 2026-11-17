// ============================================================
// SwarmOrbCB.hlsli
// 経験玉の調整値（SwarmOrbCB）。
// ============================================================

#ifndef SWARM_ORB_CB_HLSLI
#define SWARM_ORB_CB_HLSLI

// ============================================================
// 経験玉の調整値。デフォルトのレジスタは b2。HitCS（経験玉を生成）と
// OrbMoveCS（動かす / 拾う）の両方が読む。
// SwarmTypes.h の Swarm::OrbCB と一致させること
// ============================================================
#ifndef SWARM_ORB_CB_REG
#define SWARM_ORB_CB_REG b2
#endif

cbuffer SwarmOrbCB : register(SWARM_ORB_CB_REG)
{
    float g_OrbAttractRadius; // この内側でプレイヤーへ引き寄せ始める
    float g_OrbPickupRadius; // この内側で拾われる
    float g_OrbAccel; // 引き寄せの加速度
    float g_OrbMaxSpeed;

    float g_OrbAmount; // 経験玉 1 つの経験値（今は全体共通。いずれ敵ごとに）
    float g_OrbY; // 経験玉はこの高さに浮く
    float2 _orbPad;
};

#endif
