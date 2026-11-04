// ============================================================
// SwarmContactCS.hlsl
// 敵がプレイヤーに触れる -> ダメージ。敵スロット 1 つに 1 スレッド。
//
// どちらの体も縦向きのカプセルなので、判定は XZ の円の距離と、
// 2 本の直線部分の間の縦の隙間を合わせたもの。
// 群れの上を跳び越えると縦の隙間が空いて当たらない。
// これは意図どおり。
//
// 敵ごとに自分のクールダウンを持つので、30 体の群れでも 1 ステップで
// 30 倍のダメージにはならない。ダメージは x100 の固定小数でカウンタに入れ、
// CPU が差分を取ってプレイヤーのステートマシンに渡す（無敵フレームは
// そちらが持つ）。
//
// 自爆兵（enemyExtra.kind）は殴らない。プレイヤーに触れると導火線に
// 火が付き、g_BomberFuseTime 後に爆発する：まだ g_BomberBlastRadius 内に
// いればプレイヤーは g_BomberBlastDamage を受け、スロットは DEAD になり
// （撃破数に数えず経験玉も出さない：プレイヤーが倒したのではない）、GPU の
// 爆発エフェクト用だけにダメージ 0 の範囲を残す。火が付いた自爆兵
// （と突撃爆弾兵）はダメージを受けない（SwarmInvulnerable、2026-10-18）。
// 倒せる（通常の撃破 + 経験玉）のは火が付く前だけ。
//
// 当たるたびに、どこから来たかも playerHits（u6）に記録し、
// CPU がプレイヤーをノックバックできるようにする（殴りは少し、爆発は約 3 倍 + 跳ね上げ）。
//
// 凍った敵（enemySlow.z > 0、アイスランス）は当たらず、導火線にも火が付かない。
// ダッシュ中の突撃兵は g_ChargerDashDamageMul 倍強く当たり、爆発のように
// プレイヤーをノックバックする（2026-10-08）。
//
// 生きている敵の Enemy.attackCooldown を書くのはここだけ（HitCS /
// AreaDamageCS の撃破側は、死んだスロットのそれに撃破の属性を残す。SwarmKillElementMark）。
// 普通の自爆兵の SwarmEnemyExtra.fuse と、突撃爆弾兵が爆発した時のそれ（0 に戻す）も
// ここだけが書く。他の種類の fuse は AICS のもの。
// ============================================================
#define SWARM_AREA_POOL_U u4
#define SWARM_AREA_STATE_U u5
#define SWARM_AREA_DEF_T t0
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<float4> enemySlow : register(t1); // z > 0 = 凍結中

RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);
RWBuffer<uint> enemyStates : register(u2);
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u3);
// プレイヤーが受けた攻撃の方向。CPU のノックバック用（2026-10-01）。
// Swarm::PlayerHitInfo：殴り x, z, 回数、爆発 x, z, 回数、トゲ x, z, 回数、風 x, z, 回数（2026-10-17）。
// x / z = 単位ベクトル（敵または爆心 -> プレイヤー）の和 * HIT_DIR_SCALE。
// クリアしない：CPU がカウンタと同じく差分を取る
RWByteAddressBuffer playerHits : register(u6);
static const float HIT_DIR_SCALE = 1000.0; // = Swarm::kHitDirScale

// `from` から来た攻撃 1 回を、バイトオフセット `base` の記録に足す
void RecordHit(uint base, float3 from)
{
    float2 d = g_PlayerPos.xz - from.xz;
    float len = length(d);
    d = (len > 1e-4) ? d / len : float2(0.0, 0.0);
    uint prev;
    playerHits.InterlockedAdd(base + 0u, asuint((int) round(d.x * HIT_DIR_SCALE)), prev);
    playerHits.InterlockedAdd(base + 4u, asuint((int) round(d.y * HIT_DIR_SCALE)), prev);
    playerHits.InterlockedAdd(base + 8u, 1u, prev);
}

// AI パスは敵を (enemyRadius + playerRadius) のすぐ「外」で止めるので、
// 半径ぴったりの判定だとプレイヤーが立ち止まっている間は一度も当たらない。
// 止まった時の隙間は約 1 cm。skin はそれを余裕をもって埋める。
static const float CONTACT_SKIN = 0.05;

// プレイヤーのカプセルが敵のカプセルから `reach` 以内か（XZ + 縦の隙間）。
// halfLen = 敵カプセルの直線部分の半分（エリートは大きい）
bool PlayerWithinCapsule(float3 pos, float halfLen, float reach)
{
    float dx = pos.x - g_PlayerPos.x;
    float dz = pos.z - g_PlayerPos.z;
    float dxzSq = dx * dx + dz * dz;

    // 2 本の直線部分の間の縦の隙間（重なっていれば 0）
    float dy = abs(pos.y - g_PlayerPos.y);
    float gap = max(0.0, dy - (halfLen + g_PlayerCapsuleHalf));
    return dxzSq + gap * gap <= reach * reach;
}

