// ============================================================
// SwarmCommon.hlsli
// C++ 側（Swarm/SwarmTypes.h）とレイアウトを完全に一致させること。
// StructuredBuffer にはリフレクションがないので、ずれても黙って壊れる。
//
// レイアウトの規則：どの構造体も 16 バイトの倍数にし、
// float3 が 16 バイト境界をまたがないようにする。
//
// 注意：生死フラグはこれらの構造体に入っていない。別の
// RWBuffer<uint> に置いている。SM5.0 のアトミックは
// RWByteAddressBuffer と RWBuffer<uint> / RWStructuredBuffer<uint> でしか使えず、
// スロットの確保に InterlockedCompareExchange が要るため。
// ============================================================

#ifndef SWARM_COMMON_HLSLI
#define SWARM_COMMON_HLSLI

// ---- 固定ステップ（このパイプラインの全 CS で共通） ----
static const float SWARM_FIXED_STEP = 0.02; // Unity のデフォルト（50Hz）

// ---- スロットの状態（別の状態バッファに入る） ----
static const uint SWARM_DEAD = 0;
static const uint SWARM_ALIVE = 1;

// ---- カウンタのバイトオフセット（RWByteAddressBuffer） ----
static const uint SWARM_CNT_ALIVE_ENEMIES = 0;
static const uint SWARM_CNT_KILLS = 4;
static const uint SWARM_CNT_PLAYER_DAMAGE = 8;
static const uint SWARM_CNT_ALIVE_PROJ = 12;

// プレイヤーに一番近い敵。key = (asuint(dist) & 0xFFFFF000) | slot。
// 正の float は uint として比べても大小が同じなので、InterlockedMin 1 回で
// 最短距離とその敵を同時に求められる。下位 12 ビット = 4096 スロット。kMaxEnemies
// は 4096 以下に保つこと（超えるとこの符号化が壊れる）
static const uint SWARM_CNT_NEAREST_KEY = 16;
static const uint SWARM_CNT_NEAREST_POS = 20;
static const uint SWARM_CNT_NEAREST_VEL = 32;
static const uint SWARM_CNT_NEAREST_DIST = 44;
// プレイヤーが拾った経験値、固定小数 x100。KILLS / PLAYER_DAMAGE と同じく
// 永久に累積し、CPU が差分を取る。拾った時に
// OrbMoveCS が書く。
static const uint SWARM_CNT_EXP = 48;
// 生きている経験玉の数。毎ステップクリア（ALIVE_ENEMIES と同じ）
static const uint SWARM_CNT_ALIVE_ORBS = 52;
// 範囲攻撃。どちらも毎ステップクリア（ALIVE_ENEMIES と同じ）
static const uint SWARM_CNT_ALIVE_AREAS = 56;
static const uint SWARM_CNT_TICKING_AREAS = 60; // このステップでダメージを与える範囲
// 合計 64 バイト
static const uint SWARM_NO_TARGET_KEY = 0xFFFFFFFFu;
static const uint SWARM_SLOT_MASK = 0xFFFu;
static const uint SWARM_DIST_MASK = 0xFFFFF000u;

#define SWARM_HP_SCALE 100.0
uint SwarmHpToFixed(float hp)
{
    return (uint) (hp * SWARM_HP_SCALE + 0.5);
}
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

    // animIndex 2 = 被弾の硬直。HitCS が animTime = 0 で立て、MoveCS が
    // animTime を g_HitStun まで数えてから消す。VS はこれを読んで
    // 光らせる。他の値は VAT アニメーション用に予約。
    float animTime;
    uint animIndex;
    float attackCooldown;
};

// ============================================================
// 敵の種類 + 種類ごとの状態：8 バイト。敵プールと同じ添字の
// 並列バッファ（SwarmEnemy は 48B のまま）。
// SwarmTypes.h の Swarm::EnemyExtra と一致させること
// 生成リクエストは種類を animIndex に入れて運ぶ。SpawnEnemyCS /
// RecycleCS がそれをここへ移し、animIndex を 0 に戻す。
// ============================================================
static const uint SWARM_KIND_MOB = 0u;    // 接触で殴る
static const uint SWARM_KIND_BOMBER = 1u; // 接触で導火線に点火し、g_BomberFuseTime 後に爆発
static const uint SWARM_KIND_ELITE = 2u;  // 大型の雑魚：体格 g_EliteScale、殴り g_EliteDamageMul、経験玉 g_EliteExpMul
static const uint SWARM_KIND_BOSS = 3u;   // ステージのボス：g_Boss*、被弾硬直なし、SwarmBossInfo に報告する
static const uint SWARM_KIND_GHOST = 4u;  // 最終ウェーブの幽霊：HP は雑魚と同じ、速い、壁や台地を突き抜けてまっすぐ飛ぶ（2026-09-30）
static const uint SWARM_KIND_SPLITTER = 5u;  // 死ぬと分裂体 3 体に分かれる（SwarmCorpseTrackCS -> 分裂リング -> CPU）（2026-10-03）
static const uint SWARM_KIND_SPLITLING = 6u; // スプリッターの小さな子。もう分裂しない
static const uint SWARM_KIND_BRUTE = 7u;     // 重装の雑魚：HP 3 倍、遅い、大きい。4 分目から雑魚と入れ替わる（2026-10-07）
static const uint SWARM_KIND_CHARGER = 8u;   // 止まって溜め（地面に赤い帯）、固定した線上を突進する（2026-10-08）
static const uint SWARM_KIND_SHIELD = 9u;    // 被弾ごとに g_ShieldArmor を減らす（最低でも g_ShieldMinFrac は通る）（2026-10-08）
static const uint SWARM_KIND_SPIKEBALL = 10u; // 砂漠のトゲ玉（2026-10-17）：硬い、遅い、転がる。触るとプレイヤーを弾き飛ばす（ContactCS -> playerHits のトゲ枠）。硬直せず、吹き飛ばされない
static const uint SWARM_KIND_BURROWER = 11u;  // 砂漠の砂潜り（2026-10-17）：砂の下を進み（砂丘だけ見え、当たらず、噛まない）、プレイヤーの横で g_BurrowSurfaceTime だけ地上に出て、また潜る。fuse > 0 = 地上にいる残り秒、<= 0 = -(潜っている秒)
static const uint SWARM_KIND_SANDMAGE = 12u;  // 砂漠の砂嵐の術師（2026-10-17）：距離を保ち、溜めてから竜巻を詠唱する（castEvents -> CPU が SWARM_KIND_TORNADO を生成）。fuse > 0 = 溜めの経過秒、< 0 = クールダウンの残り、0 = 準備完了
static const uint SWARM_KIND_TORNADO = 13u;   // 砂嵐の術師の竜巻（2026-10-17）：「敵の弾」。yaw に沿ってまっすぐ飛び（幽霊のように壁を抜ける）、当たらず狙われず、触るとプレイヤーを吹き上げ、fuse が尽きると死ぬ。fuse = 残り秒（0 = 生成直後、< 0 = 期限切れ）
static const uint SWARM_KIND_DASHBOMBER = 15u; // 突撃爆弾兵（2026-10-18）：プレイヤーが g_DashBomberTrigger 内に入ると自分で点火し、yaw を固定して g_DashBomberWindup 秒立ち、その yaw に沿ってまっすぐ走る（2026-10-20：狙い直さないので横に一歩で避けられる）。触れるか、溜めの g_DashBomberFuse 秒後に爆発（ContactCS）。fuse > 0 = 点火からの秒（AICS が数える）。点火中は自爆兵と同じく無敵
static const uint SWARM_KIND_DARKGHOST = 14u; // 最終ウェーブの黒幽霊（2026-10-18、Megabonk の黒い幽霊）：HP が非常に多い幽霊。プレイヤーより速く、大きく、g_DarkGhostDamageMul 倍で殴り、爆発級に吹き飛ばす。白い幽霊と同じく壁を抜けて飛ぶ。どちらの幽霊も硬直・凍結・減速・吹き飛ばしを受けない
// ---- 遺跡の敵（2026-10-25、ユーザー：「遺跡の雑魚を作って」 -> 呪いの鎧、石像ガーゴイル、死霊術師、鎖の番人） ----
static const uint SWARM_KIND_ARMOR = 16u;         // 呪いの鎧：硬くて遅い。1 回目の死では砕けず崩れ落ち（CorpseTrackCS -> 分裂リング）、CPU がそこに HP 半分の SWARM_KIND_ARMOR2 を起こす
static const uint SWARM_KIND_ARMOR2 = 17u;        // 蘇った呪いの鎧：普通に砕ける
static const uint SWARM_KIND_GARGOYLE = 18u;      // 石像ガーゴイル：プレイヤーが近づくまで石像（当たらない）、その後飛び（壁を抜け、g_GargoyleHover だけ浮く）、プレイヤーへ急降下してしばらく着地する。fuse：<= 0 眠り、(0, wake) 目覚め中、[wake, SWARM_GARGOYLE_DIVE) 飛行、>= SWARM_GARGOYLE_DIVE 急降下（溜め -> 急降下 -> 着地）
static const uint SWARM_KIND_NECRO = 19u;         // 死霊術師：砂嵐の術師と同じく距離を保つ。詠唱で近くの新しい死体から骸骨を起こす（CPU）
static const uint SWARM_KIND_SKELETON = 20u;      // 死霊術師が起こした骸骨：速くて脆い
static const uint SWARM_KIND_WARDEN = 21u;        // 鎖の番人：中距離で止まり、yaw を固定して溜める（突進の帯で線を見せる）。溜め終わりに CPU がその yaw に沿って鎖の鉤を投げる
// 面の固定エリート（2026-10-25）。A = 近接（大斧 / 騎兵 / 影の剣士）、B = 遠距離（弓手 / 炎術師 / 司書。
// g_FixedEliteKeepMin..Max の距離を保つ）。技は CPU（EliteAttacks）が走らせ、g_EliteCmd / g_EliteMove で動かす
static const uint SWARM_KIND_ELITE_A = 22u;
static const uint SWARM_KIND_ELITE_B = 23u;
static const float SWARM_GARGOYLE_DIVE = 1000.0;  // ガーゴイルの fuse がこれ以上 = 急降下（fuse - これ = 急降下に入ってからの秒）

// 最終ウェーブの幽霊 2 種（2026-10-18）：壁や台地を抜けてプレイヤーへまっすぐ飛び（AICS / MoveCS / PushCS）、
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
// SwarmCorpseList2CS / SwarmCorpseList3CS、2026-10-25）で作る種類：最初のパスはどれも UAV 8 本を使い切っている。
// 最初のパスではこれらの種類を除く
bool SwarmInKindList2(uint kind)
{
    return (kind >= SWARM_KIND_ARMOR && kind <= SWARM_KIND_WARDEN) || SwarmIsFixedElite(kind);
}
bool SwarmInCorpseList2(uint kind)
{
    return SwarmInKindList2(kind) || kind == SWARM_KIND_BOSS || kind == SWARM_KIND_SANDMAGE || kind == SWARM_KIND_DASHBOMBER;
}

struct SwarmEnemyExtra
{
    uint kind;
    // 自爆兵 : 点火してからの秒。0 = 未点火（点火時に 1 ステップ分足す）
    // 突撃兵 : < 0 = クールダウンの残り（0 まで数え上げる）、0 = 追跡中、> 0 = 溜め開始からの秒
    //          （SwarmChargerPhase で 溜め / 突進 / 立て直し に分ける）
    float fuse;
};

