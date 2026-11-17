// ============================================================
// LightingToon.hlsli
// トゥーンシェーディング（ShadeToon）と、その段・リム。Lighting.hlsli の中から include する
// （影・雲・フォグ・AO・点光源の合計を使う）。
// ============================================================
#ifndef LIGHTING_TOON_HLSLI
#define LIGHTING_TOON_HLSLI
#include "MathConst.hlsli"

// ------------------------------------------------------------
// トゥーンシェーディング（toonParams.x = 1）。N.L の代わりに、
// 境目を少しだけぼかした 2 段:
//   明るい側 : 太陽を全部（x toonShadowTint.w。N.L で少し横を向いた面が
//              暗くならなくなった分を下げる）
//   暗い側   : ただのグレーではなく、寒色に寄せた半球環境光
//              （toonShadowTint.rgb）
//   ハイライト : N.H が 1 に近い所に小さく硬い光
//   リム     : カメラに近い横向きの面の輪郭に細く明るい帯
//              （キャラクター、木の幹、壁。地面と遠くには
//              出さない）
// 太陽の影も段に入れる（N.L x 影）ので、落ち影も同じくっきりした縁に
// なる。点光源は滑らかなまま（松明、魔法の光）上に足す。
//
// ------------------------------------------------------------
float ToonBand(float x)
{
    return smoothstep(toonParams.y - toonParams.z, toonParams.y + toonParams.z, x);
}
// 同じ段で、遮蔽量に応じて境目を明るい側へ押したもの（SSAO）:
// くぼみや接地部分は上にグレーのグラデーションが乗るのではなく、暗い段に落ちる
float ToonBandAO(float x, float ao)
{
    // 不感帯: わずかな遮蔽（ノイズの底、平らな崖の面）では境目を動かさない。N.L が境目付近にある
    // 大きな面がフレームごとに丸ごと反転しないように。はっきりしたくぼみと接地部分だけが押す
    float occ = saturate((1.0 - ao - 0.15) / 0.85);
    float border = toonParams.y + aoParams.y * occ;
    return smoothstep(border - toonParams.z, border + toonParams.z, x);
}

float3 ToonRim(float3 N, float3 V, float3 worldPos, float lit)
{
    if (toonRim.w <= 0.0)
        return float3(0, 0, 0);
    float r = smoothstep(0.60, 0.68, 1.0 - saturate(dot(N, V)));        // 輪郭に細い帯
    r *= saturate(1.0 - abs(N.y) * 1.4);                                 // 横向きの面だけ
    r *= 1.0 - smoothstep(18.0, 32.0, distance(worldPos, cameraPosition)); // カメラの近くだけ
    return toonRim.rgb * (toonRim.w * r * (0.35 + 0.65 * lit));
}

float3 ShadeToon(float3 N, float3 V, float3 albedo, float3 worldPos, float specScale)
{
    float3 L = normalize(-dirLight.direction);
    float NdotL = max(dot(N, L), 0.0);
    uint cascade;
    float shadow = SunShadow(worldPos, N, NdotL, cascade);
    float ao = SceneAO(worldPos);   // 出口は下の 1 つ（途中で return すると fxc が X4000）
    float lit = ToonBandAO(NdotL * shadow, ao);

    float cloud = CloudSun(worldPos);   // 雲の下は太陽だけ弱く（明暗の段は変えない）
    float3 sun = dirLight.color * (dirLight.intensity * toonShadowTint.w * lit * cloud);
    float3 ambient = AmbientAt(N) * lerp(toonShadowTint.rgb, float3(1, 1, 1), lit) * lerp(1.0, ao, aoParams.z);

    float3 H = normalize(V + L);
    // 狭い窓: ローポリの平らな面は法線が H に一致すると面全体が光る。
    // カメラの近くだけ（リムと同じ）: そうしないと遠くの岩壁がプラスチックのようにきらつく
    float nearCam = 1.0 - smoothstep(18.0, 32.0, distance(worldPos, cameraPosition));
    float spot = smoothstep(0.994, 0.997, saturate(dot(N, H))) * lit * toonParams.w * specScale * nearCam * cloud;

    float3 diff, spec;
    PointLightShade(worldPos, N, V, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0, diff, spec);

    float3 c = (ambient + sun + diff) * albedo
             + dirLight.color * (dirLight.intensity * spot)
             + spec * PI * 0.5
             + ToonRim(N, V, worldPos, lit);
    c = ApplyFog(ShadowCascadeTint(c, cascade), worldPos);
    return (aoParams.w > 0.5) ? ao.xxx : c;   // aoParams.w = AO の調整用表示（グレー）
}

#endif
