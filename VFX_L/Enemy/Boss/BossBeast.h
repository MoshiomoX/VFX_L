// ============================================================
// BossBeast.h
// 遺跡 2 面目の Boss「石の守護獣」の数値と状態（2026-10-29、ユーザー「残りの Boss も作って」= 方案表の有翼の石竜）。
// BossAttacks.h が 500 行を越えているので、この Boss の分はここに纏めて BossAttacks のメンバーにする（中身は BossAttacksBeast.cpp）。
//   一段目：翼で飛ぶ（hover m。GPU は幽霊のように壁・台地を越え、keepMin〜keepMax の間合いを取る。体で殴らない）
//     ・急降下：空中で向きを固めて帯の予告 → 帯に沿って低く滑空して突っ込む（横へ避ける）
//     ・石化の息：扇の予告 → 灰色の息、扇がゆっくりプレイヤーを追う。浴びると石化が溜まり（足が遅い）、満ちると少しの間動けない
//     ・翼の突風：翼を振り上げ（正面の扇）→ 打ち下ろす。扇の中は高く吹き飛ばされる
//     ・天からの圧殺（10-29 追加）：高く舞い上がって消え、地面の影がプレイヤーを追う → 止まった影へ落ちて叩き潰し、衝撃波が広がる
//       （影から走って出て、衝撃波を跳ぶ）
//     ・石羽の散弾（10-29 追加、両方の段）：翼を振り上げ、扇に石の羽根を 2 回撃つ。2 回目は 1 回目の隙間を通る（隙間へ動き、次は戻る）
//   二段目（狂暴の咆哮の瞬間に入れ替える）：降りて（hover → 0）地面を歩く。尾の薙ぎ払い / 石化の息 / 石羽の散弾 / 突進 / 投石雨
// 翼の角度と体の回り（尾の薙ぎ払いで一回転）は毎フレーム CPU が決めて GPU の VS へ（BomberCB::bossWing / bossSpin）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>
#include <vector>

// 調整値（Enemies パネルの「Stone Beast」。BossAttacks::beast）
struct BeastTune
{
    // ---- 飛び・間合い（一段目）----
    float hover = 2.6f;            // 体を浮かせる高さ（世界 m）
    float landTime = 1.0f;         // 二段目へ入って降り切るまでの秒（咆哮の間に降りる）
    float keepMin = 9.0f;          // これより近いと下がる（AICS）
    float keepMax = 15.0f;         // ここまでは止まって撃つ
    float flapSpeed = 4.2f;        // 飛んでいる間の羽ばたき（rad/s）

    // ---- 急降下（BeastDive）----
    float diveWindup = 0.9f;       // 溜め（空中で止まって翼を振り上げる。帯が育つ。向きは始めた瞬間に固める）
    float diveSpeed = 24.0f;       // m/s
    float diveOvershoot = 4.0f;    // プレイヤーの向こうまで滑る距離 m（帯の長さ = 距離 + これ、diveMinLength〜diveMaxLength）
    float diveMinLength = 8.0f;
    float diveMaxLength = 26.0f;
    float diveWidth = 3.4f;        // 帯の幅 m
    float diveHitRadius = 3.2f;    // 滑空している Boss の中心からこの距離にプレイヤーが居れば当たり
    float diveLowHover = 0.5f;     // 滑空中の高さ（世界 m）
    float diveReach = 3.0f;        // 足が地面からこれより上なら当たらない
    float diveDamage = 30.0f;
    float diveRise = 0.6f;         // 突っ込んだ後に元の高さへ戻る秒

