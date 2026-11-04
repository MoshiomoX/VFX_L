// ============================================================
// SwarmHitCS.hlsl
// 弾 vs 敵。弾 1 発に 1 スレッド。固定ステップで ProjMoveCS の後に
// 走るので、位置はこのステップの確定値。
//
// ダメージは固定小数点の hp に InterlockedAdd で与えるので、
// 同じステップに何発の弾が同じ敵に当たってもよい。hp が 0 を
// 跨ぐのを見るのはちょうど 1 発だけで、それがとどめになる。
// 残りは生きている敵か死体のどちらかを見る。
//
// prev（足す前の hp）で何に当たったかが分かる:
//   prev == 0 or prev >= 0x80000000  -> 死体（もう死んでいる / 桁あふれ）
//   prev <= dmg                      -> この一発で倒した
//   otherwise                        -> まだ生きている
//
// 貫通はまだ無い: 弾は生きている敵に最初に当たったところで消える。
// 死体は弾を消費せずに素通りする。
//
// どの敵か（2026-10-25）: 以前は敵スロットを全部昇順に走査し、
// 届く範囲で最初に見つかった生きている敵を取っていた。結果は
// 同じだが、スロットは空間ハッシュ（SwarmEnemyBinCS、このステップの
// 最初に埋める）から取る: 判定を通る候補のうち一番小さいスロットに
// 当たり、それが死体なら次に小さいもの、と続ける。
// 問い合わせ範囲は粗い到達距離 +
// HIT_HASH_MOVE_MARGIN（ビン分けからこのパスまでに敵が動く）。
// 固定エリートのスロット（g_EliteCmd）は常に追加で判定する（瞬間移動で
// どこまでも跳ぶため）。問い合わせがグリッドの外に出る（外の敵は
// ビンに入らない）、あふれたバケツに触れる、セルが多すぎる、
// ハッシュがバインドされていない、のどれかなら以前通り全スロットを走査する。
//
// エリート / ボスはカプセルが大きく（SwarmSlotCapsule）、落とす経験玉は
// SwarmKindExpMul 倍。種類を読むのは、一番大きい体に合わせた粗い判定を
// 通った敵だけ。
// 盾兵は SwarmArmorDamage を受ける。motion に freezeTime を持つ弾
// （アイスランス）は animIndex に凍結リクエストを残す（2026-10-08）。
// とどめを刺した弾は、死んだスロットの attackCooldown に自分の属性を残す
// （SwarmKillElementMark、とどめの数字の色用、2026-10-25）。
// ============================================================
// 当たった所に範囲を残すことがある（爆発 / 燃える地面）
#define SWARM_AREA_POOL_U u6
#define SWARM_AREA_STATE_U u7
#define SWARM_AREA_DEF_T t2
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);
Buffer<uint> projBoost : register(t4); // この弾が残す範囲の威力 / 持続の倍率
Buffer<uint> projTags : register(t5);  // 連鎖のタグ。最上位ビット = 吸血の弾（SWARM_TAG_LIFESTEAL、2026-10-25）
// 生きている敵の空間ハッシュ（SwarmEnemyBinCS、このステップの最初）。未バインド = 全スロットを走査
StructuredBuffer<uint> cellCount : register(t6);
StructuredBuffer<uint> cellItems : register(t7);

StructuredBuffer<SwarmProjectile> projectiles : register(t0);
RWBuffer<uint> projStates : register(u0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u1);
RWBuffer<uint> enemyStates : register(u2);
RWByteAddressBuffer counters : register(u3);
RWStructuredBuffer<SwarmOrb> orbs : register(u4);
RWBuffer<uint> orbStates : register(u5);
static const uint HP_CORPSE_BIT = 0x80000000u;

// このステップの最初にビンを埋めてから敵が動ける距離（m）（AI の速度、突風の
// 押し、群れの押し合い）: 0.02 s のステップで ~150 m/s 未満なら収まる
static const float HIT_HASH_MOVE_MARGIN = 3.0;
// 1 回の問い合わせでこれより多いセル: 全走査とほぼ同じコストになる
static const int HIT_HASH_MAX_CELLS = 256;
static const uint HIT_NONE = 0xFFFFFFFFu;

