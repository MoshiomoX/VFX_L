// ============================================================
// SwarmKindCB.hlsli
// 敵の種類ごとの調整値（SwarmKindCB）と、それを読む関数（体格・倍率・ガーゴイル・
// 突撃兵・固定エリートなど）。SWARM_KIND_CB_REG を定義したシェーダーだけが使う。
// ============================================================

#ifndef SWARM_KIND_CB_REG
#error "SWARM_KIND_CB_REG を定義してから include すること"
#endif

#ifndef SWARM_KIND_CB_HLSLI
#define SWARM_KIND_CB_HLSLI

// ============================================================
// 敵の種類ごとの調整値と、CPU から敵への毎フレームの指示。必要なシェーダーだけが
// SWARM_KIND_CB_REG を #define して使う（ContactCS b3、敵の VS b5）。
// SwarmTypes.h の Swarm::KindCB と一致させること
// ============================================================
cbuffer SwarmKindCB : register(SWARM_KIND_CB_REG)
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

    // ---- スプリッター（SWARM_KIND_SPLITTER）とその分裂体 ----
    float g_SplitterScale;
    float g_SplitterDamageMul;
    float g_SplitterExpMul;
    float _splitterPad;
    float g_SplitlingScale;
    float g_SplitlingDamageMul;
    float g_SplitlingExpMul;
    float _splitlingPad;

    // ---- ボスの突進（BossAttacks が毎フレーム書く） ----
    // g_BossChargeOn > 0.5 の間、ボスはフローフィールド / 追跡を無視して
    // g_BossChargeDir * g_BossChargeSpeed でまっすぐ動く（速度 0 = その場で溜め）。
    // 壁には止められるが、プレイヤーの前では止まらない（突き抜ける）
    float2 g_BossChargeDir;
    float g_BossChargeSpeed;
    float g_BossChargeOn;

    // ---- 重装兵（SWARM_KIND_BRUTE） ----
    float g_BruteScale;
    float g_BruteDamageMul;
    float g_BruteExpMul;
    float _brutePad;

    // ---- 突撃兵（SWARM_KIND_CHARGER） ----
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

    // ---- 盾兵（SWARM_KIND_SHIELD） ----
    float g_ShieldScale;
    float g_ShieldDamageMul;
    float g_ShieldExpMul;
    float g_ShieldArmor;          // 被弾ごとに引く量（弾 1 発、範囲の判定 1 回）

    float g_ShieldMinFrac;        // …ただし最低でもこの割合は通る
    // ---- 凍結（アイスランス） ----
    float g_FreezeBigMul;         // エリート / ボスの凍結はこの倍率だけ短い
    float g_FreezeImmunity;       // 解けた後、新しく凍らない秒
    float g_FreezeTint;           // VS：凍った体の氷っぽさ（0 = 色を付けない）
    // ---- トゲ玉（SWARM_KIND_SPIKEBALL） ----
    float g_SpikeBallScale;
    float g_SpikeBallDamageMul;
    float g_SpikeBallExpMul;
    float g_SpikeBallRollPerMeter; // MoveCS：体格 1 の玉が 1 m 転がるごとに回るラジアン（見た目の回転速度 / 見た目の半径）
    // ---- 砂潜り（SWARM_KIND_BURROWER） ----
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

    // ---- 砂嵐の術師（SWARM_KIND_SANDMAGE）とその竜巻（SWARM_KIND_TORNADO） ----
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

    // ---- 黒幽霊（SWARM_KIND_DARKGHOST） ----
    float g_DarkGhostScale;        // 体格（白い幽霊は 1）
    float g_DarkGhostDamageMul;    // 殴りのダメージ = g_ContactDamage * これ（白い幽霊は g_GhostDamageMul を使う）
    float g_DarkGhostExpMul;       // 倒せた時の経験玉
    float g_DarkGhostAlpha;        // VS：頂点アルファ（白い幽霊より不透明）

    // ---- 突撃爆弾兵（SWARM_KIND_DASHBOMBER） ----
    float g_DashBomberScale;       // 体格
    float g_DashBomberDamageMul;   // 爆発のダメージ = g_BomberBlastDamage * これ（殴らない）
    float g_DashBomberExpMul;      // 経験玉（点火前に倒された時だけ）
    float g_DashBomberTrigger;     // プレイヤーがこの距離以内にいると自分で点火する（m、同じ高さで）

    float g_DashBomberFuse;        // プレイヤーに触れなかった場合、溜めの終わりから爆発までの秒（= 突進時間）
    float g_DashBomberDashSpeed;   // 突進中の m/s（AICS、点火時に固定した yaw に沿ってまっすぐ）
    float g_DashBomberBlastRadius; // m（見た目は g_BomberBlastArea を radius / g_BomberBlastRadius で拡縮して使い回す）
    float g_DashBomberWindup;      // 点火してから突進するまで（点滅しながら）止まっている秒

    // ---- 面ごとのボス（BossAttacks::Configure / Output::bossBurrow） ----
    float3 g_BossTint;             // 雑魚のメッシュの色に掛ける（草原は紫、砂漠は砂岩）
    float g_BossBurrow;            // 0 = 地上、1 = 砂の下（砂の守護者の潜砂）。AICS がボスの fuse に -burrow として写す

    // ---- 呪いの鎧（SWARM_KIND_ARMOR / ARMOR2） ----
    float g_ArmorScale;
    float g_ArmorDamageMul;
    float g_ArmorExpMul;           // 1 回目の死（崩れるだけ）
    float g_ArmorRevivedExpMul;

    // ---- 石像ガーゴイル（SWARM_KIND_GARGOYLE） ----
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
    float g_GargoyleDiveMin;       // 飛行中はこれと g_GargoyleDiveRange * 0.9 の間を回る（真上からの急降下は頭の上を通り過ぎる）

    // ---- 死霊術師（SWARM_KIND_NECRO）と骸骨（SWARM_KIND_SKELETON） ----
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

    // ---- 鎖の番人（SWARM_KIND_WARDEN）。鎖そのものは CPU（シーン） ----
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
    // ---- 面の固定エリート（SWARM_KIND_ELITE_A / B）。体格は面ごと（EliteAttacks::Configure） ----
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
    // 杖の狙いの優先度：SwarmAimKeyDist 参照
    float g_AimCloseRadius;
    float g_AimPriorityRange;
    float g_AimPriorityOn;
    float _aimPad;
    // 雷鳴の雄鹿の突進の姿勢（BossAttacks::Output::chargePose）：0 通常、0..1 溜め（頭を下げ、
    // 脚を開いて屈む）、1 突進中（頭を下げ、体を前に倒し、速い歩様）、1..1.3 当たった時に頭を振り上げる、
    // 突進後は 1 -> 0。敵の VS が読む。四足のボスの時だけ書く
    float g_BossChargePose;
    float g_BossLeanPivotY;        // 突進の前傾の支点（焼いたメッシュ空間の y / z）と角度（pose 1 での rad）、ボスごと（CPU）
    float g_BossLeanPivotZ;
    float g_BossLeanAngle;
    float g_BossJumpHeight;        // 狼人の飛びかかり：> 0 = pose 0..1 の間、体がこの高さ（ワールド m）の放物線で上がる
    float g_BossNoContact;         // > 0.5 = ボスは触れても殴らない（飛びかかり：体がプレイヤーを突き抜ける）
    float g_ClimbSpeed;            // 敵が壁 / 崖面を登る m/s（MoveCS）。0 = 登らない
    // 砂嵐の魔神（BossAttacks::Output から CPU が毎フレーム書く。他のボスでは 0）
    float g_BossHover;             // 体の本当の高さ（ワールド m、MoveCS が位置に入れる）
    float g_BossKeepMin;           // > 0 = 術師のように距離を保つ（AICS）：これより内側では下がり、g_BossKeepMax までは立っている
    float g_BossKeepMax;
    float g_BossFly;               // > 0.5 = 幽霊のように壁を抜け、台地を越えて飛ぶ（SwarmFliesThrough）
    // 石の守護獣（BossAttacks::Output::beast から CPU が毎フレーム書く。他のボスでは 0）
    float g_BossWing;              // ボスの翼の角度（g_AnimMode 2）rad：0 = 横に広げる、+ = 先が上がる
    float g_BossSpin;              // ボスの体に足す yaw の rad（尾の薙ぎ払いの一回転）
    // 敵の足元の燃える草（GrassFireField。AreaDamageCS）
    float g_FireOn;                // > 0.5 = g_FireMap がバインドされている
    float g_StepClock;             // AreaDamageCS の時計（秒）。固定ステップごとに必ず進める。草の火の拍、剣 / 氷が当たった時刻に使う
    float g_FireTick;              // 当たりの間隔（秒）
    float g_FireDamage;            // 1 回あたり
    float g_FireOriginX;           // 炎マップの最小の角のワールド xz
    float g_FireOriginZ;
    float g_FireSizeX;             // m
    float g_FireSizeZ;
    float g_IceOn;                 // > 0.5 = SwarmEnemyVS が凍った池の上に体を持ち上げる（マップの範囲は炎と同じ）
    float2 _firePad;
};

