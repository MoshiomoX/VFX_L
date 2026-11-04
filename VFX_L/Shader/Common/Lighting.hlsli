// ============================================================
// Lighting.hlsli
// ライトの定数バッファ（PS b0、C++ の LightBuffer と一致）と、
// 全モデルの PS が呼ぶシェーディング関数。
//
// どの関数も線形 HDR の放射輝度を返す。トーンマップもガンマも
// ここではかけない（CompositePS で一度だけかける）。
//
// ライトモデル（Unity のデフォルトのシーンライトに近い）:
//   半球環境光         : 上向きの面は空の色、下向きの面は地面の色。
//                        SetAmbientColor は両方に同じ色を入れる
//                        （= 一様な環境光）
//   平行光源           : 拡散 + GGX 鏡面反射
//   点光源             : 距離減衰、拡散 + GGX 鏡面反射
//
// 新しいライト項はここに足せば、各モデルの PS に手を入れずに
// 全モデルに反映される。
// ============================================================
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#ifndef MODEL_LIGHT_CB_REG
#define MODEL_LIGHT_CB_REG b0
#endif

#include "PointLights.hlsli"
#include "LightClusters.hlsli"   // Forward+: ピクセルのクラスタに届くライト（t10 / t11）

struct DirectionalLight
{
    float3 direction;
    float padding1;
    float3 color;
    float intensity;
};

cbuffer LightBuffer : register(MODEL_LIGHT_CB_REG)
{
    DirectionalLight dirLight;
    float3 ambientColor;        // 半球の上側（空）
    float padding;
    float3 cameraPosition;
    float padding2;
    float3 groundAmbientColor;  // 半球の下側（地面）
    float padding3;
    float3 fogColor;            // 距離フォグ（ApplyFog）。線形 HDR、= 空の地平線の色
    float fogStart;             // かかり始める、カメラからの距離（m）
    float fogEnd;               // fogMax に達する距離（m）
    float fogMax;               // 0 = フォグなし（戦闘シーン以外の既定値）
    float albedoSrgb;           // 1 = アルベドテクスチャが sRGB: シェーディング前にデコードする（DecodeAlbedo）
    float fogPad;
    // 太陽の影、カスケード 3 段（Graphics/Light/ShadowMap）。shadowSplits.w = 0 = オフ
    row_major float4x4 shadowViewProj[3]; // カスケードごとのライトビュー * 正射影
    float4 shadowSplits;        // xyz = 各カスケードが受け持つカメラからの距離、w = オン
    float4 shadowTexelWorld;    // xyz = カスケードごとのシャドウマップ 1 テクセルの大きさ（m）
    float4 shadowParams;        // x = 1 / マップサイズ、y = 法線方向のオフセット（テクセル）、z = 強さ、w = PCF 半径（テクセル）
    float4 shadowParams2;       // x = フェード開始（最後の分割に対する割合）、y = 深度バイアス、z = 1 = カスケードを色分け
    // トゥーンシェーディング（2026-10-04、ShadeToon）。toonParams.x = 0 = 上のリアル寄りのシェーディング
    float4 toonParams;          // x = オン、y = 明暗の境目（N.L x 影）、z = 境目のぼかし、w = 硬いハイライトの強さ
    float4 toonShadowTint;      // rgb = 暗い側の環境光に掛ける色（寒色）、w = 明るい側の太陽の倍率
    float4 toonRim;             // rgb = リムの色、w = 強さ（カメラに近い横向きの面）
    // スクリーンスペース環境遮蔽（2026-11-02、Graphics/PostProcess/AmbientOcclusion、SceneAO で読む）。
    // AO は前フレームに作ったもの: ワールド座標をそのフレームのビュー x 射影で投影する
    row_major float4x4 aoViewProj;
    float4 aoParams;            // x = オン、y = トゥーン: 完全に遮蔽されたとき明暗の境目（N.L）をどれだけ押すか、
                                // z = 環境光に掛ける割合、w = 1 = AO をグレーで表示（調整用）
};

// 全カスケードの深度（1 段ごとに配列の 1 スライス）と比較サンプラー
// （LESS_EQUAL、境界外 = 日なた）。ShadowMap がフレームに一度バインドする。
// それ以外の場所ではこのレジスタは使わない
Texture2DArray g_ShadowMap : register(t9);
SamplerComparisonState g_ShadowSampler : register(s2);

// 前フレームのスクリーンスペース環境遮蔽（R8、1 = 遮蔽なし。Graphics/PostProcess/AmbientOcclusion が
// フレームに一度バインドする、2026-11-02）。バインドするのは戦闘シーンだけで、他では aoParams.x は 0
Texture2D<float2> g_SceneAO : register(t12);   // x = ao、y = 前フレームのそのピクセルの視点からの深度

