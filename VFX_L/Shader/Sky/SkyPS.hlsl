// ============================================================
// SkyPS.hlsl
// すべての後ろにあるグラデーションの空（最初に描く、深度なし）。視線の
// レイはビュー射影の逆行列から作り、その仰角で
// 色を決める:
//   地平線より上 : 地平線 -> 天頂（pow のカーブ。空の大半は
//                  地平線の色に近いまま）
//   地平線より下 : 地平線 -> 下（戦闘マップでは地面に隠れ、
//                  外周の壁の向こうで見える）
// それに太陽の方向へ柔らかい光輪と小さな円盤を足す。
// 出力は他のシーンのシェーダーと同じく線形 HDR（露出 / トーンマップ / ガンマは
// CompositePS がかける）。地平線の色は距離フォグの色も兼ねるので、
// 遠くの地形が空に溶け込む。
// C++ 側: SkyRenderer::SkyCB（row_major、転置なし）
// ============================================================
cbuffer SkyCB : register(b0)
{
    row_major float4x4 g_InvViewProj;
    float3 g_CameraPos;
    float g_ZenithCurve;   // 仰角に掛ける指数（< 1 = 地平線の帯が広くなる）
    float3 g_Zenith;
    float g_SunGlow;       // 光輪の強さ
    float3 g_Horizon;
    float g_SunGlowPower;  // 光輪の締まり（大きい = 光輪が小さい）
    float3 g_Below;
    float g_SunDisk;       // 円盤の強さ（0 = なし）
    float3 g_SunDir;       // 光の進む向き（太陽から）
    float _skyPad0;
    float3 g_SunColor;
    float _skyPad1;
};

struct SkyIn
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

float4 main(SkyIn i) : SV_TARGET
{
    float4 far = mul(float4(i.ndc, 1.0, 1.0), g_InvViewProj);
    float3 dir = normalize(far.xyz / far.w - g_CameraPos);

    float up = dir.y;
    float3 c = (up >= 0.0)
        ? lerp(g_Horizon, g_Zenith, pow(saturate(up), g_ZenithCurve))
        : lerp(g_Horizon, g_Below, saturate(-up * 4.0));

    float toSun = saturate(dot(dir, -g_SunDir));
    c += g_SunColor * (g_SunGlow * pow(toSun, g_SunGlowPower));
    c += g_SunColor * (g_SunDisk * smoothstep(0.9990, 0.9995, toSun));
    return float4(c, 1.0);
}
