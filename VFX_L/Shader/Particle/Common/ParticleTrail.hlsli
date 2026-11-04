// ============================================================
// ParticleTrail.hlsli
// パーティクルごとの帯トレイル。全部 GPU で行う。
//
// パーティクルはそれぞれ TRAIL_POINTS 個の位置の固定リングを持つ:
//     trailPoints[particleIndex * TRAIL_POINTS + slot]
// 割り当てが固定 = アロケータ不要で、漏れるものも無い。記録 / 描画するのは
// GPUParticle.trailStyle != 0 のパーティクルだけ。
//
// GPUParticle.trailStyle : 0 = トレイル無し、それ以外はスタイル表の番号 + 1
// GPUParticle.trailState : bits 0-7  = リングの書き込み位置（最新の点）
//                          bits 8-15 = 有効な点の数
//
// リングは lifetime / (TRAIL_POINTS - 1) 間隔の時間格子で記録するので、
// フレームレートに関係なく帯は常に「直近 <lifetime> 秒」をカバーする。
// 生きている先頭（現在位置）は保存しない。VS がパーティクルから
// 読む。
//
// GPUParticle.h の ParticleTrailStyleGPU / kTrailPoints と一致させること
// ============================================================
#ifndef PARTICLE_TRAIL_HLSLI
#define PARTICLE_TRAIL_HLSLI

#define TRAIL_POINTS 16u

#define TRAIL_FLAG_INHERIT_COLOR 1u
#define TRAIL_FLAG_INHERIT_SIZE  2u
// エフェクトトレイル専用（EffectTrailVS）: U = 経路長 * uvRepeat。テクスチャが
// 帯全体に引き伸ばされず、ワールドに留まる
#define TRAIL_FLAG_UV_TILE       4u

struct TrailStyle
{
    float4 colorHead;
    float4 colorTail;

    float widthHead;
    float widthTail;
    float lifetime; // 帯がカバーする経路の秒数
    float intensity; // rgb に掛ける HDR の倍率

    uint flags;
    float softEdge; // 0 = くっきりした縁、1 = 中心線から薄れる
    float uvRepeat; // 帯に沿ったテクスチャの繰り返し回数
    float uvScroll; // 1 秒あたりの U
};

float TrailInterval(float lifetime)
{
    return max(lifetime, 0.01) / (float) (TRAIL_POINTS - 1u);
}

uint TrailHead(uint state)
{
    return state & 0xFFu;
}

uint TrailCount(uint state)
{
    return (state >> 8u) & 0xFFu;
}

uint TrailPack(uint head, uint count)
{
    return (head & 0xFFu) | ((count & 0xFFu) << 8u);
}

#endif