// ------------------------------------------------------------
// 敵の状態の並列バッファ enemySlow（float4、2026-10-08。以前は float2）：
//   x = 減速の残り秒、y = 減速量（毒沼、AreaDamageCS）
//   z = 凍結の残り秒（> 0：動かない、殴らない）
//   w = 凍結中：今回の凍結の長さ（VS が氷を生やす / 溶かすのに使う）
//       解けた後：再び凍れるようになるまでの残り秒
// 凍結は HitCS がリクエストする：animIndex = SWARM_ANIM_FREEZE_REQ、
// animTime = 秒。次のステップの AICS が受け取る（SwarmEnemy は 48B のまま）。
// Swarm::kAnimFreezeRequest と一致させること
// ------------------------------------------------------------
static const uint SWARM_ANIM_FREEZE_REQ = 3u;

// ============================================================
// 弾：48 バイト
// ============================================================
struct SwarmProjectile
{
    float3 position;
    float damage;

    float3 velocity;
    float lifetime;

    float radius;
    uint vfxType; // SwarmVFXTable のレシピ表の添字
    uint motion; // ビット 0-15 = 弾道表の添字、16-30 = 大きさの倍率（SwarmProjScale）。
                 // 生成リクエストではビット 31 = 曲線を左右反転
    float pathT; // 曲線を飛んでいる間のベジェパラメータ 0..1
};

// ============================================================
// 弾道
//
// SwarmMotion   : 弾のプロファイル 1 つにつき 1 行（エディタのデータ）。
//                 SwarmTypes.h の Swarm::Motion と一致させること。48 バイト。
// SwarmProjPath : 弾スロット 1 つにつき 1 つ、プールと同じ添字。
//                 この弾が飛んでいる 3 次ベジェ。生成時に GPU で作る
//                 （狙い直す時にも作り直す）。
//                 Swarm::ProjPath と一致させること。64 バイト。
//
// 制御点は射撃の座標系で書き、射撃の距離で拡縮するので、
// 1 本の曲線で近い標的にも遠い標的にも合う：
//     c.x = 銃口 -> 標的 に沿った割合
//     c.y = 横方向のずれ / 距離   （sideSign で左右反転）
//     c.z = 上方向のずれ / 距離   （ワールドの上）
//
// DROP（メテオ）は c1 を別の意味で使う（単位 m）：
//     c1.x = 着弾点から見た開始点の高さ
//     c1.y = 開始点の水平オフセット（銃口の方へ戻る向き）
//            （なので水平から atan(c1.x / c1.y) の角度で落ちる）
// 着弾点は生成時（その時の敵の位置）に固定するので、
// 地面の警告リング（SwarmDropRingVS）はちょうど落ちる場所に出る。
// ============================================================
static const uint SWARM_MOTION_STRAIGHT = 0;
static const uint SWARM_MOTION_CURVE_ONCE = 1;
static const uint SWARM_MOTION_TRACK = 2;
static const uint SWARM_MOTION_DROP = 3;
// LOB（2026-10-01、毒の魔法）：銃口から弧を描いて、生成時に一番近い敵が
// 立っていた場所へ投げる。CURVE と同じように経路を作る
// （c1 / c2 は射撃の座標系なので c.z で弧が持ち上がる）が、振る舞いは DROP と同じ：
// 途中で敵に当たらず、地形判定もせず、必ず p3 に落ちて
// そこに命中範囲を残す。
static const uint SWARM_MOTION_LOB = 4;
// プレイヤーの周りを回る（2026-11-01）：c1.x = rad/s、c1.y = 半径 m、c1.z = 高さオフセット m。path.sideSign の角度から始める
static const uint SWARM_MOTION_ORBIT = 5;

bool SwarmMotionLands(uint mode)
{
    return mode == SWARM_MOTION_DROP || mode == SWARM_MOTION_LOB;
}

static const uint SWARM_NO_TARGET = 0xFFFFFFFFu;
static const uint SWARM_MOTION_INDEX_MASK = 0xFFFFu;
static const uint SWARM_MOTION_FLIP_BIT = 0x80000000u;

// ------------------------------------------------------------
// 連鎖（基本魔法 -> 上級魔法、例：ファイアボール / ストーン
// ショット -> メテオ）。生成リクエストは並列の uint2 を持てる
// （SwarmSpawnProjCS t4）：
//   x = 連鎖タグ：ビット k =「この弾が消えたら、杖の上級魔法 k を
//       そこで発動してよい」。スロットごとに projTags に保持する。
//   y = フラグ：SWARM_SPAWN_AT_POS = DROP はプレイヤーに一番近い敵を
//       狙わず、リクエストの位置に落ちる。
// SwarmProjEndCS は、このステップで消えたタグ付きの弾をリングに書く
// （ヘッダ 16B：[0] = これまでに書いたイベントの総数、その後に
// SWARM_MAX_TRIGGER_EVENTS 個の 16B のエントリ：地面の位置 xyz + タグ）。
// CPU はリングをリードバックし、まだ見ていないエントリを取る。
// SwarmTypes.h の Swarm::kMaxTriggerEvents / kSpawnAtPos と一致させること
// ------------------------------------------------------------
static const uint SWARM_MAX_TRIGGER_EVENTS = 128u;
static const uint SWARM_SPAWN_AT_POS = 1u;

// ------------------------------------------------------------
// 弾が残す範囲の強化（2026-10-02）。y のビット 8-19 = 威力の倍率、
// ビット 20-31 = 持続の倍率、256 = 1.0（0 は 1.0 として読む）。
// SpawnProjCS が y >> SWARM_SPAWN_BOOST_SHIFT をスロットごとに projBoost に保持し、
// HitCS / ProjMoveCS がそれを SwarmSpawnAreaFromDef に渡す。
// 威力 =「魔法威力」の能力カード、持続 = 魔力解放中に詠唱した弾
// （持続する範囲だけ長くなる）。
// SwarmTypes.h の Swarm::kSpawnBoostShift / PackSpawnBoost と一致させること
// ------------------------------------------------------------
static const uint SWARM_SPAWN_BOOST_SHIFT = 8u;

// ------------------------------------------------------------
// 連鎖のルーン（2026-10-16）：連鎖が残っている弾は、生きた敵に当たっても
// 消えない。その後 ProjMoveCS が SWARM_CHAIN_RADIUS 内で一番近い別の敵を
// 狙い、今の位置から新しい曲線を作る。
// HitCS にはスロットごとのバッファ用の UAV が残っていないので、連鎖の状態は
// projStates に載せる（他の読み手はどれも == / != SWARM_DEAD しか見ない）：
//   ビット 31     : SWARM_PROJ_CHAINED（生きている値。SWARM_DEAD にはならない）
//   ビット 30     : SWARM_PROJ_CHAIN_PENDING：HitCS がこのステップで何かに当たった。
//                ProjMoveCS が次の敵を選ぶ（そしてこれを消す）
//   ビット 16-23 : 残りの連鎖回数
//   ビット 0-15  : 最後に当たった敵（HitCS はスキップする。0xFFFF = なし）
// 回数は生成リクエストの pathT に入って届く（リクエストでは未使用で、
// SpawnProjCS が上書きする）。SwarmSystem::SpawnProjectile(chainCount)
// ------------------------------------------------------------
static const uint SWARM_PROJ_CHAINED = 0x80000000u;
static const uint SWARM_PROJ_CHAIN_PENDING = 0x40000000u;
static const uint SWARM_PROJ_CHAIN_LEFT_SHIFT = 16u;
static const uint SWARM_PROJ_CHAIN_LEFT_MASK = 0xFFu;
static const uint SWARM_PROJ_CHAIN_LAST_MASK = 0xFFFFu;
static const uint SWARM_PROJ_CHAIN_NO_LAST = 0xFFFFu;
static const float SWARM_CHAIN_RADIUS = 8.0;
uint SwarmProjChainState(uint left, uint lastHit)
{
    return SWARM_PROJ_CHAINED
         | ((left & SWARM_PROJ_CHAIN_LEFT_MASK) << SWARM_PROJ_CHAIN_LEFT_SHIFT)
         | (lastHit & SWARM_PROJ_CHAIN_LAST_MASK);
}
// 貫通（2026-10-28、黄金の矢）：同じ連鎖状態にこのビットを足したもの。
// 生きた敵に当たると、貫通を 1 つ減らし、当たった敵を覚えて（弾が通り抜ける
// 間はスキップ）弾は生き続けるが、狙い直しはしない：PENDING は立てず、
// ProjMoveCS は直進弾をまっすぐのまま飛ばす。
// 貫通するたびに威力を SWARM_PIERCE_FALLOFF 倍する。回数は
// プロファイルから（SwarmMotionPierce）。連鎖のルーンも付くと、
// 貫通数を跳躍回数に足し、弾は跳ぶ（連鎖の挙動）
static const uint SWARM_PROJ_PIERCE = 0x20000000u;
static const float SWARM_PIERCE_FALLOFF = 0.8;

// ------------------------------------------------------------
// 死んだ敵が剛体の部品に砕け散る（2026-10-02）。
// 前フレームで生きていたスロットが今 DEAD なら SwarmCorpseTrackCS が 1 つ書き、
// SwarmCorpseVS が (seed, age) の閉じた式で部品を飛ばす。
// SwarmTypes.h の Swarm::Corpse / kMaxCorpses と一致させること
// ------------------------------------------------------------
static const uint SWARM_MAX_CORPSES = 512u;

struct SwarmCorpse
{
    float3 position; // 死んだ場所（敵の位置 = 地面 + groundY）
    float yaw;
    float2 dir;      // 飛ばす方向、単位 xz（プレイヤーから離れる向き）
    float birth;     // SwarmSystem の時計での死亡時刻、0 = 空
    uint kind;
    uint seed;
    uint cause;      // 予約：死因（炎 / 毒 / 雷のバリエーション用）。今は 0
    float2 _pad;
};

float SwarmBoostDamage(uint boost)
{
    uint q = boost & 0xFFFu;
    return (q == 0u) ? 1.0 : (float) q / 256.0;
}

float SwarmBoostDuration(uint boost)
{
    uint q = (boost >> 12) & 0xFFFu;
    return (q == 0u) ? 1.0 : (float) q / 256.0;
}

// ------------------------------------------------------------
// 弾 / 範囲 1 つの大きさの倍率（拡大鏡のアイテム）。
// 弾の倍率 = 当たり判定の半径 / プロファイルの半径
// （SwarmMotion.baseRadius）。SwarmSpawnProjCS で一度だけ求め、
// SwarmProjectile.motion のビット 16-30 に入れる（読み手はどれも添字を
// SWARM_MOTION_INDEX_MASK でマスクする）。範囲は倍率を SwarmArea.flags の
// ビット 16-31 に持つ。固定小数、1024 = 1.0、0 = 1.0（CPU からの生成
// リクエストはこのビットを空のままにする）。
// 掛かる先：命中範囲の半径、パーティクルの大きさ / オフセット /
// 形状 / 速度（SwarmEmitCS）、点光源の半径、スプライトの大きさ、
// メテオの警告リング。
// ------------------------------------------------------------
static const uint SWARM_SCALE_SHIFT = 16u;
static const float SWARM_SCALE_ONE = 1024.0;

