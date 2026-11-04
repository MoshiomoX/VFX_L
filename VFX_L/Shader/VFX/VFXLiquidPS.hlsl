// ============================================================
// VFXLiquidPS.hlsl
// Liquid エントリ（2026-10-02）：水たまりの 1 ピクセルをシェーディングする。
// CPU 経路（VFXLiquidRenderer + VFXLiquidVS）と GPU 経路
// （SwarmSystem + SwarmLiquidVS）の両方で使う。LiquidCommon.hlsli を参照。
//
//   覆い     : 水たまりの SDF d（m）、fwidth(d) でアンチエイリアス
//   深さ     : エッジで 0 -> depthWidth だけ内側で 1（色、不透明度）
//   表面     : 中央は平ら、エッジで depthWidth をかけて下へ曲がる。
//              法線は SDF の勾配 + ゆっくりしたノイズの波打ち
//   光       : 太陽（拡散 + GGX ハイライト、太陽の影）、空のフレネル、
//              点光源、自発光（縁ほど強い）、泡
//   ハロー   : エッジのすぐ外の地面に出る柔らかい加算の光
// 出力は乗算済みアルファ（ONE / INV_SRC_ALPHA）。ハローはアルファ 0 で加算する。
//
// b0 = LightBuffer（太陽、カメラ、フォグ、影）、b1 = LiquidFrameCB、
// t0 = LiquidDef の表
// ============================================================
#include "../Common/Lighting.hlsli"
#include "../Common/LiquidCommon.hlsli"

cbuffer LiquidFrameCB : register(b1)
{
    float g_LTime;   // アニメーションの時計、秒
    float3 _liquidFramePad;
};

StructuredBuffer<LiquidDef> liquidDefs : register(t0);

