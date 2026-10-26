// ============================================================
// BossAttacks.h
// 面の Boss の技。Boss の体は GPU の雑魚と同じなので、技の拍子と当たりは CPU が決める
// （GPU のリードバックの Boss の位置・HP を使う。2〜3 フレーム遅れる）。
//   2026-10-03：重撃（プレイヤーの足元に赤い輪）だけ
//   2026-10-07：投石雨・衝撃波・突進・召喚を足し、どれを出すかは BossBrain（距離 + 技ごとの冷却）が決める。
//     技が終わってから gap 秒（狂暴 gapEnraged）空けて次。狂暴の間は comboChance で間を空けずに次を出す
//   ・重撃 / 投石雨：輪が warnTime で縁まで育って爆発（Hit、ranged = true：中に居るかはシーンが高さ込みで見る）
//   ・衝撃波：溜め（Boss が止まり、足元に輪が育つ）→ 地面を這う帯が waveSpeed で外へ。帯が通る瞬間に
//     足が地面近くなら当たり（跳べば避けられる）
//   ・突進：Boss → プレイヤーの向きを決めて道筋に輪を並べ、溜め → chargeSpeed で一直線。Boss の近くに
//     プレイヤーが居れば当たり。走らせるのは GPU（Output::chargeOn → BomberCB の bossCharge*）
//   ・召喚：Boss が止まって吠え、周りに小さな輪 → summonWindup 秒後に輪から雑魚・自爆兵が湧く
//     （Output::summons。湧かせるのはシーン。同時上限は見ない＝分裂体と同じ）
//   2026-10-24：面毎の Boss（BossType、Configure）。砂漠の「砂の守護者」は上の 5 つ + 砂の 3 つ（BossAttacksSand.cpp）:
//   ・砂の槍：向きを決めて線上に輪 → 溜め → 足元から先へトゲが spearSpeed で突き出る。前線が通る瞬間に線の近くなら当たり
//   ・潜砂奇襲：沈む（Output::bossBurrow → GPU が体を地面の下へ、撃てなくする）→ 砂丘がプレイヤーを追う
//     → 止まって輪 → 飛び出して叩く（輪の爆発。中に居るかはシーン）
//   ・流沙：吠える間に渦が広がり、技の後も sandDuration 秒残る。中で地面に居るプレイヤーは遅く・中心へ引かれる
//     （QuicksandPull。跳んでいる間は効かない = 抜け出し方）
// 見た目・音・被弾（無敵時間・ノックバック）はシーン（CollisionTestSceneBoss.cpp）。調整パネルは BossAttacksPanel.cpp
// ============================================================
#pragma once
#include "Enemy/Boss/BossBrain.h"
#include <SimpleMath.h>
#include <cstdint>
#include <random>
#include <vector>

class GridWorld;