uint SwarmPackScale(float scale, uint maxQ)
{
    return (uint) clamp(round(scale * SWARM_SCALE_ONE), 1.0, (float) maxQ);
}
float SwarmUnpackScale(uint q)
{
    return (q == 0u) ? 1.0 : (float) q / SWARM_SCALE_ONE;
}

// 生きている弾の大きさの倍率（1 = 作ったまま）
float SwarmProjScale(SwarmProjectile p)
{
    return SwarmUnpackScale((p.motion >> SWARM_SCALE_SHIFT) & 0x7FFFu);
}

struct SwarmMotion
{
    uint mode;
    float retargetRadius; // TRACK 専用。<= 0 = 無制限
    uint hitArea; // 弾が当たった所に生成する範囲の定義。0 = なし
    uint hitAreaFlags; // ビット 0 = 寿命切れ / 壁に当たった時も生成する

    float3 c1;
    float baseRadius; // プロファイルの当たり判定の半径：これより大きい弾は大きく描く

    float3 c2;
    float freezeTime; // > 0：当たった敵をこの秒数だけ凍らせる（アイスランス、2026-10-08。以前は _pad2）
};

struct SwarmProjPath
{
    float3 p0;
    uint target; // 敵のスロット、SWARM_NO_TARGET = 直進中

    float3 p1;
    float duration; // p0 から p3 までの秒

    float3 p2;
    float sideSign; // 横オフセットを飛行軸まわりに回す角度（ラジアン、1 発ごとに固定。0 = 右、pi = 左、それ以外 = ランダム。SwarmBuildPath）

    float3 p3;
    float speed; // 公称速度。曲線を離れた時に戻す
};

float3 SwarmBezier(SwarmProjPath c, float t)
{
    float u = 1.0 - t;
    return c.p0 * (u * u * u)
         + c.p1 * (3.0 * u * u * t)
         + c.p2 * (3.0 * u * t * t)
         + c.p3 * (t * t * t);
}

float3 SwarmBezierTangent(SwarmProjPath c, float t)
{
    float u = 1.0 - t;
    return (c.p1 - c.p0) * (3.0 * u * u)
         + (c.p2 - c.p1) * (6.0 * u * t)
         + (c.p3 - c.p2) * (3.0 * t * t);
}

// p0..p3 と duration を埋める。sideSign / speed / target は呼び出し側が持つ。
// keepHeading：c1 を使わず `heading` に沿って出る（飛行中の狙い直し）ので、
// 新しい曲線の始まりで経路が折れない。
void SwarmBuildPath(inout SwarmProjPath path, SwarmMotion m,
                    float3 from, float3 to, float3 heading, bool keepHeading)
{
    float3 chord = to - from;
    float dist = length(chord);
    float3 fwd = (dist > 1e-4) ? chord / dist : heading;

    float3 up = float3(0, 1, 0);
    float3 side = cross(up, fwd);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-6) ? side * rsqrt(sideLenSq) : float3(1, 0, 0);

    // 横オフセット（c.y）は path.sideSign（ラジアン）だけ飛行軸まわりに回す：
    // 0 = 右、pi = 左、それ以外 = 1 発ごとにランダムな方向（2026-10-06）。c.z はワールドの上のまま
    float3 lateral = side * cos(path.sideSign) + cross(fwd, side) * sin(path.sideSign);

    path.p0 = from;
    path.p3 = to;
    path.p1 = from + fwd * (m.c1.x * dist)
                   + lateral * (m.c1.y * dist)
                   + up * (m.c1.z * dist);
    path.p2 = from + fwd * (m.c2.x * dist)
                   + lateral * (m.c2.y * dist)
                   + up * (m.c2.z * dist);
    if (keepHeading)
        path.p1 = from + heading * (dist / 3.0);

    // 弧長 ≒ 弦と制御多角形の平均
    float len = 0.5 * (dist + length(path.p1 - path.p0)
                            + length(path.p2 - path.p1)
                            + length(path.p3 - path.p2));
    path.duration = max(len / max(path.speed, 0.01), SWARM_FIXED_STEP);
}

// DROP：開始点から着弾点への直線。ハンドルを線上に置き、
// 進んだ距離が s(t) = 0.4 t + 0.6 t^2 になるようにする：落ちるほど速くなる
// （着弾時に公称速度の 1.6 倍、平均すると公称速度）。
// path.speed は呼び出し側が設定すること。
void SwarmBuildDropPath(inout SwarmProjPath path, float3 start, float3 impact)
{
    float3 d = impact - start;
    path.p0 = start;
    path.p1 = start + d * (0.4 / 3.0);
    path.p2 = start + d * ((2.0 * 0.4 + 0.6) / 3.0);
    path.p3 = impact;
    path.duration = max(length(d) / max(path.speed, 0.01), SWARM_FIXED_STEP);
}

// ============================================================
// 範囲攻撃（爆発、魔法陣）
//
// SwarmArea    : 生きている範囲 1 つ。48 バイト。Swarm::Area と一致させること。
//                一発の範囲と持続する範囲は同じもの：
//                爆発は 1 回だけ判定し、パーティクルが出ている間だけ
//                残る範囲。
// SwarmAreaDef : テンプレート。32 バイト。Swarm::AreaDef と一致させること。
//                行 0 = なし。GPU 自身が範囲を生成する時（弾の命中）に使う。
//                場所を知っているのが GPU だけなので。
//
// 形：円盤。XZ の距離 <= radius かつ |dy| <= halfHeight
// （+ 敵のカプセル）。縦方向の規則は弾の命中と同じ。
// ============================================================
static const uint SWARM_MAX_AREAS = 256; // = Swarm::kMaxAreas

static const uint SWARM_AREA_FOLLOW_PLAYER = 1u; // 中心がプレイヤーに付いて動く
static const uint SWARM_AREA_STUN = 2u; // 判定のたびに敵を止めて光らせる（被弾硬直）
// カプセル（光線）：範囲は線分 center -> areaEnds[slot] の周り radius。
// 始点 / 終点 / 半径は毎ステップ SwarmBeamCB から来る
// （AreaTickCS）、チャンネル = flags のビット 8-11。生成するのは CPU だけ。
static const uint SWARM_AREA_CAPSULE = 4u;
// 出現時にカメラを揺らす（爆発。= Swarm::kAreaShake、2026-10-03）。
// 新しく生まれた範囲を全部見つけている SwarmLiquidTrackCS が数える
static const uint SWARM_AREA_SHAKE = 8u;
static const uint SWARM_AREA_BEAM_SHIFT = 8u; // (flags >> shift) & 0xF = 光線のチャンネル
static const uint SWARM_MAX_BEAMS = 4u; // = Swarm::kMaxBeams

// 減速（2026-10-01、毒沼）：flags のビット 12-15 = q。判定のたびに中の敵を
// q / 15 だけ減速する（0 = なし）。AreaDamageCS は敵ごとに一番強い減速を
// enemySlow[slot] = (残り秒, 量) に保持し、判定のたびに更新する。なので
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

// 凍結（2026-10-10、アイススパイク）：flags のビット 4-7 = q。判定のたびに中の敵を
// q * 0.25 s 凍らせる（0 = なし）。AreaDamageCS は命中したアイスランスと同じ
// 凍結リクエスト（SWARM_ANIM_FREEZE_REQ）を残すので、エリート / ボスの半減と
// 解けた後の免疫は AICS が適用する。
static const uint SWARM_AREA_FREEZE_SHIFT = 4u; // = Swarm::kAreaFreezeShift

float SwarmAreaFreeze(uint flags)
{
    return (float) ((flags >> SWARM_AREA_FREEZE_SHIFT) & 0xFu) * 0.25;
}

// 押し出し（2026-10-12、突風）：カプセルでない範囲は吹き飛ばしの強さ q / 15 を
// 光線チャンネルのビット（8-11、CAPSULE 以外では未使用）に持つ。
// AreaDamageCS が enemyPush[slot] = (プレイヤーから離れる xz の速度,
// 残り秒, 0) を書き、MoveCS が減衰させながらそのステップの移動に足す。
// エリートは半分、ボスは 0（SwarmPushKindMul）。
static const float SWARM_PUSH_SPEED = 28.0; // = Swarm::kPushSpeed（q = 15 での m/s。減衰があるので speed * time / 2 = 3.5 m）
static const float SWARM_PUSH_TIME = 0.25;  // = Swarm::kPushTime

float SwarmAreaPush(uint flags)
{
    if ((flags & SWARM_AREA_CAPSULE) != 0u)
        return 0.0;
    return (float) ((flags >> SWARM_AREA_BEAM_SHIFT) & 0xFu) / 15.0;
}

// 引き寄せ（2026-10-22、渦）：tickNow に SWARM_AREA_EXTRA_PULL がある範囲は、
// 同じ 4 ビットを自分の中心への引き寄せとして読む。AreaDamageCS が
// enemyPush[slot] = (中心へ向かう速度, 秒, 1) を書く。
// w = 1 は引き寄せの印で、進行中の押し出しは上書きしない（突風が勝つ）。
// 1 回のパルスは範囲の判定間隔（最低 SWARM_PUSH_TIME）続くので、
// 判定の合間に引き寄せが止まらない：MoveCS は最後の SWARM_PUSH_TIME まで
// 全速を保ってから減衰させる。進む距離は
// speed x (seconds - SWARM_PUSH_TIME / 2)。速度は 1 回のパルスで
// 距離の q x SWARM_PULL_SHARE だけ進むように選ぶ：中心を越えない。
// SWARM_PULL_DEAD 内では引き寄せない（ガタつき防止）。
static const float SWARM_PULL_SHARE = 0.62; // q = 15 でのパルス 1 回あたりの距離の割合
static const float SWARM_PULL_DEAD = 0.35;  // m

// 砂の下の砂潜り：弾と光線は当たらず、噛まず、狙われない（2026-10-17）。
// 地面の範囲（メテオの爆発、毒沼、氷のトゲ…）は届く（2026-10-26、AreaDamageCS）
static const float SWARM_BURROW_RISE_TIME = 0.3; // 体が地上に出る / 潜るのにかかる秒（見た目だけ）
bool SwarmBuried(SwarmEnemyExtra ex)
{
    return (ex.kind == SWARM_KIND_BURROWER && ex.fuse <= 0.0)
        || (ex.kind == SWARM_KIND_BOSS && ex.fuse < -0.5);   // 砂の守護者が半分以上砂に潜っている（2026-10-24）
}
// 導火線に火の付いた自爆兵 / 突撃爆弾兵（2026-10-18、ユーザー：「自爆している間はどの敵も無敵」）：
// ダメージを受けず（HitCS / AreaDamageCS は SwarmUntargetable でスキップ）、硬直せず、減速・凍結されない（AICS）。
// 点火前は普通の雑魚と同じく死ぬ
bool SwarmInvulnerable(SwarmEnemyExtra ex)
{
    return (ex.kind == SWARM_KIND_BOMBER || ex.kind == SWARM_KIND_DASHBOMBER) && ex.fuse > 0.0;
}
// 弾 / 光線が当たらず、魔法 / 光線 / 最寄りの敵の検索で狙われず、HP バーも出ない
// （例外：潜っているものは地面の範囲が当たり、バーも出る。AreaDamageCS / HpBarVS 参照）：
// 潜っている砂潜り、砂嵐の術師の竜巻（2026-10-17）、点火した自爆兵 / 突撃爆弾兵（2026-10-18）
bool SwarmUntargetable(SwarmEnemyExtra ex)
{
    return SwarmBuried(ex) || ex.kind == SWARM_KIND_TORNADO || SwarmInvulnerable(ex)
        || (ex.kind == SWARM_KIND_GARGOYLE && ex.fuse <= 0.0)   // 眠っているガーゴイルは石像（2026-10-25）
        || (SwarmIsFixedElite(ex.kind) && ex.fuse < -0.5);       // 影の剣士が消えている（AICS：fuse = -hide）
}