// ワールド座標での遮蔽量。オフ、画面外、未作成、または前フレームのそのピクセルが別のものだった場所では 1
// （カメラが動いてこの面がそのとき隠れていた場合。そうしないと手前の物体の AO が
// 1 フレームだけちらついて乗る、2026-11-02）
float SceneAO(float3 worldPos)
{
    float ao = 1.0;
    if (aoParams.x > 0.5)
    {
        float4 q = mul(float4(worldPos, 1.0), aoViewProj);
        if (q.w > 0.0)
        {
            float2 uv = float2(0.5 + 0.5 * q.x / q.w, 0.5 - 0.5 * q.y / q.w);
            if (all(uv >= 0.0) && all(uv <= 1.0))
            {
                uint w, h;
                g_SceneAO.GetDimensions(w, h);
                float2 s = g_SceneAO.Load(int3(int2(uv * float2(w, h)), 0));
                // q.w = そのフレームでのこの点の視点からの深度（クリップの w = 視点からの距離）
                if (abs(s.y - q.w) < 0.04 * q.w + 0.15)
                    ao = s.x;
            }
        }
    }
    return ao;
}

// ------------------------------------------------------------
// アルベドテクスチャは UNORM で読むので、sRGB の画像はガンマが
// かかったまま来る。albedoSrgb がオンならライティングの前に線形へ
// 戻す（最後に CompositePS が 1/2.2 をかけ直す）。
// オフだと以前の白っぽい見た目のまま
// ------------------------------------------------------------
float3 DecodeAlbedo(float3 c)
{
    return (albedoSrgb > 0.5) ? pow(max(c, 0.0), 2.2) : c;
}

static const float PI = 3.14159265359;

// Lambert のマテリアルは粗さ / 金属度を持たない。ハイライトは
// ただの誘電体（F0 0.04）として、この粗さで扱う
// （Unity Standard の既定の滑らかさ 0.5 くらい）
static const float LAMBERT_SPEC_ROUGHNESS = 0.45;
static const float3 DIELECTRIC_F0 = float3(0.04, 0.04, 0.04);

// ------------------------------------------------------------
// 法線マップ（OpenGL 形式、DX 用に G を反転）-> ワールド法線
// ------------------------------------------------------------
// 使える接線がない（0 = メッシュに接線がない、または法線と平行）ときは、NaN ではなく
// 法線をそのまま返す。出口は 1 つ（途中で return すると fxc が X4000 を出す）
float3 PerturbNormal(float3 N, float3 T, float3 normalMapSample)
{
    float3 nm = normalMapSample * 2.0 - 1.0;
    nm.y = -nm.y;
    N = normalize(N);
    float3 t = T - dot(T, N) * N;
    bool hasTangent = dot(t, t) > 1e-8;   // NaN のときも false
    T = normalize(hasTangent ? t : float3(1.0, 0.0, 0.0));
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 perturbed = normalize(mul(nm, TBN));
    return hasTangent ? perturbed : N;
}

// ------------------------------------------------------------
// 距離フォグ: fogStart から fogEnd の間でライティング後の色 -> fogColor
// （smoothstep、上限 fogMax）。遠くの地形が空に溶け込む。
// 下のワールド座標付きシェーディング関数の最後でかける
// ------------------------------------------------------------
float3 ApplyFog(float3 color, float3 worldPos)
{
    float d = distance(worldPos, cameraPosition);
    float f = smoothstep(fogStart, max(fogEnd, fogStart + 1e-3), d) * fogMax;
    return lerp(color, fogColor, f);
}

