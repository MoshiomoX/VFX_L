// ============================================================
// SSAOPS.hlsl
// シーンの深度から求めるスクリーン空間アンビエントオクルージョン（2026-11-02）。
// 全画面三角形（Sky/SkyVS）でトゥーンのアウトラインの直後に描くので、
// 深度には地形 / 置物 / プレイヤー / GPU の敵が入っていて、草は
// 入っていない（草は結果を受け取るだけ）。出力：R8、1 = 開けている、
// 0 = 完全に遮蔽。SSAOBlurPS でならし、「次の」フレームの
// シェーディングが読む（Lighting.hlsli の SceneAO：フォワードレンダリングは
// ジオメトリのパス中にシェーディングするので、このフレームの AO は
// 間に合わない。静止した世界は保存した viewProj で正確に再投影でき、
// 動くものは 1 フレーム遅れる）。
//
// トゥーンでの使い方（ユーザーが選んだ、2026-11-02）：遮蔽で色を
// 暗くするのではなく、2 段の明暗の境目（ShadeToon）を押し動かす。
// これで溝や接地部分が影の段に入る。
//
// ここでのカメラ空間：x 右、y 上、z = カメラの「前方」への距離
// （正）。MSAA 深度（サンプル 0）と射影から復元する
// （eye = depthB / (d - depthA)、x = ndc.x * eye / proj11）。
// 法線は深度の隣接点から（各軸で差の小さい側を取るので、
// シルエットがにじまない）。12 点の半球カーネルはワールドの 5cm セルごとの
// ハッシュの角度で回す（下の「カーネルの回転」）。その模様はブラーで消える。
// ============================================================

Texture2DMS<float> g_Depth : register(t0);

cbuffer SSAOCB : register(b0)
{
    float depthA;       // 目からの深度 = depthB / (d - depthA)
    float depthB;
    float proj11;       // 射影の _11, _22：ndc = cam.xy * proj / cam.z
    float proj22;
    float radius;       // ワールドの m
    float bias;         // m（+ 深度に比例する分）
    float strength;     // 0..2
    float maxDistance;  // m：これより先は AO なし（遠くの地面は計算するだけ無駄）
    row_major float4x4 g_InvView;   // ビュー -> ワールド（行ベクトル）：カーネルの回転をワールドに固定する
    float pixelScale;   // 出力の 1 画素 = 深度の何画素（縦横半分で作る時 = 2、2026-11-05）
    float3 ssaoPad;
};

static const uint SSAO_SAMPLES = 12u;
// 半球（z >= 0）、中心ほど密（ループ内で半径に (i/n)^2 を掛ける）
static const float3 kKernel[SSAO_SAMPLES] =
{
    float3( 0.53,  0.11, 0.84), float3(-0.21,  0.61, 0.76), float3(-0.66, -0.28, 0.69),
    float3( 0.31, -0.74, 0.60), float3( 0.84,  0.47, 0.27), float3(-0.57,  0.75, 0.33),
    float3(-0.12, -0.33, 0.94), float3( 0.08,  0.92, 0.38), float3(-0.89,  0.07, 0.45),
    float3( 0.46, -0.41, 0.79), float3( 0.72, -0.08, 0.69), float3(-0.35, -0.86, 0.37),
};

float EyeDepth(int2 p, int2 size)
{
    p = clamp(p, int2(0, 0), size - 1);
    float d = g_Depth.Load(p, 0);
    return depthB / (d - depthA);
}

// ピクセル p（中心）の、目からの深度 z でのカメラ空間の位置
float3 CamPos(int2 p, int2 size, float z)
{
    float2 ndc = (float2(p) + 0.5) / float2(size) * float2(2.0, -2.0) + float2(-1.0, 1.0);
    return float3(ndc.x * z / proj11, ndc.y * z / proj22, z);
}

float4 main(float4 pos : SV_POSITION) : SV_Target
{
    int2 size;
    int samples;
    g_Depth.GetDimensions(size.x, size.y, samples);
    int2 p = int2(pos.xy) * (int)pixelScale;   // 半分の時は 2x2 の左上の深度を代表にする（法線の隣は深度の 1 画素隣のまま）

    float zc = EyeDepth(p, size);
    if (zc > maxDistance || zc <= 0.0)
        return float4(1.0, zc, 0.0, 1.0);   // y = 目からの深度：シェーディング側が AO を信用する前に確かめる（SceneAO）
    float3 P = CamPos(p, size, zc);

    // ---- 隣接点からの法線：各軸で深度の差が小さい側を取る ----
    float3 px0 = CamPos(p - int2(1, 0), size, EyeDepth(p - int2(1, 0), size));
    float3 px1 = CamPos(p + int2(1, 0), size, EyeDepth(p + int2(1, 0), size));
    float3 py0 = CamPos(p - int2(0, 1), size, EyeDepth(p - int2(0, 1), size));
    float3 py1 = CamPos(p + int2(0, 1), size, EyeDepth(p + int2(0, 1), size));
    float3 dx = (abs(px1.z - zc) < abs(px0.z - zc)) ? (px1 - P) : (P - px0);
    float3 dy = (abs(py1.z - zc) < abs(py0.z - zc)) ? (py1 - P) : (P - py0);
    float3 N = normalize(cross(dy, dx));
    if (N.z > 0.0) N = -N;   // カメラの方を向かせる（ここではカメラは +z を見ている）

    // ---- カーネルの回転は、ピクセルではなく「ワールド」（5 cm のセル）に固定する：カメラが動いても
    //      模様が面の上を滑らずその場に留まる（ユーザーが崖の縁で見たちらつき） ----
    float3 worldP = mul(float4(P.x, P.y, -P.z, 1.0), g_InvView).xyz;   // ビュー空間は -z を見ている
    float3 cell = floor(worldP * 20.0);
    float angle = 6.2831853 * frac(sin(dot(cell, float3(12.9898, 78.233, 37.719))) * 43758.5453);
    float3 rvec = float3(cos(angle), sin(angle), 0.0);
    float3 T = normalize(rvec - N * dot(rvec, N));
    float3 B = cross(N, T);

    float occ = 0.0;
    [unroll]
    for (uint i = 0u; i < SSAO_SAMPLES; ++i)
    {
        float scale = (float)(i + 1u) / (float)SSAO_SAMPLES;
        scale = lerp(0.15, 1.0, scale * scale);
        float3 k = kKernel[i] * scale;
        float3 S = P + (T * k.x + B * k.y + N * k.z) * radius;
        if (S.z <= 0.05) continue;

        // 画面へ戻す
        float2 ndc = float2(S.x * proj11, S.y * proj22) / S.z;
        float2 uv = float2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
        if (any(uv < 0.0) || any(uv > 1.0)) continue;
        float zs = EyeDepth(int2(uv * float2(size)), size);

        // そこの面がサンプル点より手前にある = サンプル点が何かの中に入っている
        float dz = S.z - zs;
        float b = bias + 0.002 * zc;
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(zc - zs), 1e-4));
        occ += (dz > b ? 1.0 : 0.0) * rangeCheck;
    }
    float ao = 1.0 - strength * occ / (float)SSAO_SAMPLES;
    return float4(saturate(ao), zc, 0.0, 1.0);   // y = 目からの深度（再投影が別のものに当たった場合、SceneAO が捨てる）
}