float SwarmPushKindMul(uint kind)
{
    // 出口は 1 つ（2026-10-25：途中の return で fxc が X4000 を出したため）
    bool none = (kind == SWARM_KIND_BOSS)
             || (kind == SWARM_KIND_SPIKEBALL)   // 重すぎて吹き飛ばない
             || (kind == SWARM_KIND_BURROWER)    // 砂の下
             || (kind == SWARM_KIND_TORNADO)     // 風は風を押さない
             || SwarmIsGhost(kind)               // 最終ウェーブは吹き飛ばせない（2026-10-18）
             || (kind == SWARM_KIND_GARGOYLE);   // 石（2026-10-25）
    bool half = (SwarmIsBigElite(kind) || kind == SWARM_KIND_ARMOR || kind == SWARM_KIND_ARMOR2);
    return none ? 0.0 : (half ? 0.5 : 1.0);
}

// ------------------------------------------------------------
// 範囲ごとの追加情報を SwarmArea.tickNow に詰める（2026-10-17、泥沼
// と毒沼）。48 バイトの SwarmArea には空きフィールドがなく flags も
// 埋まっているので、tickNow の上位ビットで範囲ごとの値をさらに 2 つ運ぶ。
// 下位 1 バイト（判定の状態）は AreaTickCS のもので、残りには触れない。
// 生成側がそこに SwarmAreaDef.extra（GPU）/ リクエストの tickNow（CPU、
// AreaProfile::Extra）を入れる。SwarmTypes.h の Swarm::kAreaExtra* と一致させること
//   ビット 0     : SWARM_AREA_TICK_NOW   このステップでダメージを与える
//   ビット 1     : SWARM_AREA_TICK_FIRST このステップが範囲の最初の判定
//   ビット 2     : SWARM_AREA_TICK_SEEN  以前に判定したことがある（消えない）
//   ビット 8-11 : 毒素 q / 15：判定のダメージのうちこの割合はすぐには与えず、
//               敵（enemyPoison）に溜めて SWARM_TOXIN_TIME かけて
//               減らす。毒素のある範囲は 1 体の敵に重ならない：
//               一番強いものだけ効く（毒沼）
//   ビット 12    : SWARM_AREA_EXTRA_SPLASH：最初の判定だけダメージ
//               （泥沼：着地で飛沫、その後は減速だけ）
//   ビット 13    : SWARM_AREA_EXTRA_PULL：flags の押し出しのビットで、代わりに
//               範囲の中心へ引き寄せる（渦、2026-10-22）
// ------------------------------------------------------------
static const uint SWARM_AREA_TICK_NOW = 1u;
static const uint SWARM_AREA_TICK_FIRST = 2u;
static const uint SWARM_AREA_TICK_SEEN = 4u;
static const uint SWARM_AREA_TICK_MASK = 0xFFu;
static const uint SWARM_AREA_EXTRA_TOXIN_SHIFT = 8u;
static const uint SWARM_AREA_EXTRA_SPLASH = 1u << 12u;
static const uint SWARM_AREA_EXTRA_PULL = 1u << 13u;   // = Swarm::kAreaExtraPull
static const uint SWARM_AREA_EXTRA_LIFESTEAL = 1u << 14u;   // = Swarm::kAreaExtraLifesteal：当たった敵に印を付ける（吸血のルーン、2026-10-25）
static const uint SWARM_TAG_LIFESTEAL = 0x80000000u;      // = Swarm::kTagLifesteal：弾の連鎖タグ（projTags）の最上位ビット
static const uint SWARM_AREA_EXTRA_ELEMENT_SHIFT = 16u;   // ビット 16..19：ダメージの属性（ダメージ数字の色、2026-10-17）
uint SwarmAreaElement(uint tickNow)
{
    return (tickNow >> SWARM_AREA_EXTRA_ELEMENT_SHIFT) & 0xFu;
}
static const float SWARM_TOXIN_TIME = 1.0; // 毒素 1 回分が減りきるまでの秒

float SwarmAreaToxin(uint tickNow)
{
    return (float) ((tickNow >> SWARM_AREA_EXTRA_TOXIN_SHIFT) & 0xFu) / 15.0;
}

static const uint SWARM_HITAREA_ON_EXPIRE = 1u; // SwarmMotion.hitAreaFlags
// 停止 / 加速（2026-10-16、ストーンショット：岩が一瞬空中に止まり、
// 大きく加速して飛ぶ）。48 バイトの SwarmMotion のレイアウトを変えないよう、
// hitAreaFlags の空きビットに詰める。
//   ビット 8-15 : 停止時間（0.02 s 単位、0 = なし）。SpawnProjCS が pathT に
//               -hold を入れ、ProjMoveCS が 0 まで数え上げてから飛ばす
//   ビット 16    : 加速：曲線パラメータを 2 乗する（速度 0 で始まり、
//               公称速度の 2 倍で終わる。合計時間は同じ）
static const uint SWARM_HITAREA_HOLD_SHIFT = 8u;
static const uint SWARM_HITAREA_HOLD_MASK = 0xFFu;
static const float SWARM_HITAREA_HOLD_UNIT = 0.02;
static const uint SWARM_HITAREA_EASE_IN = 1u << 16u;
// ビット 20..23：弾のダメージ属性（0 なし、1 炎、2 氷、3 雷、4 奥術、5 土、6 毒、7 風、8 光）。
// ダメージ数字の色だけに使う（2026-10-17）：HitCS が敵の状態ワードに入れ、SwarmDamageTextCS が読む
static const uint SWARM_HITAREA_ELEMENT_SHIFT = 20u;
uint SwarmMotionElement(SwarmMotion m)
{
    return (m.hitAreaFlags >> SWARM_HITAREA_ELEMENT_SHIFT) & 0xFu;
}
// ビット 24..27：最初の命中の後に弾が貫通する敵の数（0..15、2026-10-28、黄金の矢）。
// Swarm::kHitAreaPierceShift と一致させること
static const uint SWARM_HITAREA_PIERCE_SHIFT = 24u;
uint SwarmMotionPierce(SwarmMotion m)
{
    return (m.hitAreaFlags >> SWARM_HITAREA_PIERCE_SHIFT) & 0xFu;
}
// 敵の状態ワード：下位 1 バイトに SWARM_DEAD / SWARM_ALIVE、ビット 8..11 = 最後の命中の属性（ダメージ数字の色）。
// 読み手はどれも SWARM_DEAD とだけ比べる。ダメージのパスは CAS で属性を書くので、
// その間にとどめを刺した側が書いた DEAD を上書きすることはない
static const uint SWARM_STATE_ELEMENT_SHIFT = 8u;
static const uint SWARM_STATE_LIFESTEAL = 1u << 12u;   // 最後の命中が吸血の魔法だった（SwarmDamageTextCS がこの敵の HP 減少を吸血の合計に足す）
uint SwarmStateElement(uint state)
{
    return (state >> SWARM_STATE_ELEMENT_SHIFT) & 0xFu;
}
// とどめの一撃の属性（2026-10-25）。とどめを刺した側が SWARM_DEAD を書くので、状態ワードには載せられない：
// HitCS / AreaDamageCS は死んだスロットの attackCooldown に -(1 + 属性) として残す。生きている敵のクールダウンは
// 負にならず（ContactCS が 0 でクランプ）、SpawnEnemyCS / RecycleCS がスロットを上書きするまで死んだスロットの
// クールダウンは誰も読まない。SwarmDamageTextCS がこれで撃破の数字に色を付ける
float SwarmKillElementMark(uint elem)
{
    return -1.0 - (float) elem;
}
uint SwarmKillElement(float attackCooldown)
{
    return (attackCooldown < -0.5) ? (uint) (-attackCooldown - 0.5) : 0u;
}
// SwarmDamageTextCS の prevHp（前フレームの HP、固定小数）にこのビットが立っている：その後 RecycleCS がスロットに
// 新しい敵を入れた（下位ビット = その生成時の HP）。前の敵の保留中の数字は捨て、減少量は新しい HP から測る。
// 生きている敵の HP にこのビットが立つことはない（HP が 0 を越えて折り返すのはとどめの一撃だけで、そのスロットはもう DEAD）
static const uint SWARM_PREVHP_RECYCLED = 0x80000000u;
static const float SWARM_HOLD_FORWARD = 1.0; // 停止する弾は銃口からこれだけ前に生まれる（m）
float SwarmMotionHold(SwarmMotion m)
{
    return (float) ((m.hitAreaFlags >> SWARM_HITAREA_HOLD_SHIFT) & SWARM_HITAREA_HOLD_MASK) * SWARM_HITAREA_HOLD_UNIT;
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
    uint tickNow; // 下位 1 バイト = 判定の状態（SWARM_AREA_TICK_*、AreaTickCS が書く）、上位ビット = 生成時に設定する追加情報（毒素、飛沫）
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
    uint extra; // SwarmArea.tickNow に入る（毒素の割合、飛沫のみ。SWARM_AREA_EXTRA_* 参照）
};

// ---- GPU 側の生成 ----
// 範囲を生成するシェーダーは、このファイルを include する前に
// 3 つのレジスタを #define する。例：
//     #define SWARM_AREA_POOL_U  u6
//     #define SWARM_AREA_STATE_U u7
//     #define SWARM_AREA_DEF_T   t2
#ifdef SWARM_AREA_POOL_U
RWStructuredBuffer<SwarmArea> areas : register(SWARM_AREA_POOL_U);
RWBuffer<uint> areaStates : register(SWARM_AREA_STATE_U);
StructuredBuffer<SwarmAreaDef> areaDefs : register(SWARM_AREA_DEF_T);
// 任意：生成したものが進んでいた方向（範囲スロットごと）
// （xy = 単位 xz、w = 1「新しい」）。SwarmLiquidTrackCS が範囲を最初に見た時に
// 一度だけ読み、w を消す。UAV に余裕のあるシェーダーは SWARM_AREA_DIR_U を
// #define する（ProjMoveCS：LOB の毒瓶がそこに落ちる）。HitCS は UAV が
// 残っていないので、その範囲は方向を持たない（液面は角度を自分で選ぶ）
#ifdef SWARM_AREA_DIR_U
RWStructuredBuffer<float4> areaDirs : register(SWARM_AREA_DIR_U);
#endif

