    #include "Common/ParticleCommon.hlsli"

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    // この描画でどのビルボードを扱うか（2026-10-09、火 / 黒煙がちらついた件）：
    //   0 = 全部（従来どおりブレンド 1 パス）
    //   1 = 不透明なトゥーンのみ（最初に描き、深度を書く。PS は alpha < 0.5 を切る）
    //   2 = 不透明なトゥーン以外の全部（ブレンド、深度は読むだけ）
    //   3 = 歪みパス（2026-10-20）：歪みの強さを持つパーティクルだけを
    //       オフセットのターゲットへ（ParticleDistortPS）。colour.a x level / 3
    // 生存リストは InterlockedAdd で詰めるので順番が毎フレーム変わる。
    // ほぼ不透明なトゥーンの煙をその順で描くと、どれが手前に来るかが
    // 毎フレーム入れ替わる。深度を書けば距離で隠し合う。
    float g_Pass;
};

// GPUParticlePS の PARTICLE_TOON と同じビット（ParticleRenderMode::kToon）
#define PARTICLE_TOON_BIT (1u << 14)
// これ以上不透明（開始 alpha x 1.3）なトゥーンの煙を不透明として扱う
static const float SOLID_TOON_OPACITY = 0.85;

StructuredBuffer<GPUParticle> particles : register(t0);
StructuredBuffer<uint> aliveList : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation uint atlasRows : TEXCOORD1;
    nointerpolation uint atlasCols : TEXCOORD2;
    nointerpolation uint uvFrame : TEXCOORD3;
    nointerpolation uint sheet : TEXCOORD4; // ParticleSheets の番号（particle.textureIndex）
    nointerpolation uint alphaBlend : TEXCOORD5; // 1 = アルファブレンド、0 = 加算
    nointerpolation uint renderMode : TEXCOORD6; // トゥーンのビット（PARTICLE_TOON_*、GPUParticlePS）
    // トゥーン：x = 開始 alpha に対する残りの寿命（形を削る）、y = 不透明度（開始 alpha x 1.3）
    nointerpolation float2 toonFade : TEXCOORD7;
    nointerpolation uint solidPass : TEXCOORD8;  // 1 = 深度を書くパスで描く（PS は alpha < 0.5 を切る）
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output = (VSOutput) 0;

    uint particleIndex = aliveList[instanceID]; // ← instanceID を使うように変更
    GPUParticle p = particles[particleIndex];

    if (p.isAlive < 0.5)
    {
        output.position = float4(0, 0, -1, 1);
        return output;
    }

    static const float2 corners[6] =
    {
        float2(-1, 1), float2(1, 1), float2(-1, -1),
        float2(-1, -1), float2(1, 1), float2(1, -1),
    };

    static const float2 uvs[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };

    uint cornerIndex = vertexID; // ← vertexID（0~5）をそのまま使う
    float2 corner = corners[cornerIndex];
    output.uv = uvs[cornerIndex];

    output.atlasRows = p.atlasRows;
    output.atlasCols = p.atlasCols;
    output.uvFrame = p.uvFrame;
    output.sheet = (uint) max(p.textureIndex, 0);
    output.alphaBlend = (((uint) p.renderMode) & PARTICLE_BILLBOARD_ALPHA) ? 1u : 0u;
    output.renderMode = (uint) p.renderMode;
    float startA = max(p.startColor.a, 1e-3);
    output.toonFade = float2(saturate(p.color.a / startA), saturate(startA * 1.3));

    // パスの振り分け（g_Pass 参照）：このパスで扱わないパーティクルは面積 0 の三角形にする
    bool solid = (((uint) p.renderMode) & PARTICLE_TOON_BIT) != 0u && output.alphaBlend != 0u
              && output.toonFade.y >= SOLID_TOON_OPACITY;
    uint distortLevel = ParticleDistortLevel(p.renderMode);
    bool distortOnly = ParticleDistortOnly(p.renderMode);
    bool distortPass = (g_Pass > 2.5);
    if (distortPass)
    {
        if (distortLevel == 0u)
        {
            output.position = float4(0, 0, -1, 1);
            return output;
        }
        // 強さを畳み込む。PS がこれを読む。歪みのみの煙は強さを colour.a に持つ（他に読む
        // ものはない）。見える煙は色のパス用に薄い colour.a を持つので、屈折には代わりに
        // 寿命のフェード（colour.a / start a）を使い、強さを保つ（2026-10-20：風 = 屈折 + 少しの白）
        float fadeNow = distortOnly ? p.color.a : output.toonFade.x;
        output.color.a = fadeNow * (float) distortLevel / 3.0;
    }
    else if ((g_Pass > 0.5 && g_Pass < 1.5 && !solid) || (g_Pass > 1.5 && solid) || distortOnly)
    {
        output.position = float4(0, 0, -1, 1);
        return output;
    }
    output.solidPass = (solid && g_Pass > 0.5 && !distortPass) ? 1u : 0u;

    float3 camRight = float3(g_View[0][0], g_View[1][0], g_View[2][0]);
    float3 camUp = float3(g_View[0][1], g_View[1][1], g_View[2][1]);

    float3 worldPos;
    if (ParticleIsStreak(p.renderMode))
    {
        // 流線（2026-10-20）：長軸は速度に沿う（四角形がカメラを向いたままになるよう
        // ビュー平面に投影）、短軸 = サイズ。uv の上 = 先頭。
        // まだ動き出していないパーティクルは立てて描く
        float3 v = p.velocity;
        float sp = length(v);
        float3 toCam = normalize(g_CameraPosition - p.position);
        float3 axis = (sp > 1e-3) ? v / sp : camUp;
        axis -= toCam * dot(axis, toCam);
        float al = length(axis);
        axis = (al > 1e-3) ? axis / al : camUp;
        float3 side = normalize(cross(toCam, axis));
        float halfLen = max(p.size * 0.5, sp * ParticleStretch(p.renderMode) * 0.5);
        worldPos = p.position + axis * (corner.y * halfLen) + side * (corner.x * p.size * 0.5);
    }
    else
    {
        float cosR = cos(p.rotation);
        float sinR = sin(p.rotation);
        float2 rotated;
        rotated.x = corner.x * cosR - corner.y * sinR;
        rotated.y = corner.x * sinR + corner.y * cosR;

        float halfSize = p.size * 0.5;
        worldPos = p.position
                 + camRight * rotated.x * halfSize
                 + camUp * rotated.y * halfSize;
    }

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    output.position = mul(viewPos, g_Projection);
    output.color = distortPass ? float4(p.color.rgb, output.color.a) : p.color;

    return output;
}