class BossAttacks
{
public:
    // 爆発・当たり（シーンが被弾・見た目・音を出す）
    struct Hit
    {
        BossMove kind = BossMove::Slam;
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ（輪の中心・衝撃波の中心・突進中の Boss）
        float radius = 3.0f;
        float damage = 0.0f;
        bool  ranged = true;                   // true = 輪の爆発（中に居るかはシーンが見る）、false = もう当たった（衝撃波・突進）
        DirectX::SimpleMath::Vector2 dir = { 0.0f, 1.0f };   // ranged = false の時の吹き飛ばす向き
        bool  thunder = false;                 // 雷鳴の雄鹿：突進の道筋に落ちる雷（ranged。見た目は雷、跳んでも避けられない。2026-10-26）
    };
    // 地面の警告の見た目（シーンが SwarmSystem::WarnCircle へ写す）
    struct Visual
    {
        DirectX::SimpleMath::Vector3 center;
        float radius = 1.0f;
        float progress = 0.0f;   // 負 = 縁だけ
        float band = 0.0f;       // > 0 = 幅 band の帯（衝撃波）
        // 円以外の形（SwarmSystem::WarnCircle と同じ。2026-10-26 大サソリ）：yaw = 向き（rad、+Z が 0）、
        // shape > 0 = 扇の半角 rad（鋏の薙ぎ払い）、shape < 0 = 幅 -shape m の帯を yaw の向きへ radius m（毒針の突っ込み）
        float yaw = 0.0f;
        float shape = 0.0f;
    };
    struct Input
    {
        float dt = 0.0f;
        bool  bossAlive = false;
        float bossHpRatio = 1.0f;
        DirectX::SimpleMath::Vector3 bossPos;
        DirectX::SimpleMath::Vector3 player;   // カプセルの中心
        float playerFeetY = 0.0f;
        float damageMul = 1.0f;                // MobSpawner の今のダメージ倍率
    };
    struct Output
    {
        std::vector<Hit> hits;
        std::vector<DirectX::SimpleMath::Vector3> rockFalls;   // 投石雨：岩が落ち始める所（落ちる見た目。rockFallTime 秒後に爆発）
        int  ringsPlaced = 0;                       // 置いた輪の数（警告の音）
        BossMove started = BossMove::None;          // この瞬間に始まった技（咆哮・溜めの見た目と音）
        bool waveLaunched = false;                  // 衝撃波が出た瞬間（地面を叩く見た目と音）
        DirectX::SimpleMath::Vector3 wavePos;
        bool chargeGo = false;                      // 突進が走り出した瞬間
        // GPU の Boss への指示（BomberCB の bossCharge*）。chargeOn の間、Boss は (dir × speed) で真っ直ぐ動く
        bool  chargeOn = false;
        DirectX::SimpleMath::Vector2 chargeDir = { 0.0f, 1.0f };
        float chargeSpeed = 0.0f;
        // 四足の Boss（雷鳴の雄鹿）の突進の姿勢 → BomberCB::bossChargePose（0 普段 / 0→1 溜め / 1 突進中 / 1〜1.3 当てた瞬間の
        // 頭の振り上げ / 終わると 1→0）。他の Boss は 0 のまま（2026-10-26）
        float chargePose = 0.0f;
        bool  thunderStruck = false;                // 雷鳴の雄鹿：この瞬間に道筋の輪へ雷の予告を置いた（音）
        // 召喚：この瞬間に湧かせる所（地面の高さ）と種類（Swarm::kEnemyKind*）
        struct Summon { DirectX::SimpleMath::Vector3 pos; uint32_t kind = 0; };
        std::vector<Summon> summons;

        // ---- 砂の守護者 ----
        // 砂の槍：溜めが終わってトゲが走り出した線（この瞬間だけ。トゲの見た目・土煙はシーン）
        struct SpearLine { DirectX::SimpleMath::Vector3 start, end; };
        std::vector<SpearLine> spearLines;
        float spearSpeed = 0.0f;       // トゲが先へ進む速さ（見た目を判定に合わせる）
        // 潜砂奇襲：GPU の Boss の沈み具合 0（地上）〜 1（地面の下・撃てない）。毎フレーム BomberCB::bossBurrow へ
        float bossBurrow = 0.0f;
        bool  burrowSank = false;      // 潜り始めた瞬間（砂の噴き上げ）
        bool  burrowMound = false;     // 砂丘を出す間（地面の下を動く・止まって輪）
        bool  burrowEmerged = false;   // 飛び出した瞬間（同じフレームに Hit も出る）
        // 流沙：渦が出た瞬間（見た目・音）。渦が有る間の様子は BossAttacks::Quicksand()
        bool  quicksandStarted = false;
        DirectX::SimpleMath::Vector3 quicksandPos;

