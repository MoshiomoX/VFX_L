// ============================================================
// SwarmEnemyTypes.hlsli
// 敵の構造体（SwarmEnemy / SwarmEnemyExtra / SwarmCorpse）、種類の番号と種類ごとの判定、
// 敵の状態ワードのビット。
// ============================================================

#ifndef SWARM_ENEMY_TYPES_HLSLI
#define SWARM_ENEMY_TYPES_HLSLI

// ============================================================
// 敵：48 バイト
// ============================================================
struct SwarmEnemy
{
    float3 position;
    uint hp;

    float3 velocity;
    float moveSpeed;

    float yaw;

    // animIndex SWARM_ANIM_STUN（2）= 被弾の硬直。HitCS が animTime = 0 で立て、MoveCS が
    // animTime を g_HitStun まで数えてから消す。VS はこれを読んで
    // 光らせる。他の値は VAT アニメーション用に予約。
    float animTime;
    uint animIndex;
    float attackCooldown;
};

// ============================================================
// 敵の生成リクエスト：32 バイト。Swarm::EnemySpawn と一致させること。
// SpawnEnemyCS / RecycleCS が本体（SwarmEnemy）と種類（SwarmEnemyExtra）に分けて書く
// ============================================================
struct SwarmEnemySpawn
{
    float3 position;
    uint hp;          // 固定小数点
    float moveSpeed;
    float yaw;
    uint kind;        // SWARM_KIND_*
    uint _pad;
};
SwarmEnemy SwarmEnemyFromSpawn(SwarmEnemySpawn req)
{
    SwarmEnemy e = (SwarmEnemy) 0;
    e.position = req.position;
    e.hp = req.hp;
    e.moveSpeed = req.moveSpeed;
    e.yaw = req.yaw;
    return e;
}