// 他のプールと同じ CAS の走査。プールが満杯 -> 範囲なし（劣化するだけで、壊れはしない）。
// scale: 生成したものの大きさの倍率（弾の SwarmProjScale、1 = 作ったまま）
// boost: 弾の projBoost（SwarmBoostDamage / SwarmBoostDuration）、0 = なし。
//        持続の分は持続する範囲だけ伸ばす（一発の範囲はどのみち 1 回だけ判定）
// dir  : 生成したものの xz の速度（長さは任意）。SWARM_AREA_DIR_U がある時だけ保持
// extraOr: この範囲 1 つに足す SWARM_AREA_EXTRA_* のビット（それを残した弾の吸血の印、2026-10-25）
void SwarmSpawnAreaFromDefX(uint defId, float3 pos, uint salt, float scale, uint boost, float2 dir, uint extraOr)
{
    if (defId == 0u)
        return;

    SwarmAreaDef d = areaDefs[defId];
    bool lasting = d.tickInterval < d.duration;

    SwarmArea a = (SwarmArea) 0;
    a.center = pos;
    a.radius = d.radius * scale;
    a.damage = d.damage * SwarmBoostDamage(boost);
    a.timeLeft = d.duration * (lasting ? SwarmBoostDuration(boost) : 1.0);
    a.tickInterval = d.tickInterval;
    a.tickTimer = 0.0;
    a.halfHeight = d.halfHeight * scale;
    a.flags = (d.flags & 0xFFFFu & ~SWARM_AREA_FOLLOW_PLAYER) // 命中から生まれた範囲：その場に留まる
        | (SwarmPackScale(scale, 0xFFFFu) << SWARM_SCALE_SHIFT);
    a.vfxType = d.vfxType;
    a.tickNow = (d.extra | extraOr) & ~SWARM_AREA_TICK_MASK; // 追加情報だけ。判定の状態は空から始める

    uint start = (salt * 97u) % SWARM_MAX_AREAS;
    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        uint slot = (start + k) % SWARM_MAX_AREAS;
        uint was;
        InterlockedCompareExchange(areaStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
        if (was == SWARM_DEAD)
        {
            areas[slot] = a;
#ifdef SWARM_AREA_DIR_U
            float dirLenSq = dot(dir, dir);
            areaDirs[slot] = float4((dirLenSq > 1e-8) ? dir * rsqrt(dirLenSq) : float2(0.0, 0.0), 0.0, 1.0);
#endif
            return;
        }
    }
}
void SwarmSpawnAreaFromDef(uint defId, float3 pos, uint salt, float scale, uint boost, float2 dir)
{
    SwarmSpawnAreaFromDefX(defId, pos, salt, scale, boost, dir, 0u);
}
#endif

// ============================================================
// 経験玉：32 バイト
// ============================================================
struct SwarmOrb
{
    float3 position;
    float amount;

    float3 velocity;
    float _pad;
};

// ============================================================
// レシピ：vfxType の中身。32 バイト。
// 各範囲は種類ごとの表を指す。弾は複数のエミッタ（芯 + 尾）、
// メッシュ、ライトを持てる。それぞれを専用の CS が、
// 同じ「弾 1 つに 1 スレッド」の骨組みで処理する。
// SwarmVFXTable.h の Swarm::VFXRecipe と一致させること
// ============================================================
struct SwarmRecipe
{
    uint particleStart;
    uint particleCount;
    uint modelStart;
    uint modelCount;

    uint lightStart;
    uint lightCount;
    uint spriteStart; // SwarmSpriteDef の表（GPU の範囲だけ。以前は _pad）
    uint spriteCount;
};

// ============================================================
// フレームごとの定数。デフォルトのレジスタは b0。ParticleCommon.hlsli
// （b0/b1 を使う）も include する CS は、このファイルを include する前に
// SWARM_FRAME_CB_REG を #define すること。
// ============================================================
#ifndef SWARM_FRAME_CB_REG
#define SWARM_FRAME_CB_REG b0
#endif

cbuffer SwarmFrameCB : register(SWARM_FRAME_CB_REG)
{
    float3 g_PlayerPos;
    float g_PlayerRadius;

    float g_Step;
    uint g_MaxEnemies;
    uint g_MaxProjectiles;
    uint g_MaxOrbs;

    float3 g_GridOrigin; // セル (0,0) のワールド位置
    float g_CellSize;

    uint g_GridW;
    uint g_GridD;
    uint g_Seed;
    uint g_PlayerAlive; // 0 = 死亡、敵は追うのをやめる
};

// ============================================================
// 敵 AI の調整値。デフォルトのレジスタは b1。すでに b1 を使っている CS は
// このファイルを include する前に SWARM_AI_CB_REG を #define すること。
// SwarmTypes.h の Swarm::AICB と一致させること
// ============================================================
#ifndef SWARM_AI_CB_REG
#define SWARM_AI_CB_REG b1
#endif

cbuffer SwarmAICB : register(SWARM_AI_CB_REG)
{
    float g_SeparationRadius;
    float g_SeparationPower;
    float g_AvoidPower;
    float g_LookAhead;

    float g_GroundY;
    float g_EnemyRadius;
    float g_EnemyCapsuleHalf;
    float g_VelocityLag;

    float g_MaxSpeedMul;
    float g_TurnSpeed;
    float g_PlayerPushOut;
    float g_ContactDamage;

    float g_AttackInterval;
    float g_PlayerCapsuleHalf;
    float g_HitStun; // 被弾後に敵が止まる秒（HitCS が animIndex = 2 を立てる）
    float g_HitFlash; // 硬直開始時の頂点色の倍率。1 まで減衰する
};

