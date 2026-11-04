// ============================================
// ParticleUpdateCS.hlsl
// パーティクル更新 CS: 寿命 / 物理 / 寿命に沿った色 / 生存リスト。
//
// 画面の歪みレベルを持つ生きたビルボードも数え
// （g_Stats[0]、GPUParticleSystem::HasDistortion がリードバック、2026-10-25）、
// 歪ませるものがある間だけシーンが歪みのパスを走らせるようにする。
// （以前ここにあった持ち主ごとの生存数は C++ 側から一度もバインドされて
// おらず、2026-10-25 に削除した。）
// ============================================

#include "Common/ParticleCommon.hlsli"
#include "../Common/WindSources.hlsli"

// 世界の風（2026-10-12）: renderMode の 26-29 ビットが立っているパーティクルは
// 風の方へ流される（煙 / 霧 / 雨 / 砂。火花は 0 のまま）。
// GPUParticleSystem::ParticleWindCB と揃えること（b2。b0 / b1 は ParticleCommon のもの）
cbuffer ParticleWindCB : register(b2)
{
    float2 g_WindDir;       // 単位 xz、風の吹く向き
    float g_WindSpeed;      // m/s（基本）、天気の強さはもう掛けてある
    float g_WindGust;       // m/s（突風の振幅）、同上
    float g_WindWaveScale;  // 1 m あたりの突風の波の数
    float g_WindWaveSpeed;  // 1 秒あたりのラジアン
    float g_WindTime;       // 草の風の時計（草の葉と同じ波）
    float _windPad;
};

// xz での世界の風（GrassVS の突風の式）
float2 ParticleWindAt(float2 xz)
{
    float phase = dot(xz, g_WindDir) * g_WindWaveScale * 6.2831853 - g_WindTime * g_WindWaveSpeed;
    float w = sin(phase) * 0.5 + 0.5;
    w *= w;
    return g_WindDir * (g_WindSpeed + g_WindGust * w);
}

StructuredBuffer<ColorKey> colorKeys : register(t0);
StructuredBuffer<WindSource> g_WindSources : register(t1);   // 局所的な風: 砂旋（SwarmWindSourceCS、2026-10-19）
RWStructuredBuffer<GPUParticle> particles : register(u0);
AppendStructuredBuffer<uint> deadList : register(u1);
RWBuffer<uint> g_DrawArgs : register(u2);
RWStructuredBuffer<uint> aliveList : register(u3); // 生きているパーティクルの番号（ビルボード）
RWBuffer<uint> g_Stats : register(u4);             // [0] = 歪みレベルを持つ生きたビルボード（C++ 側が毎フレームクリア）
RWStructuredBuffer<uint> aliveMesh : register(u5); // 生きているメッシュパーティクル、バケツごとに PARTICLE_MESH_BUCKET_CAP 個
RWBuffer<uint> meshCounts : register(u6); // バケツごとのパーティクル数（各サブメッシュの描画引数にコピーする）

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_MaxParticles)
        return;

    GPUParticle p = particles[id.x];

    if (p.isAlive < 0.5)
        return;

    p.age += g_DeltaTime;

    if (p.age >= p.lifetime)
    {
        p.isAlive = 0.0;
        deadList.Append(id.x);
        particles[id.x] = p;
        return;
    }

    // --- 生存: 描画引数に数え、番号を生存リストに足す ---
    // ビルボードとメッシュはリストが別（描画パスも別）。renderMode で選ぶ
    uint instanceIndex;
    if (ParticleMeshSlotPlusOne(p.renderMode) != 0u)
    {
        // メッシュ: バケツ（メッシュ番号 x 受光 / 発光）ごとに aliveMesh の 1 区間。
        // 上限を超えても数は増え続けるが何も書かない。
        // ParticleMeshVS が上限以上のインスタンスを捨てる
        uint bucket = ParticleMeshBucket(p.renderMode);
        InterlockedAdd(meshCounts[bucket], 1, instanceIndex);
        if (instanceIndex < PARTICLE_MESH_BUCKET_CAP)
            aliveMesh[bucket * PARTICLE_MESH_BUCKET_CAP + instanceIndex] = id.x;
    }
    else
    {
        InterlockedAdd(g_DrawArgs[1], 1, instanceIndex);
        aliveList[instanceIndex] = id.x;

        // GPUParticleVS の g_Pass 3 と同じ判定（歪みのターゲットに描くのはこれだけ）
        if (ParticleDistortLevel(p.renderMode) != 0u)
            InterlockedAdd(g_Stats[0], 1u);
    }

    float t = GetLifeRatio(p);

    // atlasAnimate = (first << 12) | count（InitParticleFrame を参照）
    if (p.atlasAnimate > 0)
    {
        int count = p.atlasAnimate & 0xFFF;
        int first = p.atlasAnimate >> 12;
        p.uvFrame = first + min((int) (t * count), count - 1);
    }

    p.velocity += p.acceleration * g_DeltaTime;
    p.velocity *= (1.0 - p.drag * g_DeltaTime);

    // 風: 水平の速度を世界の風へ近づける（煙は流され、火花は流されない）
    float windResp = ParticleWindResponse(p.renderMode);
    if (windResp > 0.0)
    {
        // 風の流線（2026-10-20）は空気そのもの: 局所的な風速に煙の約 5 倍の速さで
        // 合わせるので、渦の螺旋が 0.1 s 以内に見える
        float rate = ParticleIsStreak(p.renderMode) ? PARTICLE_WIND_RATE * 5.0 : PARTICLE_WIND_RATE;
        float k = saturate(windResp * rate * g_DeltaTime);
        float vortexLift;
        float3 vortexAir = WindSourceAir(g_WindSources, p.position.xz, vortexLift);
        float2 wind = ParticleWindAt(p.position.xz) + vortexAir.xz;
        p.velocity.xz += (wind - p.velocity.xz) * k;
        if (vortexLift > 0.0)   // 上昇気流は風源の中だけ（外で 0 へ引き戻さない）
            p.velocity.y += (vortexAir.y - p.velocity.y) * k * vortexLift;
    }

    p.position += p.velocity * g_DeltaTime;

    if (p.colorKeyCount > 0)
    {
        float4 result = colorKeys[p.colorKeyOffset].color;
        for (int k = 1; k < p.colorKeyCount; k++)
        {
            ColorKey prev = colorKeys[p.colorKeyOffset + k - 1];
            ColorKey next = colorKeys[p.colorKeyOffset + k];
            if (t >= prev.time && t <= next.time)
            {
                // 同じ時刻のキーが 2 つ: 0 除算しない（NaN の色はブルームで広がってしまう）
                float localT = (t - prev.time) / max(next.time - prev.time, 1e-5);
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
        p.color = lerp(p.startColor, p.endColor, t);
    }

    p.size = lerp(p.startSize, p.endSize, t);
    p.rotation += p.angularVel * g_DeltaTime;
    p.rot3 += p.angVel3 * g_DeltaTime;
    particles[id.x] = p;
}
