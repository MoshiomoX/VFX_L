// ============================================================
// SwarmEnemyAICS.hlsl
// 敵のスロット 1 つに 1 スレッド。このステップの速度を決める：
//   追跡（プレイヤーへ向かうフローフィールド、近ければ直線）
//   + 分離（空間ハッシュの 3x3 セル）+ 回避（通れないセルからの
//   柔らかい押し）+ ハードブロック（通れないセルに入る軸の速度を
//   消す）
//
// 書くのは速度「だけ」。位置は別のディスパッチの SwarmEnemyMoveCS が
// 積分するので、ここの全スレッドは一貫した位置のスナップショットを
// 読める -- CPU 版が最初に全員の位置を vector へ写すのと
// 同じ理由。
//
// もとは CPU の ChaseAISystem.cpp と同じロジックだった（CPU 版は 2026-09-30 に削除済み）。
// + プレイヤーのブロック（実体のある円、回り込んで滑る）
// エリート（体が大きい）はより遠くで止まる：SwarmKindScale。
//
// 凍結（アイスランス、2026-10-08）：HitCS が animIndex に要求を残す
// （SWARM_ANIM_FREEZE_REQ、animTime = 秒）。このパスがそれを
// enemySlow.z に変え、凍った敵を止めておく。
// 突撃兵（2026-10-08）：突撃兵の enemyExtra.fuse を書くのはここだけ
// （溜め -> 突進 -> 息切れ -> クールダウン、SwarmChargerPhase 参照）。突進の
// 向きは溜めの開始時に設定した yaw（MoveCS は 0 でない速度の方へしか
// yaw を回さないので、突撃兵が立っている間は保たれる）。
// ============================================================
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<uint> terrain : register(t1);
StructuredBuffer<uint> cellCount : register(t2);   // 空間ハッシュ（SwarmEnemyBinCS）
StructuredBuffer<uint> cellItems : register(t3);
StructuredBuffer<float2> flowField : register(t4); // セルごとのプレイヤーへの向き（CPU の FlowField）
StructuredBuffer<float> terrainHeight : register(t6);         // 崖は壁と同じく塞ぐ（SwarmSlopeOk）
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
// 減速（x 残り秒、y 量。AreaDamageCS）+ 凍結（z 残り秒、w 長さ / 免疫）、ここで減らしていく
RWStructuredBuffer<float4> enemySlow : register(u1);
// 種類 + 導火線。ContactCS が自爆兵の導火線に火を付け、このパスは突撃兵の分を回す
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u2);
// 分裂リング（SwarmCorpseTrackCS がスプリッターの死をここに書く）：溜めを終えた砂嵐の術師は
// 位置 + SWARM_KIND_SANDMAGE を書き、CPU がプレイヤーへ向けて砂旋を生成する（2026-10-17）
RWByteAddressBuffer castEvents : register(u3);
static const uint SWARM_MAX_SPLIT_EVENTS = 128u;   // Swarm::kMaxSplitEvents

// 分裂リングの 1 要素：位置 + 種類（下位 16 ビット）| 何が起きたか（上位 16 ビット、2026-10-25）
void CastEvent(float3 pos, uint word)
{
    uint s;
    castEvents.InterlockedAdd(0, 1u, s);
    uint at = 16u + (s % SWARM_MAX_SPLIT_EVENTS) * 16u;
    castEvents.Store3(at, asuint(pos));
    castEvents.Store(at + 12u, word);
}

// 詠唱する敵（砂嵐の術師 / 死霊術師 / 鎖の番人、2026-10-25 に共通化）：fuse 0 = 準備完了 -> > 0 = 溜め（増えていく、
// 立ち止まり、開始時に固定したプレイヤーの方を向く）-> `windup` で詠唱イベント（残りは CPU がやる）-> < 0 =
// クールダウン（0 まで増える）。プレイヤーが同じ高さで `range` 以内にいるときだけ始める。moveSpeed 0 = 訓練用の
// 的：詠唱しない。withYaw：イベントの上位 16 ビットに固定した yaw を載せる（番人の鎖の線）
float CasterStep(uint i, float3 pos, float f, float windup, float cooldown, float range, uint kind, bool withYaw)
{
    if (f < 0.0)
        f = min(f + g_Step, 0.0);
    else if (f > 0.0)
    {
        f += g_Step;
        if (f >= windup)
        {
            uint word = kind;
            if (withYaw)
                word |= ((uint) (frac(enemies[i].yaw / 6.2831853 + 1.0) * 65535.0 + 0.5) & 0xFFFFu) << 16;
            CastEvent(pos, word);
            f = -cooldown;
        }
    }
    if (f == 0.0 && g_PlayerAlive != 0u && enemies[i].animIndex != 2u && enemies[i].moveSpeed > 0.01
        && abs(g_PlayerPos.y - pos.y) < 1.5)
    {
        float2 toP = g_PlayerPos.xz - pos.xz;
        if (dot(toP, toP) <= range * range)
        {
            f = g_Step;
            enemies[i].yaw = atan2(toP.x, toP.y);   // 溜めの間はプレイヤーの方を向く（番人の鎖はこの向きへ飛ぶ）
        }
    }
    return f;
}

// 突撃兵は、突進の道筋の地面がプレイヤーまで開いている（歩ける、崖が無い）ときだけ
// 溜める：そうでなければ壁に突っ込むだけになる
bool ChargeLineClear(float3 pos, float2 dir, float dist)
{
    float2 prev = pos.xz;
    bool ok = true;
    int n = (int) ceil(dist);
    for (int k = 1; k <= n && ok; ++k)
    {
        float2 q = pos.xz + dir * min((float) k, dist);
        ok = SwarmIsWalkable(terrain, float3(q.x, 0.0, q.y))
          && SwarmStepHeightOk(terrainHeight, prev, q, false);
        prev = q;
    }
    return ok;
}