// ============================================================
// 敵の種類 + 種類ごとの状態：8 バイト。敵プールと同じ添字の
// 並列バッファ（SwarmEnemy は 48B のまま）。
// SwarmEnemyTypes.h の Swarm::EnemyExtra と一致させること
// ============================================================
static const uint SWARM_KIND_MOB = 0u;    // 接触で殴る
static const uint SWARM_KIND_BOMBER = 1u; // 接触で導火線に点火し、g_BomberFuseTime 後に爆発
static const uint SWARM_KIND_ELITE = 2u;  // 大型の雑魚：体格 g_EliteScale、殴り g_EliteDamageMul、経験玉 g_EliteExpMul
static const uint SWARM_KIND_BOSS = 3u;   // ステージのボス：g_Boss*、被弾硬直なし、SwarmBossInfo に報告する
static const uint SWARM_KIND_GHOST = 4u;  // 最終ウェーブの幽霊：HP は雑魚と同じ、速い、壁や台地を突き抜けてまっすぐ飛ぶ
static const uint SWARM_KIND_SPLITTER = 5u;  // 死ぬと分裂体 3 体に分かれる（SwarmCorpseTrackCS -> 敵の出来事の環 -> CPU）
static const uint SWARM_KIND_SPLITLING = 6u; // スプリッターの小さな子。もう分裂しない
static const uint SWARM_KIND_BRUTE = 7u;     // 重装の雑魚：HP 3 倍、遅い、大きい
static const uint SWARM_KIND_CHARGER = 8u;   // 止まって溜め（地面に赤い帯）、固定した線上を突進する
static const uint SWARM_KIND_SHIELD = 9u;    // 被弾ごとに g_ShieldArmor を減らす（最低でも g_ShieldMinFrac は通る）
static const uint SWARM_KIND_SPIKEBALL = 10u; // 砂漠のトゲ玉：硬い、遅い、転がる。触るとプレイヤーを弾き飛ばす（ContactCS -> playerHits のトゲ枠）。硬直せず、吹き飛ばされない
static const uint SWARM_KIND_BURROWER = 11u;  // 砂漠の砂潜り：砂の下を進み（砂丘だけ見え、当たらず、噛まない）、プレイヤーの横で g_BurrowSurfaceTime だけ地上に出て、また潜る。fuse > 0 = 地上にいる残り秒、<= 0 = -(潜っている秒)
static const uint SWARM_KIND_SANDMAGE = 12u;  // 砂漠の砂嵐の術師：距離を保ち、溜めてから竜巻を詠唱する（enemyEvents -> CPU が SWARM_KIND_TORNADO を生成）。fuse > 0 = 溜めの経過秒、< 0 = クールダウンの残り、0 = 準備完了
static const uint SWARM_KIND_TORNADO = 13u;   // 砂嵐の術師の竜巻：「敵の弾」。yaw に沿ってまっすぐ飛び（幽霊のように壁を抜ける）、当たらず狙われず、触るとプレイヤーを吹き上げ、fuse が尽きると死ぬ。fuse = 残り秒（0 = 生成直後、< 0 = 期限切れ）
static const uint SWARM_KIND_DASHBOMBER = 15u; // 突撃爆弾兵：プレイヤーが g_DashBomberTrigger 内に入ると自分で点火し、yaw を固定して g_DashBomberWindup 秒立ち、その yaw に沿ってまっすぐ走る（狙い直さない）。触れるか、溜めの g_DashBomberFuse 秒後に爆発（ContactCS）。fuse > 0 = 点火からの秒（AICS が数える）。点火中は自爆兵と同じく無敵
static const uint SWARM_KIND_DARKGHOST = 14u; // 最終ウェーブの黒幽霊：HP が非常に多い幽霊。プレイヤーより速く、大きく、g_DarkGhostDamageMul 倍で殴り、爆発級に吹き飛ばす。白い幽霊と同じく壁を抜けて飛ぶ。どちらの幽霊も硬直・凍結・減速・吹き飛ばしを受けない
// ---- 遺跡の敵（呪いの鎧、石像ガーゴイル、死霊術師、鎖の番人） ----
static const uint SWARM_KIND_ARMOR = 16u;         // 呪いの鎧：硬くて遅い。1 回目の死では砕けず崩れ落ち（CorpseTrackCS -> 敵の出来事の環）、CPU がそこに HP 半分の SWARM_KIND_ARMOR2 を起こす
static const uint SWARM_KIND_ARMOR2 = 17u;        // 蘇った呪いの鎧：普通に砕ける
static const uint SWARM_KIND_GARGOYLE = 18u;      // 石像ガーゴイル：プレイヤーが近づくまで石像（当たらない）、その後飛び（壁を抜け、g_GargoyleHover だけ浮く）、プレイヤーへ急降下してしばらく着地する。fuse：<= 0 眠り、(0, wake) 目覚め中、[wake, SWARM_GARGOYLE_DIVE) 飛行、>= SWARM_GARGOYLE_DIVE 急降下（溜め -> 急降下 -> 着地）
static const uint SWARM_KIND_NECRO = 19u;         // 死霊術師：砂嵐の術師と同じく距離を保つ。詠唱で近くの新しい死体から骸骨を起こす（CPU）
static const uint SWARM_KIND_SKELETON = 20u;      // 死霊術師が起こした骸骨：速くて脆い
static const uint SWARM_KIND_WARDEN = 21u;        // 鎖の番人：中距離で止まり、yaw を固定して溜める（突進の帯で線を見せる）。溜め終わりに CPU がその yaw に沿って鎖の鉤を投げる
// 面の固定エリート。A =近接（大斧 / 騎兵 / 影の剣士）、B = 遠距離（弓手 / 炎術師 / 司書。
// g_FixedEliteKeepMin..Max の距離を保つ）。技は CPU（EliteAttacks）が走らせ、g_EliteCmd / g_EliteMove で動かす
static const uint SWARM_KIND_ELITE_A = 22u;
static const uint SWARM_KIND_ELITE_B = 23u;
static const float SWARM_GARGOYLE_DIVE = 1000.0;  // ガーゴイルの fuse がこれ以上 = 急降下（fuse - これ = 急降下に入ってからの秒）