        // ---- 大サソリ（2026-10-26）----
        bool  stingLunge = false;      // 毒針：溜めが終わって突っ込み始めた瞬間（音）
        bool  clawSwung = false;       // 鋏の薙ぎ払い：鋏を振った瞬間（当たらなくても。音・見た目）
        DirectX::SimpleMath::Vector2 clawDir = { 0.0f, 1.0f };
        // ---- 墓守の騎士（2026-10-26）：溜めが終わった瞬間に投げる鎖（根元 = Boss の中心、向き）。鉤の飛び・当たり・引き寄せは
        //      シーンの鎖（CollisionTestSceneRuins.cpp、鎖の番人と同じ物）。掛かったらシーンが OnChainCaught を呼ぶ ----
        struct ChainThrow { DirectX::SimpleMath::Vector3 pos; float yaw = 0.0f; };
        std::vector<ChainThrow> chainThrows;
        // ---- 月下の狼人（2026-10-26）----
        bool  pounceJump = false;      // 跳び始めた瞬間（音）
        bool  wolfSlash = false;       // 爪の突っ込みが始まった瞬間（音・斬撃の見た目。clawDir の向き）
        bool  howled = false;          // 吠えた瞬間（召喚と同じフレーム。音・見た目）
        float jumpHeight = 0.0f;       // 跳びの高さ（VS が chargePose の放物線に掛ける。BomberCB::bossJumpHeight）
        bool  noContact = false;       // 技の間 Boss が体で殴らない（飛びかかり：地面を走り抜ける体がプレイヤーに触れて無敵になり、着地の当たりが外れた）
    };

    // 流沙の渦の今の様子（技が終わっても残る）
    struct QuicksandField
    {
        bool  active = false;
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ
        float radius = 0.0f;                   // 今の半径（広がっている途中は小さい）
        float strength = 0.0f;                 // 0〜1（広がる・消える）
    };

    // ---- 共通（Enemies パネルの「Boss Attacks」）----
    bool  enabled = true;
    float firstDelay = 3.0f;        // 現れてから最初の技まで（秒）
    float gap = 2.5f;               // 技が終わってから次の技まで（秒）
    float gapEnraged = 1.5f;        // HP が enrageHpRatio を切った後
    float enrageHpRatio = 0.5f;
    float comboChance = 0.3f;       // 狂暴の間、技の直後に間を空けず次を出す確率
    float range = 35.0f;            // Boss とプレイヤーがこれより離れていたら技を出さない（洞の奥から外を叩かない）

    // ---- 重撃（Slam）----
    int   count = 3;                // 輪の数
    float spacing = 0.45f;          // 輪を置く間隔（秒）
    float warnTime = 1.2f;          // 輪が出てから爆発まで（秒）
    float radius = 3.0f;            // m
    float damage = 25.0f;           // 難度の倍率 1 の時。当たると × MobSpawner の今のダメージ倍率

    // ---- 投石雨（RockRain）----
    int   rockCount = 9;            // 岩の数（1 つ目はプレイヤーの足元、残りは周りのばらばらな所）
    float rockSpacing = 0.12f;      // 輪を置く間隔（秒）
    float rockWarn = 1.1f;          // 輪が出てから落ちるまで（秒）
    float rockRadius = 2.0f;
    float rockSpread = 7.0f;        // プレイヤーの周りのこの半径の中に散らす（m）
    float rockDamage = 18.0f;
    float rockFallTime = 0.45f;     // 爆発のこれだけ前に岩が空から落ち始める（BossRockFall.json の落ちる時間と合わせる）

    // ---- 衝撃波（Shockwave）----
    float waveWindup = 0.9f;        // 溜め（Boss が止まって足元の輪が育つ）
    float waveWindupRadius = 4.0f;  // 溜めの輪の半径
    float waveSpeed = 11.0f;        // 帯が広がる速さ m/s
    float waveMaxRadius = 28.0f;
    float waveBand = 1.4f;          // 帯の幅 m
    float waveDodgeHeight = 0.6f;   // 足が地面からこれより上なら当たらない（跳べば避けられる）
    float waveDamage = 22.0f;

