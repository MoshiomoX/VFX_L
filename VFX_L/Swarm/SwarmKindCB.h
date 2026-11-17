// ============================================================
// SwarmKindCB.h
// 敵の種類ごとの調整値と CPU からの毎フレームの指示、固定エリートへの指示の値。
// HLSL 側は Shader/Common/SwarmKindCB.hlsli（SwarmKindCB）。
// ============================================================
#pragma once
#include <cstdint>

namespace Swarm
{
    // ============================================================
    // 敵の種類ごとの調整値と、CPU から敵への毎フレームの指示（ContactCS などは b3、雑魚の VS は b5）
    // 爆発のダメージはプレイヤーだけ。雑魚には入らない（見た目の範囲は威力 0）
    // ============================================================
    struct KindCB
    {
        float    fuseTime = 1.0f;       // 点火から爆発までの秒数
        float    triggerMargin = 0.15f; // 接触（半径の和）+ これ以内で点火
        float    blastRadius = 2.5f;    // 爆発の瞬間にプレイヤー（カプセル）がこの中なら被弾
        float    blastDamage = 30.0f;

        uint32_t blastArea = 0;         // 爆発の見た目に出す範囲（AreaDef の番号）。0 = 出さない
        float    swell = 0.35f;         // VS: 爆発直前の膨らみ（1 + これ 倍まで）
        float    flashGain = 4.0f;      // VS: 点滅の明るさ（Bloom で光る）
        float    _pad = 0.0f;

        // ---- エリート（kEnemyKindElite）。種類の規則を読む pass はこの CB を持っているので相乗りする ----
        float    eliteScale = 2.2f;       // 体格（モデル・当たり / 接触の半径・HP 条の高さ）
        float    eliteDamageMul = 40.0f / 15.0f;   // 接触ダメージの倍率
        float    eliteExpMul = 20.0f;     // 落とす経験値オーブの倍率（1 個で雑魚 20 体分）
        float    _elitePad = 0.0f;

        // ---- 面の Boss（kEnemyKindBoss）----
        float    bossScale = 3.0f;           // 4 だと目の前に来た時に画面の大半を塞ぐ
        float    bossDamageMul = 40.0f / 15.0f;   // 接触ダメージの倍率
        float    bossExpMul = 100.0f;
        float    _bossPad = 0.0f;

        // ---- 最終ウェーブの幽霊（kEnemyKindGhost）----
        float    ghostHover = 0.7f;          // VS: 地面からこれだけ浮く（判定の位置は地面のまま）
        float    ghostAlpha = 0.55f;         // VS: 頂点 alpha（alpha blend で描く）
        float    ghostGlow = 1.6f;           // VS: 青白の色に掛ける HDR の倍率
        float    ghostDamageMul = 1.5f;      // 接触ダメージの倍率

        // ---- スプリッター（kEnemyKindSplitter）と分裂体（kEnemyKindSplitling）----
        float    splitterScale = 1.15f;      // 少し大きい
        float    splitterDamageMul = 1.0f;
        float    splitterExpMul = 1.5f;      // HP は雑魚の 2 倍、分裂体の分も足すと雑魚 2.4 体分
        float    _splitterPad = 0.0f;
        float    splitlingScale = 0.6f;
        float    splitlingDamageMul = 0.5f;
        float    splitlingExpMul = 0.3f;
        float    _splitlingPad = 0.0f;

        // ---- Boss の突進（BossAttacks が毎フレーム書く）----
        // bossChargeOn = 1 の間、Boss は流れ場・追跡を無視して (dirX, dirZ) × bossChargeSpeed で真っ直ぐ動く
        // （速さ 0 = その場で溜め）。壁では止まる。プレイヤーにぶつかっても止まらない（突き抜ける）
        float    bossChargeDirX = 0.0f;
        float    bossChargeDirZ = 1.0f;
        float    bossChargeSpeed = 0.0f;
        float    bossChargeOn = 0.0f;

        // ---- 重装兵（kEnemyKindBrute）----
        float    bruteScale = 1.3f;          // 体格（モデル・当たり / 接触の半径）
        float    bruteDamageMul = 1.6f;      // 接触ダメージ
        float    bruteExpMul = 3.0f;         // HP が雑魚の 3 倍なので経験も 3 倍
        float    _brutePad = 0.0f;