// 杖の狙いの競争（MoveCS、SWARM_CNT_AIM_KEY）がキーにする距離（優先度 3 段階：
// 近すぎる雑魚 > ボス > エリート > その他）。
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

// ---- 石像ガーゴイルの補助関数。fuse：<= 0 眠り、(0, wake) 目覚め中、[wake, DIVE) 飛行、
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
        || (ex.kind == SWARM_KIND_BOSS && g_BossFly > 0.5);   // 魔神の一段目
}

// 被弾硬直を受けない種類（HitCS / AreaDamageCS）：ボス（絶え間ない攻撃で釘付けに
// なってしまう）、トゲ玉、溜めを始めた突撃兵（当たるたびに MoveCS が突進を止めてしまう）、
// 最終ウェーブの幽霊 2 種（攻撃で足止めできない）
bool SwarmNoFlinch(SwarmEnemyExtra extra)
{
    uint kind = extra.kind;
    return (kind == SWARM_KIND_BOSS) || (kind == SWARM_KIND_SPIKEBALL) || SwarmIsGhost(kind)
        || (kind == SWARM_KIND_CHARGER && extra.fuse > 0.0) || SwarmInvulnerable(extra)
        || (kind == SWARM_KIND_GARGOYLE)    // 石
        || SwarmIsFixedElite(kind);         // 固定エリートは技の溜めがある
}