bool PlayerWithin(float3 pos, float reach)
{
    return PlayerWithinCapsule(pos, g_EnemyCapsuleHalf, reach);
}

// 自爆兵 / 突撃爆弾兵の爆発：`radius` 内のプレイヤーは `dmg` と爆発のノックバックを受け、スロットは DEAD になる
// （撃破なし、経験玉なし）。見た目は BomberBlast の範囲を `scale` 倍したもの（その元の半径は自爆兵のもの）
void Explode(uint i, float3 pos, float radius, float dmg, float scale)
{
    if (g_PlayerAlive != 0u && PlayerWithin(pos, radius + g_PlayerRadius))
    {
        uint prev;
        counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE, SwarmHpToFixed(dmg), prev);
        RecordHit(12u, pos);   // 爆発：自爆兵から
    }
    enemyStates[i] = SWARM_DEAD;
    enemyExtra[i].fuse = 0.0;
    SwarmSpawnAreaFromDef(g_BomberBlastArea, pos, i, scale, 0u, float2(0.0, 0.0));
}

void UpdateBomber(uint i, SwarmEnemyExtra extra)
{
    float3 pos = enemies[i].position;

    if (extra.fuse <= 0.0)
    {
        // ---- 未点火：触れたら火を付ける ----
        float reach = g_EnemyRadius + g_PlayerRadius + g_BomberTriggerMargin;
        if (g_PlayerAlive != 0u && PlayerWithin(pos, reach))
            enemyExtra[i].fuse = g_Step;
        return;
    }

    // ---- 点火済み：数え上げて、最後に爆発 ----
    // その間にプレイヤーが死んでも燃え続ける（爆発が誰にも当たらないだけ）
    float fuse = extra.fuse + g_Step;
    if (fuse < g_BomberFuseTime)
    {
        enemyExtra[i].fuse = fuse;
        return;
    }

    Explode(i, pos, g_BomberBlastRadius, g_BomberBlastDamage, 1.0);
}