    // ---- 突進（Charge）----
    float chargeWindup = 1.1f;      // 溜め（Boss が止まって道筋の輪が育つ）
    float chargeSpeed = 20.0f;      // m/s
    float chargeLength = 24.0f;     // 走る距離 m（壁に当たれば GPU が止める）
    float chargeLaneRadius = 1.4f;  // 道筋の輪の半径
    float chargeLaneSpacing = 2.4f; // 道筋の輪の間隔 m
    float chargeHitRadius = 2.6f;   // Boss の中心からこの距離にプレイヤーが居れば当たり
    float chargeDamage = 35.0f;
    // 雷鳴の雄鹿（BossType::Stag）だけ：突進で道筋の輪を通り過ぎる度にそこへ雷の予告 → stagThunderWarn 秒後に落ちる
    // （跳んでも避けられない、横へ避けた後も道筋に立っていると打たれる）。姿勢（chargePose）の秒もここ（2026-10-26）
    bool  stagThunder = true;
    float stagThunderWarn = 0.45f;  // 予告から落雷まで
    float stagThunderDamage = 16.0f;// 基準（× damageMul）。精鋭の雷光と同じ
    float stagThunderRadius = 1.5f;
    float stagTossTime = 0.18f;     // 当てた瞬間に頭を振り上げる秒
    float stagRecoverTime = 0.35f;  // 突進が終わってから頭が上がる秒

    // ---- 召喚（Summon）----
    float summonWindup = 1.4f;       // 吠えてから湧くまで（Boss は止まる）
    int   summonCount = 6;           // 湧く数
    int   summonCountEnraged = 9;    // HP が enrageHpRatio を切った後
    float summonRingMin = 3.5f;      // Boss の周りのこの距離の間に置く（m）
    float summonRingMax = 7.0f;
    float summonRingRadius = 1.1f;   // 湧く所の輪の半径
    // 湧く種類（Swarm::kEnemyKind*）と重み。Configure が Boss 毎に入れる（草原 = 雑魚 0.7 / 自爆兵 0.3）
    struct SummonPick { uint32_t kind = 0; float weight = 0.0f; };
    static constexpr int kSummonPicks = 3;
    SummonPick summonPicks[kSummonPicks] = { { 0u, 0.7f }, { 1u, 0.3f }, { 0u, 0.0f } };

    // ---- 砂の槍（SandSpear。砂の守護者）----
    float spearWindup = 0.9f;        // 溜め（Boss が止まって線上の輪が育つ）
    float spearLength = 22.0f;       // 線の長さ m
    float spearSpeed = 18.0f;        // 足元から先へトゲが突き出ていく速さ m/s
    float spearLaneRadius = 1.1f;    // 線上の輪の半径 = 当たりの幅（線からこれ + プレイヤーの太さ）
    float spearLaneSpacing = 1.6f;
    float spearDodgeHeight = 1.4f;   // 足が地面からこれより上なら当たらない（トゲは高いので、ほぼ跳んでも当たる）
    float spearDamage = 26.0f;
    int   spearLinesEnraged = 3;     // 狂暴の後は扇に何本（普段 1 本）
    float spearFanDeg = 24.0f;       // 扇の線の間の角度

    // ---- 潜砂奇襲（Burrow。砂の守護者）----
    float burrowSinkTime = 0.5f;     // 沈む（その場で止まる）
    float burrowSpeed = 10.0f;       // 地面の下でプレイヤーを追う速さ m/s（走り 5.5 より速い）
    float burrowMaxTravel = 3.5f;    // 追う秒の上限（追い付けなくてもここで止まる）
    float burrowStopDist = 1.2f;     // プレイヤーまでこの距離に来たら止まる
    float burrowWarn = 1.0f;         // 止まってから飛び出すまで（足元の輪が育つ。走って出る間）
    float burrowRiseTime = 0.3f;     // 体が地面から出てくる
    float burrowRadius = 3.5f;       // 飛び出しの当たりの半径
    float burrowDamage = 32.0f;

