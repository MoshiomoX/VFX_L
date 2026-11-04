// ============================================================
// LiquidCommon.hlsli
// Liquid エントリ（2026-10-02）：地面に溜まった液体の水たまり。
// 2 つの経路で同じように描く：
//   CPU : VFXLiquidEntry -> VFXLiquidRenderer -> VFXLiquidVS
//   GPU : レシピに Liquid エントリを持つ範囲 -> SwarmLiquidVS
// シェーディングはどちらも VFXLiquidPS 1 本。このファイルは両側に要るもの
// （見た目 LiquidDef、VS -> PS の構造体、液滴の配置、水たまりの
// 符号付き距離場）をまとめる。
//
// 形 = 符号付き距離場（m、内側が < 0）。液滴はそれぞれ楕円の SDF で、
// 多項式の smooth union で合成するので、近づいた液滴は円が重なるのではなく
// 柔らかいくびれでつながる。
// 値が距離なので、縁 / 深さ / ハローの幅は m で指定し、
// エッジは fwidth(d) でアンチエイリアスする。
//
// シミュレーションはしない：ある年齢での液滴の位置は (seed, 年齢, 残り時間) の
// 閉じた式で決まるので、各ピクセルが水たまり全体を組み直す。
//
// ローカル座標：x = 投げた方向（前）、y = その左。単位は m で、
// 水たまりの中心が原点。
//
// C++ 側の対応：VFXLiquidDef（VFX_Editor/VFXLiquidDef.h）、160 バイト
// ============================================================
#ifndef LIQUID_COMMON_HLSLI
#define LIQUID_COMMON_HLSLI

struct LiquidDef
{
    float4 deepColor;   // 深い所の液体の線形 rgb、a = その不透明度
    float4 edgeColor;   // 縁の線形 rgb、a = その不透明度
    float4 glowColor;   // rgb = 自発光、a = 縁がどれだけ余計に光るか
    float4 skyColor;    // rgb = 浅い角度で映り込む色、a = フレネルの強さ

    float radius;       // m。0 = 範囲の半径（GPU 経路）。CPU 経路では 2 になる
    float fill;         // 液体が届いてよい範囲（半径に対する割合）
    float spread;       // 飛沫が前方へどれだけ余計に伸びるか（0 = 円形）
    float wobble;       // 液滴の漂い（届く距離に対する割合）

    float blend;        // smooth union の距離（届く距離に対する割合）
    float rimWidth;     // m：エッジのすぐ内側の明るい帯
    float depthWidth;   // m：エッジ -> 最大深度まで（この帯で表面が曲がる）
    float bump;         // その曲がりの高さ m（エッジでの法線の傾き）

    float splashTime;   // 液滴を飛び散らせる秒数
    float dryTime;      // 最後の何秒で縮んで消えるか
    float specGain;     // 太陽のハイライト
    float roughness;    // GGX の粗さ

    float bubbles;      // 泡のセルのうち泡を持つ割合（深い所ほど多い）
    float bubbleGain;   // 泡の明るさ
    float swirl;        // ゆっくり動く表面ノイズの明るさ / 波打ち
    float swirlScale;   // 1/m

    float lift;         // 地面からの m（Z ファイティング対策）
    float cliff;        // m：中心よりこれだけ上 / 下の地面は切り落とす
    float haloWidth;    // m：エッジの外側、地面に出る柔らかい光
    float haloGain;

    uint lobes;         // 本体の周りのこぶ（0..7）
    uint spatter;       // 前方へ飛ぶ小さな飛沫（0..8）
    uint flags;         // 渦（流砂、2026-10-24）：下位 16 ビット = 回転 rad/s x 100、上位 16 ビット = ねじれ rad/m x 100（VFXLiquidDef::SetVortex）
    float pointGain;    // 点光源（拡散 + ハイライト）が液体にどれだけ効くか
};

static const uint LIQUID_MAX_DROPS = 16u;   // 本体 1 + こぶ 7 + 飛沫 8