        // ---- 突撃兵（kEnemyKindCharger）----
        // 追いかけ中、同じ高さでプレイヤーが chargerMinDist〜chargerMaxDist m に居て、突進の線が歩けるマスなら
        // 止まって chargerWindup 秒溜める（向きはその瞬間に固定、地面に予告の帯）→ chargerDashSpeed m/s で
        // chargerDashDist m 突進（壁で止まる）→ chargerRecover 秒立ち止まる → chargerCooldown 秒は普通に追いかける
        float    chargerScale = 1.0f;
        float    chargerDamageMul = 1.0f;     // 普段の接触
        float    chargerExpMul = 1.5f;
        float    chargerDashDamageMul = 1.8f; // 突進中にぶつかった時（ノックバックは爆発と同じ強さ）
        float    chargerWindup = 0.8f;
        float    chargerDashSpeed = 14.0f;
        float    chargerDashDist = 14.0f;
        float    chargerRecover = 1.2f;
        float    chargerCooldown = 4.0f;
        float    chargerMinDist = 6.0f;
        float    chargerMaxDist = 12.0f;
        float    chargerGlow = 3.0f;          // VS: 溜めの間の赤い点滅の明るさ

        // ---- 盾兵（kEnemyKindShield）----
        float    shieldScale = 1.1f;
        float    shieldDamageMul = 1.2f;
        float    shieldExpMul = 2.0f;
        float    shieldArmor = 5.0f;          // 1 発（弾 1 発・範囲の 1 tick）毎にこれだけ引く
        float    shieldMinFrac = 0.2f;        // ただし元のダメージのこの割合は必ず通る

        // ---- 凍結（アイスランス）----
        float    freezeBigMul = 0.5f;         // エリート・Boss の凍結の長さに掛ける
        float    freezeImmunity = 3.0f;       // 解けてからこの秒数は凍らない（凍らせ続けられないように）
        float    freezeTint = 1.0f;           // VS: 凍った体の氷色の強さ（0 = 色を変えない）

        // ---- トゲ玉（kEnemyKindSpikeBall）----
        float    spikeBallScale = 1.4f;       // 体格（当たり / 接触の半径。見た目の半径は SwarmSystem::spikeBallLook.radius × これ）
        float    spikeBallDamageMul = 1.0f;   // 接触ダメージ（弾き飛ばしが本体）
        float    spikeBallExpMul = 4.0f;      // HP 60 = 雑魚 4 倍
        // MoveCS：体格 1 の玉が 1m 転がる毎に回る角度（rad）= spikeBallLook.rollRate / radius。SwarmSystem が毎ステップ入れる
        float    spikeBallRollPerMeter = 1.0f / 0.6f;

        // ---- 砂潜り（kEnemyKindBurrower）----
        float    burrowScale = 1.0f;
        float    burrowDamageMul = 2.0f;      // 顔を出した時の一噛み（1 回だけ）
        float    burrowExpMul = 2.0f;
        float    burrowSurfaceDist = 2.0f;    // プレイヤーがこの距離に入ったら顔を出す（m）
        float    burrowSurfaceTime = 3.0f;    // 出ている秒 = 潜りのクールダウン（噛んだ後もここまで外で動かず、撃てる）
        float    burrowMinBuried = 1.5f;      // 潜ってからこの秒数は出ない
        float    burrowBuriedSpeedMul = 1.3f; // 砂の下の速さ
        float    burrowSurfaceSpeedMul = 0.8f;// 出ている時の速さ
        float    burrowBiteRadius = 2.4f;     // 出た時の一噛みの届く範囲（体の中心から m。surfaceDist より広く = 止まっていれば必ず当たる）
        float    burrowBiteDelay = 0.3f;      // 出てから噛むまでの秒（体が出て来る間。ここで離れれば避けられる）
        float    _burrowPad[2] = {};

        // ---- 砂嵐の術師（kEnemyKindSandMage）と砂旋（kEnemyKindTornado）----
        float    sandMageScale = 1.0f;
        float    sandMageDamageMul = 1.0f;    // 殴らないので使わない（種類の表の為）
        float    sandMageExpMul = 2.5f;       // HP 20 だが遠くに立つので倒しにくい
        float    sandMageKeepMin = 7.0f;      // これより近いとプレイヤーから下がる（m）
        float    sandMageKeepMax = 11.0f;     // これより遠いと寄って来る。間では立ち止まって詠唱
        float    sandMageCastRange = 14.0f;   // プレイヤーがこの距離の中にいる時だけ放つ（m）
        float    sandMageWindup = 0.9f;       // 溜めの秒（立ち止まって体が黄色く光る。ここで横へ逃げる）
        float    sandMageCooldown = 5.0f;     // 次の砂旋までの秒
        float    tornadoScale = 1.2f;         // 砂旋の体格（当たりの半径 = 雑魚の半径 × これ）
        float    tornadoDamageMul = 0.6f;     // 触れた時のダメージ（接触 × これ。本体は吹き飛ばし）
        float    tornadoExpMul = 0.0f;        // 使わない（プレイヤーには倒せない）
        float    tornadoLife = 4.0f;          // 飛ぶ秒（速さ MobSpawner::m_TornadoSpeed 3.5 m/s × 4 秒 = 14m）

