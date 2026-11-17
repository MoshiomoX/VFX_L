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
// 新しいライト項はここ（または下で include する Lighting*.hlsli）に足せば、
// 各モデルの PS に手を入れずに全モデルに反映される。
//   LightingFog.hlsli    距離フォグ、雲の影
//   LightingShadow.hlsli 太陽の影（カスケード 3 段）
//   LightingPBR.hlsli    GGX と平行光源の PBR
//   LightingToon.hlsli   トゥーンシェーディング
// ============================================================
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#ifndef MODEL_LIGHT_CB_REG
#define MODEL_LIGHT_CB_REG b0
#endif

#include "PointLights.hlsli"
#include "LightClusters.hlsli"   // Forward+: ピクセルのクラスタに届くライト（t10 / t11）
#include "MathConst.hlsli"

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
    // トゥーンシェーディング（ShadeToon）。toonParams.x = 0 = 上のリアル寄りのシェーディング
    float4 toonParams;          // x = オン、y = 明暗の境目（N.L x 影）、z = 境目のぼかし、w = 硬いハイライトの強さ
    float4 toonShadowTint;      // rgb = 暗い側の環境光に掛ける色（寒色）、w = 明るい側の太陽の倍率
    float4 toonRim;             // rgb = リムの色、w = 強さ（カメラに近い横向きの面）
    // スクリーンスペース環境遮蔽（Graphics/PostProcess/AmbientOcclusion、SceneAO で読む）。
    // AO は前フレームに作ったもの: ワールド座標をそのフレームのビュー x 射影で投影する
    row_major float4x4 aoViewProj;
    float4 aoParams;            // x = オン、y = トゥーン: 完全に遮蔽されたとき明暗の境目（N.L）をどれだけ押すか、
                                // z = 環境光に掛ける割合、w = 1 = AO をグレーで表示（調整用）
    // 雲の影（CloudSun）。cloudParams.x = 0 = 無し
    float4 cloudParams;         // x = 陰りの強さ、y = 1 / 雲の大きさ（1/m）、z = 覆う割合、w = 縁のぼかし
    float4 cloudOffset;         // xy = 風で流れた距離（m）
};

// 前フレームのスクリーンスペース環境遮蔽（R8、1 = 遮蔽なし。Graphics/PostProcess/AmbientOcclusion が
// フレームに一度バインドする）。バインドするのは戦闘シーンだけで、他では aoParams.x は 0
Texture2D<float2> g_SceneAO : register(t12);   // x = ao、y = 前フレームのそのピクセルの視点からの深度

// ワールド座標での遮蔽量。オフ、画面外、未作成、または前フレームのそのピクセルが別のものだった場所では 1
// （カメラが動いてこの面がそのとき隠れていた場合。そうしないと手前の物体の AO が
// 1 フレームだけちらついて乗る）
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

#include "LightingFog.hlsli"
#include "LightingShadow.hlsli"
#include "LightingPBR.hlsli"

// ------------------------------------------------------------
// 点光源（PointLights.hlsli のリスト）: 拡散 + GGX 鏡面反射を
// 1 つのループで。
//   diffuse  : radiance * N.L の合計   （アルベドは呼び出し側で掛ける）
//   specular : GGX ローブ * radiance * N.L の合計   （そのまま最終値）
//   diffuseF : 同じ拡散から各ライトの (1 - F) を差し引いたもの
//              （エネルギー保存: 鏡面で反射した分は拡散には回らない）。
//              読むのは ShadePBR だけで、他の呼び出し側は
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

#include "LightingToon.hlsli"

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
    float shadow = SunShadow(worldPos, N, NdotL, cascade) * CloudSun(worldPos);
    float3 sun = dirLight.color * (dirLight.intensity * NdotL * shadow);
    float3 sunSpec = SpecularGGX(N, V, L, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0) * sun;

    float3 diff, spec;
    PointLightShade(worldPos, N, V, LAMBERT_SPEC_ROUGHNESS, DIELECTRIC_F0, diff, spec);

    // この Lambert には 1/PI がない（ライトの強さに含めている）ので、
    // 拡散との比を保つため鏡面ローブにも同じ PI を掛ける
    float ao = SceneAO(worldPos);   // リアル寄り: 環境光だけに掛ける（SSAO）
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
    float sceneAo = SceneAO(worldPos);   // リアル寄り: 環境光だけに掛ける（SSAO）
    ao *= lerp(1.0, sceneAo, aoParams.z);
    uint cascade;
    float shadow = SunShadow(worldPos, N, max(dot(N, normalize(-dirLight.direction)), 0.0), cascade) * CloudSun(worldPos);
    float3 base = ShadePBRSun(N, V, albedo, metallic, roughness, ao, shadow);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 diff, spec, diffF;
    PointLightShade(worldPos, N, V, roughness, F0, diff, spec, diffF);
    // 各ライトの (1 - F) は ShadePBRSun の太陽の kD と同様にもう diffF に入っている
    float3 kD = (1.0 - metallic) * albedo / PI;
    float3 c = ApplyFog(ShadowCascadeTint(base + diffF * kD + spec, cascade), worldPos);
    return (aoParams.w > 0.5) ? sceneAo.xxx : c;
}

#endif