    // ---- 流沙（Quicksand。砂の守護者）----
    float sandWindup = 0.8f;         // 吠える（止まる）。渦はこの間に広がる
    float sandRadius = 10.0f;        // m（VFXData/BossQuicksand.json の液面の半径と合わせる）
    float sandDuration = 6.0f;       // 渦が残る秒（技が終わってからも）
    float sandFadeTime = 0.8f;       // 最後の秒で弱まって消える
    float sandSlow = 0.55f;          // 中で地面に居る間の走りの速さの倍率
    float sandPullEdge = 1.2f;       // 縁での中心へ引く速さ m/s
    float sandPullCenter = 2.6f;     // 中心近く（半径の 0.3 の内側）での速さ m/s
    float sandDeadRadius = 0.8f;     // 中心のこの内側は引かない（揺れないように）

    // ---- 毒針（Sting。大サソリ）：向きを固めて帯の予告 → 短く突っ込む → 尾で刺す（当たった時だけ毒）----
    float stingWindup = 0.7f;        // 溜め（止まって尾を後ろへ反らす。帯の予告が育つ）
    float stingSpeed = 14.0f;        // 突っ込む速さ m/s
    float stingLength = 8.0f;        // 突っ込む距離 m（= 帯の長さ）
    float stingWidth = 2.4f;         // 帯の幅 m（当たりは Boss から stingHitRadius）
    float stingHitRadius = 2.4f;     // 突っ込んでいる Boss の中心からこの距離にプレイヤーが居れば刺さる
    float stingDamage = 22.0f;
    float stingRest = 0.4f;          // 突っ込んだ後、その場で止まる秒（尾を戻す）
    float stingPoisonTime = 6.0f;    // 毒の秒
    float stingPoisonSlow = 0.6f;    // 毒の間の走りの速さの倍率（地面に居る時。PlayerStateComponent::groundSlow）

    // ---- 鋏の薙ぎ払い（ClawSweep。大サソリ）：正面の扇の予告 → 1 回薙ぐ ----
    float clawWindup = 0.8f;         // 溜め（向きを固めて止まる。扇が育つ）
    float clawRadius = 5.5f;         // 扇の半径 m
    float clawHalfDeg = 55.0f;       // 扇の半角（度）
    float clawDodgeHeight = 1.4f;    // 足が地面からこれより上なら当たらない
    float clawDamage = 28.0f;
    float clawRest = 0.35f;          // 振った後その場で止まる秒

    // ---- 双鎖（ChainPull。墓守の騎士）：向きを固めて 2 本の帯の予告 → 鎖を 2 本（± chainFanDeg）投げる → 掛かれば引き寄せて重撃 ----
    float chainWindup = 0.9f;        // 溜め（止まる。帯が育つ）
    float chainFanDeg = 3.5f;        // 2 本の鎖の向きの開き（± 度。7° だと 10m 先で 1.2m 開き、当たり 0.55 + 0.4 で真ん中の的に 2 本とも外れた）
    float chainRange = 14.0f;        // 届く長さ m（= 帯の長さ）
    float chainWidth = 1.1f;         // 帯の幅 m（鉤の当たりの太さ 0.55 × 2）
    float chainDamage = 18.0f;       // 掛かった時（基準 × damageMul）
    float chainPullStop = 2.6f;      // Boss の前のここまで引き寄せる（重撃が届く所）
    float chainRest = 0.5f;          // 投げてから次へ（鎖の飛ぶ時間。掛かったらこの後すぐ重撃）
    // 復活（StageDirector::bossLives = 2）：1 回倒れると bossReviveDelay 秒後に同じ所へ HP × bossReviveHpFrac で立ち上がる。
    // 立ち上がった後は forceEnraged（シーンが毎フレーム入れる）= 間が短く連技あり
    bool  forceEnraged = false;