// ------------------------------------------------------------
// 太陽の影: 1 = 日なた、0 = 完全に影（強さを掛ける前）。
// 点を含む最初のカスケードを使う（カスケード 0 が一番くっきり）。
// 面が自分自身に影を落とさないよう、点を法線方向に数テクセル押し出し
// （太陽が面をかすめるほど多めに）、(2r+1)^2 回のバイリニア比較タップで
// PCF をかける。最後のカスケードの終わりに向けてフェードアウトする。
//
// cascadeOut = 使ったカスケード、3 = なし（デバッグの色分け用）
// ------------------------------------------------------------
float SunShadow(float3 worldPos, float3 N, float NdotL, out uint cascadeOut)
{
    cascadeOut = 3u;
    float vis = 1.0;
    if (shadowSplits.w > 0.5 && NdotL > 0.0)
    {
        float4 lp = float4(0, 0, 0, 1);
        [unroll]
        for (uint c = 0u; c < 3u; ++c)
        {
            if (cascadeOut == 3u)
            {
                float3 p = worldPos + N * (shadowTexelWorld[c] * shadowParams.y * (2.0 - NdotL));
                float4 q = mul(float4(p, 1.0), shadowViewProj[c]);
                if (all(abs(q.xy) < 0.98) && q.z > 0.0 && q.z < 1.0)
                {
                    cascadeOut = c;
                    lp = q;
                }
            }
        }
        if (cascadeOut < 3u)
        {
            float2 uv = lp.xy * float2(0.5, -0.5) + 0.5;
            float depth = lp.z - shadowParams2.y;
            float texel = shadowParams.x;
            int r = (int) shadowParams.w;
            float sum = 0.0;
            float n = 0.0;
            for (int y = -r; y <= r; ++y)
            {
                for (int x = -r; x <= r; ++x)
                {
                    sum += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler,
                        float3(uv + float2(x, y) * texel, (float) cascadeOut), depth);
                    n += 1.0;
                }
            }
            vis = sum / max(n, 1.0);

            float dist = distance(worldPos, cameraPosition);
            float fadeStart = shadowSplits.z * shadowParams2.x;
            vis = lerp(vis, 1.0, saturate((dist - fadeStart) / max(shadowSplits.z - fadeStart, 1e-3)));
            vis = lerp(1.0, vis, shadowParams.z);
        }
    }
    return vis;
}

// デバッグ: カスケード 0/1/2 を赤/緑/青に色分け（shadowParams2.z = 1）
float3 ShadowCascadeTint(float3 color, uint cascade)
{
    float3 tint = float3(1, 1, 1);
    if (shadowParams2.z > 0.5 && cascade < 3u)
        tint = (cascade == 0u) ? float3(1.0, 0.55, 0.55) : (cascade == 1u) ? float3(0.55, 1.0, 0.55) : float3(0.55, 0.55, 1.0);
    return color * tint;
}

// ------------------------------------------------------------
// 半球環境光: 上からは空、下からは地面の色を、面がどれだけ
// 上を向いているかで混ぜる
// ------------------------------------------------------------
float3 AmbientAt(float3 N)
{
    return lerp(groundAmbientColor, ambientColor, saturate(N.y * 0.5 + 0.5));
}

// ------------------------------------------------------------
// 視線ベクトルを使わない Lambert（鏡面反射なし）:
// (環境光 + 平行光源) * アルベド
// ------------------------------------------------------------
float3 ShadeLambert(float3 N, float3 albedo)
{
    float3 L = normalize(-dirLight.direction);
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = dirLight.color * dirLight.intensity * NdotL;
    return (AmbientAt(N) + diffuse) * albedo;
}

// ------------------------------------------------------------
// Cook-Torrance GGX
// ------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 鏡面反射の BRDF だけ（ライトの色と N.L は呼び出し側で掛ける）
float3 SpecularGGX(float3 N, float3 V, float3 L, float roughness, float3 F0)
{
    float3 H = normalize(V + L);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    return (D * G * F) / max(denom, 1e-4);
}