// 1 回の命中（弾 1 発、範囲の判定 1 回）がこの種類に本当に与えるダメージ。
// 盾兵は被弾ごとに g_ShieldArmor を引くので、小さな判定をたくさん受けても
// （毒沼、光線）ほとんど削れず、重い一撃が効く
float SwarmArmorDamage(uint kind, float dmg)
{
    return (kind == SWARM_KIND_SHIELD && dmg > 0.0)
        ? max(dmg - g_ShieldArmor, dmg * g_ShieldMinFrac) : dmg;
}

// ---- CPU から固定エリートへの指示。Swarm::kEliteCmd* / kEliteFlag* と一致させること ----
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

// スロット 1 つの敵の体格：種類の値に、狂暴の固定エリートなら g_EliteEnragedScale を掛ける
// （SwarmEnemyVS の描く大きさと命中・接触・HP バーを揃える）
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

// 壁に対して止まる半径（当たり判定の半径を単位とする）：メッシュは
// カプセルより幅がある（約 0.9m 対 0.8m）。PushCS と同じ理由
static const float SWARM_BODY_WALL_MUL = 1.15;
// 大きな種類でも壁の間の 1 セル（2m）の隙間を通れるよう上限を設ける：
// フローフィールドはそこを通るので、ないとボスが詰まる
static const float SWARM_BODY_WALL_MAX = 0.9;

// 壁に対して使う体の半径（大きい種類ほど大きい。上限あり）
float SwarmBodyWallRadius(uint kind)
{
    return min(g_EnemyRadius * SwarmKindScale(kind) * SWARM_BODY_WALL_MUL, SWARM_BODY_WALL_MAX);
}

#endif