    // ---- 月下の狼人 ----
    // 飛びかかり（Pounce）：しゃがんで溜め → 着地点（溜めの終わりのプレイヤー）へ pounceAirTime 秒で跳ぶ（GPU は地面を真っ直ぐ、
    // VS が放物線に持ち上げる）→ 着地で輪の中に当たり（跳べば避けられる）→ 少し止まる
    float pounceWindup = 0.5f;
    float pounceAirTime = 0.6f;
    float pounceHeight = 4.0f;       // 放物線の高さ m（焼いた網の空間ではなく世界。VS で 1/3）
    float pounceRadius = 3.0f;
    float pounceDamage = 24.0f;
    float pounceRest = 0.35f;
    float pounceMaxSpeed = 32.0f;    // 跳ぶ速さの上限 m/s（遠いと届かない）
    // 爪の三連（ClawCombo）：短い帯の予告 → clawDashSpeed で clawDashLength 突っ込む（Boss の近くに居れば斬られる）× clawComboCount
    float clawComboWindup = 0.5f;    // 向きは溜めの始めに固める = この間に横へ走れば外れる（0.35 / 当たり 2.4 では 2 発目以降が避けられなかった）
    float clawDashSpeed = 16.0f;
    float clawDashLength = 6.0f;
    float clawDashWidth = 2.2f;      // 帯の幅
    float clawComboHitRadius = 1.8f;
    float clawComboDamage = 20.0f;
    int   clawComboCount = 3;
    int   clawComboCountEnraged = 5;
    // 遠吠え（Howl）：止まって吠える → 召喚（summon* と同じ輪）+ howlHasteTime 秒は技の間が howlGapMul 倍
    float howlWindup = 1.2f;
    float howlHasteTime = 8.0f;
    float howlGapMul = 0.5f;

    BossBrain brain;

    // どの Boss か（面毎）。技の有り / 無し・召喚する敵・体の色を入れ替える（Reset はしない）
    void Configure(BossType t);
    BossType Type() const { return m_Type; }
    // 体の色（GPU の BomberCB::bossTint。雑魚の網の色に掛ける）
    DirectX::SimpleMath::Vector3 bodyTint = { 1.1f, 0.5f, 1.5f };
    // 走り（chargePose 1）の前傾の支点（焼いた網の空間 y / z）と角度 rad（BomberCB::bossLean*。Configure が Boss 毎に入れる。2026-10-26）
    float leanPivotY = 0.0f, leanPivotZ = 0.0f, leanAngle = 0.0f;

    void Reset();
    void Update(const Input& in, const GridWorld& grid, Output& out);

    const std::vector<Visual>& Visuals() const { return m_Visuals; }
    BossMove Current() const { return m_Move; }
    const QuicksandField& Quicksand() const { return m_SandField; }
    // 潜砂奇襲の段（0 沈む / 1 追う / 2 止まって輪 / 3 飛び出す）。潜砂中でなければ -1（自動テスト用）
    int BurrowPhase() const { return (m_Move == BossMove::Burrow) ? m_BurrowPhase : -1; }
    // 流沙の中のプレイヤーへの効き。中で無ければ false（slowMul = 1、pull = 0）。
    // grounded = 地面に居る時だけ効く（跳べば抜けられる）。pull = 水平の速さ m/s（中心へ）
    bool QuicksandPull(const DirectX::SimpleMath::Vector3& playerFeet, bool grounded,
        float& slowMul, DirectX::SimpleMath::Vector2& pull) const;
    // 大サソリの毒（2026-10-26）：刺さった時にシーンが ApplyPoison、残り秒は Update が減らす（Boss が居なくなっても）。
    // 毒の間はシーンが PlayerStateComponent::groundSlow に stingPoisonSlow を入れる
    void  ApplyPoison() { m_PoisonLeft = (std::max)(m_PoisonLeft, stingPoisonTime); }
    float PoisonLeft() const { return m_PoisonLeft; }
    // 墓守の騎士の鎖が掛かった（シーンの鎖から）：双鎖が終わり次第、間を空けずに重撃
    void  OnChainCaught() { m_ChainCaught = true; }
    // 今狂暴か（最後の Update の HP 割合 / forceEnraged。シーンが狼人の体を赤くするのに見る）
    bool  IsEnraged() const { return m_LastEnraged; }

