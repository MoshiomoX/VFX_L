// ============================================================
// ParticleRenderCB.hlsli
// パーティクルと帯を描く VS のカメラの cbuffer（b0）。GPUParticleVS、ParticleTrailVS、
// EffectTrailVS、SwarmProjTrailVS で共用する。C++ 側：ParticleRenderCB（Particle/GPUParticle.h）
// ============================================================
#ifndef PARTICLE_RENDER_CB_HLSLI
#define PARTICLE_RENDER_CB_HLSLI

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    // この描画でどのビルボードを扱うか（GPUParticleVS だけが読む。帯の VS では使わない）：
    //   0 = 全部（ブレンド 1 パス）
    //   1 = 不透明なトゥーンのみ（最初に描き、深度を書く。PS は alpha < 0.5 を切る）
    //   2 = 不透明なトゥーン以外の全部（ブレンド、深度は読むだけ）
    //   3 = 歪みパス：歪みの強さを持つパーティクルだけを
    //       オフセットのターゲットへ（ParticleDistortPS）。colour.a x level / 3
    // 生存リストは InterlockedAdd で詰めるので順番が毎フレーム変わる。
    // ほぼ不透明なトゥーンの煙をその順で描くと、どれが手前に来るかが
    // 毎フレーム入れ替わる。深度を書けば距離で隠し合う。
    float g_Pass;
};

#endif