// 突撃爆弾兵（2026-10-18）：AICS が火を付け、g_DashBomberWindup 秒止めてから yaw 方向へ走らせる
// （fuse はそちらで数え上げる）。ここではプレイヤーに触れたステップ、または溜めの g_DashBomberFuse 秒後
// （走り抜けた）に爆発させる。殴りはしない
void UpdateDashBomber(uint i, SwarmEnemyExtra extra)
{
    if (extra.fuse <= 0.0)
        return;
    float3 pos = enemies[i].position;
    float reach = g_EnemyRadius * g_DashBomberScale + g_PlayerRadius + CONTACT_SKIN;
    bool touch = g_PlayerAlive != 0u && PlayerWithinCapsule(pos, g_EnemyCapsuleHalf * g_DashBomberScale, reach);
    if (touch || extra.fuse >= g_DashBomberWindup + g_DashBomberFuse)
        Explode(i, pos, g_DashBomberBlastRadius, g_BomberBlastDamage * g_DashBomberDamageMul,
                g_DashBomberBlastRadius / max(g_BomberBlastRadius, 0.1));
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
    bool frozen = enemySlow[i].z > 0.0;
    if (extra.kind == SWARM_KIND_BOMBER)
    {
        // 凍結中：火が付いた導火線は燃え続ける（爆弾なので）。未点火のものは触れても火が付かない
        if (!frozen || extra.fuse > 0.0)
            UpdateBomber(i, extra);
        return;
    }
    if (extra.kind == SWARM_KIND_DASHBOMBER)
    {
        UpdateDashBomber(i, extra);   // （2026-10-18）火が付いたものは凍らない（AICS）。未点火のものは殴らない
        return;
    }
    if (extra.kind == SWARM_KIND_BOSS && (SwarmBuried(extra) || g_BossNoContact > 0.5))
        return;   // 砂に潜った砂の守護者 / 飛びかかり中の狼人は触れない（技自体の当たりは CPU が判定する）
    // 技の途中 / 消えている固定エリートは殴らない：技自体の当たりは CPU が判定する（2026-10-25）
    if (SwarmIsFixedElite(extra.kind) && (SwarmEliteFlags(i) & SWARM_ELITE_NO_CONTACT) != 0u)
    {
        enemies[i].attackCooldown = max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_BURROWER)
    {
        // 砂潜り：触れても噛まない。噛むのは出てきた時の 1 回：出てから g_BurrowBiteDelay 秒後
        // （fuse は g_BurrowSurfaceTime から数え下げ、しきい値をまたいだステップがそれ）に
        // g_BurrowBiteRadius 内の全員に当たり、その後また潜る（2026-10-17）
        float f = extra.fuse;
        float thr = g_BurrowSurfaceTime - g_BurrowBiteDelay;
        if (f > 0.0 && f <= thr && f + g_Step > thr && g_PlayerAlive != 0u && !frozen)
        {
            float3 center;
            float er, eh;
            SwarmKindCapsule(extra.kind, enemies[i].position, center, er, eh);
            if (PlayerWithinCapsule(center, eh, g_BurrowBiteRadius + g_PlayerRadius))
            {
                float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind);
                uint prev;
                counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE, SwarmHpToFixed(dmg), prev);
                RecordHit(0u, enemies[i].position);   // 殴りと同じ大きさのノックバック
                enemies[i].attackCooldown = g_AttackInterval;   // VS はこれを見て攻撃の振りを再生する
            }
        }
        else
            enemies[i].attackCooldown = max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_SANDMAGE || extra.kind == SWARM_KIND_NECRO)
    {
        // 砂嵐の術師 / 死霊術師：殴らない。溜めの間（fuse > 0、AICS）に攻撃の振りを再生する（2026-10-17 / 10-25）
        enemies[i].attackCooldown = (extra.fuse > 0.0) ? g_AttackInterval : max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_WARDEN && extra.fuse > 0.0)
    {
        // 投げの溜め中の鎖の番人：振る（手の中の鉤）だけで殴らない（2026-10-25）
        enemies[i].attackCooldown = g_AttackInterval;
        return;
    }
    // 石像ガーゴイル（2026-10-25）：眠り / 目覚め / 飛行中は触れない（空中にいる）。急降下は
    // 突撃兵のダッシュと同じく当たる（下）。着地中は普通に殴る
    bool gargDive = SwarmGargoyleDiving(extra);
    if (extra.kind == SWARM_KIND_GARGOYLE && !gargDive && !SwarmGargoyleLanded(extra))
    {
        enemies[i].attackCooldown = max(enemies[i].attackCooldown - g_Step, 0.0);
        return;
    }
    if (extra.kind == SWARM_KIND_TORNADO)
    {
        // 砂旋：寿命切れ（AICS が fuse < 0 にした）-> ここでスロットを殺す。撃破なし、経験玉なし（プレイヤーが倒したのではない）。
        // プレイヤーに触れたら：接触ダメージ x g_TornadoDamageMul と「風」のノックバック（スロット 36：高く飛ぶ）。
        // 専用のクールダウンを持ち、プレイヤーが中にいる間に 1 本の砂旋が毎ステップ当たらないようにする（2026-10-17）
        if (extra.fuse < 0.0)
        {
            enemyStates[i] = SWARM_DEAD;
            return;
        }
        float cd = enemies[i].attackCooldown - g_Step;
        if (cd <= 0.0 && g_PlayerAlive != 0u)
        {
            float3 center;
            float er, eh;
            SwarmKindCapsule(extra.kind, enemies[i].position, center, er, eh);
            if (PlayerWithinCapsule(center, eh, er + g_PlayerRadius + CONTACT_SKIN))
            {
                float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind);
                uint prev;
                counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE, SwarmHpToFixed(dmg), prev);
                RecordHit(36u, enemies[i].position);
                cd = g_AttackInterval;
            }
        }
        enemies[i].attackCooldown = max(cd, 0.0);
        return;
    }

    float cd = enemies[i].attackCooldown - g_Step;

    if (cd <= 0.0 && g_PlayerAlive != 0u && !frozen)
    {
        // エリート / ボス：カプセルが大きく、当たりが強い（狂暴化した固定エリートは大きくなる、2026-10-25）
        float3 center;
        float er, eh;
        SwarmSlotCapsule(i, extra.kind, enemies[i].position, center, er, eh);
        float reach = er + g_PlayerRadius + CONTACT_SKIN;
        if (PlayerWithinCapsule(center, eh, reach))
        {
            bool charge = (extra.kind == SWARM_KIND_CHARGER)
                     && (SwarmChargerPhase(extra.fuse) == SWARM_CHARGE_DASH);
            bool dash = charge || gargDive;   // ガーゴイルの急降下は突撃兵のダッシュと同じく当たる（2026-10-25）
            float dmg = g_ContactDamage * SwarmKindDamageMul(extra.kind)
                      * (charge ? g_ChargerDashDamageMul : gargDive ? g_GargoyleDiveDamageMul : 1.0);
            uint prev;
            counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE,
                                    SwarmHpToFixed(dmg), prev);
            // 殴り：この敵から。突撃兵のダッシュは爆発のようにノックバックし、トゲ玉は
            // プレイヤーを弾き飛ばす（専用のスロット 24：CPU がより遠く高く飛ばす）（2026-10-17）。
            // 最終ウェーブの幽霊 2 種も爆発のように当たる（2026-10-18、Megabonk の幽霊はプレイヤーをマップの端まで叩き飛ばす）
            uint slotBase = (extra.kind == SWARM_KIND_SPIKEBALL) ? 24u : ((dash || SwarmIsGhost(extra.kind)) ? 12u : 0u);
            RecordHit(slotBase, enemies[i].position);
            cd = g_AttackInterval;
        }
    }

    enemies[i].attackCooldown = max(cd, 0.0);
}