    // パネルのボタン・自動テスト：次の Update で（距離・間隔・冷却を見ずに）この技を出す
    void QueueMove(BossMove m) { m_DebugMove = m; }
    void QueueVolley() { QueueMove(BossMove::Slam); }
    // 自動テストの記録用（累計）
    uint32_t volleys = 0, ringsPlaced = 0, blasts = 0, summoned = 0;
    uint32_t moveCounts[(int)BossMove::Count] = {};

    void DrawImGui();

private:
    struct Ring
    {
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ
        float radius = 3.0f;
        float age = 0.0f;
        float warn = 1.0f;
        float damage = 0.0f;
        BossMove kind = BossMove::Slam;
        bool  thunder = false;   // 雷鳴の雄鹿の落雷（Hit::thunder へ）
    };

    void StartMove(BossMove m, const Input& in, const GridWorld& grid, Output& out);
    void UpdateMove(const Input& in, const GridWorld& grid, Output& out);
    void UpdateWave(const Input& in, const GridWorld& grid, Output& out);
    void BuildVisuals(const Input& in, const GridWorld& grid);
    bool Enraged(const Input& in) const { return forceEnraged || in.bossHpRatio < enrageHpRatio; }
    void FinishMove(const Input& in);   // 技が終わった：次の技までの間を決める
    uint32_t PickSummonKind();

    // ---- 砂の守護者の技（BossAttacksSand.cpp）。Update が true を返したら技が終わり ----
    void StartSandSpear(const Input& in, const GridWorld& grid);
    bool UpdateSandSpear(const Input& in, const GridWorld& grid, Output& out, float prevTime);
    void StartBurrow(const Input& in);
    bool UpdateBurrow(const Input& in, const GridWorld& grid, Output& out);
    void StartQuicksand(const Input& in, const GridWorld& grid);
    bool UpdateQuicksandMove(const Input& in, Output& out);
    void UpdateQuicksandField(float dt);   // 渦の残り（技の後も）
    void BuildSandVisuals(const GridWorld& grid);
    // ---- 大サソリの技（BossAttacksScorpion.cpp。2026-10-26）----
    void StartSting(const Input& in, const GridWorld& grid);
    bool UpdateSting(const Input& in, const GridWorld& grid, Output& out, float prevTime);
    void StartClawSweep(const Input& in);
    bool UpdateClawSweep(const Input& in, const GridWorld& grid, Output& out, float prevTime);
    void BuildScorpionVisuals(const Input& in, const GridWorld& grid);
    void UpdateScorpionPose(Output& out);   // 毒針の尾の反り / 振り下ろし → chargePose
    // ---- 雷鳴の雄鹿の中身（BossAttacksStag.cpp。2026-10-26）：突進の姿勢 → chargePose、道筋の雷 ----
    void UpdateStagPose(const Input& in, Output& out);
    void StagThunderLane(const Input& in, const GridWorld& grid, Output& out);
    // ---- 墓守の騎士の技（BossAttacksKnight.cpp。2026-10-26）----
    void StartChainPull(const Input& in, const GridWorld& grid);
    bool UpdateChainPull(const Input& in, Output& out, float prevTime);
    void BuildKnightVisuals(const GridWorld& grid);
    // ---- 月下の狼人の技（BossAttacksWolf.cpp。2026-10-26）----
    void StartPounce(const Input& in);
    bool UpdatePounce(const Input& in, const GridWorld& grid, Output& out, float prevTime);
    void StartClawCombo(const Input& in, const GridWorld& grid);
    bool UpdateClawCombo(const Input& in, const GridWorld& grid, Output& out, float prevTime);
    bool UpdateHowl(const Input& in, Output& out);
    void BuildWolfVisuals(const Input& in, const GridWorld& grid);