// 以前の全走査が各スロットで行っていた判定。順序も比較も同じ
// （位置が NaN のときの振る舞いも以前通り）。出口は 1 つ（途中で return すると fxc が X4000 を出す）
bool CanHit(uint j, float3 ppos, float pradius, float coarseSq, uint lastHit)
{
    bool ok = (j < g_MaxEnemies) && (j != lastHit);
    if (ok)
        ok = (enemyStates[j] != SWARM_DEAD);
    float3 pos = float3(0, 0, 0);
    if (ok)
    {
        pos = enemies[j].position;
        float3 dc = ppos - pos;
        ok = !(dot(dc, dc) > coarseSq);
    }
    if (ok)
    {
        SwarmEnemyExtra extra = enemyExtra[j];
        ok = !SwarmUntargetable(extra);   // 砂の下: 当たらない（2026-10-17）
        if (ok)
        {
            float3 center;
            float er, eh;
            SwarmSlotCapsule(j, extra.kind, pos, center, er, eh);   // + 狂暴エリートの巨大化（2026-10-25）
            float3 d = ppos - center;
            d.y -= clamp(d.y, -eh, eh);
            float hitRadius = pradius + er;
            ok = !(dot(d, d) > hitRadius * hitRadius);
        }
    }
    return ok;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;
    if (projStates[i] == SWARM_DEAD)
        return;

    SwarmProjectile p = projectiles[i];

    // 落ちてくるメテオ（DROP）/ 投げた毒瓶（LOB）は途中で何にも
    // 当たらない。落ちた所に ProjMoveCS が範囲を残す
    if (SwarmMotionLands(motions[p.motion & SWARM_MOTION_INDEX_MASK].mode))
        return;

    // プールの位置を中心に、一番大きい体が収まる粗い球
    // （+ 狂暴で大きくなることのある固定エリート、2026-10-25）
    float enraged = max(1.0, g_EliteEnragedScale);
    float kMax = max(max(1.0, max(g_EliteScale, g_BossScale)),
                     max(g_FixedEliteAScale, g_FixedEliteBScale) * enraged);
    float coarse = p.radius + kMax * 2.0 * (g_EnemyRadius + g_EnemyCapsuleHalf);
    float coarseSq = coarse * coarse;
    // アイスランス（2026-10-08）: 当たった敵がこの秒数だけ凍る（リクエストは AICS が受け取る）
    float freezeTime = motions[p.motion & SWARM_MOTION_INDEX_MASK].freezeTime;
    // 連鎖のルーン（2026-10-16）: 連鎖中の弾は跳ぶ回数が残っている間は生きた敵に当たっても消えず、
    // 跳んできた元の敵には二度と当たらない（SwarmCommon.hlsli を参照）
    uint state = projStates[i];
    bool chained = (state & SWARM_PROJ_CHAINED) != 0u;
    bool pierced = chained && (state & SWARM_PROJ_PIERCE) != 0u;   // 跳ぶ代わりに貫通する（2026-10-28）
    uint chainsLeft = chained ? ((state >> SWARM_PROJ_CHAIN_LEFT_SHIFT) & SWARM_PROJ_CHAIN_LEFT_MASK) : 0u;
    uint lastHit = chained ? (state & SWARM_PROJ_CHAIN_LAST_MASK) : SWARM_PROJ_CHAIN_NO_LAST;

    // ---- どのセルを見るか（冒頭を参照）。判定を通りうるものは xz で `coarse` 以内にある ----
    uint hashCells, hashStride;
    cellCount.GetDimensions(hashCells, hashStride);
    float reach = coarse + HIT_HASH_MOVE_MARGIN;
    bool useHash = (hashCells >= g_GridW * g_GridD) && g_GridW > 0u && g_GridD > 0u && g_CellSize > 0.0
                && all(abs(p.position) < 1e20) && reach < 1e6;
    int x0 = 0, x1 = -1, z0 = 0, z1 = -1;
    if (useHash)
    {
        x0 = (int) floor((p.position.x - reach - g_GridOrigin.x) / g_CellSize);
        x1 = (int) floor((p.position.x + reach - g_GridOrigin.x) / g_CellSize);
        z0 = (int) floor((p.position.z - reach - g_GridOrigin.z) / g_CellSize);
        z1 = (int) floor((p.position.z + reach - g_GridOrigin.z) / g_CellSize);
        useHash = x0 >= 0 && z0 >= 0 && x1 < (int) g_GridW && z1 < (int) g_GridD
               && (x1 - x0 + 1) * (z1 - z0 + 1) <= HIT_HASH_MAX_CELLS;
    }
    if (useHash)
    {
        // あふれたバケツは敵の一部を落としている: それを見逃してしまう
        [loop]
        for (int oz = z0; oz <= z1; ++oz)
        {
            [loop]
            for (int ox = x0; ox <= x1; ++ox)
            {
                if (cellCount[(uint) (oz * (int) g_GridW + ox)] > SWARM_BUCKET_CAP)
                    useHash = false;
            }
        }
    }

    uint after = 0u;   // 次の候補はこのスロット以上（以前の全走査と同じ昇順）
    [loop]
    for (uint guard = 0u; guard <= g_MaxEnemies; ++guard)
    {
        // ---- 当たりうる after 以上の一番小さいスロット ----
        uint j = HIT_NONE;
        if (useHash)
        {
            [loop]
            for (int cz = z0; cz <= z1; ++cz)
            {
                [loop]
                for (int cx = x0; cx <= x1; ++cx)
                {
                    uint cell = (uint) (cz * (int) g_GridW + cx);
                    uint n = min(cellCount[cell], SWARM_BUCKET_CAP);
                    [loop]
                    for (uint k = 0u; k < n; ++k)
                    {
                        // if を入れ子にする: HLSL の && は短絡評価しないので、判定はより良いスロットのときだけ走らせる
                        uint c = cellItems[cell * SWARM_BUCKET_CAP + k];
                        if (c >= after && c < j)
                        {
                            if (CanHit(c, p.position, p.radius, coarseSq, lastHit))
                                j = c;
                        }
                    }
                }
            }
            // 固定エリートはこのステップで自分のビンから瞬間移動しているかもしれない
            [unroll]
            for (int r = 0; r < 4; ++r)
            {
                uint s1 = g_EliteCmd[r].x;
                if (s1 != 0u && s1 - 1u >= after && s1 - 1u < j)
                {
                    if (CanHit(s1 - 1u, p.position, p.radius, coarseSq, lastHit))
                        j = s1 - 1u;
                }
            }
        }
        else
        {
            [loop]
            for (uint c = after; c < g_MaxEnemies; ++c)
            {
                if (CanHit(c, p.position, p.radius, coarseSq, lastHit))
                {
                    j = c;
                    break;
                }
            }
        }
        if (j == HIT_NONE)
            return;

        SwarmEnemyExtra extra = enemyExtra[j];
        uint kind = extra.kind;
        // ---- 命中: ダメージをアトミックに与える（盾兵の装甲を先に引く） ----
        // 貫通する弾（黄金の矢、2026-10-28）は、すでに貫いた敵 1 体ごとに SWARM_PIERCE_FALLOFF を掛ける
        float shotDamage = p.damage;
        if (pierced)
            shotDamage *= pow(SWARM_PIERCE_FALLOFF, (float) (SwarmMotionPierce(motions[p.motion & SWARM_MOTION_INDEX_MASK]) - min(chainsLeft, 15u)));
        uint dmgFixed = SwarmHpToFixed(SwarmTakenDamage(j, kind, shotDamage));
        uint prev;
        InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);

        if (prev == 0u || prev >= HP_CORPSE_BIT)
        {
            after = j + 1u;
            continue; // 死体。このステップで別の弾がもう倒した
        }

        // ---- 被弾硬直: 停止 + 点滅（MoveCS / AICS / VS が読む） ----
        // ただの書き込み: 同じステップに何発当たっても書く値は同じ。
        // とどめの場合も無害（そのスロットはすぐ下で DEAD になる）。
        // ボスは硬直しない（撃たれ続けて釘付けになる）。溜めを始めた
        // 突撃兵も同じ（当たるたびに MoveCS が突進を止めてしまう）。
        // 氷の弾は硬直の代わりに凍結リクエストを残す
        bool noFlinch = SwarmNoFlinch(extra);   // （+ 最終ウェーブの幽霊 2 種、2026-10-18）
        if (freezeTime > 0.0)
        {
            enemies[j].animIndex = SWARM_ANIM_FREEZE_REQ;
            enemies[j].animTime = freezeTime;
        }
        else if (!noFlinch)
        {
            enemies[j].animIndex = 2u;
            enemies[j].animTime = 0.0;
        }

        if (prev > dmgFixed)
        {
            // とどめではない: 何が当たったかを覚える（ダメージ数字の色）。CAS にするのは、その間に別スレッドの
            // とどめが書いた DEAD を ALIVE に戻してしまわないため（2026-10-17）
            uint cur = enemyStates[j];
            if (cur != SWARM_DEAD)
            {
                uint wasState;
                // + 吸血の印: この敵の hp の減りを SwarmDamageTextCS がプレイヤーの回復に数える（2026-10-25）
                const uint steal = ((projTags[i] & SWARM_TAG_LIFESTEAL) != 0u) ? SWARM_STATE_LIFESTEAL : 0u;
                InterlockedCompareExchange(enemyStates[j], cur,
                    SWARM_ALIVE | (SwarmMotionElement(motions[p.motion & SWARM_MOTION_INDEX_MASK]) << SWARM_STATE_ELEMENT_SHIFT) | steal, wasState);
            }
        }

        if (prev <= dmgFixed)
        {
            // ---- この一発がとどめ ----
            enemyStates[j] = SWARM_DEAD;
            // とどめの数字の色 = この弾の属性（状態ワードはもう DEAD。2026-10-25）
            enemies[j].attackCooldown = SwarmKillElementMark(SwarmMotionElement(motions[p.motion & SWARM_MOTION_INDEX_MASK]));
            counters.InterlockedAdd(SWARM_CNT_KILLS, 1u, prev);

            // ---- 死体の所に経験玉を落とす ----
            // SpawnProjCS と同じ CAS の走査。開始位置を（敵、弾）で
            // ばらして、同じステップのとどめ同士が同じ区間を
            // 取り合わないようにする。プールが満杯 -> 経験玉なし。
            SwarmOrb orb;
            // 死体の足元（台地の上: position.y - groundY = 地形の高さ）
            orb.position = enemies[j].position;
            orb.position.y = enemies[j].position.y - g_GroundY + g_OrbY;
            orb.amount = g_OrbAmount * SwarmKindExpMul(kind);
            orb.velocity = float3(0, 0, 0);
            orb._pad = 0.0; // 吸引速度、0 = まだ吸われていない

            uint start = (j * 97u + i * 31u) % g_MaxOrbs;
            for (uint k = 0; k < g_MaxOrbs; ++k)
            {
                uint slot = (start + k) % g_MaxOrbs;
                uint was;
                InterlockedCompareExchange(orbStates[slot],
                                           SWARM_DEAD, SWARM_ALIVE, was);
                if (was == SWARM_DEAD)
                {
                    orbs[slot] = orb;
                    break;
                }
            }
        }

        // ---- 命中時の範囲（爆発など）。同じステップで tick する:
        // このシェーダのすぐ後に AreaTickCS / AreaDamageCS が走る ----
        SwarmSpawnAreaFromDefX(motions[p.motion & SWARM_MOTION_INDEX_MASK].hitArea,
                               p.position, i + j, SwarmProjScale(p), projBoost[i], p.velocity.xz,
                               ((projTags[i] & SWARM_TAG_LIFESTEAL) != 0u) ? SWARM_AREA_EXTRA_LIFESTEAL : 0u);

        // 弾はどちらにしても消費される -- ただし連鎖の跳躍 / 貫通が残っていれば別:
        // そのときは生きたままこの敵を覚える。連鎖中の弾は次の標的を ProjMoveCS に頼み、
        // 貫通する弾はそのまま飛び続ける（2026-10-28）
        if (chainsLeft > 0u)
            projStates[i] = SwarmProjChainState(chainsLeft - 1u, j) | (pierced ? SWARM_PROJ_PIERCE : SWARM_PROJ_CHAIN_PENDING);
        else
            projStates[i] = SWARM_DEAD;
        return;
    }
}
