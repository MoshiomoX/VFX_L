// ============================================================
// SwarmEmitCS.hlsl
// GPU の弾 1 発につき 1 スレッド。生きていればレシピを走査し、
// その中の全エミッタから弾の位置で発射する。
//
// 弾は芯（位置 + 判定）だけ。見えるものはすべて
// パーティクルで、CPU の経路と同じ。
//
// dead list の保護：1 スレッドが k 個発射するので、フレームごとの予算から
// k をアトミックに予約し、予約が空き数を超えたら抜ける。予約しておけば
// 多数のスレッドが競合しても Consume のカウンタが
// アンダーフローしない。
// ============================================================
// パーティクルの初期化 / 形状 / StochasticCount は GrassFireEmitCS と共通（EmitInit.hlsli）
#include "../Particle/Common/EmitInit.hlsli"

// ParticleCommon が b0（GlobalCB）と b1（DeadListCB）を使うので、こちらは b2
#define SWARM_FRAME_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

// SwarmAreaEmitCS.hlsl は SWARM_EMIT_AREAS を #define してこのファイルを include する：
// 発射のコードは同じで、発生源が弾ではなく生きている範囲になる。
// SwarmOrbEmitCS.hlsl も SWARM_EMIT_ORBS で同じことをする：発生源は
// プレイヤーへ吸い寄せられている経験玉で、全部 1 つのレシピ（b3）
#if defined(SWARM_EMIT_AREAS)
StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
#elif defined(SWARM_EMIT_ORBS)
StructuredBuffer<SwarmOrb> orbs : register(t0);
Buffer<uint> orbStates : register(t1);

// SwarmSystem::OrbEmitCB と一致させる
cbuffer SwarmOrbEmitCB : register(b3)
{
    uint g_OrbTrailVfx;        // レシピ番号（SwarmVFXTable::IndexOf）。0 = なし
    float g_OrbTrailMinSpeed;  // これ（m/s）より速く吸われると発射する
    uint2 _orbEmitPad;
};
#else
StructuredBuffer<SwarmProjectile> projectiles : register(t0);
Buffer<uint> projStates : register(t1);
// 出口魔法の弾は運んでいる魔法の色を見せる（2026-10-31）：タグのビット -> rgb + 混ぜ具合（TriggerBits）
Buffer<uint> projTags : register(t5);
cbuffer SwarmCarryCB : register(b3)
{
    float4 g_CarryColor[32];
};
#endif
StructuredBuffer<SwarmRecipe> recipes : register(t2);
StructuredBuffer<GPUEmitter> emitters : register(t3);
Buffer<uint> deadCount : register(t4);

RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);
RWByteAddressBuffer emitBudget : register(u2);