// Boss の様子（Swarm::BossInfo、32 バイト。SwarmEnemyCompactCS が RWByteAddressBuffer に書く）のバイトオフセット。
// 空いていた 2 語（GHOSTS / DARK_GHOSTS）で最終ウェーブの幽霊を数える
#define SWARM_BOSSINFO_ALIVE 0u
#define SWARM_BOSSINFO_HP 4u
#define SWARM_BOSSINFO_MAX_HP 8u
#define SWARM_BOSSINFO_GHOSTS 12u
#define SWARM_BOSSINFO_POS 16u
#define SWARM_BOSSINFO_DARK_GHOSTS 28u

// 固定エリートの様子（Swarm::EliteInfoBlock。SwarmEnemyKindList2CS が書く）：+0 数、+16 から 1 体 32 バイト
// （1 体の中：+0 槽・種類・hp・最大 hp、+16 位置・fuse）
#define SWARM_ELITEINFO_COUNT 0u
#define SWARM_ELITEINFO_FIRST 16u
#define SWARM_ELITEINFO_STRIDE 32u
#define SWARM_ELITEINFO_POS 16u

// 最終ウェーブの幽霊 2 種：壁や台地を抜けてプレイヤーへまっすぐ飛び（AICS / MoveCS / PushCS）、
// 硬直せず（HitCS / AreaDamageCS）、減速・凍結されず（AICS）、突風に押されず（SwarmPushKindMul）、
// 転送されない（RecycleCS）。幽霊リストから半透明で描く（CompactCS）
bool SwarmIsGhost(uint kind)
{
    return kind == SWARM_KIND_GHOST || kind == SWARM_KIND_DARKGHOST;
}
bool SwarmIsFixedElite(uint kind)
{
    return kind == SWARM_KIND_ELITE_A || kind == SWARM_KIND_ELITE_B;
}
// エリート級の種類：凍結 / 減速 / 突風の押しが半分
bool SwarmIsBigElite(uint kind)
{
    return kind == SWARM_KIND_ELITE || SwarmIsFixedElite(kind);
}

// 生存中 / 砕け中の描画リストを後のリストパス（SwarmEnemyKindList2CS /
// SwarmCorpseList2CS / SwarmCorpseList3CS）で作る種類：最初のパスはどれも UAV 8 本を使い切っている。
// 最初のパスではこれらの種類を除く
bool SwarmInKindList2(uint kind)
{
    return (kind >= SWARM_KIND_ARMOR && kind <= SWARM_KIND_WARDEN) || SwarmIsFixedElite(kind);
}
bool SwarmInCorpseList2(uint kind)
{
    return SwarmInKindList2(kind) || kind == SWARM_KIND_BOSS || kind == SWARM_KIND_SANDMAGE || kind == SWARM_KIND_DASHBOMBER;
}

// ------------------------------------------------------------
// SwarmEnemyExtra.fuse の意味は種類ごとに違う（書き手は AICS。自爆兵 / 突撃爆弾兵の爆発だけ ContactCS）：
//   自爆兵         : 点火してからの秒。0 = 未点火（点火時に 1 ステップ分足す。g_BomberFuseTime で爆発）
//   突撃兵         : < 0 = クールダウンの残り（0 まで数え上げる）、0 = 追跡中、> 0 = 溜め開始からの秒
//                    （SwarmChargerPhase で 溜め / 突進 / 立て直し に分ける）
//   砂潜り         : > 0 = 地上にいる残り秒、<= 0 = -(潜っている秒)
//   砂嵐の術師 / 死霊術師 / 鎖の番人（CasterStep）:
//                    0 = 準備完了、> 0 = 溜めの経過秒、< 0 = クールダウンの残り（0 まで数え上げる）
//   砂旋           : 0 = 生成直後、> 0 = 残り秒、< 0 = 寿命切れ（ContactCS が DEAD にする）
//   石像ガーゴイル : <= 0 = 眠り（-経過秒）、(0, g_GargoyleWakeTime) = 目覚め中、
//                    [g_GargoyleWakeTime, SWARM_GARGOYLE_DIVE) = 飛行、>= SWARM_GARGOYLE_DIVE = 急降下
//                    （fuse - SWARM_GARGOYLE_DIVE = 急降下に入ってからの秒：溜め -> 降下 -> 着地）
//   突撃爆弾兵     : > 0 = 点火からの秒（溜め g_DashBomberWindup -> 突進 -> g_DashBomberFuse で爆発）、0 = 未点火
//   ボス           : -burrow（0..1、砂の守護者の潜り具合。< -0.5 = 潜っている = SwarmBuried）
//   固定エリート   : -hide（0..1、影の剣士の消え具合。< -0.5 = 狙われない）
//   その他の種類   : 使わない（0）
// SwarmTypes.h の Swarm::EnemyExtra の説明と一致させること
// ------------------------------------------------------------
struct SwarmEnemyExtra
{
    uint kind;
    float fuse; // 種類ごとの状態（上の表）
};