// ============================================================
// 自爆兵の調整値。必要なシェーダーだけが
// SWARM_BOMBER_CB_REG を #define して使う（ContactCS b3、敵の VS b5）。
// SwarmTypes.h の Swarm::BomberCB と一致させること
// ============================================================
#ifdef SWARM_BOMBER_CB_REG
cbuffer SwarmBomberCB : register(SWARM_BOMBER_CB_REG)
{
    float g_BomberFuseTime; // 点火 -> 爆発 の秒
    float g_BomberTriggerMargin; // （半径の和 + これ）以内で点火する
    float g_BomberBlastRadius; // 爆発時にこの中にいるプレイヤーのカプセルがダメージを受ける
    float g_BomberBlastDamage;

    uint g_BomberBlastArea; // 見た目のために生成する範囲の定義（威力 0）。0 = なし
    float g_BomberSwell; // VS：爆発直前に (1 + これ) まで膨らむ
    float g_BomberFlashGain; // VS：点滅の明るさ
    float _bomberPad;

    // ---- エリート（SWARM_KIND_ELITE）。この cbuffer を共有する：種類の規則が
    // 要るパスはどれも既にこれをバインドしている ----
    float g_EliteScale; // 体格（モデル、命中 / 接触の半径、HP バーの高さ）
    float g_EliteDamageMul; // 殴りのダメージ = g_ContactDamage * これ
    float g_EliteExpMul; // 経験玉の価値 = g_OrbAmount * これ
    float _elitePad;

    // ---- ステージのボス（SWARM_KIND_BOSS） ----
    float g_BossScale;
    float g_BossDamageMul;
    float g_BossExpMul;
    float _bossPad;

    // ---- 最終ウェーブの幽霊（SWARM_KIND_GHOST） ----
    float g_GhostHover; // VS：地面からこれだけ浮く
    float g_GhostAlpha; // VS：頂点アルファ（アルファブレンドで描く）
    float g_GhostGlow; // VS：シアンの色味に掛ける HDR の倍率
    float g_GhostDamageMul; // 殴りのダメージ = g_ContactDamage * これ

    // ---- スプリッター（SWARM_KIND_SPLITTER）とその分裂体（2026-10-03） ----
    float g_SplitterScale;
    float g_SplitterDamageMul;
    float g_SplitterExpMul;
    float _splitterPad;
    float g_SplitlingScale;
    float g_SplitlingDamageMul;
    float g_SplitlingExpMul;
    float _splitlingPad;

    // ---- ボスの突進（2026-10-07、BossAttacks が毎フレーム書く） ----
    // g_BossChargeOn > 0.5 の間、ボスはフローフィールド / 追跡を無視して
    // g_BossChargeDir * g_BossChargeSpeed でまっすぐ動く（速度 0 = その場で溜め）。
    // 壁には止められるが、プレイヤーの前では止まらない（突き抜ける）
    float2 g_BossChargeDir;
    float g_BossChargeSpeed;
    float g_BossChargeOn;

    // ---- 重装兵（SWARM_KIND_BRUTE、2026-10-07） ----
    float g_BruteScale;
    float g_BruteDamageMul;
    float g_BruteExpMul;
    float _brutePad;

    // ---- 突撃兵（SWARM_KIND_CHARGER、2026-10-08） ----
    float g_ChargerScale;
    float g_ChargerDamageMul;     // 普通の殴り
    float g_ChargerExpMul;
    float g_ChargerDashDamageMul; // 突進中にプレイヤーに当たった時（爆発級に吹き飛ばす）
    float g_ChargerWindup;        // 地面に帯を出して止まっている秒
    float g_ChargerDashSpeed;
    float g_ChargerDashDist;
    float g_ChargerRecover;       // 突進後に止まっている秒
    float g_ChargerCooldown;      // 次の溜めまで普通に追う秒
    float g_ChargerMinDist;       // プレイヤーがこの距離 .. g_ChargerMaxDist にいると溜めを始める
    float g_ChargerMaxDist;
    float g_ChargerGlow;          // VS：溜め中の赤い点滅

    // ---- 盾兵（SWARM_KIND_SHIELD、2026-10-08） ----
    float g_ShieldScale;
    float g_ShieldDamageMul;
    float g_ShieldExpMul;
    float g_ShieldArmor;          // 被弾ごとに引く量（弾 1 発、範囲の判定 1 回）

    float g_ShieldMinFrac;        // …ただし最低でもこの割合は通る
    // ---- 凍結（アイスランス、2026-10-08） ----
    float g_FreezeBigMul;         // エリート / ボスの凍結はこの倍率だけ短い
    float g_FreezeImmunity;       // 解けた後、新しく凍らない秒
    float g_FreezeTint;           // VS：凍った体の氷っぽさ（0 = 色を付けない）
    // ---- トゲ玉（SWARM_KIND_SPIKEBALL、2026-10-17） ----
    float g_SpikeBallScale;
    float g_SpikeBallDamageMul;
    float g_SpikeBallExpMul;
    float g_SpikeBallRollPerMeter; // MoveCS：体格 1 の玉が 1 m 転がるごとに回るラジアン（見た目の回転速度 / 見た目の半径。2026-10-25、以前はパディング）
    // ---- 砂潜り（SWARM_KIND_BURROWER、2026-10-17） ----
    float g_BurrowScale;
    float g_BurrowDamageMul;
    float g_BurrowExpMul;
    float g_BurrowSurfaceDist;     // プレイヤーがこの距離以内に来ると地上に出る（m）

    float g_BurrowSurfaceTime;     // 地上にいる秒
    float g_BurrowMinBuried;       // 再び地上に出るまで最低これだけ潜っている
    float g_BurrowBuriedSpeedMul;  // 砂の下での速度
    float g_BurrowSurfaceSpeedMul; // 地上にいる間の速度

    float g_BurrowBiteRadius;      // 地上に出た時の 1 回の噛みつきが届く距離（m、体の中心から）
    float g_BurrowBiteDelay;       // 地上に出てから噛みつきが当たるまでの秒（先に体が上がる）
    float2 _burrowPad;

    // ---- 砂嵐の術師（SWARM_KIND_SANDMAGE）とその竜巻（SWARM_KIND_TORNADO）、2026-10-17 ----
    float g_SandMageScale;
    float g_SandMageDamageMul;     // （殴らないが、種類の表のために残す）
    float g_SandMageExpMul;
    float g_SandMageKeepMin;       // これより近い：プレイヤーから下がる（m）

    float g_SandMageKeepMax;       // これより遠い：プレイヤーへ歩く。その間 = 立って詠唱する
    float g_SandMageCastRange;     // プレイヤーがこの距離以内の時だけ詠唱する（m）
    float g_SandMageWindup;        // 竜巻が出るまで止まっている秒（体が光る）
    float g_SandMageCooldown;      // 詠唱の間隔（秒）

    float g_TornadoScale;          // 竜巻の体格（当たり半径 = g_EnemyRadius x これ）
    float g_TornadoDamageMul;      // プレイヤーを捉えた時の接触ダメージ x これ
    float g_TornadoExpMul;         // 未使用（プレイヤーに倒されることはない）
    float g_TornadoLife;           // 吹き消えるまで進む秒

    // ---- 黒幽霊（SWARM_KIND_DARKGHOST、2026-10-18） ----
    float g_DarkGhostScale;        // 体格（白い幽霊は 1）
    float g_DarkGhostDamageMul;    // 殴りのダメージ = g_ContactDamage * これ（白い幽霊は g_GhostDamageMul を使う）
    float g_DarkGhostExpMul;       // 倒せた時の経験玉
    float g_DarkGhostAlpha;        // VS：頂点アルファ（白い幽霊より不透明）

    // ---- 突撃爆弾兵（SWARM_KIND_DASHBOMBER、2026-10-18） ----
    float g_DashBomberScale;       // 体格
    float g_DashBomberDamageMul;   // 爆発のダメージ = g_BomberBlastDamage * これ（殴らない）
    float g_DashBomberExpMul;      // 経験玉（点火前に倒された時だけ）
    float g_DashBomberTrigger;     // プレイヤーがこの距離以内にいると自分で点火する（m、同じ高さで）

    float g_DashBomberFuse;        // プレイヤーに触れなかった場合、溜めの終わりから爆発までの秒（= 突進時間）
    float g_DashBomberDashSpeed;   // 突進中の m/s（AICS、点火時に固定した yaw に沿ってまっすぐ）
    float g_DashBomberBlastRadius; // m（見た目は g_BomberBlastArea を radius / g_BomberBlastRadius で拡縮して使い回す）
    float g_DashBomberWindup;      // 点火してから突進するまで（点滅しながら）止まっている秒（2026-10-20）

    // ---- 面ごとのボス（2026-10-24、BossAttacks::Configure / Output::bossBurrow） ----
    float3 g_BossTint;             // 雑魚のメッシュの色に掛ける（草原は紫、砂漠は砂岩）
    float g_BossBurrow;            // 0 = 地上、1 = 砂の下（砂の守護者の潜砂）。AICS がボスの fuse に -burrow として写す

    // ---- 呪いの鎧（SWARM_KIND_ARMOR / ARMOR2、2026-10-25） ----
    float g_ArmorScale;
    float g_ArmorDamageMul;
    float g_ArmorExpMul;           // 1 回目の死（崩れるだけ）
    float g_ArmorRevivedExpMul;

    // ---- 石像ガーゴイル（SWARM_KIND_GARGOYLE、2026-10-25） ----
    float g_GargoyleScale;
    float g_GargoyleDamageMul;     // 着地中の殴り
    float g_GargoyleExpMul;
    float g_GargoyleWakeDist;      // プレイヤーがこの距離まで来ると目覚める（水平 m）
    float g_GargoyleAutoWake;      // 眠ってからこの秒数で勝手に目覚める
    float g_GargoyleWakeTime;      // 目覚めの秒（震えて、浮き上がる）
    float g_GargoyleHover;         // 地面からの飛行の高さ（位置そのものを上げる）
    float g_GargoyleDiveRange;     // プレイヤーがこの距離まで来ると急降下する（水平）
    float g_GargoyleDiveWindup;    // 急降下の前に空中で止まる秒（地面に帯）
    float g_GargoyleDiveSpeed;     // 急降下の水平 m/s
    float g_GargoyleDiveTime;      // 急降下の秒
    float g_GargoyleLandTime;      // 急降下の後に地面にいる秒
    float g_GargoyleFlyMin;        // 次の急降下まで最低これだけ飛ぶ
    float g_GargoyleDiveDamageMul; // 急降下中にプレイヤーに触れた時（爆発級に吹き飛ばす）
    float g_GargoyleFlap;          // VS：羽ばたきの振幅（rad）
    float g_GargoyleDiveMin;       // 飛行中はこれと g_GargoyleDiveRange * 0.9 の間を回る（真上からの急降下は頭の上を通り過ぎていた）

    // ---- 死霊術師（SWARM_KIND_NECRO）と骸骨（SWARM_KIND_SKELETON）、2026-10-25 ----
    float g_NecroScale;
    float g_NecroDamageMul;
    float g_NecroExpMul;
    float g_NecroKeepMin;
    float g_NecroKeepMax;
    float g_NecroCastRange;
    float g_NecroWindup;
    float g_NecroCooldown;
    float g_SkeletonScale;
    float g_SkeletonDamageMul;
    float g_SkeletonExpMul;
    float _skeletonPad;

    // ---- 鎖の番人（SWARM_KIND_WARDEN、2026-10-25）。鎖そのものは CPU（シーン） ----
    float g_WardenScale;
    float g_WardenDamageMul;
    float g_WardenExpMul;
    float g_WardenKeepMin;
    float g_WardenKeepMax;
    float g_WardenRange;           // 鎖の届く距離 = 帯の長さ = 詠唱の射程
    float g_WardenWindup;
    float g_WardenHold;            // 投げた後に立っている（鎖が出ている）
    float g_WardenCooldown;        // 投げてから次の溜めまで（立っている時間を含む）
    float3 _wardenPad;
    // ---- 面の固定エリート（SWARM_KIND_ELITE_A / B、2026-10-25）。体格は面ごと（EliteAttacks::Configure） ----
    float g_FixedEliteAScale;
    float g_FixedEliteBScale;
    float g_FixedEliteDamageMul;    // 殴りだけ。技は CPU から当たる
    float g_FixedEliteExpMul;
    float g_FixedEliteKeepMin;      // 遠距離型（B）はこの間に立つ
    float g_FixedEliteKeepMax;
    float g_EliteSwiftMul;          // アフィックス「疾風」：歩く速さ
    float g_EliteEnragedSpeedMul;   // アフィックス「狂暴」で HP 半分以下：歩く速さ
    float g_EliteShieldDamageMul;   // アフィックス「鉄壁」：盾がある間に受けるダメージ
    float g_EliteEnragedScale;      // 狂暴：これだけ大きく描く（足元を基点に）
    float2 _fixedElitePad;
    // CPU からの指示、エリート 1 体につき 1 行：x = スロット + 1（0 = 空）、y = SWARM_ELITE_* のフラグ、z = SWARM_ELITE_CMD_*、
    // w = 溜めの光 x 1000。g_EliteMove：xy = 方向（瞬間移動ではその目標）、z = 走る速さ、w = 隠れ具合 0..1
    uint4 g_EliteCmd[4];
    float4 g_EliteMove[4];
    // 杖の狙いの優先度（2026-10-26）：SwarmAimKeyDist 参照
    float g_AimCloseRadius;
    float g_AimPriorityRange;
    float g_AimPriorityOn;
    float _aimPad;
    // 雷鳴の雄鹿の突進の姿勢（2026-10-26、BossAttacks::Output::chargePose）：0 通常、0..1 溜め（頭を下げ、
    // 脚を開いて屈む）、1 突進中（頭を下げ、体を前に倒し、速い歩様）、1..1.3 当たった時に頭を振り上げる、
    // 突進後は 1 -> 0。敵の VS が読む。四足のボスの時だけ書く
    float g_BossChargePose;
    float g_BossLeanPivotY;        // 突進の前傾の支点（焼いたメッシュ空間の y / z）と角度（pose 1 での rad）、ボスごと（CPU）
    float g_BossLeanPivotZ;
    float g_BossLeanAngle;
    float g_BossJumpHeight;        // 狼人の飛びかかり（2026-10-26）：> 0 = pose 0..1 の間、体がこの高さ（ワールド m）の放物線で上がる
    float g_BossNoContact;         // > 0.5 = ボスは触れても殴らない（飛びかかり：体がプレイヤーを突き抜ける）
    float g_ClimbSpeed;            // 敵が壁 / 崖面を登る m/s（2026-10-29、MoveCS）。0 = 登らない
    // 砂嵐の魔神（2026-10-29、BossAttacks::Output から CPU が毎フレーム書く。他のボスでは 0）
    float g_BossHover;             // 体の本当の高さ（ワールド m、MoveCS、2026-11-01：以前は描画だけで、魔法は足元の地面に当たっていた。当たり判定のカプセルは地面のまま）
    float g_BossKeepMin;           // > 0 = 術師のように距離を保つ（AICS）：これより内側では下がり、g_BossKeepMax までは立っている
    float g_BossKeepMax;
    float g_BossFly;               // > 0.5 = 幽霊のように壁を抜け、台地を越えて飛ぶ（SwarmFliesThrough）
    // 石の守護獣（2026-10-29、BossAttacks::Output::beast から CPU が毎フレーム書く。他のボスでは 0）
    float g_BossWing;              // ボスの翼の角度（g_AnimMode 2）rad：0 = 横に広げる、+ = 先が上がる
    float g_BossSpin;              // ボスの体に足す yaw の rad（尾の薙ぎ払いの一回転）
    // 敵の足元の燃える草（2026-11-01、GrassFireField。AreaDamageCS）
    float g_FireOn;                // > 0.5 = g_FireMap がバインドされている
    float g_FireClock;             // 秒、固定ステップごとに進める
    float g_FireTick;              // 当たりの間隔（秒）
    float g_FireDamage;            // 1 回あたり
    float g_FireOriginX;           // 炎マップの最小の角のワールド xz
    float g_FireOriginZ;
    float g_FireSizeX;             // m
    float g_FireSizeZ;
    float g_IceOn;                 // > 0.5 = SwarmEnemyVS が凍った池の上に体を持ち上げる（マップの範囲は炎と同じ）
    float2 _firePad;
};

// 杖の「一番近い敵」の競争（MoveCS、SWARM_CNT_NEAREST_KEY）がキーにする距離（2026-10-26、ユーザー：「範囲にボスが
// いればボスを先に撃つ。優先度は 3 段階。ただしプレイヤーに近すぎる雑魚がいれば雑魚を先に」）。
// 帯（小さい方が勝つ）：g_AimCloseRadius 内の雑魚は本当の距離（0..R）のまま。g_AimPriorityRange 内のボスは
// R + dist/100（近くない雑魚には全部勝つ）。エリートは R + 0.5 + dist/100。それ以外
// （遠い雑魚、範囲外のボス / エリート）は R + 1 + dist なので、これらは距離順のまま。出口は 1 つ（X4000）
float SwarmAimKeyDist(uint kind, float dist)
{
    float r = g_AimCloseRadius;
    bool boss = (kind == SWARM_KIND_BOSS);
    bool elite = (kind == SWARM_KIND_ELITE) || SwarmIsFixedElite(kind);
    bool mobClose = !boss && !elite && dist <= r;
    bool inRange = dist <= g_AimPriorityRange;
    float keyed = r + 1.0 + dist;
    if (boss && inRange) keyed = r + dist * 0.01;
    else if (elite && inRange) keyed = r + 0.5 + dist * 0.01;
    else if (mobClose) keyed = dist;
    return (g_AimPriorityOn > 0.5) ? keyed : dist;
}