        // ---- 黒幽霊（kEnemyKindDarkGhost）。HP・速さは MobSpawner の darkGhost* ----
        float    darkGhostScale = 1.3f;       // 体格（白幽霊は 1）
        float    darkGhostDamageMul = 3.0f;   // 接触ダメージの倍率（白幽霊は ghostDamageMul）
        float    darkGhostExpMul = 10.0f;     // 倒せた時の経験値（HP 40 倍なので滅多に無い）
        float    darkGhostAlpha = 0.85f;      // VS: 頂点 alpha（白幽霊より濃い）

        // ---- 突撃爆弾兵（kEnemyKindDashBomber）。HP・平時の速さは MobSpawner の m_DashBomber* ----
        float    dashBomberScale = 1.0f;
        float    dashBomberDamageMul = 1.2f;  // 爆発のダメージ = 自爆兵の爆発 × これ
        float    dashBomberExpMul = 1.5f;     // 点火前に倒せた時の経験値
        float    dashBomberTrigger = 9.0f;    // プレイヤーがこの距離（m）に入ると点火して突っ込む
        float    dashBomberFuse = 1.0f;       // 溜めが終わってから爆発までの秒 = 突っ込む時間（12 m/s × 1.0 = 12m、点火距離 9m を走り抜ける）。触れればその場で爆発
        float    dashBomberDashSpeed = 12.0f; // 突っ込む速さ m/s（突撃兵 14、プレイヤーのスライディング 13 なら振り切れる）
        float    dashBomberBlastRadius = 2.5f;
        float    dashBomberWindup = 0.45f;    // 点火してから走り出すまでの溜め秒（その場で赤く点滅）

        // ---- 面毎の Boss（BossAttacks::Configure / Output::bossBurrow をシーンが毎フレーム書く）----
        float    bossTint[3] = { 1.1f, 0.5f, 1.5f };   // 雑魚の網の色に掛ける（草原 = 紫、砂漠 = 砂岩）
        float    bossBurrow = 0.0f;           // 0 = 地上、1 = 砂の下（砂の守護者の潜砂。AICS が Boss の fuse へ -burrow で写す）

        // ---- 呪いの鎧（kEnemyKindArmor / ArmorRevived）。HP・速さ・蘇るまでの秒は MobSpawner ----
        float    armorScale = 1.25f;
        float    armorDamageMul = 1.5f;
        float    armorExpMul = 1.0f;          // 1 回目の死（崩れるだけ）
        float    armorRevivedExpMul = 2.5f;   // 蘇った鎧を倒した時

        // ---- 石像ガーゴイル（kEnemyKindGargoyle）----
        float    gargoyleScale = 1.1f;
        float    gargoyleDamageMul = 1.0f;    // 着地している時の殴り
        float    gargoyleExpMul = 3.0f;
        float    gargoyleWakeDist = 7.0f;     // プレイヤーがこの距離（水平 m）に来ると目覚める
        float    gargoyleAutoWake = 10.0f;    // 眠ってからこの秒で勝手に目覚める（湧いた石像が置き去りにならないように）
        float    gargoyleWakeTime = 0.8f;     // 目覚め（震えて石が剥がれ、浮き上がる）の秒
        float    gargoyleHover = 2.2f;        // 飛ぶ高さ（地面から m。位置そのものを上げる = 弾も上を狙う）
        float    gargoyleDiveRange = 7.5f;    // プレイヤーがこの距離（水平）に入ったら急降下
        float    gargoyleDiveWindup = 0.45f;  // 急降下の溜め（空中で止まり、地面に予告の帯）
        float    gargoyleDiveSpeed = 15.0f;   // 急降下の速さ（水平 m/s）
        float    gargoyleDiveTime = 0.55f;    // 急降下の秒（15 × 0.55 = 8.3m、狙った所を少し過ぎて着地）
        float    gargoyleLandTime = 1.4f;     // 着地してから地上にいる秒（ここが倒し時）
        float    gargoyleFlyMin = 1.5f;       // 飛び始めてからこの秒は急降下しない
        float    gargoyleDiveDamageMul = 1.8f;// 急降下でぶつかった時（ノックバックは爆発と同じ強さ）
        float    gargoyleFlap = 0.7f;         // VS: 翼の羽ばたきの振れ幅（rad）
        float    gargoyleDiveMin = 4.5f;      // 飛んでいる間はプレイヤーからこれ〜diveRange × 0.9 の間を回る（真上から急降下すると頭の上を素通りする）

