// ============================================================
// SwarmSprite.hlsli
// GPU の範囲（弾の命中）が始めるスプライトシートのアニメーション。
// SwarmSpriteCS は、スロットに新しい範囲が現れると範囲のレシピの Sprite エントリ
// ごとに 1 インスタンスを始め、プールの年齢を進め、終わったインスタンスを
// 片付ける。SwarmSpriteVS/PS が生きているインスタンスをすべて描く。
// インスタンスは専用のリングプールにあるので、（短い）範囲が消えた後も
// アニメーションは再生を続ける。
// SwarmVFXTable.h の Swarm::VFXSpriteDef / Swarm::SpriteInstance と一致させること
// ============================================================
#ifndef SWARM_SPRITE_HLSLI
#define SWARM_SPRITE_HLSLI

#define SWARM_SPRITE_ADDITIVE 1u // SPRITE_FLAG_ADDITIVE と同じビット
#define SWARM_SPRITE_LOOP     2u
// ビット 8-15: スプライトのテクスチャ配列のスライス

// レシピの Sprite エントリ 1 つ（80 バイト）
struct SwarmSpriteDef
{
    float3 offset;   // 範囲の中心から
    float height;    // m
    float4 color;
    float2 cellUV;   // 配列スライスの uv での 1 コマの大きさ
    float2 pivot;    // コマの中、左上から 0..1
    uint cols;
    uint frameCount;
    float frameTime; // 1 コマの秒数、再生速度を掛けた後
    float life;      // インスタンスが生きる秒数
    uint facing;     // SPRITE_FACING_*
    uint flags;
    float rotation;  // ラジアン
    float aspect;    // 幅 / 高さ
};

// 再生中のアニメーション 1 つ（32 バイト）
struct SwarmSprite
{
    float3 position;
    float age;
    uint def;
    uint alive;
    float sizeScale; // 範囲のサイズ倍率（拡大鏡）。0 = 1
    uint _pad;
};

#endif