// ------------------------------------------------------------
// 敵の状態の並列バッファ enemyStatus（float4。xy = 減速、zw = 凍結）：
//   x = 減速の残り秒、y = 減速量（毒沼、AreaDamageCS）
//   z = 凍結の残り秒（> 0：動かない、殴らない）
//   w = 凍結中：今回の凍結の長さ（VS が氷を生やす / 溶かすのに使う）
//       解けた後：再び凍れるようになるまでの残り秒
// 凍結は HitCS がリクエストする：animIndex = SWARM_ANIM_FREEZE_REQ、
// animTime = 秒。次のステップの AICS が受け取る（SwarmEnemy は 48B のまま）。
// Swarm::kAnimFreezeRequest と一致させること
// ------------------------------------------------------------
static const uint SWARM_ANIM_FREEZE_REQ = 3u;
// animIndex：被弾の硬直（HitCS / AreaDamageCS が立て、MoveCS が g_HitStun 秒で消す）。Swarm::kAnimStun と一致させること
#define SWARM_ANIM_STUN 2u

// ------------------------------------------------------------
// 死んだ敵が剛体の部品に砕け散る。
// 前フレームで生きていたスロットが今 DEAD なら SwarmCorpseTrackCS が 1 つ書き、
// SwarmCorpseVS が (seed, age) の閉じた式で部品を飛ばす。
// SwarmTypes.h の Swarm::Corpse / kMaxCorpses と一致させること
// ------------------------------------------------------------
static const uint SWARM_MAX_CORPSES = 512u;

struct SwarmCorpse
{
    float3 position; // 死んだ場所（敵の位置 = 地面 + centerAboveFeet）
    float yaw;
    float2 dir;      // 飛ばす方向、単位 xz（プレイヤーから離れる向き）
    float birth;     // SwarmSystem の時計での死亡時刻、0 = 空
    uint kind;
    uint seed;
    uint _pad0;      // 未使用（16 バイト境界合わせ）。常に 0
    float2 _pad;
};

// 砂の下の砂潜り：弾と光線は当たらず、噛まず、狙われない。
// 地面の範囲（メテオの爆発、毒沼、氷のトゲ…）は届く（AreaDamageCS）
static const float SWARM_BURROW_RISE_TIME = 0.3; // 体が地上に出る / 潜るのにかかる秒（見た目だけ）
bool SwarmBuried(SwarmEnemyExtra ex)
{
    return (ex.kind == SWARM_KIND_BURROWER && ex.fuse <= 0.0)
        || (ex.kind == SWARM_KIND_BOSS && ex.fuse < -0.5);   // 砂の守護者が半分以上砂に潜っている
}
// 導火線に火の付いた自爆兵 / 突撃爆弾兵：
// ダメージを受けず（HitCS / AreaDamageCS は SwarmUntargetable でスキップ）、硬直せず、減速・凍結されない（AICS）。
// 点火前は普通の雑魚と同じく死ぬ
bool SwarmInvulnerable(SwarmEnemyExtra ex)
{
    return (ex.kind == SWARM_KIND_BOMBER || ex.kind == SWARM_KIND_DASHBOMBER) && ex.fuse > 0.0;
}
// 弾 / 光線が当たらず、魔法 / 光線 / 最寄りの敵の検索で狙われず、HP バーも出ない
// （例外：潜っているものは地面の範囲が当たり、バーも出る。AreaDamageCS / HpBarVS 参照）：
// 潜っている砂潜り、砂嵐の術師の竜巻、点火した自爆兵 / 突撃爆弾兵
bool SwarmUntargetable(SwarmEnemyExtra ex)
{
    return SwarmBuried(ex) || ex.kind == SWARM_KIND_TORNADO || SwarmInvulnerable(ex)
        || (ex.kind == SWARM_KIND_GARGOYLE && ex.fuse <= 0.0)   // 眠っているガーゴイルは石像
        || (SwarmIsFixedElite(ex.kind) && ex.fuse < -0.5);       // 影の剣士が消えている（AICS：fuse = -hide）
}

