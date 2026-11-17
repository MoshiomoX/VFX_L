// ============================================================
// SwarmAreaTypes.hlsli
// 範囲攻撃の構造体（SwarmArea / SwarmAreaDef）と、flags / tickState / extra のビット
// （減速・凍結・毒素など）。押し出し・引き寄せ・光線のチャンネルは専用の欄。
// ============================================================

#ifndef SWARM_AREA_TYPES_HLSLI
#define SWARM_AREA_TYPES_HLSLI

// ============================================================
// 範囲攻撃（爆発、魔法陣）
//
// SwarmArea    : 生きている範囲 1 つ。64 バイト。Swarm::Area と一致させること。
//                一発の範囲と持続する範囲は同じもの：
//                爆発は 1 回だけ判定し、パーティクルが出ている間だけ
//                残る範囲。
// SwarmAreaDef : テンプレート。48 バイト。Swarm::AreaDef と一致させること。
//                行 0 = なし。GPU 自身が範囲を生成する時（弾の命中）に使う。
//                場所を知っているのが GPU だけなので。
//
// 形：円盤。XZ の距離 <= radius かつ |dy| <= halfHeight
// （+ 敵のカプセル）。縦方向の規則は弾の命中と同じ。
// ============================================================
static const uint SWARM_MAX_AREAS = 256; // = Swarm::kMaxAreas

// ---- flags のビット 0-3 ----
static const uint SWARM_AREA_FOLLOW_PLAYER = 1u; // 中心がプレイヤーに付いて動く
static const uint SWARM_AREA_STUN = 2u; // 判定のたびに敵を止めて光らせる（被弾硬直）
// カプセル（光線）：範囲は線分 center -> areaEnds[slot] の周り radius。
// 始点 / 終点 / 半径は毎ステップ SwarmBeamCB の SwarmArea.beamChannel 番から来る
// （AreaTickCS）。生成するのは CPU だけ。
static const uint SWARM_AREA_CAPSULE = 4u;
// 出現時にカメラを揺らす（爆発。= Swarm::kAreaShake）。
// 新しく生まれた範囲を全部見つけている SwarmAreaTrackCS が数える
static const uint SWARM_AREA_SHAKE = 8u;
static const uint SWARM_MAX_BEAMS = 4u; // = Swarm::kMaxBeams

// 減速（毒沼）：flags のビット 12-15 = q。判定のたびに中の敵を
// q / 15 だけ減速する（0 = なし）。AreaDamageCS は敵ごとに一番強い減速を
// enemyStatus[slot] = (残り秒, 量) に保持し、判定のたびに更新する。なので
// 敵が沼に立っている間は続き、判定を 1 回外れてから
// SWARM_SLOW_LINGER で切れる。AICS が moveSpeed を
// (1 - 量) 倍にして秒を数え下げる。エリート / ボスは半分。
static const uint SWARM_AREA_SLOW_SHIFT = 12u;
static const float SWARM_SLOW_LINGER = 0.15;
static const float SWARM_SLOW_BIG_MUL = 0.5;

float SwarmAreaSlow(uint flags)
{
    return (float) ((flags >> SWARM_AREA_SLOW_SHIFT) & 0xFu) / 15.0;
}

// 凍結（アイススパイク）：flags のビット 4-7 = q。判定のたびに中の敵を
// q * SWARM_AREA_FREEZE_STEP s 凍らせる（0 = なし）。AreaDamageCS は命中したアイスランスと同じ
// 凍結リクエスト（SWARM_ANIM_FREEZE_REQ）を残すので、エリート / ボスの半減と
// 解けた後の免疫は AICS が適用する。
static const uint SWARM_AREA_FREEZE_SHIFT = 4u; // = Swarm::kAreaFreezeShift
#define SWARM_AREA_FREEZE_STEP 0.25 // = Swarm::kAreaFreezeStep
#define SWARM_AREA_FREEZE_MASK 0xF0u // = 0xF << SWARM_AREA_FREEZE_SHIFT。凍結があるかどうかだけ見るとき用

float SwarmAreaFreeze(uint flags)
{
    return (float) ((flags >> SWARM_AREA_FREEZE_SHIFT) & 0xFu) * SWARM_AREA_FREEZE_STEP;
}

// 押し出し（突風）：SwarmArea.pushQ = 吹き飛ばしの強さ q（0..15、q / 15）。
// AreaDamageCS が enemyPush[slot] = (プレイヤーから離れる xz の速度,
// 残り秒, 0) を書き、MoveCS が減衰させながらそのステップの移動に足す。
// エリートは半分、ボスは 0（SwarmPushKindMul）。
static const float SWARM_PUSH_SPEED = 28.0; // q = 15 での m/s。減衰があるので speed * time / 2 = 3.5 m
static const float SWARM_PUSH_TIME = 0.25;  // 減衰しきるまでの秒

// 引き寄せ（渦）：SwarmArea.pullQ = 自分の中心へ引き寄せる強さ q（0..15）。AreaDamageCS が
// enemyPush[slot] = (中心へ向かう速度, 秒, 1) を書く。
// w = 1 は引き寄せの印で、進行中の押し出しは上書きしない（突風が勝つ）。
// 1 回のパルスは範囲の判定間隔（最低 SWARM_PUSH_TIME）続くので、
// 判定の合間に引き寄せが止まらない：MoveCS は最後の SWARM_PUSH_TIME まで
// 全速を保ってから減衰させる。進む距離は
// speed x (seconds - SWARM_PUSH_TIME / 2)。速度は 1 回のパルスで
// 距離の q x SWARM_PULL_SHARE だけ進むように選ぶ：中心を越えない。
// SWARM_PULL_DEAD 内では引き寄せない（ガタつき防止）。
// 生きている引き寄せの範囲は風源にもなる（SwarmWindSourceCS）
static const float SWARM_PULL_SHARE = 0.62; // q = 15 でのパルス 1 回あたりの距離の割合
static const float SWARM_PULL_DEAD = 0.35;  // m