float4 main(LiquidVSOut i) : SV_TARGET
{
    LiquidDef L = liquidDefs[i.ids.x];
    float R = i.info.x;
    float age = i.info.y;
    float left = i.info.z;
    uint seed = i.ids.y;

    // ---- 形 ----
    float2 g;
    // グリッドは正方形。大きさを決めた元の円の外には何も描かない
    float reachOut = R * L.fill + max(L.haloWidth, 0.0) + 0.05;
    if (dot(i.local, i.local) > reachOut * reachOut)
        discard;
    float d = LiquidSDF(L, i.local, seed, R, age, left, g_LTime, g);
    float px = max(fwidth(d), 1e-4);
    float cover = saturate(0.5 - d / px) * i.mask;
    float halo = (d > 0.0) ? (1.0 - smoothstep(0.0, max(L.haloWidth, 1e-3), d)) * L.haloGain * i.mask : 0.0;
    if (cover <= 0.002 && halo <= 0.002)
        discard;

    float inside = -d;
    float deep = smoothstep(0.0, max(L.depthWidth, 1e-3), inside);
    float rim = 1.0 - smoothstep(0.0, max(L.rimWidth, 1e-3), inside);
    // トゥーン（2026-10-04、シーンの toonParams に従う）：なめらかな傾斜の代わりにくっきりした段
    bool toon = toonParams.x > 0.5;
    if (toon)
    {
        float dw = max(L.depthWidth, 1e-3);
        deep = smoothstep(dw * 0.5 - px, dw * 0.5 + px, inside);
        float rw0 = max(L.rimWidth, 1e-3);
        rim = 1.0 - smoothstep(rw0 * 0.6 - px, rw0 * 0.6 + px, inside);
    }

    // ---- 表面の法線 ----
    // 高さ h(d) = bump * S(-d / depthWidth)、S = smoothstep  ->  grad h = -slope * grad d
    // N = normalize(-dh/dx, 1, -dh/dz) = normalize(slope * grad d, 1)
    float2 fwd = i.frame.xy;
    float2 lft = i.frame.zw;
    float x = saturate(inside / max(L.depthWidth, 1e-3));
    float slope = L.bump * 6.0 * x * (1.0 - x) / max(L.depthWidth, 1e-3);
    float2 gw = g.x * fwd + g.y * lft;   // ローカル -> ワールド xz
    // ゆっくりした渦模様：明るさと小さな波打ち（ノイズの差分）
    // 渦（2026-10-24、砂の守護者の流砂）：ノイズを中心からの距離に応じてねじり、
    // 全体を回して渦の腕を巻かせる。前から左へ回る = 風源の渦と同じ向き
    float2 q = i.local;
    float2 drift = float2(g_LTime * 0.15, -g_LTime * 0.11);
    if (L.flags != 0u)
    {
        float vSpin = (float) (L.flags & 0xFFFFu) * 0.01;
        float vTwist = (float) (L.flags >> 16) * 0.01;
        float va = vTwist * length(q) - vSpin * g_LTime;
        float vs, vc;
        sincos(va, vs, vc);
        q = float2(q.x * vc - q.y * vs, q.x * vs + q.y * vc);
        drift = float2(0.0, 0.0);
    }
    float2 sp = q * L.swirlScale + drift;
    float n0 = LiquidNoise(sp);
    float nx = LiquidNoise(sp + float2(0.25, 0.0));
    float ny = LiquidNoise(sp + float2(0.0, 0.25));
    float2 ripple = (float2(n0 - nx, n0 - ny) * (2.0 * L.swirl * deep));
    float2 rw = ripple.x * fwd + ripple.y * lft;
    float3 N = normalize(float3(slope * gw.x + rw.x, 1.0, slope * gw.y + rw.y));

    // ---- 光 ----
    float3 V = normalize(cameraPosition - i.world);
    float3 Ld = normalize(-dirLight.direction);
    float NdotL = saturate(dot(N, Ld));
    uint cascade;
    float shadow = SunShadow(i.world, float3(0.0, 1.0, 0.0), NdotL, cascade);
    float lit = toon ? ToonBand(NdotL * shadow) : NdotL * shadow;
    float3 sun = dirLight.color * dirLight.intensity * lit * (toon ? toonShadowTint.w : 1.0);

    // トゥーン：渦模様は 2 段のまだらにする
    float swirlN = toon ? smoothstep(0.5 - fwidth(n0), 0.5 + fwidth(n0), n0) : n0;
    float3 albedo = lerp(L.edgeColor.rgb, L.deepColor.rgb, deep) * (1.0 + L.swirl * (swirlN - 0.5) * 2.0);
    const float3 F0 = float3(0.02, 0.02, 0.02);   // 水に近い値
    float3 spec;
    float fres;
    if (toon)
    {
        // 小さく硬いハイライトと、浅い角度でくっきり映る空の帯
        float3 H = normalize(V + Ld);
        spec = dirLight.color * (dirLight.intensity * L.specGain * 0.6 * lit)
             * smoothstep(0.990, 0.995, saturate(dot(N, H)));
        fres = smoothstep(0.74, 0.76, 1.0 - saturate(dot(N, V))) * 0.5;
    }
    else
    {
        spec = SpecularGGX(N, V, Ld, max(L.roughness, 0.04), F0) * sun * L.specGain;
        fres = pow(1.0 - saturate(dot(N, V)), 5.0);
    }
    float3 sky = L.skyColor.rgb * (fres * L.skyColor.a);
    // pointGain 0 なら光源のループを飛ばす：プールはそれぞれ自分の光源を持つので、N 個
    // 重なると各プールの各ピクセルが N 個の光源を回っていた（2026-10-05、毒沼）
    float3 pd = float3(0.0, 0.0, 0.0);
    float3 ps = float3(0.0, 0.0, 0.0);
    if (L.pointGain > 0.0)
        PointLightShade(i.world, N, V, max(L.roughness, 0.04), F0, pd, ps);
    // 液体のプールはたいていすぐ上に自分の光源を持つ（毒：0.5m）。そのままの
    // 強さだと液面のハイライトがブルームのしきい値を大きく超えていた
    pd *= L.pointGain;
    ps *= L.pointGain;
    // 液体は拡散がほとんど見えない。見えるのはほぼ反射と自発光
    float3 ambient = toon ? AmbientAt(N) * lerp(toonShadowTint.rgb, float3(1, 1, 1), lit) : AmbientAt(N);
    float3 diffuse = albedo * (ambient + sun + pd) * 0.6;
    float3 glow = L.glowColor.rgb * (1.0 + L.glowColor.a * rim);
    float bub = LiquidBubbles(L, i.local, seed, deep, g_LTime) * L.bubbleGain;

    float3 c = diffuse + spec + ps + sky + glow + bub * (L.edgeColor.rgb + 0.3);
    // トゥーン：エッジのすぐ内側に暗いインクの線（幅は最低 1.5 px）
    if (toon)
    {
        float lineW = max(0.035, px * 1.5);
        c *= lerp(0.25, 1.0, smoothstep(lineW - px, lineW + px, inside));
    }
    c = ApplyFog(c, i.world);
    float alpha = cover * lerp(L.edgeColor.a, L.deepColor.a, deep);

    // ハロー：自発光だけで、下にあるものに加算する（アルファ 0）。加算する光は
    // フォグの色へ寄せるのではなくフォグで暗くする（寄せると地面が明るくなってしまう）
    float fogF = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), distance(i.world, cameraPosition)) * fogMax;
    float3 haloRgb = L.glowColor.rgb * (1.0 + L.glowColor.a) * halo * (1.0 - fogF) * (1.0 - cover);
    return float4(c * alpha + haloRgb, alpha);
}