// ハードブロックの探針（2026-10-20）：探針の点が開いているのは、そのセルが歩けて、ここからの
// 坂が歩けて（崖 = 壁、下りの段差は allowDrop のときだけ開く）、かつその点での体の
// 円が何にも触れない（SwarmBodyContact、MoveCS が毎ステップかけるのと同じ判定）とき。
// 中心線の坂の判定だけだと、1.5〜2 m の先読みで、高台の坂の脇の 0.6 m の段差や
// 台地のセルの角（そのセルは歩ける）を通してしまい、AI は MoveCS が毎ステップ
// 拒否する方向へ押し続けていた（段々の台地での soak）。
// 上りは探針自身の地面から測る（坂を上るのは壁ではない）。下りが
// 開いているときは、足の方が高ければ足から測る（縁から踏み出す：後ろの台地は壁ではなく、
// 円の下の低い地面は数えない）
// allowClimb（2026-10-29、敵の壁登り）：探針の地面が足より体の段差の余裕以上に
// 高いのは登る面であって壁ではない - MoveCS がそこへ押し付けて登る
bool ProbeBlocked(float3 pos, float2 probe, float r, bool allowDrop, float feet, bool allowClimb = false)
{
    if (!SwarmIsWalkable(terrain, float3(probe.x, 0.0, probe.y)))
        return true;
    // 足ではなく中心の下の地面から測る：登っている間は足が上がり、面の最後の
    // SWARM_BODY_CLIFF_RISE では下の段差判定（地面 -> 頂上）が軸を止めるので、登る敵は毎回
    // 頂上の手前で手を放していた（自動テスト climb、2026-10-29）
    // 探針での体の円の前端も見る：止まっていた敵はごく小さい速度から動き直すので、
    // 探針の中心はまだ低い側にあるのに体の円はもう面に触れていて、下の体の判定に
    // 止められ、そのまま立ち尽くしていた（soak 2026-10-29：段々の台地の階段状の縁で 30 体が詰まった）
    if (allowClimb)
    {
        float base = min(feet, SwarmTerrainHeightRaw(terrainHeight, pos.xz)) + SWARM_BODY_CLIFF_RISE;
        float2 ahead = probe - pos.xz;
        float len = length(ahead);
        float2 front = (len > 1e-4) ? probe + ahead / len * r : probe;
        if (SwarmTerrainHeightRaw(terrainHeight, probe) > base
            || (SwarmIsWalkable(terrain, float3(front.x, 0.0, front.y)) && SwarmTerrainHeightRaw(terrainHeight, front) > base))
            return false;
    }
    // バイリニアまたは生の値（MoveCS の StepOk と同じ）：台地の縁のバイリニアのくぼみの中からだと、
    // バイリニアの判定だけでは台地へ戻る一歩をすべて急すぎる上りと判定してしまう
    if (!SwarmStepHeightOk(terrainHeight, pos.xz, probe, allowDrop) && !SwarmRawStepOk(terrainHeight, pos.xz, probe, allowDrop))
        return true;
    float footH = SwarmTerrainHeightRaw(terrainHeight, probe);
    if (allowDrop)
        footH = max(footH, feet);
    return SwarmBodyContact(terrain, terrainHeight, probe, r, !allowDrop, footH).z > 0.0;
}

// プレイヤーからこのセル数以内は直線で追う：フローフィールドのセル単位の
// 段差で、プレイヤーを囲む輪ががたつくため
static const float kDirectChaseCells = 1.5;

// ワールド xz でのフローの向き：近い 4 つのセル中心でバイリニアに混ぜ、
// セルの境目ごとに敵の向きがパチッと変わらないようにする。
// (0,0) のセル（届かない / 目標）は混ぜた結果をゼロへ引き寄せ、
// 呼び出し側はそれを「経路なし、直接追う」として扱う。
// 別の高さにあるセル（中心が敵の地面から kFlowLevelStep を超えて上 / 下）は
// 除く（2026-10-20）：段々の台地の脇では、台地のセルの向き
// - ここ下からは通れない経路 - が混ざり、敵を面 / 角へ向けてしまい、
// そこでぴくぴくしていた
static const float kFlowLevelStep = 1.5;   // = FlowField::maxStep
float2 FlowAt(float2 xz, float groundH)
{
    float fx = (xz.x - g_GridOrigin.x) / g_CellSize - 0.5;
    float fz = (xz.y - g_GridOrigin.z) / g_CellSize - 0.5;
    int ix = (int) floor(fx), iz = (int) floor(fz);
    float tx = fx - ix, tz = fz - iz;
    int w = (int) g_GridW, d = (int) g_GridD;

    float2 sum = float2(0, 0);
    float wsum = 0.0;
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        int cx = ix + (k & 1), cz = iz + (k >> 1);
        float wgt = ((k & 1) ? tx : 1.0 - tx) * ((k >> 1) ? tz : 1.0 - tz);
        bool inside = (cx >= 0 && cx < w && cz >= 0 && cz < d);
        float2 centre = float2(g_GridOrigin.x + (cx + 0.5) * g_CellSize, g_GridOrigin.z + (cz + 0.5) * g_CellSize);
        bool sameLevel = abs(SwarmTerrainHeight(terrainHeight, centre) - groundH) <= kFlowLevelStep;
        if (inside && sameLevel)
        {
            sum += flowField[cz * w + cx] * wgt;
            wsum += wgt;
        }
    }
    return (wsum > 1e-4) ? sum / wsum : float2(0, 0);
}