// VS -> PS（VFXLiquidVS と SwarmLiquidVS の両方がこれを書く）
struct LiquidVSOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float2 local : TEXCOORD1;                 // m、x = 前、y = 左
    float mask : TEXCOORD2;                   // 中心と同じ地面で 1、崖で切られる所で 0
    nointerpolation float4 info : TEXCOORD3;  // x = 半径 m、y = 年齢 s、z = 残り時間 s
    nointerpolation float4 frame : TEXCOORD4; // xy = 前（ワールド xz）、zw = 左（ワールド xz）
    nointerpolation uint2 ids : TEXCOORD5;    // x = LiquidDef の番号、y = seed
};

// ---- グリッド：LIQUID_GRID x LIQUID_GRID 枚の四角形の正方形、各 6 頂点 ----
static const uint LIQUID_GRID = 16u;
static const uint LIQUID_VERTS = LIQUID_GRID * LIQUID_GRID * 6u;

// 頂点 ID -> 正方形の端から端までを -1..1
float2 LiquidGridCorner(uint vid)
{
    static const float2 kCorner[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    uint q = vid / 6u;
    float2 cell = float2((float) (q % LIQUID_GRID), (float) (q / LIQUID_GRID));
    return (cell + kCorner[vid % 6u]) / (float) LIQUID_GRID * 2.0 - 1.0;
}

uint LiquidHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// 0..1、(seed, 液滴, 何番目の値) ごとに 1 つ
float LiquidRand(uint seed, uint i, uint k)
{
    return (float) (LiquidHash(seed ^ (i * 0x9E3779B9u) ^ (k * 0x85EBCA6Bu)) & 0xFFFFFFu) / 16777216.0;
}

// ワールド xz での前 / 左。方向がゼロなら seed から角度を選ぶ
void LiquidFrame(float2 dir, uint seed, out float2 fwd, out float2 left)
{
    float lenSq = dot(dir, dir);
    if (lenSq > 1e-6)
        fwd = dir * rsqrt(lenSq);
    else
    {
        float a = LiquidRand(seed, 99u, 0u) * 6.2831853;
        fwd = float2(cos(a), sin(a));
    }
    left = float2(-fwd.y, fwd.x);
}

float LiquidEaseOutBack(float t)
{
    const float c1 = 1.4;
    float u = t - 1.0;
    return 1.0 + (c1 + 1.0) * u * u * u + c1 * u * u;
}

struct LiquidDrop
{
    float2 c;       // 中心、ローカル m
    float2 axis;    // 飛ばされた向き（単位ベクトル、ローカル）
    float r;        // 半幅、m
    float stretch;  // axis 方向の半長 / 半幅
};

// 半径 R、経過 age 秒、残り left 秒の水たまりの i 番目の液滴。
//   i = 0 : 本体       i = 1..lobes : こぶ      その後が飛沫
LiquidDrop LiquidDropAt(LiquidDef L, uint i, uint seed, float R, float age, float left, float time)
{
    float reach = R * L.fill;   // 液体はこの円の内側に収まる
    float r0 = LiquidRand(seed, i, 0u);
    float r1 = LiquidRand(seed, i, 1u);
    float r2 = LiquidRand(seed, i, 2u);
    float r3 = LiquidRand(seed, i, 3u);
    bool spatter = i > L.lobes;

    float ang, dist, rad, stretch;
    if (i == 0u)
    {
        // 本体：大きく、着弾点より少し前。
        // 渦の水たまり（flags != 0、砂の守護者の流砂。2026-10-24）は届く範囲いっぱいの丸い本体 1 つ
        bool vortexPool = (L.flags != 0u);
        ang = 0.0;
        dist = vortexPool ? 0.0 : 0.10 * reach;
        rad = (vortexPool ? 0.97 : 0.42) * reach;
        stretch = vortexPool ? 1.0 : 1.0 + 0.25 * L.spread;
    }
    else if (!spatter)
    {
        // こぶは全周に出る。前へ飛んだものほど遠く、小さく、細長くなる
        ang = (r0 * 2.0 - 1.0) * 3.14159265;
        float fwd = 0.5 + 0.5 * cos(ang);
        dist = reach * (0.22 + 0.28 * r1) * (1.0 + 0.8 * L.spread * fwd);
        rad = reach * (0.20 + 0.12 * r2) * (1.0 - 0.3 * fwd);
        stretch = 1.0 + 0.6 * L.spread * fwd;
    }
    else
    {
        // 飛沫：±50 度の円錐で前方へ飛ぶ小さな滴
        ang = (r0 * 2.0 - 1.0) * 0.9;
        dist = reach * (0.62 + 0.30 * r1);
        rad = reach * (0.05 + 0.06 * r2);
        stretch = 1.0 + 1.2 * L.spread;
    }
    // 伸ばした液滴が届く範囲からはみ出さないようにする
    dist = min(dist, max(reach - rad * stretch * 1.1, 0.0));
    float2 axis = float2(cos(ang), sin(ang));

    // ---- 飛散：少し行き過ぎながら飛び出し、小さな塊から元の大きさまで育つ ----
    float fling = LiquidEaseOutBack(saturate(age / max(L.splashTime, 1e-3)));
    float grow = 0.35 + 0.65 * saturate(age / max(L.splashTime * 1.4, 1e-3));
    // ---- 落ち着く：こぶは本体へ引き戻され（表面張力）、飛沫は細っていく ----
    float settle = smoothstep(L.splashTime, L.splashTime + 1.0, age);
    if (i >= 1u && !spatter)
        dist *= lerp(1.0, 0.86, settle);
    if (spatter)
        rad *= lerp(1.0, 0.7, settle);
    // ---- 生きている間：ゆっくり漂って脈打つ。液滴ごとに位相が違う ----
    float ph = r3 * 6.2831853;
    float2 drift = float2(sin(time * 1.3 + ph), cos(time * 1.1 + ph * 1.7)) * (L.wobble * reach * settle);
    float breathe = 1.0 + 0.06 * sin(time * 2.1 + ph * 2.3);
    // ---- 乾く：最後の dryTime で縮んで消える。飛沫が先 ----
    float dry = saturate(left / max(L.dryTime, 1e-3));
    float dryK = spatter ? dry * dry : sqrt(dry);

    LiquidDrop d;
    d.c = axis * (dist * fling) + drift;
    d.axis = axis;
    d.r = rad * grow * breathe * dryK;
    d.stretch = stretch;
    return d;
}

// 楕円の SDF（近似。Inigo Quilez の上界 k0 (k0 - 1) / k1）。半軸 ab、
// q は楕円自身の座標系。g = q を通る等値線の単位法線
// （(x/a)^2 + (y/b)^2 の勾配は 2 q / ab^2）
float LiquidEllipseSDF(float2 q, float2 ab, out float2 g)
{
    float2 qa = q / ab;
    float2 qb = q / (ab * ab);
    float k0 = length(qa);
    float k1 = max(length(qb), 1e-6);
    bool centre = k0 < 1e-4;   // ちょうど中心：方向が無いので、距離は短い方の半軸にする
    g = centre ? float2(0.0, 0.0) : qb / k1;
    return centre ? -min(ab.x, ab.y) : k0 * (k0 - 1.0) / k1;
}

// 多項式の smooth union（IQ）：h = clamp(0.5 + 0.5 (b - a) / k)、
// d = lerp(b, a, h) - k h (1 - h)。偏微分はちょうど h と 1 - h になるので、
// 勾配も同じ重みで混ぜる。
void LiquidSmoothUnion(inout float d, inout float2 g, float d2, float2 g2, float k)
{
    float h = saturate(0.5 + 0.5 * (d2 - d) / k);
    d = lerp(d2, d, h) - k * h * (1.0 - h);
    g = lerp(g2, g, h);
}

// ローカル点 p から水たまりのエッジまでの符号付き距離（m）、内側が < 0。
// grad = その勾配（ローカル、ほぼ単位長、外向き）
float LiquidSDF(LiquidDef L, float2 p, uint seed, float R, float age, float left, float time, out float2 grad)
{
    uint count = min(1u + min(L.lobes, 7u) + min(L.spatter, 8u), LIQUID_MAX_DROPS);
    float k = max(L.blend * R * L.fill, 1e-3);
    float d = 1e3;
    grad = float2(0.0, 0.0);

    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        LiquidDrop dr = LiquidDropAt(L, i, seed, R, age, left, time);
        if (dr.r <= 1e-4)
            continue;
        float2 v = p - dr.c;
        float2 perp = float2(-dr.axis.y, dr.axis.x);
        float2 q = float2(dot(v, dr.axis), dot(v, perp));
        float2 gq;
        float di = LiquidEllipseSDF(q, float2(dr.r * dr.stretch, dr.r), gq);
        float2 gi = gq.x * dr.axis + gq.y * perp;   // ローカル座標に戻す
        LiquidSmoothUnion(d, grad, di, gi, k);
    }
    return d;
}

