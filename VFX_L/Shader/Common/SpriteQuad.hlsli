// ============================================================
// SpriteQuad.hlsli
// スプライトシートの 1 フレームを四角形で描く。CPU の Sprite エントリの
// レンダラー（VFXSpriteVS）と GPU の範囲のスプライト（SwarmSpriteVS）で共用。
// VFX_Editor/VFXSpriteRenderer.h の VFXSpriteQuad（80 バイト）と一致させること。
// ============================================================
#ifndef SPRITE_QUAD_HLSLI
#define SPRITE_QUAD_HLSLI

#define SPRITE_FACING_BILLBOARD 0u // カメラを向く
#define SPRITE_FACING_UPRIGHT   1u // 立てる（ワールド Y）、Y まわりにだけ回る
#define SPRITE_FACING_GROUND    2u // XZ 平面に寝かせ、画像の上をカメラから遠い側へ向ける

#define SPRITE_FLAG_ADDITIVE    1u // このビットが無ければアルファブレンド（乗算済みの出力）
// ビット 8-15：テクスチャ配列のスライス（GPU 経路のみ）

struct SpriteQuad
{
    float3 position; // ピボットのワールド位置
    float rotation;  // ラジアン、四角形の面内での回転
    float2 size;     // 幅、高さ（m）
    float2 pivot;    // フレーム内のピボット、左上から 0..1
    float4 uvRect;   // xy = フレームの左上の uv、zw = uv の大きさ
    float4 color;
    uint facing;
    uint flags;
    float2 _pad;
};

// ワールド空間でのカメラの基底（ビュー行列から。揺れを含む）
struct SpriteCamera
{
    float3 right;
    float3 up;
    float3 forward;
};

// vid 0..5 = 三角形 2 枚
void SpriteCorner(SpriteQuad q, uint vid, SpriteCamera cam, out float3 world, out float2 uv)
{
    static const float2 k[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };
    float2 c = k[vid];
    uv = q.uvRect.xy + c * q.uvRect.zw;

    // ピボットからのずれ：x は右、y は上（画像の v は下向きに増える）
    float2 local = float2((c.x - q.pivot.x) * q.size.x, (q.pivot.y - c.y) * q.size.y);
    float s = sin(q.rotation);
    float co = cos(q.rotation);
    local = float2(local.x * co - local.y * s, local.x * s + local.y * co);

    float3 right = cam.right;
    float3 up = cam.up;
    if (q.facing != SPRITE_FACING_BILLBOARD)
    {
        float3 flatRight = float3(cam.right.x, 0.0, cam.right.z);
        float len = length(flatRight);
        right = (len > 1e-4) ? flatRight / len : float3(1, 0, 0);
        if (q.facing == SPRITE_FACING_UPRIGHT)
        {
            up = float3(0, 1, 0);
        }
        else
        {
            // 地面：画像の上をカメラから遠い側へ向ける
            up = float3(-right.z, 0.0, right.x);
            if (dot(up, cam.forward) < 0.0)
                up = -up;
        }
    }
    world = q.position + right * local.x + up * local.y;
}

#endif
