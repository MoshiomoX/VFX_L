// ============================================
// ParticleUpdateCS.hlsl
// 粒子更新CS — 物理シミュレーション・補間・死亡判定
// ============================================

#include "Common/ParticleCommon.hlsli"

// --- Buffer宣言 ---
StructuredBuffer<ColorKey> colorKeys : register(t0);
RWStructuredBuffer<GPUParticle> particles : register(u0);
AppendStructuredBuffer<uint> deadList : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_MaxParticles)
        return;

    GPUParticle p = particles[id.x];

    if (p.isAlive < 0.5)
        return;

    // --- 寿命更新 ---
    p.age += g_DeltaTime;

    // --- 死亡判定 ---
    if (p.age >= p.lifetime)
    {
        p.isAlive = 0.0;
        deadList.Append(id.x);
        particles[id.x] = p;
        return;
    }

    // --- 寿命比率 (0.0 ~ 1.0) ---
    float t = GetLifeRatio(p);

    // --- アトラスアニメーション ---
    if (p.atlasAnimate > 0)
    {
        int totalFrames = p.atlasRows * p.atlasCols;
        p.uvFrame = (int) (t * totalFrames);
        p.uvFrame = min(p.uvFrame, totalFrames - 1);
    }

    // --- 物理更新 ---
    p.velocity += p.acceleration * g_DeltaTime;
    p.velocity *= (1.0 - p.drag * g_DeltaTime);
    p.position += p.velocity * g_DeltaTime;

    // --- 色補間 ---
    if (p.colorKeyCount > 0)
    {
        // Color over Lifetime（複数キーフレーム補間）
        float4 result = colorKeys[p.colorKeyOffset].color;
        for (int k = 1; k < p.colorKeyCount; k++)
        {
            ColorKey prev = colorKeys[p.colorKeyOffset + k - 1];
            ColorKey next = colorKeys[p.colorKeyOffset + k];
            if (t >= prev.time && t <= next.time)
            {
                float localT = (t - prev.time) / (next.time - prev.time);
                result = lerp(prev.color, next.color, localT);
                break;
            }
            if (t > next.time)
                result = next.color;
        }
        p.color = result;
    }
    else
    {
        // 従来の2色線形補間
        p.color = lerp(p.startColor, p.endColor, t);
    }

    // --- サイズ補間 ---
    p.size = lerp(p.startSize, p.endSize, t);

    // --- 回転更新 ---
    p.rotation += p.angularVel * g_DeltaTime;

    // --- 書き戻し ---
    particles[id.x] = p;
}