    BossType m_Type = BossType::StoneGiant;

    std::vector<Ring>   m_Rings;
    std::vector<Visual> m_Visuals;
    std::mt19937 m_Rng{ 20261007u };

    BossMove m_Move = BossMove::None;   // 今出している技
    float    m_MoveTime = 0.0f;         // その技を始めてから
    float    m_Gap = 0.0f;              // 次の技まで（技を出していない間だけ減る）
    bool     m_WasAlive = false;
    BossMove m_DebugMove = BossMove::None;

    // 重撃・投石雨：まだ置いていない輪
    int   m_Pending = 0;
    float m_PendingTimer = 0.0f;
    DirectX::SimpleMath::Vector3 m_RainCenter;   // 投石雨を始めた時のプレイヤーの足元

    // 衝撃波（技が終わった後も帯は広がり続ける）
    bool  m_WaveOn = false;
    bool  m_WaveHitDone = false;
    float m_WaveRadius = 0.0f;
    DirectX::SimpleMath::Vector3 m_WaveCenter;

    // 突進
    DirectX::SimpleMath::Vector3 m_ChargeStart;   // 溜めを始めた時の Boss（地面の高さ）
    DirectX::SimpleMath::Vector2 m_ChargeDir = { 0.0f, 1.0f };
    bool  m_ChargeHitDone = false;
    // 雷鳴の雄鹿（2026-10-26）：姿勢と道筋の雷
    float m_ChargeTossT = -1.0f;     // 当ててからの秒（< 0 = 振り上げていない）
    float m_ChargeRecover = 0.0f;    // 突進が終わってから頭が上がるまでの残り秒
    int   m_ChargeLanesPassed = 0;   // 通り過ぎて雷を置いた道筋の輪の数

    // 召喚：湧く所（StartMove で決める）
    std::vector<Output::Summon> m_SummonPoints;

    // 砂の槍：線の向き（1〜spearLinesEnraged 本）と始めた所。トゲの前線は溜めが終わってから進む
    std::vector<DirectX::SimpleMath::Vector2> m_SpearDirs;
    DirectX::SimpleMath::Vector3 m_SpearStart;
    bool  m_SpearHitDone = false;

    // 潜砂奇襲：0 沈む → 1 追う → 2 止まって輪 → 3 飛び出す
    int   m_BurrowPhase = 0;
    float m_BurrowPhaseTime = 0.0f;
    float m_BurrowSink = 0.0f;           // 0〜1（Output::bossBurrow）
    DirectX::SimpleMath::Vector3 m_BurrowRingCenter;

    // 流沙の渦
    QuicksandField m_SandField;
    float m_SandAge = 0.0f;

    // 大サソリ（2026-10-26）：毒針の突っ込みの始点、刺したか。毒の残り秒（プレイヤー側の状態だがここで数える）
    DirectX::SimpleMath::Vector3 m_StingStart;
    bool  m_StingHitDone = false;
    bool  m_ClawHitDone = false;
    float m_PoisonLeft = 0.0f;

    // 墓守の騎士（2026-10-26）：双鎖の始点・鎖が掛かったか（終わり次第 重撃）
    DirectX::SimpleMath::Vector3 m_ChainStart;
    bool  m_ChainCaught = false;
    bool  m_LastEnraged = false;

    // 月下の狼人（2026-10-26）：飛びかかりの着地点・跳んだか、爪の三連の何回目・今の突っ込みの始点、遠吠えの残り秒
    DirectX::SimpleMath::Vector3 m_PounceTarget;
    bool  m_PounceAir = false;
    int   m_ComboIndex = 0, m_ComboCount = 3;
    float m_ComboPhaseTime = 0.0f;
    bool  m_ComboDashing = false, m_ComboHitDone = false;
    float m_HowlHasteLeft = 0.0f;
};
