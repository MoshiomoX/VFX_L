// ============================================================
// SwarmAreaDamageCS.hlsl
// 範囲ダメージ。敵 1 体に 1 スレッドで、範囲スロットを走査する。
//
// 範囲ごとではなく敵ごとにスレッドを割り当てるのは意図的: このステップで
// tick する範囲を全部先に合算し、hp はこのディスパッチでこの敵を受け持つ
// 唯一のスレッドが 1 回だけ触る。なので hp の競合はここでは一切起きず、
// 円 3 つに立っている敵も倒されるのは 1 回、落とす経験玉は 1 個、
// 撃破数も 1 になる。
//
// このステップに tick する範囲が無いとき（ほぼ毎ステップ）は、各スレッドが
// カウンタを 1 回読むだけで戻る。
//
// 撃破の処理は SwarmHitCS と同じ: state -> DEAD、撃破カウンタ、
// 経験玉。両者は揃えておくこと（エリートの体格と経験玉の値も）。
//
// 減速ビット（SwarmAreaSlow）を持つ範囲が tick すると、その敵の
// enemySlow も更新する（2026-10-01、毒沼）。
// 盾兵の装甲は範囲の tick ごとに別々に引かれる
// （SwarmArmorDamage、2026-10-08）: 沼 3 つなら小さな 3 発になる。
//
// 毒素（2026-10-17、毒沼）: 毒素の割合（a.tickNow の SwarmAreaToxin）を
// 持つ範囲は、その場では残りの分しか与えない。割合の分は
// enemyPoison[slot] =（残りダメージ、毎秒のダメージ、最後に盛られてからの秒）
// に足され、ここで毎ステップ減らしていく（撃破の経路は
// 同じ）。同じレシピ（vfxType）の毒素範囲は 1 体の敵に重ならない
// （強い方だけ: 二重詠唱の猛毒の沼 2 つは 1 回分）が、別レシピなら重なる
// （2026-10-26: 猛毒の沼の上の胞子の霧は両方効く）。
// 飛沫だけの範囲（泥沼）は最初の tick だけ
// ダメージを与える。
//
// 燃える草（2026-11-01、GrassFireField）: 燃えている texel の上に立つ敵は
// g_FireTick 秒ごとに g_FireDamage を受ける。スロットごとに拍をずらし、
// 撃破経路も同じ。飛んでいる敵と埋まっている敵には届かない。
// ============================================================
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> areas : register(t0);
Buffer<uint> areaStates : register(t1);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t2);
StructuredBuffer<float4> areaEnds : register(t3); // スロットごとのカプセル（光線）の終点。AreaTickCS が書く
Texture2D<float4> g_FireMap : register(t4);       // z = 燃焼中（GrassFireField）。g_FireOn の間だけバインド

RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWBuffer<uint> enemyStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<SwarmOrb> orbs : register(u3);
RWBuffer<uint> orbStates : register(u4);
// x / y = 減速（残り秒、強さ）。AICS が読んでカウントダウンする。z / w = 凍結（ここでは触らない）
RWStructuredBuffer<float4> enemySlow : register(u5);
// ノックバック（突風、2026-10-12）: xy = プレイヤーから離れる向きの速度、z = 残り秒。MoveCS が適用して減衰させる。
// w = 1: 代わりに渦の中心への引き寄せ（2026-10-22）。押しが来たら押しで上書きされる
RWStructuredBuffer<float4> enemyPush : register(u6);
// 溜まった毒素（2026-10-17）: x = まだ減らす分のダメージ、y = 毎秒のダメージ、z = 最後に盛られてからの秒（見た目用）
RWStructuredBuffer<float4> enemyPoison : register(u7);