// ルックアップテクスチャを使わない split-sum の環境 BRDF（Karis「Physically Based Shading on Mobile」、
// 元は Lazarov）: この角度と粗さで、面が環境光のうちどれだけを鏡面反射するか
float3 EnvBRDFApprox(float3 F0, float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// sunScale: 平行光源のうち面に届く割合（影）
float3 ShadePBRSun(float3 N, float3 V, float3 albedo,
                   float metallic, float roughness, float ao, float sunScale)
{
    float3 L = normalize(-dirLight.direction);
    float3 H = normalize(V + L);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = (NDF * G * F) / max(denom, 0.0001);

    float3 kD = (float3(1, 1, 1) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    float3 radiance = dirLight.color * (dirLight.intensity * sunScale);

    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // エネルギー保存した環境光（2026-11-02。以前はどの面も AmbientAt(N) * albedo * ao）:
    //   鏡面 = 反射方向に見える半球環境光 x 環境 BRDF
    //   拡散 = 鏡面が取らなかった残り。金属では 0
    // （IBL のキューブマップはない。半球の色をプリフィルタ済みの環境の代わりにする）
    float NdotV = max(dot(N, V), 1e-4);
    float3 envSpec = EnvBRDFApprox(F0, roughness, NdotV);
    float3 R = reflect(-V, N);
    float3 ambientSpec = AmbientAt(R) * envSpec;
    float3 ambientDiff = AmbientAt(N) * albedo * (float3(1, 1, 1) - envSpec) * (1.0 - metallic);
    float3 ambient = (ambientDiff + ambientSpec) * ao;
    return ambient + Lo;
}

float3 ShadePBR(float3 N, float3 V, float3 albedo,
                float metallic, float roughness, float ao)
{
    return ShadePBRSun(N, V, albedo, metallic, roughness, ao, 1.0);
}

// ------------------------------------------------------------
// 点光源（PointLights.hlsli のリスト）: 拡散 + GGX 鏡面反射を
// 1 つのループで。
//   diffuse  : radiance * N.L の合計   （アルベドは呼び出し側で掛ける）
//   specular : GGX ローブ * radiance * N.L の合計   （そのまま最終値）
//   diffuseF : 同じ拡散から各ライトの (1 - F) を差し引いたもの
//              （エネルギー保存: 鏡面で反射した分は拡散には回らない、
//              2026-11-02）。読むのは ShadePBR だけで、他の呼び出し側は
//              これのないオーバーロードを使い、使われない合計は fxc が
//              削る
// このフレームでクラスタを作ってあれば（LightClusters.hlsli、Forward+）
// 球がピクセルのクラスタに触れるライトだけを回る。
// そうでなければ（エディタのシーン、オフ時）以前どおりリスト全部。
// ------------------------------------------------------------
void PointLightAccum(PointLight l, float3 worldPos, float3 N, float3 V,
                     float roughness, float3 F0,
                     inout float3 diffuse, inout float3 specular, inout float3 diffuseF)
{
    // ここでも PointLightShade でも途中で return しない: fxc がすべての呼び出し側で
    // 「未初期化の可能性」と報告する。範囲判定を先に、安く（距離の 2 乗で）
    // 済ませるので、範囲外のライトはそれ以上何もしない
    float3 d = l.position - worldPos;
    float dist2 = dot(d, d);
    if (dist2 < l.radius * l.radius)
    {
        float dist = sqrt(dist2);
        float3 L = d / max(dist, 1e-4);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            float3 radiance = l.color * (l.intensity * PointLightAttenuation(dist, l.radius));
            diffuse += radiance * NdotL;
            specular += SpecularGGX(N, V, L, roughness, F0) * radiance * NdotL;
            float3 F = FresnelSchlick(max(dot(normalize(V + L), V), 0.0), F0);
            diffuseF += (float3(1, 1, 1) - F) * radiance * NdotL;
        }
    }
}

void PointLightShade(float3 worldPos, float3 N, float3 V,
                     float roughness, float3 F0,
                     out float3 diffuse, out float3 specular, out float3 diffuseF)
{
    diffuse = float3(0, 0, 0);
    specular = float3(0, 0, 0);
    diffuseF = float3(0, 0, 0);

    uint count = min(g_PointLightCount[0], MAX_POINT_LIGHTS);
    LightClusterInfo info = g_LightClusterInfo[0];
    if (count > 0u && info.enabled != 0u)
    {
        uint base = LightClusterOf(info, worldPos) * LIGHT_CLUSTER_WORDS;
        uint words = (count + 31u) >> 5;
        uint visited = 0u;
        [loop]
        for (uint w = 0u; w < words; ++w)
        {
            uint m = g_LightClusterMasks[base + w];
            [loop]
            while (m != 0u)
            {
                uint j = firstbitlow(m);
                m &= m - 1u;
                PointLightAccum(g_PointLights[w * 32u + j], worldPos, N, V, roughness, F0, diffuse, specular, diffuseF);
                ++visited;
            }
        }
        if (info.debugView != 0u)
            specular += LightClusterHeat(visited);   // specular にはアルベドを掛けないのでここに足す
    }
    else
    {
        for (uint i = 0u; i < count; ++i)
            PointLightAccum(g_PointLights[i], worldPos, N, V, roughness, F0, diffuse, specular, diffuseF);
    }
}

// フレネルで重み付けした拡散なし版（Lambert、トゥーン、液面: 以前の見た目のまま）
void PointLightShade(float3 worldPos, float3 N, float3 V,
                     float roughness, float3 F0,
                     out float3 diffuse, out float3 specular)
{
    float3 unused;
    PointLightShade(worldPos, N, V, roughness, F0, diffuse, specular, unused);
}

// ------------------------------------------------------------
// トゥーンシェーディング（2026-10-04、toonParams.x = 1）。N.L の代わりに、
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
// 同じ段で、遮蔽量に応じて境目を明るい側へ押したもの（SSAO、2026-11-02）:
// くぼみや接地部分は上にグレーのグラデーションが乗るのではなく、暗い段に落ちる
float ToonBandAO(float x, float ao)
{
    // 不感帯: わずかな遮蔽（ノイズの底、平らな崖の面）では境目を動かさない。N.L が境目付近にある
    // 大きな面がフレームごとに丸ごと反転しないように（2026-11-02、ユーザー: 被弾時に崖の壁が
    // 明 / 暗で切り替わる）。はっきりしたくぼみと接地部分だけが押す
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

    float3 sun = dirLight.color * (dirLight.intensity * toonShadowTint.w * lit);
    float3 ambient = AmbientAt(N) * lerp(toonShadowTint.rgb, float3(1, 1, 1), lit) * lerp(1.0, ao, aoParams.z);

    float3 H = normalize(V + L);
    // 狭い窓: ローポリの平らな面は法線が H に一致すると面全体が光る。
    // カメラの近くだけ（リムと同じ）: そうしないと遠くの岩壁がプラスチックのようにきらつく
    float nearCam = 1.0 - smoothstep(18.0, 32.0, distance(worldPos, cameraPosition));
    float spot = smoothstep(0.994, 0.997, saturate(dot(N, H))) * lit * toonParams.w * specScale * nearCam;

    float3 diff, spec;
    PointLightShade(worldPos, N, V, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0, diff, spec);

    float3 c = (ambient + sun + diff) * albedo
             + dirLight.color * (dirLight.intensity * spot)
             + spec * PI * 0.5
             + ToonRim(N, V, worldPos, lit);
    c = ApplyFog(ShadowCascadeTint(c, cascade), worldPos);
    return (aoParams.w > 0.5) ? ao.xxx : c;   // aoParams.w = AO の調整用表示（グレー）
}

// ------------------------------------------------------------
// Lambert + 平行光源のハイライト + 点光源（視線ベクトルと点光源の
// ためにワールド座標が要る）
// ------------------------------------------------------------
float3 ShadeLambert(float3 N, float3 albedo, float3 worldPos)
{
    float3 V = normalize(cameraPosition - worldPos);
    if (toonParams.x > 0.5)
        return ShadeToon(N, V, albedo, worldPos, 1.0);

    // 平行光源: 視線なし版と同じ拡散にハイライトを足し、
    // どちらも太陽の影で削る
    float3 L = normalize(-dirLight.direction);
    float NdotL = max(dot(N, L), 0.0);
    uint cascade;
    float shadow = SunShadow(worldPos, N, NdotL, cascade);
    float3 sun = dirLight.color * (dirLight.intensity * NdotL * shadow);
    float3 sunSpec = SpecularGGX(N, V, L, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0) * sun;

    float3 diff, spec;
    PointLightShade(worldPos, N, V, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0, diff, spec);

    // この Lambert には 1/PI がない（ライトの強さに含めている）ので、
    // 拡散との比を保つため鏡面ローブにも同じ PI を掛ける
    float ao = SceneAO(worldPos);   // リアル寄り: 環境光だけに掛ける（SSAO、2026-11-02）
    float3 lit = (AmbientAt(N) * lerp(1.0, ao, aoParams.z) + sun + diff) * albedo + (sunSpec + spec) * PI;
    lit = ApplyFog(ShadowCascadeTint(lit, cascade), worldPos);
    return (aoParams.w > 0.5) ? ao.xxx : lit;
}

// PBR + 点光源: 上の平行光源の項と同じ分け方
float3 ShadePBR(float3 N, float3 V, float3 albedo,
                float metallic, float roughness, float ao, float3 worldPos)
{
    // トゥーン: 金属度 / 粗さは硬いハイライトの強さにだけ効く（滑らか = 明るい光）
    if (toonParams.x > 0.5)
        return ShadeToon(N, V, albedo * ao, worldPos, saturate(1.2 - roughness));
    float sceneAo = SceneAO(worldPos);   // リアル寄り: 環境光だけに掛ける（SSAO、2026-11-02）
    ao *= lerp(1.0, sceneAo, aoParams.z);
    uint cascade;
    float shadow = SunShadow(worldPos, N, max(dot(N, normalize(-dirLight.direction)), 0.0), cascade);
    float3 base = ShadePBRSun(N, V, albedo, metallic, roughness, ao, shadow);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 diff, spec, diffF;
    PointLightShade(worldPos, N, V, roughness, F0, diff, spec, diffF);
    // 各ライトの (1 - F) は ShadePBRSun の太陽の kD と同様にもう diffF に入っている（2026-11-02。以前は diff * (1 - metallic)）
    float3 kD = (1.0 - metallic) * albedo / PI;
    float3 c = ApplyFog(ShadowCascadeTint(base + diffF * kD + spec, cascade), worldPos);
    return (aoParams.w > 0.5) ? sceneAo.xxx : c;
}

#endif