    // ---- 石化の息（PetrifyBreath）----
    float breathWindup = 1.0f;     // 溜め（頭を下げる。扇が育つ）
    float breathTime = 2.2f;       // 吐いている秒
    float breathRadius = 10.0f;    // 扇の半径 m
    float breathHalfDeg = 25.0f;   // 扇の半角（度）
    float breathTurnDeg = 25.0f;   // 吐いている間に扇がプレイヤーへ向く速さ（度 / 秒）
    float breathTick = 0.25f;      // 当たりを見る間隔
    float breathReach = 2.5f;      // 足が地面からこれより上なら当たらない
    float breathDamage = 6.0f;     // 1 回分（押さない）
    float breathRest = 0.4f;
    float breathMouthHeight = 4.2f;// 息の見た目の口の高さ（世界 m、浮きの上）。見た目だけ
    // 石化：息に 1 回当たる毎に petrifyPerHit 溜まる。溜まっている間は走りが (1 - petrifySlow × 溜まり) 倍、当たらなければ
    // petrifyDecay / 秒で抜ける。1 に届くと petrifyLockTime 秒は石（ほとんど動けない）→ 0 に戻る
    float petrifyPerHit = 0.22f;
    float petrifySlow = 0.55f;
    float petrifyDecay = 0.3f;
    float petrifyLockTime = 1.2f;
    float petrifyLockSlow = 0.06f;

    // ---- 翼の突風（WingGust）----
    float gustWindup = 1.0f;       // 翼を振り上げる（扇が育つ。向きは始めた瞬間に固める）
    float gustRadius = 13.0f;
    float gustHalfDeg = 30.0f;     // 8m 先で溜めの 1 秒に横へ 5.5m 走れば外れる角度
    float gustDamage = 10.0f;      // 高く吹き飛ばす（KnockKind::Wind）
    float gustBeatTime = 0.14f;    // 打ち下ろしの翼の振り
    float gustRest = 0.5f;

    // ---- 尾の薙ぎ払い（TailSweep、二段目）----
    float sweepWindup = 0.7f;      // 尾を持ち上げる（足元の輪が育つ）
    float sweepRadius = 7.5f;      // 輪の半径 m
    float sweepTime = 0.5f;        // 一回転の秒
    float sweepDodgeHeight = 0.9f; // 足が地面からこれより上なら当たらない（跳べば避けられる）
    float sweepDamage = 26.0f;
    float sweepRest = 0.4f;

    // ---- 天からの圧殺（SkyCrush、一段目。10-29 ユーザー「技をあと 2 つ」）----
    float crushRise = 0.7f;        // 舞い上がる秒
    float crushHigh = 16.0f;       // 舞い上がる高さ（世界 m。カメラの外）
    float crushTrack = 1.6f;       // 影がプレイヤーを追う秒
    float crushFollowSpeed = 7.0f; // 影の追う速さ m/s（走り 5.5 より速い = 追う間は逃げ切れない）
    float crushLock = 0.6f;        // 影が止まってから落ち始めるまで（ここで影から走って出る）
    float crushFall = 0.3f;        // 落ちる秒
    float crushRadius = 4.0f;      // 叩き潰しの半径（影の大きさ）
    float crushReach = 2.0f;       // 足が地面からこれより上なら当たらない
    float crushDamage = 34.0f;     // + 衝撃波（共通の衝撃波と同じ帯・速さ・威力。跳べば避けられる）
    float crushRest = 1.0f;        // 落ちた後、地面に居る秒（体で殴る）
    float crushRiseBack = 0.8f;    // 元の高さへ戻る秒

    // ---- 石羽の散弾（FeatherShards、両方の段）----
    float shardWindup = 0.7f;      // 翼を振り上げる（1 回目の筋の予告が育つ。向きは始めた瞬間に固める）
    int   shardCount = 7;          // 1 回の羽根の数
    int   shardCountEnraged = 9;
    float shardSpacingDeg = 15.0f; // 隣の羽根との角度（7 本 = ±45°）
    int   shardVolleys = 2;        // 撃つ回数（2 回目からは半分ずらす = 前の回の隙間を通る）
    float shardVolleyGap = 0.6f;   // 回の間の秒
    float shardSpeed = 15.0f;      // m/s
    float shardRange = 22.0f;      // 飛ぶ距離 m
    float shardHitRadius = 0.45f;  // + プレイヤーの太さ
    float shardReach = 1.6f;       // 足が地面からこれより上なら当たらない（羽根は胸の高さ）
    float shardDamage = 14.0f;
    float shardRest = 0.4f;
};