float SwarmPushKindMul(uint kind)
{
    // 出口は 1 つ（途中の return があると fxc が X4000 を出す）
    bool none = (kind == SWARM_KIND_BOSS)
             || (kind == SWARM_KIND_SPIKEBALL)   // 重すぎて吹き飛ばない
             || (kind == SWARM_KIND_BURROWER)    // 砂の下
             || (kind == SWARM_KIND_TORNADO)     // 風は風を押さない
             || SwarmIsGhost(kind)               // 最終ウェーブは吹き飛ばせない
             || (kind == SWARM_KIND_GARGOYLE);   // 石
    bool isHalf = (SwarmIsBigElite(kind) || kind == SWARM_KIND_ARMOR || kind == SWARM_KIND_ARMOR2);
    return none ? 0.0 : (isHalf ? 0.5 : 1.0);
}

// ------------------------------------------------------------
// 敵の被弾の記録：敵プールと同じ添字の並列バッファ enemyHitInfo（16 バイト）。
// Swarm::EnemyHitInfo と一致させること。SpawnEnemyCS / RecycleCS が 0 に戻す。
//   lastHit     : 最後に当たった攻撃（とどめ以外）。ビット 0..3 = 属性（ダメージ数字の色）、
//                 SWARM_HIT_LIFESTEAL = 吸血の魔法だった（SwarmDamageTextCS がこの敵の HP 減少を吸血の合計に足す）。
//                 HitCS / AreaDamageCS が 1 語で書く
//   killElement : とどめの一撃の属性。死んだスロットの撃破の数字の色（SwarmDamageTextCS）
//   bladeTime   : 最後に回る剣が当たった時刻（g_StepClock、0 = まだ）。1 回の通過で 1 回だけ当てる（AreaDamageCS）
//   iceTime     : 最後に氷の範囲が当たった時刻（g_StepClock、0 = まだ）。アイススパイクの隣の列を弾く（AreaDamageCS）
// ------------------------------------------------------------
struct SwarmEnemyHitInfo
{
    uint lastHit;
    uint killElement;
    float bladeTime;
    float iceTime;
};
static const uint SWARM_HIT_ELEMENT_MASK = 0xFu;
static const uint SWARM_HIT_LIFESTEAL = 1u << 4u;
uint SwarmHitWord(uint elem, bool lifesteal)
{
    return (elem & SWARM_HIT_ELEMENT_MASK) | (lifesteal ? SWARM_HIT_LIFESTEAL : 0u);
}
uint SwarmHitElement(uint lastHit)
{
    return lastHit & SWARM_HIT_ELEMENT_MASK;
}
// SwarmDamageTextCS の prevHp（前フレームの HP、固定小数）にこのビットが立っている：その後 RecycleCS がスロットに
// 新しい敵を入れた（下位ビット = その生成時の HP）。前の敵の保留中の数字は捨て、減少量は新しい HP から測る。
// 生きている敵の HP にこのビットが立つことはない（HP が 0 を越えて折り返すのはとどめの一撃だけで、そのスロットはもう DEAD）
static const uint SWARM_PREVHP_RECYCLED = 0x80000000u;
// HitCS / AreaDamageCS：InterlockedAdd で引く前の HP がこれ以上 = 0 を越えて折り返した（もう死体）。
// SWARM_PREVHP_RECYCLED と同じく「生きている HP は最上位ビットが立たない」ことを前提にしている
static const uint SWARM_HP_CORPSE_BIT = 0x80000000u;

#endif