        // ---- 死霊術師（kEnemyKindNecro）と骸骨（kEnemyKindSkeleton）----
        float    necroScale = 1.0f;
        float    necroDamageMul = 1.0f;       // 殴らないので使わない（種類の表の為）
        float    necroExpMul = 3.0f;
        float    necroKeepMin = 8.0f;         // これより近いとプレイヤーから下がる（m）
        float    necroKeepMax = 13.0f;        // これより遠いと寄って来る
        float    necroCastRange = 16.0f;
        float    necroWindup = 1.2f;          // 溜めの秒（立ち止まって杖を掲げ、体が緑に光る）
        float    necroCooldown = 7.0f;
        float    skeletonScale = 0.92f;
        float    skeletonDamageMul = 0.8f;
        float    skeletonExpMul = 0.3f;
        float    _skeletonPad = 0.0f;

        // ---- 鎖の番人（kEnemyKindWarden）。鎖そのもの（飛ぶ速さ・当たり・引き寄せ）は CPU（シーン）----
        float    wardenScale = 1.15f;
        float    wardenDamageMul = 1.2f;      // 殴り・鎖が当たった時（接触 × これ）
        float    wardenExpMul = 2.5f;
        float    wardenKeepMin = 5.0f;
        float    wardenKeepMax = 10.0f;
        float    wardenRange = 12.0f;         // 鎖の届く長さ = 予告の帯の長さ・投げ始める距離（m）
        float    wardenWindup = 0.9f;         // 溜め（向きを固定、地面に帯）。ここで横へ避ける
        float    wardenHold = 1.4f;           // 投げてから立ったままの秒（鎖が戻るまで）
        float    wardenCooldown = 6.0f;       // 投げてから次の溜めまで（hold を含む）
        float    _wardenPad[3] = {};
        // ---- 面の固定エリート（kEnemyKindEliteA / EliteB）。体格は面毎（EliteAttacks::Configure が入れる）----
        float    fixedEliteAScale = 2.0f;
        float    fixedEliteBScale = 1.7f;
        float    fixedEliteDamageMul = 40.0f / 15.0f;   // 殴り（接触 × これ）。技の当たりは CPU
        float    fixedEliteExpMul = 20.0f;
        float    fixedEliteKeepMin = 8.0f;    // B（遠距離型）が立つ距離
        float    fixedEliteKeepMax = 13.0f;
        float    eliteSwiftMul = 1.6f;        // 疾風：歩く速さ
        float    eliteEnragedSpeedMul = 1.3f; // 狂暴（HP 半分以下）：歩く速さ
        float    eliteShieldDamageMul = 0.5f; // 鉄壁：盾が残っている間に受けるダメージ
        float    eliteEnragedScale = 1.2f;    // 狂暴：見た目の大きさ（足元が基点）
        float    _fixedElitePad[2] = {};
        // CPU の指示（kMaxEliteCmd 体分）。[k][0] = 槽 + 1（0 = 空き）、[1] = kEliteFlag*、[2] = kEliteCmd*、[3] = 溜めの光 × 1000
        uint32_t eliteCmd[4][4] = {};
        // [k] = xz（向き、瞬間移動の時は行き先）、走る速さ m/s、隠れ具合 0..1（影の剣士の消え）
        float    eliteMove[4][4] = {};
        // ---- 杖の狙いの優先度 ----
        // MoveCS の杖の狙い（SWARM_CNT_AIM_KEY）の鍵に使う距離を種類で付け替える（SwarmAimKeyDist）：雑魚が aimCloseRadius 以内 → その雑魚、
        // 無ければ aimPriorityRange 以内の Boss → エリート → 残りは距離順。aimPriorityOn 0 で純粋な最寄り
        float    aimCloseRadius = 3.5f;
        float    aimPriorityRange = 30.0f;
        float    aimPriorityOn = 1.0f;
        float    _aimPad = 0.0f;
        // ---- 雷鳴の雄鹿の突進の姿勢（BossAttacks::Output::chargePose をシーンが毎フレーム写す）----
        // 0 = 普段、0→1 = 溜め（頭を下げ、脚を開いて腰を落とす）、1 = 突進中（頭は下げたまま、胴を前へ傾け脚を速く振る）、
        // 1〜1.3 = 当てた瞬間の頭の振り上げ、突進が終わると 1→0 で頭が上がる。VS が直接読む（四足の Boss の時だけ CPU が書く）
        float    bossChargePose = 0.0f;
        // 走りの前傾の支点（焼いた網の空間の y / z）と角度（rad、pose 1 の時）。Boss 毎に CPU が入れる：雄鹿 = 臀 (0.5, -0.6) 0.17、
        // 大サソリ = 低い胴 (-0.6, 0) 0.08、他は 0 = 傾けない
        float    bossLeanPivotY = 0.0f;
        float    bossLeanPivotZ = 0.0f;
        float    bossLeanAngle = 0.0f;
        // 狼人の飛びかかり：> 0 の間、VS が bossChargePose（0 → 1）の放物線で体をこの高さ（世界 m）まで持ち上げる
        float    bossJumpHeight = 0.0f;
        float    bossNoContact = 0.0f;        // 1 = Boss が体で殴らない（飛びかかりの間。ContactCS）
        float    climbSpeed = 3.0f;           // 雑魚が壁を登る速さ m/s（MoveCS。0 = 登らない。SwarmSystem::climbSpeed を毎フレーム入れる）
        // ---- 砂嵐の魔神（BossAttacks::Output をシーンが毎フレーム写す。他の Boss は全部 0）----
        float    bossHover = 0.0f;            // 体の高さ（世界 m。位置そのものを上げる = 魔法の当たる高さも同じ）
        float    bossKeepMin = 0.0f;          // > 0 = 術師のように間合いを取る（AICS）：これより近いと下がる、bossKeepMax までは止まる
        float    bossKeepMax = 0.0f;
        float    bossFly = 0.0f;              // 1 = 幽霊のように壁・台地を越えて飛ぶ（SwarmFliesThrough。一段目の魔神）
        // ---- 石の守護獣（BossAttacks::Output::beast をシーンが毎フレーム写す。他の Boss は 0）----
        float    bossWing = 0.0f;             // 翼（飾りの g_AnimMode 2）の角度 rad：0 = 横へ広げる、+ = 先を上へ
        float    bossSpin = 0.0f;             // 体の追加の回り rad（尾の薙ぎ払いで一回転。VS が向きに足す）
        // ---- 燃えている草の上の敵（GrassFireField。AreaDamageCS）----
        float    fireOn = 0.0f;
        float    stepClock = 0.0f;            // AreaDamageCS の時計（秒）。固定ステップ毎に SwarmSystem が必ず進める。草の火の拍、剣 / 氷が当たった時刻に使う
        float    fireTick = 0.5f;
        float    fireDamage = 0.0f;           // 1 回あたり
        float    fireOriginX = 0.0f, fireOriginZ = 0.0f;
        float    fireSizeX = 1.0f, fireSizeZ = 1.0f;
        float    iceOn = 0.0f;                // 1 = SwarmEnemyVS が氷の上の体を持ち上げる
        float    _firePad[2] = {};
    };
    static_assert(sizeof(KindCB) == 864, "SwarmKindCB layout mismatch");

    // 固定エリートへの指示（KindCB::eliteCmd。HLSL の SWARM_ELITE_* と同じ値）
    constexpr uint32_t kMaxEliteCmd = 4;
    constexpr uint32_t kEliteCmdAI = 0;        // いつもの AI（A は追い、B は距離を取る）
    constexpr uint32_t kEliteCmdStand = 1;     // その場に立ってプレイヤーを向く
    constexpr uint32_t kEliteCmdMove = 2;      // eliteMove の向きへ速さで走る（速さ 0 = その向きで立つ。壁は普段通り止まる）
    constexpr uint32_t kEliteCmdTeleport = 3;  // MoveCS が eliteMove の xz（地面の上）へ置き、プレイヤーを向かせる
    constexpr uint32_t kEliteFlagSwift = 1u;
    constexpr uint32_t kEliteFlagShield = 2u;
    constexpr uint32_t kEliteFlagEnraged = 4u;
    constexpr uint32_t kEliteFlagNoContact = 8u;  // 殴らない（消えている間・技の最中）
    constexpr uint32_t kEliteFlagAttack = 16u;    // 攻撃の動きを繰り返す（技の最中）
    constexpr uint32_t kEliteFlagSpin = 32u;      // 体ごと回る（大斧の回転斬り）
}