// 石羽の散弾の羽根 1 枚（技の後も飛ぶ。BossAttacks::BeastShards()）
struct BeastShard
{
    DirectX::SimpleMath::Vector3 pos;      // 今の所（地面の上 1m。撃った所は Boss の高さから下りて来る）
    DirectX::SimpleMath::Vector2 dir = { 0.0f, 1.0f };
    float travelled = 0.0f;
    float startLift = 0.0f;                // 撃った時の地面の上 1m からの高さ（飛ぶ間に 0 へ）
    bool  hitDone = false;
    uint32_t id = 0;
};

// シーンへの出力（BossAttacks::Output::beast）
struct BeastOutput
{
    float wing = 0.0f;             // 翼の角度（rad。0 = 横へ広げる、+ = 先を上へ）→ BomberCB::bossWing
    float spin = 0.0f;             // 体の追加の回り（rad、尾の薙ぎ払い）→ BomberCB::bossSpin
    bool  flying = false;          // 一段目（羽ばたいている）
    bool  landing = false;         // 二段目へ入った瞬間（咆哮と同じフレーム。降りる見た目・音）
    bool  diveGo = false;          // 急降下が走り出した瞬間（音）
    bool  diving = false;          // 滑空している間（土煙を Boss に付ける）
    bool  diveHit = false;         // 急降下が当たった瞬間
    // 石化の息：吐いている間 on、向き（rad、+Z が 0）。started = 吐き始めた瞬間
    bool  breathStarted = false;
    bool  breathOn = false;
    float breathYaw = 0.0f;
    // 翼の突風：打ち下ろした瞬間（向き）
    bool  gustBeat = false;
    DirectX::SimpleMath::Vector2 gustDir = { 0.0f, 1.0f };
    // 尾の薙ぎ払い：回り始めた瞬間
    bool  sweepStart = false;
    // 石化：今の溜まり 0〜1・石の残り秒（プレイヤーへの効きはシーン）、石になった瞬間 / 解けた瞬間
    float petrify = 0.0f;
    float stoneLeft = 0.0f;
    bool  petrified = false;
    bool  stoneBroke = false;
    // 天からの圧殺：舞い上がった / 落ち始めた / 叩き潰した瞬間（場所）
    bool  crushRose = false;
    bool  crushFalling = false;
    bool  crushImpact = false;
    DirectX::SimpleMath::Vector3 crushPos;
    // 石羽の散弾：撃った瞬間（回毎）、地面に落ちて消えた羽根の所
    bool  shardsFired = false;
    std::vector<DirectX::SimpleMath::Vector3> shardEnds;
};

// 技の途中の状態（BossAttacks の中だけ）
struct BeastState
{
    bool  phase2 = false;
    float hoverBase = 0.0f;        // 降りる途中の高さ（世界 m）
    float wing = 0.0f;             // 今の翼の角度（滑らかに追う）
    float wingWant = -9.0f;        // 技が決める翼の角度（-9 = 決めない = 飛んでいれば羽ばたき、降りていれば畳む）
    float wingRate = 4.0f;         // そこへ向かう速さ（rad/s）
    float flapClock = 0.0f;
    float spin = 0.0f;
    float pose = 0.0f;             // 頭を下げる（Output::chargePose）
    DirectX::SimpleMath::Vector3 foot;   // 技を始めた時の足元（扇・帯・輪の根元）
    DirectX::SimpleMath::Vector2 dir = { 0.0f, 1.0f };
    float moveHover = -1.0f;       // 技が決める高さ（急降下・天からの圧殺。< 0 = 浮きの既定）
    // 急降下
    float diveLength = 10.0f;
    bool  diveHitDone = false;
    // 天からの圧殺：0 舞い上がる / 1 影が追う / 2 影が止まる / 3 落ちる / 4 地面 / 5 戻る
    int   crushPhase = 0;
    float crushT = 0.0f;
    DirectX::SimpleMath::Vector3 shadow;   // 影の所（地面の高さ）
    // 石羽の散弾
    std::vector<BeastShard> shards;
    uint32_t nextShardId = 1;
    int   shardVolleyDone = 0;
    int   shardCountNow = 7;
    // 石化の息
    float breathTickLeft = 0.0f;
    bool  breathFired = false;
    // 翼の突風・尾の薙ぎ払い
    bool  struck = false;
    // 石化（プレイヤー側の状態だがここで数える。Boss が居なくても減る）
    float petrify = 0.0f;
    float stoneLeft = 0.0f;
};
