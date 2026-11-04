// ============================================================
// SwarmDamageTextCS.hlsl
// 浮かぶダメージの数字（2026-10-17）。1 フレームに 1 回、敵のスロット 1 つに
// 1 スレッド。スロットの hp を前フレームの値（prevHp）と比べ、減った分を
// イベントにする。CPU がそれをリードバックし、敵の頭上に数字として描く
// （UI/DamageNumbers）。
//
// ダメージを与える各パスでやらずにここでやる理由：HitCS は D3D11.0 の UAV 8 本を
// 使い切っていて、1 か所なら弾・範囲の tick・毒素の削り・接触を
// すべて同じように拾える。
//
// まとめ方：1 体の敵には 1 秒に何度も当たる（光線の tick、毒の
// 削り）。減った分はスロットごとに貯め（pending.x 直接、.y 毒）、
// 最初に減ってから g_DtxMerge 秒経ったら出す。敵が死んだらすぐ出す
// （そのときは撃破フラグ付き。残っていた hp も含める）。
// 毒：減った分のうち、このフレームに削れた毒素（enemyPoison.x が
// 減った量）に合う部分を毒として数え、残りを直接として数える。
//
// イベントリング（dmgEvents、RAW）：[0] = これまでに書いたイベント数、続いて 16 バイト
// × SWARM_MAX_DAMAGE_TEXTS の float3 位置（体の上）+ uint
// packed（SWARM_DTX_* のビット：下位 24 ビットに量 ×10）。CPU は
// 分裂リングと同じようにリードバックする（ステージング 3 本、2〜3 フレーム遅れ）。
//
// KillAll は prevHp と pending を消すので、フィールドを一掃しても何も出ない。
// 同じフレーム内に死んで再利用されたスロットは取りこぼす（まれ）。
//
// hp が残ったまま DEAD になったスロットは倒されていない：自爆兵 / 突撃爆弾兵の
// 自爆、砂嵐の術師の砂旋の寿命切れ（hp 1,000,000）、
// 「狙えない」砂旋など。これらは何も出さない（2026-10-19：以前は砂旋が
// 消えるときに「1000000 倒した」と出ていた）。撃破フラグは
// このフレームに hp が本当に 0 になったときだけ付ける。
//
// 2026-10-25:
// - 撃破の数字は倒した一撃の属性を使う。HitCS / AreaDamageCS が死んだスロットの
//   attackCooldown に残す（SwarmKillElement）：その時点で状態ワードは
//   ただの DEAD になっているため。
// - 前フレーム以降に RecycleCS が上書きしたスロットは prevHp に SWARM_PREVHP_RECYCLED が
//   入っている（+ 新しい敵の生成時の hp）：前の敵の貯めていた数字と毒素は
//   忘れ、減った分は新しい生成時の hp から測る（以前は重装兵が雑魚に
//   転送されると、hp の差がダメージとして表示されていた）。
// ============================================================
#define SWARM_AI_CB_REG b2
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmDamageTextCB : register(b4)
{
    float g_DtxStep;    // フレームの dt
    float g_DtxMerge;   // 直接ダメージを出す前に、続きを待つ秒数
    uint g_DtxOn;       // 0 = prevHp / pending の更新だけする
    float g_DtxPoisonMerge;   // 毒素の削り用の同じ値（少しずつ減るので長めにまとめ、1 つの数字に見せる）
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<float4> enemyPoison : register(t2);   // x = まだ削れていない毒素
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);

RWBuffer<uint> prevHp : register(u0);                  // 前フレームに見た hp（固定小数）、死んだスロットは 0
RWStructuredBuffer<float4> pending : register(u1);     // x 直接、y 毒（hp）、z 最初に減ってからの秒、w 前フレームに見た毒素
RWByteAddressBuffer dmgEvents : register(u2);
RWByteAddressBuffer lifesteal : register(u3);   // +0：吸血魔法の印が付いた敵から奪った hp（固定小数）、ずっと累積する（CPU が差分を取る。2026-10-25）

static const uint SWARM_MAX_DAMAGE_TEXTS = 512u;   // Swarm::kMaxDamageTexts
static const uint SWARM_DTX_AMOUNT_MASK = 0x00FFFFFFu;   // 量 × 10
static const uint SWARM_DTX_POISON = 1u << 24;           // ほとんどが毒素の削り -> 緑
static const uint SWARM_DTX_KILL = 1u << 25;             // 敵が死んだ -> 大きく
static const uint SWARM_DTX_ELEMENT_SHIFT = 26u;         // ビット 26..29：最後の一撃の属性（状態ワード）、数字の色

float HpOf(uint raw)
{
    // 倒された敵の hp はアンダーフローしている（InterlockedAdd で 0 を越えた）：0 として扱う
    return (raw > 0x7FFFFFFFu) ? 0.0 : (float) raw / SWARM_HP_SCALE;
}