// 種類ごとの体格の倍率（半径とモデル）
float SwarmKindScale(uint kind)
{
    return (kind == SWARM_KIND_ELITE) ? g_EliteScale
         : (kind == SWARM_KIND_BOSS) ? g_BossScale
         : (kind == SWARM_KIND_SPLITTER) ? g_SplitterScale
         : (kind == SWARM_KIND_SPLITLING) ? g_SplitlingScale
         : (kind == SWARM_KIND_BRUTE) ? g_BruteScale
         : (kind == SWARM_KIND_CHARGER) ? g_ChargerScale
         : (kind == SWARM_KIND_SHIELD) ? g_ShieldScale
         : (kind == SWARM_KIND_SPIKEBALL) ? g_SpikeBallScale
         : (kind == SWARM_KIND_BURROWER) ? g_BurrowScale
         : (kind == SWARM_KIND_SANDMAGE) ? g_SandMageScale
         : (kind == SWARM_KIND_TORNADO) ? g_TornadoScale
         : (kind == SWARM_KIND_DARKGHOST) ? g_DarkGhostScale
         : (kind == SWARM_KIND_DASHBOMBER) ? g_DashBomberScale
         : (kind == SWARM_KIND_ARMOR || kind == SWARM_KIND_ARMOR2) ? g_ArmorScale
         : (kind == SWARM_KIND_GARGOYLE) ? g_GargoyleScale
         : (kind == SWARM_KIND_NECRO) ? g_NecroScale
         : (kind == SWARM_KIND_SKELETON) ? g_SkeletonScale
         : (kind == SWARM_KIND_WARDEN) ? g_WardenScale
         : (kind == SWARM_KIND_ELITE_A) ? g_FixedEliteAScale
         : (kind == SWARM_KIND_ELITE_B) ? g_FixedEliteBScale : 1.0;
}

// 種類ごとの殴りのダメージ倍率（g_ContactDamage に掛ける）
float SwarmKindDamageMul(uint kind)
{
    return (kind == SWARM_KIND_ELITE) ? g_EliteDamageMul
         : (kind == SWARM_KIND_BOSS) ? g_BossDamageMul
         : (kind == SWARM_KIND_GHOST) ? g_GhostDamageMul
         : (kind == SWARM_KIND_SPLITTER) ? g_SplitterDamageMul
         : (kind == SWARM_KIND_SPLITLING) ? g_SplitlingDamageMul
         : (kind == SWARM_KIND_BRUTE) ? g_BruteDamageMul
         : (kind == SWARM_KIND_CHARGER) ? g_ChargerDamageMul
         : (kind == SWARM_KIND_SHIELD) ? g_ShieldDamageMul
         : (kind == SWARM_KIND_SPIKEBALL) ? g_SpikeBallDamageMul
         : (kind == SWARM_KIND_BURROWER) ? g_BurrowDamageMul
         : (kind == SWARM_KIND_SANDMAGE) ? g_SandMageDamageMul
         : (kind == SWARM_KIND_TORNADO) ? g_TornadoDamageMul
         : (kind == SWARM_KIND_DARKGHOST) ? g_DarkGhostDamageMul
         : (kind == SWARM_KIND_DASHBOMBER) ? g_DashBomberDamageMul
         : (kind == SWARM_KIND_ARMOR || kind == SWARM_KIND_ARMOR2) ? g_ArmorDamageMul
         : (kind == SWARM_KIND_GARGOYLE) ? g_GargoyleDamageMul
         : (kind == SWARM_KIND_NECRO) ? g_NecroDamageMul
         : (kind == SWARM_KIND_SKELETON) ? g_SkeletonDamageMul
         : (kind == SWARM_KIND_WARDEN) ? g_WardenDamageMul
         : SwarmIsFixedElite(kind) ? g_FixedEliteDamageMul : 1.0;
}

// 種類ごとの経験玉の価値の倍率（g_OrbAmount に掛ける）
float SwarmKindExpMul(uint kind)
{
    return (kind == SWARM_KIND_ELITE) ? g_EliteExpMul
         : (kind == SWARM_KIND_BOSS) ? g_BossExpMul
         : (kind == SWARM_KIND_SPLITTER) ? g_SplitterExpMul
         : (kind == SWARM_KIND_SPLITLING) ? g_SplitlingExpMul
         : (kind == SWARM_KIND_BRUTE) ? g_BruteExpMul
         : (kind == SWARM_KIND_CHARGER) ? g_ChargerExpMul
         : (kind == SWARM_KIND_SHIELD) ? g_ShieldExpMul
         : (kind == SWARM_KIND_SPIKEBALL) ? g_SpikeBallExpMul
         : (kind == SWARM_KIND_BURROWER) ? g_BurrowExpMul
         : (kind == SWARM_KIND_SANDMAGE) ? g_SandMageExpMul
         : (kind == SWARM_KIND_TORNADO) ? g_TornadoExpMul
         : (kind == SWARM_KIND_DARKGHOST) ? g_DarkGhostExpMul
         : (kind == SWARM_KIND_DASHBOMBER) ? g_DashBomberExpMul
         : (kind == SWARM_KIND_ARMOR) ? g_ArmorExpMul
         : (kind == SWARM_KIND_ARMOR2) ? g_ArmorRevivedExpMul
         : (kind == SWARM_KIND_GARGOYLE) ? g_GargoyleExpMul
         : (kind == SWARM_KIND_NECRO) ? g_NecroExpMul
         : (kind == SWARM_KIND_SKELETON) ? g_SkeletonExpMul
         : (kind == SWARM_KIND_WARDEN) ? g_WardenExpMul
         : SwarmIsFixedElite(kind) ? g_FixedEliteExpMul : 1.0;
}

// ---- 石像ガーゴイルの補助関数（2026-10-25）。fuse：<= 0 眠り、(0, wake) 目覚め中、[wake, DIVE) 飛行、
//      DIVE + [0, windup) 急降下前に空中で止まる、+ [windup, windup + dive) 急降下、その後は着地 ----
float SwarmGargoyleDiveT(SwarmEnemyExtra ex) { return ex.fuse - SWARM_GARGOYLE_DIVE; }
bool SwarmGargoyleDiving(SwarmEnemyExtra ex)
{
    float d = SwarmGargoyleDiveT(ex);
    return ex.kind == SWARM_KIND_GARGOYLE && d >= g_GargoyleDiveWindup && d < g_GargoyleDiveWindup + g_GargoyleDiveTime;
}
bool SwarmGargoyleLanded(SwarmEnemyExtra ex)
{
    return ex.kind == SWARM_KIND_GARGOYLE && SwarmGargoyleDiveT(ex) >= g_GargoyleDiveWindup + g_GargoyleDiveTime;
}
// 地面からの高さ：目覚め中に上がり、g_GargoyleHover で飛び、急降下中はまっすぐ降りる
float SwarmGargoyleHover(SwarmEnemyExtra ex)
{
    // 出口は 1 つ（途中で return すると fxc が呼び出し元すべてで X4000 を出す）
    float k = 0.0;
    if (ex.kind == SWARM_KIND_GARGOYLE && ex.fuse > 0.0)
    {
        float d = SwarmGargoyleDiveT(ex);
        if (ex.fuse < g_GargoyleWakeTime)
            k = smoothstep(0.35 * g_GargoyleWakeTime, g_GargoyleWakeTime, ex.fuse);
        else if (ex.fuse < SWARM_GARGOYLE_DIVE || d < g_GargoyleDiveWindup)
            k = 1.0;
        else
            k = 1.0 - saturate((d - g_GargoyleDiveWindup) / max(0.65 * g_GargoyleDiveTime, 1e-3));   // 最初の 65% で降り、その後は地面すれすれを進む
    }
    return g_GargoyleHover * k;
}
// 敵が地面の位置から本当にどれだけ高い所にいるか（飛行中のガーゴイル、浮いているボス）
float SwarmBodyHover(SwarmEnemyExtra ex)
{
    return SwarmGargoyleHover(ex) + (ex.kind == SWARM_KIND_BOSS ? g_BossHover : 0.0);
}
// 空中にいる（目覚めの震えを過ぎた後、飛行、急降下）：幽霊のように壁を抜けて飛ぶ（MoveCS / PushCS / AICS）
bool SwarmGargoyleAirborne(SwarmEnemyExtra ex)
{
    return ex.kind == SWARM_KIND_GARGOYLE && ex.fuse > 0.35 * g_GargoyleWakeTime && !SwarmGargoyleLanded(ex);
}
// 壁を抜け、台地を越えてまっすぐ飛ぶ（最終ウェーブの幽霊、竜巻、空中のガーゴイル、浮いている間の砂嵐の魔神）
bool SwarmFliesThrough(SwarmEnemyExtra ex)
{
    return SwarmIsGhost(ex.kind) || ex.kind == SWARM_KIND_TORNADO || SwarmGargoyleAirborne(ex)
        || (ex.kind == SWARM_KIND_BOSS && g_BossFly > 0.5);   // 魔神の一段目（2026-10-29）
}

// 被弾硬直を受けない種類（HitCS / AreaDamageCS）：ボス（絶え間ない攻撃で釘付けに
// なってしまう）、トゲ玉、溜めを始めた突撃兵（当たるたびに MoveCS が突進を止めてしまう）、
// 最終ウェーブの幽霊 2 種（2026-10-18：攻撃で群れを押しとどめられてはいけない）
bool SwarmNoFlinch(SwarmEnemyExtra extra)
{
    uint kind = extra.kind;
    return (kind == SWARM_KIND_BOSS) || (kind == SWARM_KIND_SPIKEBALL) || SwarmIsGhost(kind)
        || (kind == SWARM_KIND_CHARGER && extra.fuse > 0.0) || SwarmInvulnerable(extra)
        || (kind == SWARM_KIND_GARGOYLE)    // 石（2026-10-25）
        || SwarmIsFixedElite(kind);         // 固定エリートは技の溜めがある（2026-10-25）
}

// 1 回の命中（弾 1 発、範囲の判定 1 回）がこの種類に本当に与えるダメージ。
// 盾兵は被弾ごとに g_ShieldArmor を引くので、小さな判定をたくさん受けても
// （毒沼、光線）ほとんど削れず、重い一撃が効く（2026-10-08）
float SwarmArmorDamage(uint kind, float dmg)
{
    return (kind == SWARM_KIND_SHIELD && dmg > 0.0)
        ? max(dmg - g_ShieldArmor, dmg * g_ShieldMinFrac) : dmg;
}

