// ============================================================
// SwarmTerrain.hlsli
// 地形の参照：歩ける判定、敵の空間ハッシュ、高さの取得、坂 / 段差の判定、体と壁の接触。
// ============================================================

#ifndef SWARM_TERRAIN_HLSLI
#define SWARM_TERRAIN_HLSLI

// ============================================================
// 地形の参照：1 = 歩ける
// ============================================================
bool SwarmIsWalkable(StructuredBuffer<uint> walkable, float3 p)
{
    int gx = (int) floor((p.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((p.z - g_GridOrigin.z) / g_CellSize);

    if (gx < 0 || gx >= (int) g_GridW)
        return false;
    if (gz < 0 || gz >= (int) g_GridD)
        return false;

    return walkable[gz * g_GridW + gx] != 0;
}

// ============================================================
// 敵の空間ハッシュ。セルは地形のグリッドと同じ（g_CellSize）。
// SwarmEnemyBinCS が毎ステップ埋める：cellCount[cell] = そのセルの生きている
// 敵の数、cellItems[cell * CAP + k] = それらのスロット番号。CAP を超えた
// 分は捨てる（読み手は min() でクランプする）。2m のセルと 0.4m の敵なら
// 詰まったセルでも約 7 体なので、32 あれば押し出しパスが解消するための
// 押し合いの分も余裕がある。
// AI（分離）と PushCS（重なりの解消）は 3x3 のセルだけを見る。
// ============================================================
static const uint SWARM_BUCKET_CAP = 32u;

// ワールド位置のセル番号。グリッドの外なら -1
int SwarmCellOf(float3 p)
{
    int gx = (int) floor((p.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((p.z - g_GridOrigin.z) / g_CellSize);
    bool inside = (gx >= 0 && gx < (int) g_GridW && gz >= 0 && gz < (int) g_GridD);
    return inside ? (gz * (int) g_GridW + gx) : -1;
}

// ============================================================
// 地形のハイトフィールド（GridWorld::Heights）。グリッドのセル 1 つに
// SWARM_HEIGHT_SUB 個のセル。平地では 0、坂では歩ける面の高さ。
// 敵は y をこれに合わせ（MoveCS）、弾はこれより下を飛ぶと
// 爆発する（ProjMoveCS）。GridWorld::kHeightSub と一致させること
// ============================================================
static const uint SWARM_HEIGHT_SUB = 4u;

float SwarmHeightCell(StructuredBuffer<float> heights, int hx, int hz)
{
    // 出口は 1 つ（そうしないと fxc が X4000 を出す）
    int hw = (int) (g_GridW * SWARM_HEIGHT_SUB);
    int hd = (int) (g_GridD * SWARM_HEIGHT_SUB);
    bool inside = (hx >= 0 && hx < hw && hz >= 0 && hz < hd);
    return inside ? heights[clamp(hz, 0, hd - 1) * hw + clamp(hx, 0, hw - 1)] : 0.0;
}

// ワールド xz でのバイリニアサンプル（GridWorld::SampleHeight と同じ式）
float SwarmTerrainHeight(StructuredBuffer<float> heights, float2 xz)
{
    float s = g_CellSize / (float) SWARM_HEIGHT_SUB;
    float fx = (xz.x - g_GridOrigin.x) / s - 0.5;
    float fz = (xz.y - g_GridOrigin.z) / s - 0.5;
    int ix = (int) floor(fx);
    int iz = (int) floor(fz);
    float tx = fx - ix;
    float tz = fz - iz;
    float h00 = SwarmHeightCell(heights, ix, iz), h10 = SwarmHeightCell(heights, ix + 1, iz);
    float h01 = SwarmHeightCell(heights, ix, iz + 1), h11 = SwarmHeightCell(heights, ix + 1, iz + 1);
    return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
}

// ============================================================
// 崖。台地のセルは歩ける（敵は上をうろつく）うえ、高さをハイトフィールドに
// 持っているので、グリッドだけでは敵が台地の側面をそのまま
// 滑り上がれてしまう。台地の側面は 0.5m の高さセル 1 つの中で
// 数 m の段差になり、坂は 30 度以下：
// SWARM_MAX_WALK_SLOPE（高さ / 水平距離）より急な移動は拒否する。
// FlowField::maxStep（2m のセルあたり）と生成器の坂に合わせること
// ============================================================
static const float SWARM_MAX_WALK_SLOPE = 0.84; // tan(40 deg)

bool SwarmSlopeOk(StructuredBuffer<float> heights, float2 from, float2 to)
{
    float run = length(to - from);
    float rise = abs(SwarmTerrainHeight(heights, to) - SwarmTerrainHeight(heights, from));
    return rise <= SWARM_MAX_WALK_SLOPE * run + 0.05; // + バイリニアの折れ目の分の余裕
}

// ============================================================
// 台地から飛び降りる。プレイヤーより高い所にいる敵は
// 崖を下りてよい（落ちて、MoveCS が重力をかける）。
// 登るのは拒否する。プレイヤーが少なくとも
// SWARM_DROP_MIN_BELOW 低い所にいる間だけ：プレイヤーが同じ台地の上にいれば
// 崖はどちら向きにも壁のまま。FlowField（CPU）も同じ規則を使うので、
// フィールドが、GPU が許さない縁から敵を下ろす経路を
// 作ることはない。群れの押し出し（PushCS）は誰も落とさない。
// feet = 敵の足の高さ（地形の空間、position.y - g_CenterAboveFeet）。
// 敵の高さは、中心の下の地面と足のうち高い方：
// バイリニアの地面は縁の手前 0.25m でぼやけて下がるので、中心の下の地面だけだと
// 縁のところで判定が可 / 不可を行き来する。Settle は
// 体全体が縁を越えるまで足を台地の高さに保つので、
// 飛び降りの間は足の高さで判定が続く
// ============================================================
static const float SWARM_DROP_MIN_BELOW = 1.0; // = Swarm::kFlowDropMinBelow

bool SwarmDropAllowed(StructuredBuffer<float> heights, float2 enemyXZ, float feet)
{
    float h = max(SwarmTerrainHeight(heights, enemyXZ), feet);
    return g_PlayerAlive != 0u
        && SwarmTerrainHeight(heights, g_PlayerPos.xz) <= h - SWARM_DROP_MIN_BELOW;
}

// SwarmSlopeOk と同じだが、allowDrop の時はどんな高さの降下も通す
bool SwarmStepHeightOk(StructuredBuffer<float> heights, float2 from, float2 to, bool allowDrop)
{
    float rise = SwarmTerrainHeight(heights, to) - SwarmTerrainHeight(heights, from);
    float lim = SWARM_MAX_WALK_SLOPE * length(to - from) + 0.05;
    return rise <= lim && (allowDrop || -rise <= lim);
}

// ============================================================
// 体の円 vs 壁と崖（中心だけでなく体が台地の側面にめり込まないように）。
// 体は円周上の 8 点でサンプルする。
// 点のセルが塞がっているか、そこの地面が
// 崖（中心より、歩ける坂の分 +
// SWARM_BODY_CLIFF_RISE より高い / 低い）なら、その点は接触とみなす。高さは
// バイリニアのサンプルではなく生の 0.5m の高さセル：台地の側面はセルの境界に
// あるので、段差はちょうど面の位置にある（バイリニアは 0.25m 手前から
// 上がり始める）。余裕は坂の階段状の段差（セルあたり約 0.27m）を吸収する。
// 戻り値 xy = 接触した点の方向の和（押し出しは
// 逆向き）、z = 接触した点の数。
// ============================================================
static const float SWARM_BODY_CLIFF_RISE = 0.35;
// 体の幅で許す傾き：一番急な坂は約 30 度なので、
// SWARM_MAX_WALK_SLOPE（40 度）より厳しくしても、エリートの半径 0.9m の
// 下にある約 1m の段差を捉えられる
static const float SWARM_BODY_MAX_SLOPE = 0.62; // tan(32 deg)

// xz を含む 0.5m の高さセルの高さ（補間しない）
float SwarmTerrainHeightRaw(StructuredBuffer<float> heights, float2 xz)
{
    float s = g_CellSize / (float) SWARM_HEIGHT_SUB;
    return SwarmHeightCell(heights, (int) floor((xz.x - g_GridOrigin.x) / s),
                                    (int) floor((xz.y - g_GridOrigin.z) / s));
}

// countDrops = false：低い地面は数えない（縁から飛び降りる敵は
// 体をはみ出させてよい）。数えるのは壁と上り面だけ。
// footH：上りを測る基準の高さ（敵の足、地形の空間）。
// デフォルト = 中心の下の地面。MoveCS は本当の足の高さを渡す：
// 縁から降りる敵は、中心がもう低い地面の上にあっても
// 足はまだ後ろの台地と同じ高さで、
// それを壁と数えてはいけない
float3 SwarmBodyContact(StructuredBuffer<uint> walkable, StructuredBuffer<float> heights, float2 c, float radius,
                        bool countDrops = true, float footH = -1e30)
{
    float hc = (footH > -1e29) ? footH : SwarmTerrainHeightRaw(heights, c);
    float maxRise = SWARM_BODY_MAX_SLOPE * radius + SWARM_BODY_CLIFF_RISE;
    float3 acc = float3(0, 0, 0);
    [unroll]
    for (int k = 0; k < 8; ++k)
    {
        float a = (float) k * 0.7853982;
        float2 d = float2(cos(a), sin(a));
        float2 q = c + d * radius;
        float rise = SwarmTerrainHeightRaw(heights, q) - hc;
        if (!SwarmIsWalkable(walkable, float3(q.x, 0.0, q.y))
            || rise > maxRise || (countDrops && -rise > maxRise))
            acc += float3(d, 1.0);
    }
    return acc;
}

// 生の高さセルでの SwarmSlopeOk。崖面から体を押し出すステップに使う：
// 面から 0.25m 以内ではバイリニアの高さがもう崖を登っているので、
// 離れる動きが SwarmSlopeOk には急な降下に見えるため
bool SwarmRawSlopeOk(StructuredBuffer<float> heights, float2 from, float2 to)
{
    float rise = abs(SwarmTerrainHeightRaw(heights, to) - SwarmTerrainHeightRaw(heights, from));
    return rise <= SWARM_MAX_WALK_SLOPE * length(to - from) + SWARM_BODY_CLIFF_RISE;
}

// 生の高さセルでの SwarmStepHeightOk：生の地面の上りが歩ける坂 + 坂の余裕を
// 超えなければ通す。下りも同じで、allowDrop なら何でも通す。
// MoveCS はこれかバイリニアの判定のどちらかが通ればステップを受け入れる：台地の縁から
// 0.25m 以内ではバイリニアの地面が面の途中まで下がり、どのステップも
// 「急すぎる上り」になる。生のセルにはそういう凹みがない。本当の面を登るのは
// ここでもやはり失敗し（0.5m のセル 1 つの中に段差全体がある）、どのみち体の判定が
// 中心を面の線から離しておく
bool SwarmRawStepOk(StructuredBuffer<float> heights, float2 from, float2 to, bool allowDrop)
{
    float rise = SwarmTerrainHeightRaw(heights, to) - SwarmTerrainHeightRaw(heights, from);
    float lim = SWARM_MAX_WALK_SLOPE * length(to - from) + SWARM_BODY_CLIFF_RISE;
    return rise <= lim && (allowDrop || -rise <= lim);
}

#endif