// 作った色の明るさは保ち、色相を運んでいる魔法の色へ寄せる
float3 CarryTint(float3 c, float4 carry)
{
    return lerp(c, carry.rgb * max(max(c.r, c.g), c.b), carry.a);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    float4 carry = float4(0, 0, 0, 0);

    // ---- 発生源：どこで、どの速さで、どのレシピか ----
    float3 srcPos;
    float3 srcVel;
    uint srcVfx;
    float srcScale = 1.0; // 大きさの倍率（拡大鏡）。1 = 作ったまま
#if defined(SWARM_EMIT_AREAS)
    if (i >= SWARM_MAX_AREAS)
        return;
    if (areaStates[i] == SWARM_DEAD)
        return;
    srcPos = areas[i].center;
    srcVel = float3(0, 0, 0);
    srcVfx = areas[i].vfxType;
    srcScale = SwarmAreaScale(areas[i]);
#elif defined(SWARM_EMIT_ORBS)
    if (i >= g_MaxOrbs || g_OrbTrailVfx == 0u)
        return;
    if (orbStates[i] == SWARM_DEAD)
        return;
    if (orbs[i]._pad <= g_OrbTrailMinSpeed) // _pad = 吸引の速さ（SwarmOrbMoveCS）
        return;
    srcPos = orbs[i].position;
    srcVel = orbs[i].velocity;
    srcVfx = g_OrbTrailVfx;
#else
    if (i >= g_MaxProjectiles)
        return;
    if (projStates[i] == SWARM_DEAD)
        return;
    srcPos = projectiles[i].position;
    srcVel = projectiles[i].velocity;
    // 停止中の弾（pathT < 0、ストーンショット）：まだ動いていない。向きだけ
    // 残し（ごく遅い速度）、本体が標的の方を向くようにする（2026-10-16）
    if (projectiles[i].pathT < 0.0)
    {
        float vLenSq = dot(srcVel, srcVel);
        srcVel = (vLenSq > 1e-8) ? srcVel * (rsqrt(vLenSq) * 0.01) : float3(0, 0, 0);
    }
    srcVfx = projectiles[i].vfxType;
    srcScale = SwarmProjScale(projectiles[i]);
    uint carryTag = projTags[i] & ~SWARM_TAG_LIFESTEAL;
    if (carryTag != 0u)
        carry = g_CarryColor[firstbitlow(carryTag)];
#endif

    SwarmRecipe r = recipes[srcVfx];
    if (r.particleCount == 0u)
        return;

    uint seed = g_BaseSeed + i * 7919u;

    for (uint ei = 0u; ei < r.particleCount; ++ei)
    {
        GPUEmitter e = emitters[r.particleStart + ei];
        if (carry.a > 0.0)
        {
            e.startColorMin.rgb = CarryTint(e.startColorMin.rgb, carry);
            e.startColorMax.rgb = CarryTint(e.startColorMax.rgb, carry);
            e.endColorMin.rgb = CarryTint(e.endColorMin.rgb, carry);
            e.endColorMax.rgb = CarryTint(e.endColorMax.rgb, carry);
        }
        // 発生源が大きい = エフェクト全体を拡大：オフセット、発射形状、速度（寿命の間に
        // どこまで広がるか）、重力、パーティクルの大きさ。少し密にもする（x scale。
        // 面積は scale^2 で増えるが、パーティクルのプールは共有なので）
        e.position *= srcScale;
        e.shapeSize *= srcScale;
        e.speedRange *= srcScale;
        e.gravity *= srcScale;
        e.sizeRange *= srcScale;
        e.emitRate *= srcScale;
        e.position = srcPos + e.position; // 発生源についていく。エントリの位置はオフセット（レイヤー）

        // 単体（岩、矢、水晶）：1 フレームにちょうど 1 個、次のフレームの
        // 更新で死ぬまで生きる（寿命は 1〜2 フレームの間）
        // = 常にちょうど 1 個だけ描かれ、重なってちらつかない（2026-10-16）
        bool single = (((uint) e.renderMode) & PARTICLE_SINGLE_BODY) != 0u;
        uint k = single ? 1u : StochasticCount(e.emitRate, g_DeltaTime, seed);
        if (k == 0u)
            continue;
        if (single)
        {
            e.lifetimeRange = float2(g_DeltaTime * 1.02, g_DeltaTime * 1.02); // 描かれるよう 1 フレームより長く、長いフレームの後でも次のフレームで死ぬよう 2 フレームより短く
            e.speedRange = float2(0, 0);
        }

        // ---- 空きプールから予約する ----
        uint mine;
        emitBudget.InterlockedAdd(0, k, mine);
        uint freeNow = deadCount[0];
        if (mine >= freeNow)
            return;
        if (mine + k > freeNow)
            k = freeNow - mine;

        for (uint n = 0u; n < k; ++n)
        {
            uint particleIndex = deadList.Consume();
            uint s = seed + (ei * 131u + n) * 1099u;

            float3 pos = e.position;
            float3 vel = float3(0, 0, 0);
            EmitShape(e, s, pos, vel);

            // 掃き出し発射：弾は前回の発射から約 velocity * dt 動いたので、
            // その線分に沿って後ろへばらまく。これが無いと速い弾は
            // 点線の軌跡を残す。下の属性の乱数列を変えないよう
            // 専用のシードを使う
            // 発生源と一緒に飛ぶパーティクル（矢の本体、岩、
            // 水晶）は本体そのもの：ちょうど発生源の位置に出し、掃き出さない。
            // でないと 1 つの本体のコピーが軌道上に伸びて見える（2026-10-16）
            uint sweepSeed = s ^ 0x9E3779B9u;
            bool inherit = (((uint) e.renderMode) & PARTICLE_INHERIT_SOURCE_VELOCITY) != 0u;
            if (!inherit)
                pos -= srcVel * (g_DeltaTime * Random(sweepSeed));
            else
                vel += srcVel;

            // ---- パーティクルの初期化：ParticleEmitCS と同じ（EmitInit.hlsli） ----
            particles[particleIndex] = InitParticleFromEmitter(e, s, pos, vel);
        }
    }
}