[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    SwarmEnemyExtra extra = enemyExtra[i];
    bool big = (SwarmIsBigElite(extra.kind) || extra.kind == SWARM_KIND_BOSS);

    // ---- 面のボス：どれだけ砂に潜っているか（砂の守護者の潜砂、2026-10-24）。CPU（BossAttacks）が
    //      g_BossBurrow を動かす。ボスの fuse = -burrow にして、SwarmBuried / VS が砂潜りと同じように見る ----
    if (extra.kind == SWARM_KIND_BOSS)
    {
        extra.fuse = -saturate(g_BossBurrow);
        enemyExtra[i].fuse = extra.fuse;
    }
    // ---- 面の固定エリート（2026-10-25）：CPU（EliteAttacks）が g_EliteCmd で技を動かす。どれだけ
    //      姿を消しているかを fuse に入れる（-hide：半分から SwarmUntargetable、VS が地面へ縮めて沈める） ----
    int eliteRow = -1;
    if (SwarmIsFixedElite(extra.kind))
    {
        eliteRow = SwarmEliteEntry(i);
        extra.fuse = (eliteRow >= 0) ? -saturate(g_EliteMove[eliteRow].w) : 0.0;
        enemyExtra[i].fuse = extra.fuse;
    }

    // ---- 砂旋（2026-10-17）：寿命が尽きるまで yaw の向きへ moveSpeed でまっすぐ進む。減速・凍結・
    //      分離・壁は無し（MoveCS は幽霊と同じに扱う）。fuse：0 = 生成直後、> 0 = 残り秒、
    //      < 0 = 寿命切れ（このステップで ContactCS がスロットを DEAD にする） ----
    if (extra.kind == SWARM_KIND_TORNADO)
    {
        float f = extra.fuse;
        if (f == 0.0) f = g_TornadoLife;
        else if (f > 0.0) { f -= g_Step; if (f <= 0.0) f = -1.0; }
        enemyExtra[i].fuse = f;
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        enemies[i].velocity = (f > 0.0) ? float3(ys, 0.0, yc) * enemies[i].moveSpeed : float3(0, 0, 0);
        return;
    }

    // ---- 状態：減速（毒沼）と凍結（氷）。どの早期 return よりも前に減らす ----
    float4 st = enemySlow[i];
    float slowMul = 1.0;
    if (st.x > 0.0)
    {
        slowMul = 1.0 - saturate(st.y);
        st.x -= g_Step;
        if (st.x <= 0.0)
            st.xy = float2(0, 0);
    }
    // 前のステップで HitCS が要求した凍結。凍っている間とまだ免疫がある間は無視する
    if (enemies[i].animIndex == SWARM_ANIM_FREEZE_REQ)
    {
        float want = enemies[i].animTime * (big ? g_FreezeBigMul : 1.0);
        if (st.z <= 0.0 && st.w <= 0.0 && want > 0.0)
            st.zw = float2(want, want);
        enemies[i].animIndex = 0u;
        enemies[i].animTime = 0.0;
    }
    bool frozen = st.z > 0.0;
    if (frozen)
    {
        st.z -= g_Step;
        if (st.z <= 0.0)
            st.zw = float2(0.0, g_FreezeImmunity); // 解凍：次に凍るまでしばらく空ける
    }
    else if (st.w > 0.0)
        st.w = max(st.w - g_Step, 0.0);
    // 最終ウェーブの幽霊は減速も凍結もしない（2026-10-18：氷 / 毒で群れを足止めできてはいけない）。
    // 火の付いた自爆兵 / 突撃爆弾兵も同じ（もう爆弾なので）
    if (SwarmIsGhost(extra.kind) || SwarmInvulnerable(extra))
    {
        slowMul = 1.0;
        frozen = false;
        st = float4(0, 0, 0, 0);
    }
    enemySlow[i] = st;

    if (frozen)
    {
        // 凍った突撃兵は溜め / 突進を取りやめる
        if (extra.kind == SWARM_KIND_CHARGER && extra.fuse > 0.0)
            enemyExtra[i].fuse = -0.5 * g_ChargerCooldown;
        enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
        return;
    }

    float3 pos = enemies[i].position;

    // ---- 突撃兵：溜め -> 突進 -> 息切れ -> クールダウン（fuse） ----
    uint cphase = SWARM_CHARGE_CHASE;
    if (extra.kind == SWARM_KIND_CHARGER)
    {
        float f = extra.fuse;
        if (f < 0.0)
            f = min(f + g_Step, 0.0);
        else if (f > 0.0)
        {
            f += g_Step;
            if (f > g_ChargerWindup + SwarmChargerDashTime() + g_ChargerRecover)
                f = -g_ChargerCooldown;
        }
        // 溜めを始める：プレイヤーが同じ高さ、範囲内、間に障害物なし。突進の
        // 向きはここで固定する（yaw）ので、地面の帯が行き先をそのまま示す
        // （moveSpeed 0 = 動いてはいけない訓練用の的：突進しない）
        if (f == 0.0 && g_PlayerAlive != 0u && enemies[i].animIndex != 2u
            && enemies[i].moveSpeed > 0.01 && abs(g_PlayerPos.y - pos.y) < 1.0)
        {
            float2 toP = g_PlayerPos.xz - pos.xz;
            float dist = length(toP);
            if (dist >= g_ChargerMinDist && dist <= g_ChargerMaxDist
                && ChargeLineClear(pos, toP / dist, min(dist + 1.0, g_ChargerDashDist)))
            {
                f = g_Step;
                enemies[i].yaw = atan2(toP.x, toP.y);
            }
        }
        cphase = SwarmChargerPhase(f);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
    }

    // ---- 砂潜り：砂の下（fuse <= 0、減っていく）-> プレイヤーの横で顔を出す（fuse = g_BurrowSurfaceTime、
    //      減っていく）-> また潜る。moveSpeed 0 = 訓練用の的：潜ったまま（2026-10-17） ----
    bool buried = false;
    if (extra.kind == SWARM_KIND_BURROWER)
    {
        float f = extra.fuse;
        if (f > 0.0)
        {
            f -= g_Step;
            if (f <= 0.0) f = -g_Step;
        }
        else
        {
            f -= g_Step;
            if (g_PlayerAlive != 0u && -f >= g_BurrowMinBuried && enemies[i].moveSpeed > 0.01 && abs(g_PlayerPos.y - pos.y) < 1.5)
            {
                float2 toP = g_PlayerPos.xz - pos.xz;
                if (dot(toP, toP) <= g_BurrowSurfaceDist * g_BurrowSurfaceDist)
                    f = g_BurrowSurfaceTime;
            }
        }
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        buried = f <= 0.0;
    }

    // ---- 砂嵐の術師：準備完了（fuse 0）-> 溜め（fuse が g_SandMageWindup まで増える、立ち止まる）-> 詠唱
    //      （castEvents、CPU が砂旋を生成する）-> クールダウン（fuse < 0 が 0 まで増える）。プレイヤーが同じ
    //      高さで g_SandMageCastRange 以内のときだけ。moveSpeed 0 = 訓練用の的：詠唱しない（2026-10-17） ----
    bool casting = false;
    if (extra.kind == SWARM_KIND_SANDMAGE)
    {
        float f = CasterStep(i, pos, extra.fuse, g_SandMageWindup, g_SandMageCooldown, g_SandMageCastRange, SWARM_KIND_SANDMAGE, false);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        casting = f > 0.0;
    }
    // ---- 死霊術師（2026-10-25）：砂嵐の術師と同じ詠唱の流れ。詠唱で骸骨を起こす（CPU、MobSpawner） ----
    if (extra.kind == SWARM_KIND_NECRO)
    {
        float f = CasterStep(i, pos, extra.fuse, g_NecroWindup, g_NecroCooldown, g_NecroCastRange, SWARM_KIND_NECRO, false);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        casting = f > 0.0;
    }
    // ---- 鎖の番人（2026-10-25）：yaw を固定して溜め（地面の帯、SwarmChargeLineVS）、最後に投げる
    //      （イベントに yaw を載せ、鉤はシーンが飛ばす）。その後、鎖が出ている間 g_WardenHold だけ立ち止まる ----
    if (extra.kind == SWARM_KIND_WARDEN)
    {
        float f = CasterStep(i, pos, extra.fuse, g_WardenWindup, g_WardenCooldown, g_WardenRange, SWARM_KIND_WARDEN, true);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        casting = f > 0.0 || f < -(g_WardenCooldown - g_WardenHold);
    }

    // ---- 石像ガーゴイル（2026-10-25）：眠り = 石像（fuse <= 0 が減っていく）-> プレイヤーが g_GargoyleWakeDist
    //      以内に来るか g_GargoyleAutoWake 経つと目覚める -> 目覚め中（震えて浮き上がる）-> 幽霊のようにプレイヤーへ飛ぶ
    //      -> プレイヤーが g_GargoyleDiveRange 以内：yaw を固定して空中で止まり（地面の帯）、それに沿って急降下し、
    //      着地して g_GargoyleLandTime だけ立つ（殴るチャンス）-> 浮き上がってまた飛ぶ。目覚め / 急降下 / 着地は
    //      分裂リングへ送る（CPU が石くず・土煙・音を出す）。moveSpeed 0 = 訓練用の的：目覚めない ----
    bool gargStand = false, gargDive = false;
    if (extra.kind == SWARM_KIND_GARGOYLE)
    {
        float f = extra.fuse;
        float2 toP = g_PlayerPos.xz - pos.xz;
        float distSq = dot(toP, toP);
        float landAt = g_GargoyleDiveWindup + g_GargoyleDiveTime;
        if (f <= 0.0)
        {
            f -= g_Step;
            bool near = (g_PlayerAlive != 0u) && distSq <= g_GargoyleWakeDist * g_GargoyleWakeDist;
            if (enemies[i].moveSpeed > 0.01 && (near || -f >= g_GargoyleAutoWake))
            {
                f = g_Step;
                CastEvent(pos, SWARM_KIND_GARGOYLE | (0u << 16));
            }
        }
        else if (f < SWARM_GARGOYLE_DIVE)
        {
            f = min(f + g_Step, SWARM_GARGOYLE_DIVE - 1.0);
            if (f >= g_GargoyleWakeTime + g_GargoyleFlyMin && g_PlayerAlive != 0u
                && distSq <= g_GargoyleDiveRange * g_GargoyleDiveRange && distSq >= g_GargoyleDiveMin * g_GargoyleDiveMin)
            {
                f = SWARM_GARGOYLE_DIVE + g_Step;
                enemies[i].yaw = atan2(toP.x, toP.y);   // 急降下の線。ここで固定する
                CastEvent(pos, SWARM_KIND_GARGOYLE | (1u << 16));
            }
        }
        else
        {
            float d0 = f - SWARM_GARGOYLE_DIVE;
            f += g_Step;
            float d1 = f - SWARM_GARGOYLE_DIVE;
            if (d0 < landAt && d1 >= landAt)
                CastEvent(pos, SWARM_KIND_GARGOYLE | (2u << 16));
            // また飛ぶ：目覚めの震えを過ぎた所から再開し、なめらかに浮き上がらせる（SwarmGargoyleHover）
            if (d1 >= landAt + g_GargoyleLandTime)
                f = 0.36 * g_GargoyleWakeTime;
        }
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        gargDive = SwarmGargoyleDiving(extra);
        gargStand = (f < g_GargoyleWakeTime) || (f >= SWARM_GARGOYLE_DIVE && !gargDive);
    }

    // ---- 突撃爆弾兵（2026-10-18）：同じ高さでプレイヤーが g_DashBomberTrigger 以内に来ると自分に火を付け、
    //      yaw をプレイヤーへ固定し、g_DashBomberWindup 秒立ち止まって（点滅）、その yaw に沿ってまっすぐ走る
    //      （下の突進を参照）。触れるか導火線が尽きるまで走る（ContactCS が爆発させる）。
    //      2026-10-20：以前は 12 m/s で毎ステップ狙い直していた = ほぼ避けられない。今は横に一歩動けば避けられる。
    //      fuse > 0 = 火が付いてからの秒（ここで数え、ContactCS / VS が読む）。moveSpeed 0 = 訓練用の的：火が付かない ----
    bool dashing = false, dashWindup = false;
    if (extra.kind == SWARM_KIND_DASHBOMBER)
    {
        float f = extra.fuse;
        if (f > 0.0)
            f += g_Step;
        else if (g_PlayerAlive != 0u && enemies[i].moveSpeed > 0.01 && abs(g_PlayerPos.y - pos.y) < 1.5)
        {
            float2 toP = g_PlayerPos.xz - pos.xz;
            if (dot(toP, toP) <= g_DashBomberTrigger * g_DashBomberTrigger)
            {
                f = g_Step;
                enemies[i].yaw = atan2(toP.x, toP.y);   // 突進の向き。ここで固定する
            }
        }
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        dashWindup = f > 0.0 && f <= g_DashBomberWindup;
        dashing = f > g_DashBomberWindup;
    }

    // ---- 立てと言われた固定エリート（溜め、炎の息、消える技）：立ち止まり、プレイヤーの方か、CPU が固定した
    //      向き（速度 0 の走る指示）へ向き直る ----
    if (eliteRow >= 0)
    {
        uint ecmd = g_EliteCmd[eliteRow].z;
        float4 emv = g_EliteMove[eliteRow];
        if (ecmd == SWARM_ELITE_CMD_STAND || ecmd == SWARM_ELITE_CMD_TELEPORT || (ecmd == SWARM_ELITE_CMD_MOVE && emv.z <= 0.01))
        {
            float2 face = (ecmd == SWARM_ELITE_CMD_MOVE) ? emv.xy : (g_PlayerPos.xz - pos.xz);
            if (dot(face, face) > 1e-6)
            {
                float want = atan2(face.x, face.y);
                float dy = want - enemies[i].yaw;
                dy -= 6.2831853 * floor((dy + 3.14159265) / 6.2831853);   // -pi..pi に回り込ませる
                float turn = (ecmd == SWARM_ELITE_CMD_MOVE) ? 30.0 : 9.0;  // 固定した線へはすぐ向き、プレイヤーへは回って向く
                enemies[i].yaw += clamp(dy, -turn * g_Step, turn * g_Step);
            }
            enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
            return;
        }
    }

    // ---- 被弾の硬直 / 火の付いた自爆兵 / 溜め中・息切れ中の突撃兵：立ち止まる ----
    // 速度を 0 にして、抜けるときは下の遅れを通ってまた加速させる。
    // 火の付いた自爆兵は爆発までその場に留まるので、プレイヤーは走って爆風から逃げられる。
    // 溜めを始めた突撃兵は怯まない（そうしないと攻撃を浴び続けて止められてしまう）
    bool lit = (extra.kind == SWARM_KIND_BOMBER) && (extra.fuse > 0.0);
    bool stunned = (enemies[i].animIndex == 2u) && (cphase == SWARM_CHARGE_CHASE) && !dashing;
    // 顔を出した砂潜りは出た場所に立つ（1 回噛んでからまた潜る）
    bool surfaced = (extra.kind == SWARM_KIND_BURROWER) && !buried;
    if (stunned || lit || surfaced || casting || dashWindup || gargStand || cphase == SWARM_CHARGE_WINDUP || cphase == SWARM_CHARGE_RECOVER)
    {
        // y は落下速度（MoveCS の持ち物）：空中で硬直した敵は落ち続ける
        enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
        return;
    }

    float3 oldV = enemies[i].velocity; // 自分のスロット：前のステップでこのスレッドが書いた
    float moveSpeed = enemies[i].moveSpeed * slowMul;
    if (eliteRow >= 0)   // 固定エリートのアフィックス（2026-10-25）
    {
        uint efl = g_EliteCmd[eliteRow].y;
        if ((efl & SWARM_ELITE_SWIFT) != 0u) moveSpeed *= g_EliteSwiftMul;
        if ((efl & SWARM_ELITE_ENRAGED) != 0u) moveSpeed *= g_EliteEnragedSpeedMul;
    }
    if (extra.kind == SWARM_KIND_BURROWER)
        moveSpeed *= buried ? g_BurrowBuriedSpeedMul : g_BurrowSurfaceSpeedMul;

    // ---- 追跡：フローフィールドに従い、近ければ直接追う ----
    // フィールドは箱や壁を回り込む経路を出す（以前の直線だと、敵が
    // 障害物の向こう側に押し付けられていた）。プレイヤーの近くか、
    // フィールドに答えが無い所では直線に戻す
    float3 moveDir = float3(0, 0, 0);
    if (g_PlayerAlive != 0u)
    {
        float3 toPlayer = g_PlayerPos - pos;
        toPlayer.y = 0.0;
        float lenSq = dot(toPlayer, toPlayer);
        float direct = kDirectChaseCells * g_CellSize;

        // 高さの段 = 中心の下の生の高さセル（足ではない：踏み出す途中で縁に支えられた敵は、
        // 足はまだ台地の上でも、もう低いセルの経路に属している）
        float2 flow = FlowAt(pos.xz, SwarmTerrainHeightRaw(terrainHeight, pos.xz));
        // 近いが別の高さ（プレイヤーが上 / 下の台地にいる）：
        // 直線だと崖にぶつかるので、坂までフィールドに従い続ける
        bool sameLevel = abs(g_PlayerPos.y - pos.y) < 1.0;
        bool useFlow = (lenSq > direct * direct || !sameLevel) && (dot(flow, flow) > 0.01);
        if (SwarmFliesThrough(extra))
            useFlow = false; // 幽霊 / 飛んでいるガーゴイルはプレイヤーへまっすぐ飛ぶ（壁を抜け、台地を越える）
        if (useFlow)
            moveDir = normalize(float3(flow.x, 0.0, flow.y));
        else if (lenSq > 1e-6)
            moveDir = toPlayer * rsqrt(lenSq);
        // 詠唱する敵は距離を保つ（砂嵐の術師 2026-10-17、死霊術師 / 鎖の番人 2026-10-25）。近すぎる = 後退
        // （壁では下のハードブロックが止める）、帯の中 = 立ち止まる（分離だけ）、遠すぎる = 普通に追う
        float keepMin = 0.0, keepMax = 0.0;
        if (extra.kind == SWARM_KIND_SANDMAGE) { keepMin = g_SandMageKeepMin; keepMax = g_SandMageKeepMax; }
        else if (extra.kind == SWARM_KIND_NECRO) { keepMin = g_NecroKeepMin; keepMax = g_NecroKeepMax; }
        else if (extra.kind == SWARM_KIND_WARDEN) { keepMin = g_WardenKeepMin; keepMax = g_WardenKeepMax; }
        else if (extra.kind == SWARM_KIND_ELITE_B) { keepMin = g_FixedEliteKeepMin; keepMax = g_FixedEliteKeepMax; }   // 遠距離型の固定エリート（2026-10-25）
        // 浮かぶ砂嵐の魔神（2026-10-29）：CPU が g_BossKeepMax を設定している間（一段目）だけ。飛ぶので
        // 別の高さでも距離を保つ
        bool bossKeep = (extra.kind == SWARM_KIND_BOSS) && (g_BossKeepMax > 0.0);
        if (bossKeep) { keepMin = g_BossKeepMin; keepMax = g_BossKeepMax; }
        if (keepMax > 0.0 && (sameLevel || bossKeep))
        {
            if (lenSq < keepMin * keepMin)
            {
                moveDir = (lenSq > 1e-6) ? -toPlayer * rsqrt(lenSq) : float3(0, 0, 0);
                // 魔神は何の上でも後退するが、フィールドの外へは出ない：外周のセルは壁登りの表があっても雑魚には
                // 歩けないので、代わりに縁に沿って滑る（または角で立ち止まる）
                if (bossKeep && !SwarmIsWalkable(terrain, pos + moveDir * 3.0))
                {
                    float3 side = float3(-moveDir.z, 0.0, moveDir.x);
                    if (SwarmIsWalkable(terrain, pos + side * 3.0))
                        moveDir = side;
                    else if (SwarmIsWalkable(terrain, pos - side * 3.0))
                        moveDir = -side;
                    else
                        moveDir = float3(0, 0, 0);
                }
            }
            else if (lenSq <= keepMax * keepMax)
                moveDir = float3(0, 0, 0);
        }
        // 飛んでいるガーゴイル（2026-10-25）は g_GargoyleDiveMin から急降下距離 × 0.9 の間でプレイヤーの周りを回り、
        // 急降下がそこから始まってプレイヤーの上に降りるようにする（真上からだと頭の上を通り過ぎていた）
        if (extra.kind == SWARM_KIND_GARGOYLE && extra.fuse < SWARM_GARGOYLE_DIVE && lenSq > 1e-6)
        {
            float3 n = toPlayer * rsqrt(lenSq);
            float spin = ((i & 1u) != 0u) ? 1.0 : -1.0;
            if (lenSq < g_GargoyleDiveMin * g_GargoyleDiveMin)
                moveDir = normalize(-n + float3(-n.z, 0.0, n.x) * (0.6 * spin));
            else if (lenSq <= 0.81 * g_GargoyleDiveRange * g_GargoyleDiveRange)
                moveDir = float3(-n.z, 0.0, n.x) * (0.7 * spin);
        }
    }

    // ---- 分離：周りの 3x3 セルにいる生きた近傍 ----
    // 以前はプール 4096 全体の総当たりのループだった。空間ハッシュで
    // （9 セル内の生存数）に抑えている。g_SeparationRadius は
    // <= g_CellSize のままにすること。でないと 2 セル先の近傍を取りこぼす
    float3 sep = float3(0, 0, 0);
    float sepRadSq = g_SeparationRadius * g_SeparationRadius;

    int gx = (int) floor((pos.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((pos.z - g_GridOrigin.z) / g_CellSize);
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int cx = gx + dx, cz = gz + dz;
            if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                continue;
            uint cell = (uint) (cz * (int) g_GridW + cx);
            uint n = min(cellCount[cell], SWARM_BUCKET_CAP);
            for (uint k = 0u; k < n; ++k)
            {
                uint j = cellItems[cell * SWARM_BUCKET_CAP + k];
                if (j == i)
                    continue;

                float3 away = pos - enemies[j].position;
                // 別の高さ（上 / 下の台地にいる、または落ちて通り過ぎている）：触れていない。
                // 以前は崖の下の群れが上の縁にいる敵を押し戻していた
                if (abs(away.y) > 1.5)
                    continue;
                away.y = 0.0;
                float dSq = dot(away, away);
                if (dSq > sepRadSq || dSq < 1e-6)
                    continue;

                float dist = sqrt(dSq);
                sep += away / dist * (1.0 - dist / g_SeparationRadius);
            }
        }
    }

    // ---- 回避：隣の通れないセルから柔らかく押し離す（幽霊 / 飛んでいるガーゴイルには無し） ----
    bool ghost = SwarmFliesThrough(extra);
    float3 avoid = float3(0, 0, 0);
    float cs = g_CellSize;
    // 下りの崖はプレイヤーが低い所にいる間は開いている（SwarmDropAllowed）。回避とハードブロックで共用する
    bool allowDrop = SwarmDropAllowed(terrainHeight, pos.xz, pos.y - g_GroundY);
    // （崖の面はわざと回避しない：2026-10-20 に試したら、台地の側面から押し離す力が
    // 敵を斜め隣の木々へ押し込み、そこで詰まった）
    if (!ghost)
    {
        if (!SwarmIsWalkable(terrain, pos + float3(cs, 0, 0)))
            avoid.x -= 1.0;
        if (!SwarmIsWalkable(terrain, pos - float3(cs, 0, 0)))
            avoid.x += 1.0;
        if (!SwarmIsWalkable(terrain, pos + float3(0, 0, cs)))
            avoid.z -= 1.0;
        if (!SwarmIsWalkable(terrain, pos - float3(0, 0, cs)))
            avoid.z += 1.0;
    }

    // ---- 目標速度を組み立てる ----
    float3 target = moveDir * moveSpeed
                  + sep * g_SeparationPower
                  + avoid * g_AvoidPower;
    target.y = 0.0;

    // ---- 速度の上限：分離が重なると moveSpeed を大きく超えることがある ----
    float maxSpeed = moveSpeed * g_MaxSpeedMul;
    float tLenSq = dot(target, target);
    if (tLenSq > maxSpeed * maxSpeed)
        target *= maxSpeed * rsqrt(tLenSq);

    // ---- 慣性：目標へ指数的に近づける ----
    // g_Step は固定ステップなので、フレームレートに依存しない
    float k = 1.0 - exp(-g_VelocityLag * g_Step);
    float3 v = lerp(oldV, target, k);

    // ---- ボスの突進（2026-10-07）：CPU（BossAttacks）がボスを直線に沿って動かす ----
    // 慣性なし（すぐ突進に入る）、減速なし、プレイヤーの前で止まらない：突き抜ける。
    // 壁と崖では下のハードブロックが止める
    bool charging = (extra.kind == SWARM_KIND_BOSS) && (g_BossChargeOn > 0.5);
    if (charging)
        v = float3(g_BossChargeDir.x, 0.0, g_BossChargeDir.y) * g_BossChargeSpeed;
    // ---- 固定エリートの走る指示（騎兵の突進、2026-10-25）：CPU の線に沿ってまっすぐ、プレイヤーを突き抜ける ----
    bool eliteDash = false;
    if (eliteRow >= 0 && g_EliteCmd[eliteRow].z == SWARM_ELITE_CMD_MOVE)
    {
        float4 emv = g_EliteMove[eliteRow];
        v = float3(emv.x, 0.0, emv.y) * emv.z;
        charging = true;
        eliteDash = emv.z > 6.0;
    }
    // ---- 突撃兵の突進：溜めで固定した yaw に沿ってまっすぐ、プレイヤーを突き抜ける ----
    float dashSpeed = g_ChargerDashSpeed * slowMul;
    if (cphase == SWARM_CHARGE_DASH)
    {
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        v = float3(ys, 0.0, yc) * dashSpeed;
        charging = true;
    }
    // ---- 火の付いた突撃爆弾兵（溜め終わり）：火が付いたときに固定した yaw に沿ってまっすぐ、プレイヤーが
    //      いた所を突き抜けて走る（触れると ContactCS が爆発させる）。MoveCS は yaw を速度の方へ回すが、それがこの yaw なのでずれない ----
    if (dashing)
    {
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        v = float3(ys, 0.0, yc) * g_DashBomberDashSpeed;
        charging = true;
    }
    // ---- ガーゴイルの急降下（2026-10-25）：開始時に固定した yaw に沿ってまっすぐ降りてくる（高さは MoveCS が下げる） ----
    if (gargDive)
    {
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        v = float3(ys, 0.0, yc) * g_GargoyleDiveSpeed;
        charging = true;
    }

    // ---- プレイヤーは実体がある ----
    // 地形のハードブロックと同じ考えを円に対して使う：
    // 接触半径の内側へ入ってしまう速度成分を消し、接線方向の成分は
    // 残すので、群れはプレイヤーに積み重ならずに回り込んで
    // 周りを囲む。
    // すでに重なっている敵はやさしく押し出す。
    // プレイヤーと同じ高さのときだけ：プレイヤーの真上の台地の縁にいる敵は
    // 触れていないので、踏み出して落ちる必要がある（2026-10-01）
    if (g_PlayerAlive != 0u && abs(g_PlayerPos.y - pos.y) < 1.5 && !charging)
    {
        float3 toP = g_PlayerPos - pos;
        toP.y = 0.0;
        float dist = length(toP);
        float contact = g_PlayerRadius + g_EnemyRadius * SwarmSlotScale(i, extra.kind);   // 狂暴化した固定エリートはより遠くで止まる（2026-10-25）

        float3 n;
        if (dist > 1e-4)
            n = toP / dist;
        else
        {
            // ど真ん中：スロット番号から向きを選び、重なった敵が
            // 全員同じ向きに押すのではなく散らばるようにする
            float a = (float) i * 2.399;
            n = float3(cos(a), 0.0, sin(a));
            dist = 0.0;
        }

        float along = dot(v, n); // > 0 = プレイヤーへ向かって動いている
        float nextDist = dist - along * g_Step;

        if (along > 0.0 && nextDist < contact)
            v -= n * along;

        if (dist < contact)
            v -= n * (contact - dist) * g_PlayerPushOut;
    }

    // ---- ハードブロックは「最後に」かける（なめらか化とプレイヤーの押しの後） ----
    // なめらかにした速度には壁へ向かう古い成分がまだ残っていることがあり、
    // 以前はプレイヤーの押し出しがこの判定の後に来て、敵を木 / 壁へ
    // 押し込んでいた（2026-09-30）。先読み = 体の半径 +
    // 速度 * g_LookAhead なので、メッシュは体の半分まで壁に沈まずに
    // 壁の面で止まる。崖（急すぎる、SwarmSlopeOk 参照）は
    // 壁として数える：それを登る / 降りる軸を消すので、
    // 敵は台地の側面に沿って滑り、フローフィールドが坂へ
    // 導く。斜めも確かめる（以前は両軸とも空いていて角の
    // セルだけ塞がっていると、敵が角を突っ切っていた）。
    // すでに通れないセルの中にいる敵はこれを飛ばす：MoveCS が歩いて外へ出す。
    // プレイヤーが低い所にいる（SwarmDropAllowed）と、「下りの」崖は
    // 壁ではない：敵は縁から歩き出して落ちる（2026-10-01）
    if (!ghost && SwarmIsWalkable(terrain, pos))
    {
        float r = SwarmBodyWallRadius(extra.kind);   // MoveCS / PushCS と同じ体の半径
        float feet = pos.y - g_GroundY;
        // 突進中の突撃兵は 2 ステップ先しか見ない：14 m/s だと普段の
        // 先読み（0.35 秒 = 5 m）では、プレイヤーの後ろにある壁の 5 m 手前で突進が終わってしまう
        float look = (cphase == SWARM_CHARGE_DASH || dashing || eliteDash) ? 2.0 * g_Step : g_LookAhead;
        float ax = (v.x != 0.0) ? v.x * look + sign(v.x) * r : 0.0;
        float az = (v.z != 0.0) ? v.z * look + sign(v.z) * r : 0.0;

        // 壁登り（2026-10-29）：敵が「行きたい」軸（フローフィールド / 直線の追跡がそちらを向いている）は、
        // 前にそびえる面があっても止めない - MoveCS が登る。フローフィールドは、坂より近いときだけ
        // 登る方を選んでいる。突進と、群れ / プレイヤーの押しだけで面へ押された軸は以前どおり止める
        bool canClimb = g_ClimbSpeed > 0.0 && !(cphase == SWARM_CHARGE_DASH || dashing || eliteDash);
        bool climbX = canClimb && v.x != 0.0 && moveDir.x * sign(v.x) > 0.5;
        bool climbZ = canClimb && v.z != 0.0 && moveDir.z * sign(v.z) > 0.5;
        bool blockX = ax != 0.0 && ProbeBlocked(pos, pos.xz + float2(ax, 0), r, allowDrop, feet, climbX);
        bool blockZ = az != 0.0 && ProbeBlocked(pos, pos.xz + float2(0, az), r, allowDrop, feet, climbZ);
        float lostX = blockX ? v.x : 0.0;
        float lostZ = blockZ ? v.z : 0.0;
        if (blockX) v.x = 0.0;
        if (blockZ) v.z = 0.0;
        // 探針は中心線に沿って出す。中心が通れないセルの隣のセル境界ちょうどにあると
        // （木のセルの隣で x がちょうど -28.00）探針がそのセルに落ち、隣の空いた通路へ
        // 向かう軸が消され、敵が何分もその場で立ち尽くしたり回ったりしていた
        // （soak 2026-10-03）。両側にも探針を出し、片側だけ空いていれば
        // そちらへ滑らせる（MoveCS が角を横にかわす）。
        // 2026-10-20：横の探針にも体の判定（ProbeBlocked）を使い、体の半径 + 余裕の分だけ
        // 外に出す（ちょうど半径 1 つ分だと、そこの体がまだ角をかすっていた）。滑る速度は
        // 移動速度の半分以上にする：塞がれた軸は毎ステップ 0 から動き直すので（なめらか化）、
        // 失った速度は約 0.5 m/s しかなく、以前の「もう片方の軸がほぼ止まっているときだけ」の
        // 規則はほとんど発動せず、敵が角の横でぴくぴくしていた
        // もう片方の軸がほぼ止まっている間だけ（失った速度ではなく移動速度に対して）：
        // もう片方の軸の本当の速度を上書きして滑らせると、フローと争って
        // 敵が行ったり来たりぴくぴくした（2026-10-20 に試した）
        float side = r + 0.25;
        float slide = max(0.5 * moveSpeed, 0.0);
        if (blockZ && abs(v.x) < 0.25 * moveSpeed)
        {
            bool openL = !ProbeBlocked(pos, pos.xz + float2(-side, az), r, allowDrop, feet);
            bool openR = !ProbeBlocked(pos, pos.xz + float2(side, az), r, allowDrop, feet);
            if (openL != openR)
                v.x = (openL ? -1.0 : 1.0) * max(abs(lostZ), slide);
        }
        if (blockX && abs(v.z) < 0.25 * moveSpeed)
        {
            bool openL = !ProbeBlocked(pos, pos.xz + float2(ax, -side), r, allowDrop, feet);
            bool openR = !ProbeBlocked(pos, pos.xz + float2(ax, side), r, allowDrop, feet);
            if (openL != openR)
                v.z = (openL ? -1.0 : 1.0) * max(abs(lostX), slide);
        }
        // 崖も角として数える（2026-10-20）：台地のセルは歩けるので、歩けるかの判定だけでは
        // 敵が段々の台地の角へ斜めに押し込めてしまった。すると MoveCS の体の
        // 円が、その一歩でも両方の滑りでも角に引っかかり、横へかわす
        // 向きも決まらなかった（押し出しが速度に沿ってまっすぐ後ろを向いていた）
        if (v.x != 0.0 && v.z != 0.0 && ProbeBlocked(pos, pos.xz + float2(ax, az), r, allowDrop, feet, climbX && climbZ))
        {
            // 角：余裕のある方の軸を残し、もう片方を消す
            if (abs(v.x) > abs(v.z)) v.z = 0.0; else v.x = 0.0;
        }
    }
    // 壁 / 崖にぶつかった突撃兵は、そこで突進をやめて息切れに入る
    if (cphase == SWARM_CHARGE_DASH && dot(v.xz, v.xz) < 0.25 * dashSpeed * dashSpeed)
        enemyExtra[i].fuse = g_ChargerWindup + SwarmChargerDashTime() + g_Step;
    v.y = oldV.y;   // 落下速度は MoveCS のもの（重力）、AI は xz だけを操る
    enemies[i].velocity = v;
}