void Emit(uint i, float direct, float poison, bool kill, uint elem)
{
    float total = direct + poison;
    if (total < 0.05)
        return;
    SwarmEnemy e = enemies[i];
    uint kind = enemyExtra[i].kind;
    float3 center;
    float er, eh;
    SwarmSlotCapsule(i, kind, e.position, center, er, eh);   // 狂暴化したエリートは大きい（2026-10-25）
    float3 pos = center + float3(0.0, eh + er + 0.25, 0.0);   // 頭のすぐ上

    uint amount = min((uint) round(total * 10.0), SWARM_DTX_AMOUNT_MASK);
    uint packed = amount;
    if (poison > direct) packed |= SWARM_DTX_POISON;
    if (kill) packed |= SWARM_DTX_KILL;
    packed |= (elem & 0xFu) << SWARM_DTX_ELEMENT_SHIFT;

    uint n;
    dmgEvents.InterlockedAdd(0, 1u, n);
    uint at = 16u + (n % SWARM_MAX_DAMAGE_TEXTS) * 16u;
    dmgEvents.Store3(at, asuint(pos));
    dmgEvents.Store(at + 12u, packed);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;

    uint hpRaw = enemies[i].hp;
    uint state = enemyStates[i];
    bool dead = state == SWARM_DEAD;
    // 死んだスロットの状態ワードはただの DEAD：倒した側が属性を attackCooldown に残している（2026-10-25）
    uint elem = dead ? SwarmKillElement(enemies[i].attackCooldown) : SwarmStateElement(state);
    uint prevRaw = prevHp[i];
    float4 p = pending[i];
    if ((prevRaw & SWARM_PREVHP_RECYCLED) != 0u)
    {
        // 前フレーム以降に RecycleCS がここへ新しい敵を置いた（2026-10-25）：その生成時の hp から測り、
        // 前の敵が貯めていたもの（数字と見ていた毒素）は忘れる
        prevRaw &= ~SWARM_PREVHP_RECYCLED;
        p = float4(0.0, 0.0, 0.0, 0.0);
    }
    float toxinNow = enemyPoison[i].x;
    float drain = max(0.0, p.w - toxinNow);   // このフレームに削れた毒素（新しく盛られた分は増やすだけ）
    p.w = toxinNow;

    // ---- 吸血（2026-10-25）：最後の一撃が吸血魔法のもの（SWARM_STATE_LIFESTEAL、HitCS / AreaDamageCS）
    //      -> このフレームに減った hp をプレイヤーの回復に回す。倒した一撃はスロットを DEAD にする（印なし）：数えない ----
    if ((state & SWARM_STATE_LIFESTEAL) != 0u && prevRaw != 0u)
    {
        float lost = HpOf(prevRaw) - HpOf(hpRaw);
        if (lost > 0.0)
        {
            uint was;
            lifesteal.InterlockedAdd(0, SwarmHpToFixed(lost), was);
        }
    }

    if (g_DtxOn == 0u)
    {
        prevHp[i] = dead ? 0u : hpRaw;
        pending[i] = float4(0.0, 0.0, 0.0, toxinNow);
        return;
    }

    // 死んだスロットでも hp はそのまま読む：倒されたなら 0（またはアンダーフロー -> 0）、
    // 自爆 / 寿命切れなら元の値のまま -> 減っていないので何も出さない
    float hp = HpOf(hpRaw);
    float prev = HpOf(prevRaw);
    if (prevRaw != 0u && hp < prev)
    {
        float drop = prev - hp;
        float poisonPart = min(drop, drain);
        p.x += drop - poisonPart;
        p.y += poisonPart;
    }
    if (p.x + p.y > 0.0)
        p.z += g_DtxStep;

    if (dead)
    {
        // 前フレームは生きていて hp が本当に尽きた：貯めていた分をすべて撃破フラグ付きで出す。
        // hp を残して死んだ（自爆した、砂旋が消えた）：貯めていた分は普通の数字として出す
        if (prevRaw != 0u && (!SwarmUntargetable(enemyExtra[i]) || SwarmBuried(enemyExtra[i])))   // 埋まっている敵も地面の範囲で死ぬことがある（2026-10-26）
            Emit(i, p.x, p.y, hp <= 0.0, elem);
        p.xyz = float3(0.0, 0.0, 0.0);
    }
    else
    {
        // 直接の命中と毒素の削りは、それぞれ別の時計で出す（どちらも最初に減ったときから測る）
        if (p.x > 0.0 && p.z >= g_DtxMerge) { Emit(i, p.x, 0.0, false, elem); p.x = 0.0; }
        if (p.y > 0.0 && p.z >= g_DtxPoisonMerge) { Emit(i, 0.0, p.y, false, elem); p.y = 0.0; }
        if (p.x + p.y <= 0.0) p.z = 0.0;
    }
    prevHp[i] = dead ? 0u : hpRaw;
    pending[i] = p;
}