// pushQ / pullQ（0..15）-> 強さ 0..1
float SwarmAreaStrength(uint q)
{
    return (float) (q & 0xFu) / 15.0;
}

// ------------------------------------------------------------
// 判定の状態 SwarmArea.tickState。書くのは AreaTickCS だけ（生成時は 0）
//   ビット 0 : SWARM_AREA_TICK_NOW   このステップでダメージを与える
//   ビット 1 : SWARM_AREA_TICK_FIRST このステップが範囲の最初の判定
//   ビット 2 : SWARM_AREA_TICK_SEEN  以前に判定したことがある（消えない）
// ------------------------------------------------------------
static const uint SWARM_AREA_TICK_NOW = 1u;
static const uint SWARM_AREA_TICK_FIRST = 2u;
static const uint SWARM_AREA_TICK_SEEN = 4u;

// ------------------------------------------------------------
// 範囲ごとの追加情報 SwarmArea.extra（生成側が SwarmAreaDef.extra（GPU）/ Swarm::Area::extra（CPU、
// AreaProfile::Extra）から入れる）。Swarm::kAreaExtra* と一致させること
//   ビット 0-3   : 毒素 q / 15：判定のダメージのうちこの割合はすぐには与えず、
//                 敵（enemyPoison）に溜めて SWARM_TOXIN_TIME かけて
//                 減らす。毒素のある範囲は 1 体の敵に重ならない：
//                 一番強いものだけ効く（毒沼）
//   ビット 4     : SWARM_AREA_EXTRA_SPLASH：最初の判定だけダメージ
//                 （泥沼：着地で飛沫、その後は減速だけ）
//   ビット 5     : SWARM_AREA_EXTRA_LIFESTEAL：当たった敵に吸血の印を付ける
//   ビット 6     : SWARM_AREA_EXTRA_BLADE：回る剣（下を参照）
//   ビット 8-11  : ダメージの属性（ダメージ数字の色）
//   ビット 16-23 : 回る剣のガード（× 0.01 秒）
// ------------------------------------------------------------
static const uint SWARM_AREA_EXTRA_TOXIN_SHIFT = 0u;
static const uint SWARM_AREA_EXTRA_SPLASH = 1u << 4u;
static const uint SWARM_AREA_EXTRA_LIFESTEAL = 1u << 5u;   // = Swarm::kAreaExtraLifesteal
static const uint SWARM_TAG_LIFESTEAL = 0x80000000u;      // = Swarm::kTagLifesteal：弾の連鎖タグ（projTags）の最上位ビット
static const uint SWARM_AREA_EXTRA_ELEMENT_SHIFT = 8u;
uint SwarmAreaElement(uint extra)
{
    return (extra >> SWARM_AREA_EXTRA_ELEMENT_SHIFT) & 0xFu;
}
// 回る剣（= Swarm::kAreaExtraBlade）：同じステップの剣の範囲は一番強い 1 つだけ。
// 前に剣が当たってからガード秒以内の敵には当てない（1 回の通過で 1 回）
static const uint SWARM_AREA_EXTRA_BLADE = 1u << 6u;
static const uint SWARM_AREA_EXTRA_GUARD_SHIFT = 16u;
float SwarmAreaBladeGuard(uint extra)
{
    return (float) ((extra >> SWARM_AREA_EXTRA_GUARD_SHIFT) & 0xFFu) * 0.01;
}
static const float SWARM_TOXIN_TIME = 1.0; // 毒素 1 回分が減りきるまでの秒

float SwarmAreaToxin(uint extra)
{
    return (float) ((extra >> SWARM_AREA_EXTRA_TOXIN_SHIFT) & 0xFu) / 15.0;
}


struct SwarmArea
{
    float3 center;
    float radius;

    float damage; // 判定 1 回あたり
    float timeLeft;
    float tickInterval;
    float tickTimer; // 次の判定までの秒。生成時に 0 = 最初のステップで判定する

    float halfHeight;
    uint flags; // ビット 0-15 = SWARM_AREA_*、16-31 = 大きさの倍率（SwarmAreaScale）
    uint vfxType; // GPU 側パーティクルのレシピ番号。0 = なし（エフェクトは CPU が再生する）
    uint tickState; // 判定の状態（SWARM_AREA_TICK_*、AreaTickCS が書く）

    uint extra;       // 生成時に設定する追加情報（SWARM_AREA_EXTRA_*）
    uint beamChannel; // SWARM_AREA_CAPSULE の範囲だけ：SwarmBeamCB の添字
    uint pushQ;       // 吹き飛ばしの強さ 0..15（突風）
    uint pullQ;       // 中心への引き寄せの強さ 0..15（渦）
};

// 範囲の大きさの倍率（1 = 作ったまま）。radius / halfHeight には掛け済み
float SwarmAreaScale(SwarmArea a)
{
    return SwarmUnpackScale(a.flags >> SWARM_SCALE_SHIFT);
}

struct SwarmAreaDef
{
    float radius;
    float halfHeight;
    float damage;
    float duration;

    float tickInterval;
    uint flags;
    uint vfxType;
    uint extra; // SwarmArea.extra に入る（SWARM_AREA_EXTRA_*）

    uint pushQ; // SwarmArea.pushQ に入る
    uint pullQ; // SwarmArea.pullQ に入る
    uint2 _pad;
};


#endif
