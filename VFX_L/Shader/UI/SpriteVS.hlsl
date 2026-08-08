// ============================================================
// SpriteVS.hlsl
// 2D スプライト描画（インスタンシング、スクリーン座標）
// 頂点バッファを使わず SV_VertexID(0~5) と SV_InstanceID で quad を展開する。
// 座標系はピクセル単位：左上 (0,0)、右下 (screenW, screenH)。
// ============================================================

cbuffer SpriteCB : register(b0)
{
    float2 g_ScreenSize; // 画面サイズ（ピクセル）
    float2 _pad0;
};

// C++ 側の SpriteInstance と一致させる（48 bytes）
struct SpriteInstance
{
    float2 position; // 左上のスクリーン座標（ピクセル）
    float2 size; // 幅・高さ（ピクセル）
    float4 color; // 乗算色（アルファ含む）
    float4 uvRect; // 使用する UV 領域（xy=左上, zw=幅高さ）。全体なら (0,0,1,1)
};

StructuredBuffer<SpriteInstance> instances : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput o = (VSOutput) 0;

    SpriteInstance s = instances[instanceID];

    // quad の 6 頂点（左上原点、右下方向へ広がる）
    static const float2 corners[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };

    float2 corner = corners[vertexID];

    // ピクセル座標 → NDC 変換（左上 (0,0) を NDC (-1, +1) に対応させる）
    float2 pixel = s.position + corner * s.size;
    float2 ndc;
    ndc.x = (pixel.x / g_ScreenSize.x) * 2.0 - 1.0;
    ndc.y = -((pixel.y / g_ScreenSize.y) * 2.0 - 1.0);

    o.position = float4(ndc, 0.0, 1.0);
    o.uv = s.uvRect.xy + corner * s.uvRect.zw;
    o.color = s.color;

    return o;
}