// 滑らかなバリューノイズ 0..1（表面のゆっくりした渦模様）
float LiquidNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    uint2 q = asuint(int2(i));
    float a = (float) (LiquidHash(q.x * 73856093u ^ q.y * 19349663u) & 0xFFFFu) / 65535.0;
    float b = (float) (LiquidHash((q.x + 1u) * 73856093u ^ q.y * 19349663u) & 0xFFFFu) / 65535.0;
    float c = (float) (LiquidHash(q.x * 73856093u ^ (q.y + 1u) * 19349663u) & 0xFFFFu) / 65535.0;
    float d = (float) (LiquidHash((q.x + 1u) * 73856093u ^ (q.y + 1u) * 19349663u) & 0xFFFFu) / 65535.0;
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// 泡：水たまりを 0.32m のセルに分け、各セルは確率 bubbles x deep で泡を 1 つ持つ。
// 泡は自分の周期で膨らんで弾ける。
// 戻り値は泡の縁 + うっすらしたドームの明るさ。泡が無ければ 0
float LiquidBubbles(LiquidDef L, float2 p, uint seed, float deep, float time)
{
    const float cell = 0.32;
    float2 g = floor(p / cell);
    float2 f = p / cell - g;
    uint2 q = asuint(int2(g));
    uint h = LiquidHash(seed ^ (q.x * 73856093u) ^ (q.y * 19349663u));
    float have = ((float) (h & 0xFFFFu) / 65535.0 <= L.bubbles * deep) ? 1.0 : 0.0;   // このセルに泡がある
    float u = (float) ((h >> 16) & 0xFFu) / 255.0;
    float w = (float) ((h >> 24) & 0xFFu) / 255.0;
    float2 c = 0.25 + 0.5 * float2(u, w);
    float period = 1.2 + 1.3 * u;
    float ph = frac(time / period + w);
    float rb = (0.08 + 0.12 * w) * smoothstep(0.0, 0.85, ph);   // 半径（セル単位）
    float pop = 1.0 - smoothstep(0.85, 1.0, ph);
    float dist = length(f - c);
    float ring = 1.0 - smoothstep(0.0, 0.035, abs(dist - rb));
    float dome = (1.0 - smoothstep(0.0, max(rb, 1e-3), dist)) * 0.35;
    return (ring + dome) * pop * step(0.01, rb) * have;
}

// 両方の頂点シェーダーで共用：グリッドの頂点 1 つを地面に置く。
// xz = そのワールド位置、lp = 同じ点のローカル座標、
// ground = 水たまり中心の高さ、heightAt = xz の下の地形の高さ
// （地形が無い所では ground をもう一度渡す）。pos / world / local / mask を書く
void LiquidPlaceVertex(LiquidDef L, float ground, float heightAt, float2 xz, float2 lp,
                       float4x4 viewProj, inout LiquidVSOut o)
{
    float dh = heightAt - ground;
    float cliff = max(L.cliff, 1e-3);
    // 中心と同じ地面では 1、そこから cliff / 2 〜 cliff 離れる間に 0 へ落ちる
    o.mask = saturate(1.0 - (abs(dh) - cliff * 0.5) / (cliff * 0.5));
    float y = clamp(heightAt, ground - cliff, ground + cliff) + L.lift;
    o.world = float3(xz.x, y, xz.y);
    o.pos = mul(float4(o.world, 1.0), viewProj);
    o.local = lp;
}

#endif