static const uint HP_CORPSE_BIT = 0x80000000u;
static const uint SWARM_ELEMENT_FIRE = 1u;   // = Swarm::DamageElement::Fire

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    if (j >= g_MaxEnemies)
        return;
    bool ticking = counters.Load(SWARM_CNT_TICKING_AREAS) != 0u;
    uint stateNow = enemyStates[j];   // このパスで書くのはこのスレッドだけ
    if (stateNow == SWARM_DEAD)
        return;
    float4 poison = enemyPoison[j];
    bool fireBeat = false;
    if (g_FireOn > 0.5 && g_FireTick > 0.0)
    {
        float beat = g_FireClock / g_FireTick + frac((float) j * 0.618034);
        fireBeat = floor(beat) != floor(beat - g_Step / g_FireTick);
    }
    if (!ticking && poison.x <= 0.0 && !fireBeat)
        return;

    float3 epos = enemies[j].position;
    SwarmEnemyExtra extra = enemyExtra[j];
    uint kind = extra.kind;
    // 埋まっている（砂の下の砂潜り、潜砂中の砂の守護者）: 地面の範囲はそれでも届く（2026-10-26、
    // ユーザー:「砂潜りがほとんど当たらない、地面系の魔法は当たるように」）。光線（カプセル）は砂の上を通る（下のループ）。
    // ほかの狙えない敵（砂旋、点火した自爆兵、眠っている石像ガーゴイル、消えたエリート）は届かないまま
    bool buried = SwarmBuried(extra);
    if (SwarmUntargetable(extra) && !buried)
        return;
    float3 ecenter;
    float er, eh;
    SwarmSlotCapsule(j, kind, epos, ecenter, er, eh);   // + 狂暴エリートの巨大化（2026-10-25）
    uint hitElem = 0u;        // ダメージ数字の色: このステップで一番強く当たった範囲の属性
    bool stealHit = false;    // 吸血の範囲がこのステップでダメージを与えた（SWARM_AREA_EXTRA_LIFESTEAL、2026-10-25）
    float hitElemDmg = 0.0;

    float total = 0.0;

    // ---- 足元で燃えている草 ----
    if (fireBeat && !buried && !SwarmFliesThrough(extra))
    {
        float2 fuv = (epos.xz - float2(g_FireOriginX, g_FireOriginZ)) / float2(g_FireSizeX, g_FireSizeZ);
        uint fw, fh;
        g_FireMap.GetDimensions(fw, fh);
        if (all(fuv >= 0.0) && all(fuv < 1.0) && g_FireMap.Load(int3(int2(fuv * float2(fw, fh)), 0)).z > 0.5)
        {
            float burn = SwarmTakenDamage(j, kind, g_FireDamage);
            total += burn;
            hitElemDmg = burn;
            hitElem = SWARM_ELEMENT_FIRE;
        }
    }
    bool stun = false;
    float freeze = 0.0;   // この敵が立っている範囲のうち一番長い凍結（SwarmAreaFreeze）
    float iceDamage = 0.0; // 一番強い凍結範囲（合算しない、下を参照）
    // 毒素を持つ範囲: レシピごとに一番強いもの（同じ魔法の沼 2 つが 1 体に重なっても 1 回分、
    // 胞子の霧と猛毒の沼なら両方）。スロットは SWARM_TOXIN_KINDS 個で vfxType をキーにする。スロットより
    // 種類が多いと最後のスロットにまとめる（今の毒素レシピは 2 つ）
    #define SWARM_TOXIN_KINDS 3
    float poisonDamage[SWARM_TOXIN_KINDS] = { 0.0, 0.0, 0.0 };
    float poisonShare[SWARM_TOXIN_KINDS] = { 0.0, 0.0, 0.0 };
    uint  poisonKind[SWARM_TOXIN_KINDS] = { 0xffffffffu, 0xffffffffu, 0xffffffffu };
    float slow = 0.0;     // この敵が立っている範囲のうち一番強い減速
    float slowTime = 0.0; // その範囲の次の tick まで + 少し
    float push = 0.0;     // その中で一番強いノックバック（SwarmAreaPush）
    float pull = 0.0;     // 範囲の中心への一番強い引き寄せ（SWARM_AREA_EXTRA_PULL、渦）
    float2 pullTo = float2(0.0, 0.0); // その範囲の中心
    float pullTime = SWARM_PUSH_TIME; // その範囲の tick 間隔（1 回の脈動は次の tick まで続く）

    for (uint k = 0; ticking && k < SWARM_MAX_AREAS; ++k)
    {
        if (areaStates[k] == SWARM_DEAD)
            continue;

        SwarmArea a = areas[k];
        if ((a.tickNow & SWARM_AREA_TICK_NOW) == 0u)
            continue;

        if (buried && (a.flags & SWARM_AREA_CAPSULE) != 0u)
            continue; // 光線は砂の上を飛ぶので、埋まっている敵には届かない
        // カプセル（光線）: 中心 -> 終点の線分上の最近点から測る
        float3 c = a.center;
        if ((a.flags & SWARM_AREA_CAPSULE) != 0u)
        {
            float3 seg = areaEnds[k].xyz - a.center;
            float t = saturate(dot(ecenter - a.center, seg) / max(dot(seg, seg), 1e-4));
            c = a.center + seg * t;
        }
        float3 d = ecenter - c;

        // 縦方向: 板の厚み + カプセルの直線部分の外 -> 外れ
        if (abs(d.y) > a.halfHeight + eh + er)
            continue;

        float reach = a.radius + er;
        if (d.x * d.x + d.z * d.z > reach * reach)
            continue;

        // 凍結の範囲（アイススパイクは線に沿って一列に並べ、全部が同じステップで tick し
        // 少し重なる）は一番強いものだけを取るので、2 つが重なった所の敵が
        // 2 回当たることはない。それ以外は今まで通り合算する。
        float f = SwarmAreaFreeze(a.flags);
        float dmg = SwarmTakenDamage(j, kind, a.damage);
        // 飛沫だけ（泥沼）: 着弾だけが当たり、残る沼は減速させるだけ
        if ((a.tickNow & SWARM_AREA_EXTRA_SPLASH) != 0u && (a.tickNow & SWARM_AREA_TICK_FIRST) == 0u)
            dmg = 0.0;
        // このステップで一番強く当たった範囲の属性でダメージ数字を色付けする（2026-10-17）。
        // 飛沫だけの 0 化の後に置く（2026-10-25）: 残った泥沼は何も当てないので、本当に当たったものの
        // 属性を塗り替えたり吸血の印を消したりしてはいけない
        if (dmg > hitElemDmg) { hitElemDmg = dmg; hitElem = SwarmAreaElement(a.tickNow); }
        if (dmg > 0.0 && (a.tickNow & SWARM_AREA_EXTRA_LIFESTEAL) != 0u)
            stealHit = true;
        float tox = SwarmAreaToxin(a.tickNow);
        if (f > 0.0)
            iceDamage = max(iceDamage, dmg);
        else if (tox > 0.0)
        {
            // このレシピのスロットを探す（または空き。空きが無い -> 最後のスロット）
            uint slot = SWARM_TOXIN_KINDS - 1;
            [unroll] for (uint q = 0; q < SWARM_TOXIN_KINDS; ++q)
            {
                if (poisonKind[q] == a.vfxType || poisonKind[q] == 0xffffffffu) { slot = q; break; }
            }
            if (poisonKind[slot] == 0xffffffffu) poisonKind[slot] = a.vfxType;
            if (dmg > poisonDamage[slot])
            {
                poisonDamage[slot] = dmg;
                poisonShare[slot] = tox;
            }
        }
        else
            total += dmg;
        if ((a.flags & SWARM_AREA_STUN) != 0u && dmg > 0.0)
            stun = true;
        freeze = max(freeze, f);
        float s = SwarmAreaSlow(a.flags);
        if (s > 0.0)
        {
            slow = max(slow, s);
            slowTime = max(slowTime, min(a.tickInterval, 2.0) + SWARM_SLOW_LINGER);
        }
        float pq = SwarmAreaPush(a.flags);
        if ((a.tickNow & SWARM_AREA_EXTRA_PULL) != 0u)
        {
            if (pq > pull) { pull = pq; pullTo = c.xz; pullTime = clamp(a.tickInterval, SWARM_PUSH_TIME, 1.0); }
        }
        else
            push = max(push, pq);
    }

    // ---- ノックバック: プレイヤーから離れる向き、強い方が勝つ（ダメージの早期 return より前） ----
    float kindMul = SwarmPushKindMul(kind);
    push *= kindMul;
    pull *= kindMul;
    if (push > 0.0)
    {
        float2 away = epos.xz - g_PlayerPos.xz;
        float len = length(away);
        away = (len > 1e-3) ? away / len : float2(1.0, 0.0);
        float2 vel = away * (push * SWARM_PUSH_SPEED);
        float4 cur = enemyPush[j];
        if (cur.z <= 0.0 || cur.w > 0.5 || dot(vel, vel) > dot(cur.xy, cur.xy))
            enemyPush[j] = float4(vel, SWARM_PUSH_TIME, 0.0);
    }
    else if (pull > 0.0)
    {
        // ---- 渦の中心への引き寄せ: 脈動 1 回で距離の一定割合、中心を越えない ----
        float2 to = pullTo - epos.xz;
        float len = length(to);
        float4 cur = enemyPush[j];
        // 進行中の押し（w = 0）はそのまま。前の引き寄せはこのステップのものに置き換える
        if (len > SWARM_PULL_DEAD && (cur.z <= 0.0 || cur.w > 0.5))
        {
            float speed = pull * SWARM_PULL_SHARE * len / (pullTime - 0.5 * SWARM_PUSH_TIME);
            enemyPush[j] = float4(to / len * min(speed, pull * SWARM_PUSH_SPEED), pullTime, 1.0);
        }
    }

    // ---- 減速: 更新する。まだ効いているものを弱めることはしない ----
    if (slow > 0.0)
    {
        if (SwarmIsBigElite(kind) || kind == SWARM_KIND_BOSS)
            slow *= SWARM_SLOW_BIG_MUL;
        float4 cur = enemySlow[j];
        if (cur.x <= 0.0)
            cur.y = 0.0;
        cur.xy = float2(max(cur.x, slowTime), max(cur.y, slow));
        enemySlow[j] = cur;
    }

    total += iceDamage;

    // ---- 毒素: 溜まっている分（前に盛られた分）を減らし、それからこのステップの割合を溜める ----
    float toxinAdd = 0.0;
    [unroll] for (uint q = 0; q < SWARM_TOXIN_KINDS; ++q)
    {
        float add = poisonDamage[q] * poisonShare[q];
        toxinAdd += add;
        total += poisonDamage[q] - add;
    }
    bool poisonDirty = false;
    if (poison.x > 0.0)
    {
        float drained = min(poison.x, poison.y * g_Step);
        total += drained;
        poison.x -= drained;
        poison.z += g_Step;
        if (poison.x <= 1e-3)
            poison = float4(0, 0, 0, 0); // 1 回分が終わった: 速度も一緒にリセット
        poisonDirty = true;
    }
    if (toxinAdd > 0.0)
    {
        // 1 回分ごとに SWARM_TOXIN_TIME かけて減る。上に重なった 2 回目は自分の速度を足す
        poison.x += toxinAdd;
        poison.y += toxinAdd / SWARM_TOXIN_TIME;
        poison.z = 0.0;
        poisonDirty = true;
    }
    if (poisonDirty)
        enemyPoison[j] = poison;

    if (total <= 0.0)
        return;

    uint dmgFixed = SwarmHpToFixed(total);

    uint prev;
    InterlockedAdd(enemies[j].hp, (uint) (-(int) dmgFixed), prev);
    if (prev == 0u || prev >= HP_CORPSE_BIT)
        return; // もう死体

    // 砂の下でも当たりは数えるが、凍結も硬直もしない（見えるものが無い）
    if (buried) { freeze = 0.0; stun = false; }
    // ボスは硬直しない。溜めを始めた突撃兵も同じ（HitCS を参照）
    bool noFlinch = SwarmNoFlinch(extra);   // （+ 最終ウェーブの幽霊 2 種、2026-10-18）
    // 氷の範囲は硬直の代わりに凍結リクエストを残す（当たったアイスランスと同じ、HitCS を参照）
    if (freeze > 0.0)
    {
        enemies[j].animIndex = SWARM_ANIM_FREEZE_REQ;
        enemies[j].animTime = freeze;
    }
    else if (stun && !noFlinch)
    {
        enemies[j].animIndex = 2u;
        enemies[j].animTime = 0.0;
    }

    if (prev > dmgFixed && hitElemDmg > 0.0)
    {
        // とどめではなく、このステップで範囲が本当に当たった（毒素が減っただけではない。それだと
        // 毎ステップ属性が消える）: 何が当たったかを覚える（ダメージ数字の色）。CAS、HitCS を参照（2026-10-17）
        uint cur = enemyStates[j];
        if (cur != SWARM_DEAD)
        {
            uint wasState;
            InterlockedCompareExchange(enemyStates[j], cur,
                SWARM_ALIVE | (hitElem << SWARM_STATE_ELEMENT_SHIFT) | (stealHit ? SWARM_STATE_LIFESTEAL : 0u), wasState);
        }
    }

    if (prev <= dmgFixed)
    {
        // ---- 範囲で倒された ----
        enemyStates[j] = SWARM_DEAD;
        // とどめの数字の色（2026-10-25）: このステップで一番強く当たった範囲。毒素が減っただけで
        // 倒れたときは最後に当たったものの属性（毒を盛った沼）
        enemies[j].attackCooldown = SwarmKillElementMark((hitElemDmg > 0.0) ? hitElem : SwarmStateElement(stateNow));
        counters.InterlockedAdd(SWARM_CNT_KILLS, 1u, prev);

        SwarmOrb orb;
        orb.position = epos;
        orb.position.y = epos.y - g_GroundY + g_OrbY; // 死体の足元（SwarmHitCS を参照）
        orb.amount = g_OrbAmount * SwarmKindExpMul(kind);
        orb.velocity = float3(0, 0, 0);
        orb._pad = 0.0;

        uint start = (j * 97u) % g_MaxOrbs;
        for (uint s = 0; s < g_MaxOrbs; ++s)
        {
            uint slot = (start + s) % g_MaxOrbs;
            uint was;
            InterlockedCompareExchange(orbStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
            if (was == SWARM_DEAD)
            {
                orbs[slot] = orb;
                break;
            }
        }
    }
}