// ---- CPU から固定エリートへの指示（2026-10-25）。Swarm::kEliteCmd* / kEliteFlag* と一致させること ----
static const uint SWARM_ELITE_CMD_AI = 0u;        // いつもの AI（A は追い、B は距離を保つ）
static const uint SWARM_ELITE_CMD_STAND = 1u;     // プレイヤーの方を向いて立つ
static const uint SWARM_ELITE_CMD_MOVE = 2u;      // g_EliteMove.xy に沿って .z の速さで走る（0 = その向きで立つ）
static const uint SWARM_ELITE_CMD_TELEPORT = 3u;  // MoveCS が g_EliteMove.xy の地面に置き、プレイヤーの方を向かせる
static const uint SWARM_ELITE_SWIFT = 1u;
static const uint SWARM_ELITE_SHIELD = 2u;
static const uint SWARM_ELITE_ENRAGED = 4u;
static const uint SWARM_ELITE_NO_CONTACT = 8u;    // 殴らない（消えている、技の途中）
static const uint SWARM_ELITE_ATTACK = 16u;       // 攻撃のクリップをループする（技の最中）
static const uint SWARM_ELITE_SPIN = 32u;         // 体ごと回る（大斧の回転斬り）

// このスロットの g_EliteCmd の行、-1 = なし（出口は 1 つ：途中の return で fxc が X4000 を出す）
int SwarmEliteEntry(uint slot)
{
    int found = -1;
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        if (g_EliteCmd[k].x == slot + 1u)
            found = k;
    }
    return found;
}
uint SwarmEliteFlags(uint slot)
{
    int k = SwarmEliteEntry(slot);
    return (k >= 0) ? g_EliteCmd[k].y : 0u;
}

// 1 回の命中がこのスロットの敵に本当に与えるダメージ：盾兵の装甲、その後に鉄壁エリートの
// 盾（CPU が壊れたと判断するまで半分）
float SwarmTakenDamage(uint slot, uint kind, float dmg)
{
    float d = SwarmArmorDamage(kind, dmg);
    if (SwarmIsFixedElite(kind) && (SwarmEliteFlags(slot) & SWARM_ELITE_SHIELD) != 0u)
        d *= g_EliteShieldDamageMul;
    return d;
}

// ---- 突撃兵の段階（SwarmEnemyExtra.fuse から求める） ----
static const uint SWARM_CHARGE_CHASE = 0u;   // fuse <= 0（クールダウンは 0 まで数え上げる）
static const uint SWARM_CHARGE_WINDUP = 1u;
static const uint SWARM_CHARGE_DASH = 2u;
static const uint SWARM_CHARGE_RECOVER = 3u;

float SwarmChargerDashTime()
{
    return g_ChargerDashDist / max(g_ChargerDashSpeed, 0.1);
}

uint SwarmChargerPhase(float fuse)
{
    float w = g_ChargerWindup;
    float d = w + SwarmChargerDashTime();
    return (fuse <= 0.0) ? SWARM_CHARGE_CHASE
         : (fuse <= w) ? SWARM_CHARGE_WINDUP
         : (fuse <= d) ? SWARM_CHARGE_DASH : SWARM_CHARGE_RECOVER;
}

// この種類の敵の体のカプセル。モデルは足元を基点に拡縮するので、
// 体が大きいほど中心も高くなる。
// pos = SwarmEnemy.position（普通の雑魚のカプセルの中心）
void SwarmKindCapsule(uint kind, float3 pos, out float3 center, out float radius, out float halfLen)
{
    float k = SwarmKindScale(kind);
    radius = g_EnemyRadius * k;
    halfLen = g_EnemyCapsuleHalf * k;
    center = pos;
    center.y += (k - 1.0) * (g_EnemyRadius + g_EnemyCapsuleHalf);
}

// スロット 1 つの敵の体格：種類の値に、狂暴の固定エリートなら g_EliteEnragedScale を掛ける（2026-10-25：
// SwarmEnemyVS は足元からそれだけ大きく描くが、命中・接触・HP バーは以前は元の大きさのままだった）
float SwarmSlotScale(uint slot, uint kind)
{
    float k = SwarmKindScale(kind);
    if (SwarmIsFixedElite(kind) && (SwarmEliteFlags(slot) & SWARM_ELITE_ENRAGED) != 0u)
        k *= g_EliteEnragedScale;
    return k;
}
// SwarmSlotScale を使う SwarmKindCapsule
void SwarmSlotCapsule(uint slot, uint kind, float3 pos, out float3 center, out float radius, out float halfLen)
{
    float k = SwarmSlotScale(slot, kind);
    radius = g_EnemyRadius * k;
    halfLen = g_EnemyCapsuleHalf * k;
    center = pos;
    center.y += (k - 1.0) * (g_EnemyRadius + g_EnemyCapsuleHalf);
}
#endif

// ============================================================
// クォータニオン（xyz = axis * sin(a / 2)、w = cos(a / 2)）。トゲ
// 玉の転がり（2026-10-25）：MoveCS がこのステップの回転をスロットの
// 向き（enemyRoll）に重ね、SwarmSpikeBallVS がそれでメッシュを回す
// ============================================================
float4 SwarmQuatAxisAngle(float3 axis, float angle)
{
    float s, c;
    sincos(0.5 * angle, s, c);
    return float4(axis * s, c);
}
// a * b = 先に b、次に a
float4 SwarmQuatMul(float4 a, float4 b)
{
    return float4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}
float3 SwarmQuatRotate(float4 q, float3 v)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}
// トゲ玉の保存済みの向き。まだ一度も書かれていない（ゼロ）= 以前の静止時と同じ、スロットごとに固定の姿勢
float4 SwarmSpikeBallRoll(float4 stored, uint slot)
{
    return (dot(stored, stored) > 0.25) ? stored
         : SwarmQuatAxisAngle(normalize(float3(0.3, 1.0, 0.2)), (float) slot * 1.7);
}

// ============================================================
// 経験玉の調整値。デフォルトのレジスタは b2。HitCS（経験玉を生成）と
// OrbMoveCS（動かす / 拾う）の両方が読む。
// SwarmTypes.h の Swarm::OrbCB と一致させること
// ============================================================
#ifndef SWARM_ORB_CB_REG
#define SWARM_ORB_CB_REG b2
#endif

cbuffer SwarmOrbCB : register(SWARM_ORB_CB_REG)
{
    float g_OrbAttractRadius; // この内側でプレイヤーへ引き寄せ始める
    float g_OrbPickupRadius; // この内側で拾われる
    float g_OrbAccel; // 引き寄せの加速度
    float g_OrbMaxSpeed;

    float g_OrbAmount; // 経験玉 1 つの経験値（今は全体共通。いずれ敵ごとに）
    float g_OrbY; // 経験玉はこの高さに浮く
    float2 _orbPad;
};

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
// 台地から飛び降りる（2026-10-01）。プレイヤーより高い所にいる敵は
// 崖を下りてよい（落ちて、MoveCS が重力をかける）。
// 登るのは今までどおり拒否する。プレイヤーが少なくとも
// SWARM_DROP_MIN_BELOW 低い所にいる間だけ：プレイヤーが同じ台地の上にいれば
// 崖はどちら向きにも壁のまま。FlowField（CPU）も同じ規則を使うので、
// フィールドが、GPU が許さない縁から敵を下ろす経路を
// 作ることはない。群れの押し出し（PushCS）は誰も落とさない。
// feet = 敵の足の高さ（地形の空間、position.y - g_GroundY）。
// 敵の高さは、中心の下の地面と足のうち高い方（2026-10-20）：
// バイリニアの地面は縁の手前 0.25m で
// ぼやけて下がる。段々の台地（起伏の上に
// 2.5m の段）では台地がプレイヤーの地面より 1.5-2m しか高くないことが多く、
// 中心の下の地面だけでは縁のところで余裕を下回り、
// 飛び降りがまた不可に戻った：体が押し戻され、次のステップでは
// また可 -> 敵が縁でずっと上下していた（soak）。Settle は
// 体全体が縁を越えるまで足を台地の高さに保つので、
// 飛び降りの間は足の高さで判定が続く
// ============================================================
static const float SWARM_DROP_MIN_BELOW = 1.0;

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
// 体の円 vs 壁と崖（2026-10-01）。
// 以前 MoveCS / PushCS は中心だけを見ていたので、群れが敵を壁面まで押し、
// 体の半分が台地の側面にめり込んでいた
// （自動テスト「clip」：半径 0.4m のうち 0.37m、エリートは 0.88m のうち 0.73m）。
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
// 壁に対して止まる半径（当たり判定の半径を単位とする）：メッシュは
// カプセルより幅がある（約 0.9m 対 0.8m）。PushCS と同じ理由
static const float SWARM_BODY_WALL_MUL = 1.15;
// 大きな種類でも壁の間の 1 セル（2m）の隙間を通れるよう上限を設ける：
// フローフィールドはそこを通るので、ないとボスが詰まる
static const float SWARM_BODY_WALL_MAX = 0.9;

// countDrops = false：低い地面は数えない（縁から飛び降りる敵は
// 体をはみ出させてよい）。数えるのは壁と上り面だけ。
// footH：上りを測る基準の高さ（敵の足、地形の空間）。
// デフォルト = 中心の下の地面。MoveCS は本当の足の高さを渡す：
// 縁から降りる敵は、中心がもう低い地面の上にあっても
// 足はまだ後ろの台地と同じ高さで、
// それを壁と数えてはいけない（以前は縁でちょうど止まっていた）
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
// 離れる動きが SwarmSlopeOk には急な降下に見え、
// そこに生成された敵は二度と出られなかった
bool SwarmRawSlopeOk(StructuredBuffer<float> heights, float2 from, float2 to)
{
    float rise = abs(SwarmTerrainHeightRaw(heights, to) - SwarmTerrainHeightRaw(heights, from));
    return rise <= SWARM_MAX_WALK_SLOPE * length(to - from) + SWARM_BODY_CLIFF_RISE;
}

// 生の高さセルでの SwarmStepHeightOk（2026-10-20）：生の地面の上りが歩ける坂 + 坂の余裕を
// 超えなければ通す。下りも同じで、allowDrop なら何でも通す。
// MoveCS はこれかバイリニアの判定のどちらかが通ればステップを受け入れる：台地の縁から
// 0.25m 以内ではバイリニアの地面が面の途中まで下がるので、足がそれに付いて
// 行った敵は台地の方へ二度と戻れず（どのステップも「急すぎる上り」になる）、
// 縁にずっと引っかかっていた。生のセルにはそういう凹みがない。本当の面を登るのは
// ここでもやはり失敗し（0.5m のセル 1 つの中に段差全体がある）、どのみち体の判定が
// 中心を面の線から離しておく
bool SwarmRawStepOk(StructuredBuffer<float> heights, float2 from, float2 to, bool allowDrop)
{
    float rise = SwarmTerrainHeightRaw(heights, to) - SwarmTerrainHeightRaw(heights, from);
    float lim = SWARM_MAX_WALK_SLOPE * length(to - from) + SWARM_BODY_CLIFF_RISE;
    return rise <= lim && (allowDrop || -rise <= lim);
}

#ifdef SWARM_BOMBER_CB_REG
// 壁に対して使う体の半径（大きい種類ほど大きい。上限あり）
float SwarmBodyWallRadius(uint kind)
{
    return min(g_EnemyRadius * SwarmKindScale(kind) * SWARM_BODY_WALL_MUL, SWARM_BODY_WALL_MAX);
}
#endif

#endif
