// ============================================================
// SwarmTypes.h
// Shader/Common/SwarmCommon.hlsli と一対一で対応する定義。
//
// StructuredBuffer には反射が無い。ズレても警告は出ず、
// ただ結果が壊れるだけなので、必ず両方を同時に直すこと。
// static_assert でサイズだけは機械的に守る。
//
// ※state（生死）は本体の構造体に入れず、別の RWBuffer<uint> に置く。
//   SM5.0 の原子操作は RWByteAddressBuffer と
//   RWBuffer<uint> / RWStructuredBuffer<uint> にしか使えない。
//   スロットの取り合いに InterlockedCompareExchange が要るので、
//   生死だけは独立した uint 配列にする必要がある。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    // 固定ステップ（Unity の FixedUpdate 既定値に合わせた）
    constexpr float kFixedStep = 0.02f;
    constexpr int   kMaxSubSteps = 4;   // コマ落ち時の死のスパイラル防止

    // プール容量。溢れたら生成しない（粒子の deadCount ガードと同じ思想）
    constexpr uint32_t kMaxEnemies = 4096;
    constexpr uint32_t kMaxProjectiles = 8192;
    constexpr uint32_t kMaxOrbs = 4096;

    // 1フレームに受け付ける生成依頼の上限
    constexpr uint32_t kMaxSpawnEnemyPerFrame = 256;
    constexpr uint32_t kMaxSpawnProjPerFrame = 512;

    // 範囲攻撃。HLSL の SWARM_MAX_AREAS と一致させること
    constexpr uint32_t kMaxAreas = 256;
    constexpr uint32_t kMaxAreaDefs = 64;          // 0 番 = 無し
    constexpr uint32_t kMaxSpawnAreaPerFrame = 64;

    constexpr float kHpScale = 100.0f;
    inline uint32_t HpToFixed(float hp) { return (uint32_t)(hp * kHpScale + 0.5f); }
    inline float    HpFromFixed(uint32_t h) { return (float)h / kHpScale; }

    constexpr uint32_t kStateDead = 0;
    constexpr uint32_t kStateAlive = 1;

    // ============================================================
    // 本体データ（生死は別バッファ）
    // ============================================================
    struct Enemy
    {
        Vector3  position;
        uint32_t hp = 0;
        Vector3  velocity;
        float    moveSpeed = 3.5f;
        float    yaw = 0.0f;

        // ---- アニメーション ----
        // animTime は MoveCS が進める時計（雑魚の VS の歩き・盾や砂の山の揺れ・風源の出だしが読む）。
        // 将来の VAT（頂点アニメーションテクスチャ）も、CS が animTime を進めて VS が引く形を想定
        float    animTime = 0.0f;
        uint32_t animIndex = 0;   // 0=待機 1=歩行 2=被弾（HitCS が書く。animTime が経過秒。VS が閃光に使う）
        // 接触攻撃の残り待ち時間（生きている敵では ContactCS だけが書く。0 以上）。
        // 倒された槽には HitCS / AreaDamageCS が倒した一撃の属性を -(1 + 属性) で残す（ダメージの数字の色。2026-10-25）
        float    attackCooldown = 0.0f;
    };
    static_assert(sizeof(Enemy) == 48, "SwarmEnemy layout mismatch");

    // ============================================================
    // 雑魚の種類（Enemy 本体の 48B は変えず、スロットと同じ添字の並行バッファに持つ）
    //   生成依頼では Enemy::animIndex に種類を入れて運ぶ（SpawnEnemyCS / RecycleCS が
    //   並行バッファへ移して animIndex を 0 に戻す。弾の motion の bit31 と同じ流儀）
    // ============================================================
    constexpr uint32_t kEnemyKindMob = 0;      // 普通の雑魚（接触で殴る）
    constexpr uint32_t kEnemyKindBomber = 1;   // 自爆兵（接触で点火 → fuseTime 秒後に爆発）
    constexpr uint32_t kEnemyKindElite = 2;    // エリート（大きい雑魚。BomberCB の elite* で体格・接触ダメージ・経験値を倍にする）
    constexpr uint32_t kEnemyKindBoss = 3;     // 面の Boss（BomberCB の boss*。怯まない。HP と位置は BossInfo で CPU へ）
    constexpr uint32_t kEnemyKindGhost = 4;    // 最終ウェーブの幽霊（2026-09-30）：雑魚の HP、速い、壁も台地も素通り、半透明の青白
    constexpr uint32_t kEnemyKindSplitter = 5; // スプリッター（2026-10-03）：死ぬと小さい分裂体を 3 体出す（SwarmCorpseTrackCS → 分裂の環 → MobSpawner）
    constexpr uint32_t kEnemyKindSplitling = 6;// 分裂体：スプリッターの小さい子。もう分裂しない
    constexpr uint32_t kEnemyKindBrute = 7;    // 重装兵（2026-10-07 夜）：HP 3 倍・遅い・大きい。4 分から雑魚と入れ替わる（Megabonk の Goblin Tank 相当）
    // 突撃兵（2026-10-08）：近付くと止まって溜め（地面に赤い予告の帯）→ 向きを固定して一直線に突進 → 息切れで立ち止まる。
    // 段階は EnemyExtra::fuse に持つ（SwarmEnemyAICS が書く。HLSL の SwarmChargerPhase）
    constexpr uint32_t kEnemyKindCharger = 8;
    // 盾兵（2026-10-08）：受ける 1 発毎のダメージから BomberCB::shieldArmor を引く（最低 shieldMinFrac 倍は通る）。
    // 毒の沼・光線の細かい tick はほとんど通らず、一発の重い魔法がよく効く。身体の前に塔の盾（SwarmShieldVS）
    constexpr uint32_t kEnemyKindShield = 9;
    // トゲ玉（2026-10-17、砂漠の敵）：厚くて遅い転がるトゲの玉。触れたプレイヤーを弾き飛ばす（PlayerHitInfo の spike）。
    // 怯まない・突風で押せない。雑魚のメッシュは描かず SwarmSpikeBallVS が球 + トゲを描く。HP・速さは MobSpawner::m_SpikeBall*
    constexpr uint32_t kEnemyKindSpikeBall = 10;
    // 砂潜り（2026-10-17、砂漠の敵）：砂の下を進む（砂の山だけ見える・当たらない・噛まない・狙われない）、プレイヤーの
    // 足元で顔を出し、burrowBiteDelay 秒後に周り burrowBiteRadius を 1 回噛んで（ダメージ ×2 + 小さなノックバック）、
    // burrowSurfaceTime 秒でまた潜る（出ている間は動かない・追わない。ユーザー 10-17「冒头一咬就走」）。EnemyExtra.fuse > 0 = 出ている残り秒、<= 0 = -(潜っている秒)
    constexpr uint32_t kEnemyKindBurrower = 11;
    // 砂嵐の術師（2026-10-17、砂漠の敵。ユーザー「能把玩家吹飞的」→ 四案から「飛ぶ砂旋」）：プレイヤーから距離を取り
    // （sandMageKeepMin〜Max の間に立つ）、sandMageWindup 秒溜めて砂旋（kEnemyKindTornado）を 1 つ放つ。放つ瞬間は AICS が
    // 分裂の環（castEvents）に書き、CPU（MobSpawner::SpawnFromEvents）がプレイヤーへ向けて湧かせる。EnemyExtra.fuse > 0 = 溜めの経過秒、< 0 = クールダウンの残り
    constexpr uint32_t kEnemyKindSandMage = 12;
    // 砂旋（2026-10-17）：敵の「弾」を敵の 1 種類として実装した物。yaw の向きへ moveSpeed で一直線（幽霊と同じく壁も台地も素通り）、
    // 撃っても当たらない・狙われない・HP バー無し、触れたプレイヤーを高く吹き飛ばす（PlayerHitInfo の wind）、tornadoLife 秒で消える
    // （ContactCS が DEAD にする。撃破に数えない・経験値無し・砕け散らない、死亡の土煙だけ出る）。体は描かない
    // （2026-10-20 にモデルを廃止。見えるのは足元の砂埃 SwarmTornadoEmitCS と、風源 SwarmWindSourceCS で巻かれる草・粒子）
    constexpr uint32_t kEnemyKindTornado = 13;
    // 黒幽霊（2026-10-18、Megabonk の黒い幽霊。ユーザー「十分過ぎても死なない → 幽霊を強化、黒幽霊も」）：最終ウェーブの時間切れ
    // finalDarkGhostStart 秒後から出る、幽霊の上位。HP = 雑魚 × darkGhostHpMul（普通のビルドでは倒せない）、速さ = 雑魚 × darkGhostSpeedMul
    // （走りより速く、スライディングなら振り切れる）、体格 darkGhostScale、接触 × darkGhostDamageMul + 爆発級のノックバック。
    // 白幽霊と同じく壁も台地も素通り・怯まない・凍らない・遅くならない・突風で飛ばない（SwarmIsGhost）。描画は幽霊のリスト（VS が種類で色を変える）
    constexpr uint32_t kEnemyKindDarkGhost = 14;
    // 突撃爆弾兵（2026-10-18、ユーザー「点火したら直接プレイヤーへ突っ込む自爆兵」）：プレイヤーが dashBomberTrigger 内に入ると自分で点火し、
    // その瞬間の向き（Enemy.yaw）を固定 → dashBomberWindup 秒その場で溜め → 固定した向きへ dashBomberDashSpeed で一直線に突っ込む
    // （2026-10-20 ユーザー「ほぼ 100% 当たる」→ 毎ステップ狙い直すのをやめて突撃兵と同じ「向き固定 + 溜め」に。横へ一歩で躱せる）。
    // 触れた瞬間、または溜めの後 dashBomberFuse 秒で爆発（半径 dashBomberBlastRadius、
    // 自爆兵の爆発 × dashBomberDamageMul + 爆発級のノックバック）。**点火中は無敵**（自爆兵も同じ。SwarmInvulnerable：撃っても当たらない・
    // 狙われない・怯まない・凍らない）。EnemyExtra.fuse > 0 = 点火からの秒（AICS が数える）。皮は Kenney Blocky H（灰色ロボット）
    constexpr uint32_t kEnemyKindDashBomber = 15;
    // ---- 遺跡の敵（2026-10-25、ユーザー「遺跡の雑魚を作って」→ 呪いの鎧・石像ガーゴイル・死霊術師・鎖の番人を選んだ）----
    // 呪いの鎧：硬くて遅い。1 回目の死では砕けず崩れ落ち（SwarmCorpseTrackCS が分裂の環に書く）、CPU（MobSpawner）が
    // armorReviveDelay 秒後に同じ所へ「蘇った鎧」を HP 半分で湧かせる。蘇った鎧は普通に砕ける
    constexpr uint32_t kEnemyKindArmor = 16;
    constexpr uint32_t kEnemyKindArmorRevived = 17;
    // 石像ガーゴイル：プレイヤーの近くに石像（動かない・撃っても当たらない・狙われない）で湧き、近付かれる / 時間が経つと
    // 目覚めて飛ぶ（壁も台地も素通り、高さ gargoyleHover）→ 近くで向きを固定して急降下（爆発級のノックバック）→ 着地して
    // しばらく地上で殴れる → また飛ぶ。EnemyExtra.fuse（AICS が唯一の書き手）：<= 0 眠り（-経過秒）、(0, wakeTime) 目覚め、
    // [wakeTime, SWARM_GARGOYLE_DIVE) 飛行、>= SWARM_GARGOYLE_DIVE 急降下（溜め → 降下 → 着地）
    constexpr uint32_t kEnemyKindGargoyle = 18;
    // 死霊術師：砂嵐の術師と同じく距離を取って溜め、詠唱の瞬間に分裂の環に書く → CPU が近くの最近の死体の所から
    // 骸骨（kEnemyKindSkeleton）を起こす（死体が無ければ自分の周り）
    constexpr uint32_t kEnemyKindNecro = 19;
    constexpr uint32_t kEnemyKindSkeleton = 20;   // 速くて脆い・経験値少し（死霊術師が起こす）
    // 鎖の番人：中距離で止まり、向きを固定して溜め（地面に予告の帯 = 突撃兵と同じ VS）→ 溜めの終わりに分裂の環に
    // 「種類 | 向き << 16」を書く → CPU（シーン）が鎖の鉤を投げ、当たったプレイヤーを番人の前へ引き寄せる
    constexpr uint32_t kEnemyKindWarden = 21;
    // ---- 面の固定エリート（2026-10-25、ユーザー「エリートを区別化」→ 各面 2 体、どれも「予告を避ける」型）----
    // A = 近接型（草原 大斧の戦鬼 / 砂漠 砂塵の騎兵 / 遺跡 影の剣士）、B = 遠距離型（森の弓手 / 炎砂の術師 / 呪いの司書。
    // BomberCB::fixedEliteKeepMin〜Max に立つ）。どの個体かは面で決まり、皮と飾りは面毎に描画リストへ入れる（Enemy/EliteLooks.cpp）。
    // 技の流れ・当たり・予告・詞缀（疾風 / 鉄壁 / 雷光 / 狂暴）は CPU（Enemy/EliteAttacks）が持ち、BomberCB::eliteCmd / eliteMove で
    // 「立つ・決めた向きへ走る・瞬間移動・隠れる・盾で半減・怒りで大きく」を GPU に伝える。生死・HP・位置は EliteInfo で CPU へ
    constexpr uint32_t kEnemyKindEliteA = 22;
    constexpr uint32_t kEnemyKindEliteB = 23;
    inline bool IsFixedElite(uint32_t kind) { return kind == kEnemyKindEliteA || kind == kEnemyKindEliteB; }
    // 分裂の環の kind の上 16 bit（出来事の中身）。下 16 bit が種類
    constexpr uint32_t kSplitKindMask = 0xFFFFu;
    constexpr uint32_t kGargoyleEventWake = 0u;   // ガーゴイル：目覚めた
    constexpr uint32_t kGargoyleEventDive = 1u;   // 急降下を始めた（溜めの頭）
    constexpr uint32_t kGargoyleEventLand = 2u;   // 着地した
    // 描画リストの数（種類毎にテクスチャを替えて描く）。エリートは雑魚と同じメッシュ・テクスチャなので雑魚のリストで描き、
    // 大きさと色は VS が種類を見て変える
    constexpr uint32_t kEnemyKinds = 18;
    // 描画リストの添字（種類 → リスト。エリートは雑魚のリスト、分裂体はスプリッターのリスト。
    // Boss は 2026-10-24 から自分のリスト（面毎の皮と飾り。SwarmSystem::SetBossLook））
    constexpr uint32_t kDrawListMob = 0;
    constexpr uint32_t kDrawListBomber = 1;
    constexpr uint32_t kDrawListSplitter = 2;
    constexpr uint32_t kDrawListBrute = 3;    // 重装兵（緑のオーク）
    constexpr uint32_t kDrawListCharger = 4;  // 突撃兵（黒い忍者）
    constexpr uint32_t kDrawListShield = 5;   // 盾兵（紺の警官）
    constexpr uint32_t kDrawListBurrower = 6; // 砂潜り（自分のテクスチャ + 砂の山。2026-10-17）
    constexpr uint32_t kDrawListSandMage = 7; // 砂嵐の術師（自分のテクスチャ。2026-10-17。砂旋は何も描かないので描画リストを持たない）
    constexpr uint32_t kDrawListDashBomber = 8; // 突撃爆弾兵（灰色ロボット。2026-10-18）
    constexpr uint32_t kDrawListBoss = 9;     // 面の Boss（2026-10-24。皮は面毎、体の後に飾りも描く）
    // 遺跡の敵（2026-10-25）。生きている一覧は SwarmEnemyKindList2CS、砕け中の一覧は SwarmCorpseList2CS
    // （Boss・砂嵐の術師・突撃爆弾兵の砕け中もこちら = 自分の皮で砕ける）
    constexpr uint32_t kDrawListArmor = 10;   // 呪いの鎧（蘇った鎧も）
    constexpr uint32_t kDrawListGargoyle = 11;
    constexpr uint32_t kDrawListNecro = 12;
    constexpr uint32_t kDrawListSkeleton = 13;
    constexpr uint32_t kDrawListWarden = 14;
    // 面の固定エリート（2026-10-25）。生きている一覧は SwarmEnemyKindList2CS、砕け中は SwarmCorpseList3CS
    constexpr uint32_t kDrawListEliteA = 15;
    constexpr uint32_t kDrawListEliteB = 16;
    constexpr uint32_t kDrawListGhost = 17;   // 最後に alpha blend で描く（必ず最後の番号。白幽霊と黒幽霊の両方）

    struct EnemyExtra
    {
        uint32_t kind = kEnemyKindMob;
        // 自爆兵：点火からの秒数。0 = 未点火（点火した瞬間に 1 ステップ分が入る）。
        // 突撃兵：< 0 = 次の突進までの待ち（0 へ数え上げる）、0 = 追いかけ中（突進できる）、> 0 = 溜め始めてからの秒数
        float    fuse = 0.0f;
    };
    static_assert(sizeof(EnemyExtra) == 8, "SwarmEnemyExtra layout mismatch");

    // ============================================================
    // 敵の状態（並行バッファ enemySlow、float4。2026-10-08 に float2 → float4）
    //   x = 減速の残り秒、y = 減速の強さ（毒の沼。SwarmAreaDamageCS が書く）
    //   z = 凍結の残り秒（> 0 の間は動かない・殴らない）
    //   w = 凍結中：凍結の長さ（VS の出始め / 溶け際の演出用）、解けた後：再凍結できない残り秒
    // 凍結の依頼は SwarmHitCS が Enemy::animIndex = kAnimFreezeRequest、animTime = 秒 で置き、
    // 次のステップの SwarmEnemyAICS が受け取る（Enemy の 48B は変えない）
    // ============================================================
    constexpr uint32_t kAnimFreezeRequest = 3u;

    struct Projectile
    {
        Vector3  position;
        float    damage = 0.0f;
        Vector3  velocity;
        float    lifetime = 0.0f;
        float    radius = 0.25f;
        uint32_t vfxType = 0;      // index into SwarmVFXTable's recipe table
        uint32_t motion = 0;       // 運動表（Motion）の番号。生成依頼では bit31 = 曲線を左右反転
        float    pathT = 0.0f;     // 曲線上の位置 0..1。GPU が進める
    };
    static_assert(sizeof(Projectile) == 48, "SwarmProjectile layout mismatch");

    // ============================================================
    // 投射物の運動（飛び方）
    //
    // Motion   : 投射物プロファイル 1 個につき 1 行。エディタのデータがここへ入る。
    //            HLSL の SwarmMotion と同じ並び（48B）
    // ProjPath : 投射物スロットと同じ添字。今飛んでいる 3 次ベジェ。
    //            GPU が生成時（と再捕捉時）に組み立てる。CPU は中身を触らない（64B）
    //
    // 制御点は「銃口 → 標的」の座標系で、射距離に比例させて持つ。
    // 近くても遠くても同じ形の曲線になる：
    //     c.x = 銃口 → 標的 の何割の位置か
    //     c.y = 横へのずれ / 射距離（発射ごとに左右反転できる）
    //     c.z = 上へのずれ / 射距離（世界の上方向）
    // ============================================================
    constexpr uint32_t kMaxMotions = 64;
    constexpr uint32_t kMotionFlipBit = 0x80000000u;

    // ============================================================
    // 誘発（基本魔法 → 上級魔法。火球・石弾が消えた所に隕石が落ちる）
    //   生成依頼に並行の uint2 を付ける（Projectile の 48B は変えない）:
    //     x = 誘発タグ。bit k = 「この弾が消えたら、杖の k 番の上級魔法がそこで撃てる」
    //     y = kSpawnAtPos：Drop 型の着弾点を依頼の position にする（最寄りの敵を捕捉しない）
    //   タグはスロット毎の projTags に残り、SwarmProjEndCS が「このステップで消えたタグ付きの弾」を
    //   環（先頭 16B = 今までに書いた総数、以降 16B × kMaxTriggerEvents）へ書く。CPU はリードバックして差分を取る。
    //   HLSL の SWARM_MAX_TRIGGER_EVENTS / SWARM_SPAWN_AT_POS と一致させること
    //   連鎖のルーン（2026-10-16）: 跳ぶ回数は依頼の Projectile::pathT に乗せる（生成後は使われない欄）。SpawnProjCS が読んで
    //   projStates に「連鎖中」の状態値を書く（HLSL の SWARM_PROJ_CHAIN*。CPU は projStates を読まない）
    // ============================================================
    constexpr uint32_t kMaxTriggerEvents = 128;
    constexpr uint32_t kSpawnAtPos = 1u;
    // y の bit 1〜7：曲線の横ずれの回転（0〜127 = 0〜360 度。ProjectileProfile::Mirror::RandomAngle、2026-10-06）。
    // SpawnProjCS が角度にして ProjPath::sideSign（= 回転角 rad。反転は +π）へ入れる。HLSL の SWARM_SPAWN_ROLL_SHIFT と一致させること
    constexpr uint32_t kSpawnRollShift = 1u;
    constexpr uint32_t kSpawnRollMask = 0x7Fu;

    // ============================================================
    // 死んだ敵の砕け散り（2026-10-02）
    //   SwarmCorpseTrackCS が「前のフレームは生きていて今は DEAD」の槽を見つけ、その時の位置・向き・種類を
    //   環（kMaxCorpses 枠、先頭は RAW の通し番号）へ 1 つ書く。描画（SwarmCorpseVS）は部品毎に
    //   (seed, 経過時間) の式で飛び散らせる。状態は持たない。HLSL の SwarmCorpse / SWARM_MAX_CORPSES と一致
    // ============================================================
    constexpr uint32_t kMaxCorpses = 512;
    struct Corpse
    {
        Vector3  position;          // 死んだ時の位置（雑魚の position = 地面 + groundY）
        float    yaw = 0.0f;
        float    dir[2] = {};       // 飛ばす向き（xz の単位。プレイヤーから離れる向き）
        float    birth = 0.0f;      // 死んだ時刻（SwarmSystem の時計 m_AnimClock）。0 = 空き
        uint32_t kind = 0;
        uint32_t seed = 0;
        uint32_t cause = 0;         // 予約：死因（火・毒・雷で死に方を変える時用。今は 0）
        float    _pad[2] = {};
    };
    static_assert(sizeof(Corpse) == 48, "SwarmCorpse layout mismatch");

    // ============================================================
    // 弾が出す範囲（命中・着弾の hitArea）への倍率（2026-10-02）
    //   y の bit 8〜19 = 威力の倍率、bit 20〜31 = 持続の倍率（どちらも 256 = 1.0、0 は 1.0 扱い）。
    //   SwarmSpawnProjCS がスロット毎の projBoost へ写し、範囲を出す所（HitCS / ProjMoveCS）が掛ける。
    //   威力 = 能力アップ「魔法威力」、持続 = 魔力解放中に撃った弾（持続する範囲だけ伸びる）。
    //   HLSL の SWARM_SPAWN_BOOST_SHIFT / SwarmBoostDamage / SwarmBoostDuration と一致させること
    // ============================================================
    constexpr uint32_t kSpawnBoostShift = 8u;
    inline uint32_t PackSpawnBoost(float damageMul, float durationMul)
    {
        auto q = [](float v) -> uint32_t
            {
                const float c = (v < 1.0f / 256.0f) ? 1.0f / 256.0f : (v > 4095.0f / 256.0f ? 4095.0f / 256.0f : v);
                return (uint32_t)(c * 256.0f + 0.5f);
            };
        return (q(damageMul) | (q(durationMul) << 12)) << kSpawnBoostShift;
    }

    struct TriggerEvent
    {
        Vector3  position;    // 弾が消えた所の地面（地形の高さ）
        uint32_t tag = 0;
    };
    static_assert(sizeof(TriggerEvent) == 16, "SwarmTriggerEvent layout mismatch");

    enum class MotionMode : uint32_t
    {
        Straight = 0,    // 直進。敵を一切見ない
        CurveOnce = 1,   // 1 回だけ捕捉して曲線で飛ぶ。標的が死んだら今の向きで直進
        Track = 2,       // 標的が死んでも、自分に一番近い敵を探して曲線を組み直す
        // 隕石: 撃った時に一番近い敵の位置を着弾点に決め、空から斜めに落ちる（途中で敵に当たらない）。
        // c1 の意味が違う（m 単位）: c1.x = 着弾点からの高さ、c1.y = 銃口側へ戻した水平距離。
        // 落ちる所には警告の輪（SwarmSystem::dropRing）
        Drop = 3,
        // 投げ上げ（2026-10-01、毒）: 撃った時に一番近い敵の位置を着弾点に決め、銃口から山なりに投げる。
        // 道筋は曲線と同じ c1 / c2（c.z で持ち上げる）、振る舞いは Drop と同じ（途中で当たらず、地形も見ず、着弾点に hitArea）
        Lob = 4,
    };

    struct Motion
    {
        uint32_t mode = 0;
        float    retargetRadius = 0.0f;   // Track の再捕捉半径。0 以下 = 無制限
        uint32_t hitArea = 0;             // 命中した場所に出す範囲（AreaDef の番号）。0 = 無し
        uint32_t hitAreaFlags = 0;        // bit0 = 寿命切れ・壁に当たった時も出す
        Vector3  c1 = { 0.33f, 0.0f, 0.0f };
        float    baseRadius = 0.0f;       // profile の当たり半径。これより大きく撃たれた弾は見た目も大きく（拡大鏡）。0 = 倍率 1
        Vector3  c2 = { 0.66f, 0.0f, 0.0f };
        float    freezeTime = 0.0f;       // > 0 = 当たった敵をこの秒数凍らせる（アイスランス。2026-10-08、元 _pad2）
    };
    static_assert(sizeof(Motion) == 48, "SwarmMotion layout mismatch");

    struct ProjPath
    {
        Vector3  p0;
        uint32_t target = 0xFFFFFFFFu;
        Vector3  p1;
        float    duration = 1.0f;
        Vector3  p2;
        float    sideSign = 1.0f;   // 2026-10-06 から「横ずれの回転角（rad）」：0 = 右、π = 左（反転）、他 = 飛行軸の周りに回した向き（RandomAngle）
        Vector3  p3;
        float    speed = 0.0f;
    };
    static_assert(sizeof(ProjPath) == 64, "SwarmProjPath layout mismatch");

    // ============================================================
    // 範囲攻撃（爆発・法環）
    //
    // Area    : 生きている範囲 1 個（48B）。HLSL の SwarmArea と同じ並び。
    //           単発と持続は同じ物：爆発 = 1 回だけ tick して、粒子が出終わるまで残る範囲
    // AreaDef : 雛形（32B）。GPU が自分で範囲を出す時（弾の命中）に引く表。0 番 = 無し
    //
    // 形は円盤：XZ の距離 <= radius かつ 高さの差 <= halfHeight（+ 雑魚のカプセル）
    // ============================================================
    constexpr uint32_t kAreaFollowPlayer = 1u;   // 中心がプレイヤーに付いて動く
    constexpr uint32_t kAreaStun = 2u;           // tick で被弾硬直 + 閃光を入れる
    constexpr uint32_t kAreaCapsule = 4u;        // カプセル（光線）：中心 → 終点の線分の周り。起点 / 終点 / 半径は毎ステップ BeamCB から（2026-09-30）
    constexpr uint32_t kAreaShake = 8u;          // 出た時にカメラを揺らす（爆発。2026-10-03：命中の火花・死んだ時の土煙・毒の池まで揺らしていた）
    // 凍結（2026-10-10、アイススパイク）: (flags >> 4) & 0xF = q、tick で当たった敵を q × 0.25 秒凍らせる
    // （アイスランスの命中と同じ凍結の要求。エリート・Boss は半分、解けた直後は凍らない、は AICS が決める）
    constexpr uint32_t kAreaFreezeShift = 4u;
    constexpr float kAreaFreezeStep = 0.25f;
    // 生まれた範囲を GPU の VFX レシピ（Area::vfxType）毎に数える数（SwarmLiquidTrackCS、音を鳴らす用。2026-10-03）。
    // レシピ番号がこれ以上の物は最後の枠にまとめる
    constexpr uint32_t kAreaBirthKinds = 127u;
    constexpr uint32_t kAreaBeamShift = 8u;      // (flags >> 8) & 0xF = 光線のチャンネル（BeamCB の添字）。
                                                 // 光線でない範囲ではここが「吹き飛ばしの強さ q / 15」（2026-10-12 突風、SwarmAreaPush）
    constexpr float kPushSpeed = 28.0f;          // 吹き飛ばしの初速 m/s（HLSL の SWARM_PUSH_SPEED と同じ。三角形に減衰するので距離 = 速さ × 秒 / 2 = 3.5m）
    constexpr float kPushTime = 0.25f;           // 減衰しきるまでの秒（SWARM_PUSH_TIME）
    // 減速（2026-10-01、毒の池）: (flags >> 12) & 0xF = q、tick の度に中の敵を q / 15 だけ遅くする（エリート・Boss は半分）
    constexpr uint32_t kAreaSlowShift = 12u;
    constexpr uint32_t kMaxBeams = 4;            // 同時に出せる光線（HLSL の SWARM_MAX_BEAMS と同じ）
    constexpr uint32_t kHitAreaOnExpire = 1u;    // Motion::hitAreaFlags
    constexpr uint32_t kHitAreaElementShift = 20u;   // bits 20〜23 = 弾の属性（ダメージの数字の色。DamageElement。2026-10-17）
    // 範囲の追加値（2026-10-17、毒沼）: Area には空きが無く flags も埋まっているので、GPU が書く tickNow の上位ビットに乗せる。
    // 下位 1 バイトは AreaTickCS の tick 状態（HLSL の SWARM_AREA_TICK_*）、CPU から出す時は Area::tickNow、
    // GPU が出す時は AreaDef::extra に入れる（AreaProfile::Extra）
    constexpr uint32_t kAreaTickMask = 0xFFu;
    constexpr uint32_t kAreaExtraToxinShift = 8u;   // (tickNow >> 8) & 0xF = q、1 tick の威力の q / 15 を毒素として敵に溜め、1 秒かけて削る
    constexpr uint32_t kAreaExtraSplashOnly = 1u << 12;   // 最初の tick だけダメージ（泥沼：落ちた時の飛沫。残る沼は減速だけ）
    // 引き寄せ（2026-10-22、渦）：flags の吹き飛ばしの 4 bit を「範囲の中心へ引く強さ」として読む（HLSL の SWARM_AREA_EXTRA_PULL）
    constexpr uint32_t kAreaExtraPull = 1u << 13;
    // 吸血（吸血のルーン。2026-10-25）：この範囲が当てた敵の状態に吸血の印を付ける（SwarmDamageTextCS が HP の減りを吸血の累計へ）
    constexpr uint32_t kAreaExtraLifesteal = 1u << 14;
    // 弾の生成依頼の tag（= SpellStats::triggerMask）の最上位 bit：吸血の弾（誘発の環には報告しない。triggerMask は bit 30 まで）
    constexpr uint32_t kTagLifesteal = 1u << 31;
    // 吸血の割合（ルーンを重ねても増えない。GPU には印だけ渡すので 1 つの値。LifestealHeal と ItemDatabase が使う）
    constexpr float kLifestealRatio = 0.03f;
    constexpr uint32_t kAreaExtraElementShift = 16u;   // bits 16〜19 = 範囲の属性（ダメージの数字の色。2026-10-17）
    constexpr float kToxinTime = 1.0f;           // 毒素 1 回分が削り切られる秒（HLSL の SWARM_TOXIN_TIME）

    struct Area
    {
        Vector3  center;
        float    radius = 3.0f;
        float    damage = 0.0f;          // 1 tick あたり
        float    timeLeft = 0.0f;
        float    tickInterval = 0.25f;
        float    tickTimer = 0.0f;       // 0 = 出た最初のステップで tick する
        float    halfHeight = 1.5f;
        uint32_t flags = 0;
        uint32_t vfxType = 0;            // GPU 側で粒子を出すレシピ。0 = 出さない（CPU が VFX を再生する）
        uint32_t tickNow = 0;            // 下位 1 バイトは GPU が書く tick 状態。上位は出す時に入れる追加値（kAreaExtra*。2026-10-17）
    };
    static_assert(sizeof(Area) == 48, "SwarmArea layout mismatch");

    struct AreaDef
    {
        float    radius = 3.0f;
        float    halfHeight = 1.5f;
        float    damage = 0.0f;
        float    duration = 0.3f;
        float    tickInterval = 1.0e9f;
        uint32_t flags = 0;
        uint32_t vfxType = 0;
        uint32_t extra = 0;              // 出た範囲の tickNow の上位へ（毒素の割合・飛沫だけ。kAreaExtra*）
    };
    static_assert(sizeof(AreaDef) == 32, "SwarmAreaDef layout mismatch");
    
    struct Orb
    {
        Vector3  position;
        float    amount = 0.0f;
        Vector3  velocity;
        float    _pad = 0.0f;
    };
    static_assert(sizeof(Orb) == 32, "SwarmOrb layout mismatch");

    // ============================================================
    // 毎フレームの定数（b0）
    // ============================================================
    struct FrameCB
    {
        Vector3  playerPos;
        float    playerRadius = 0.4f;

        float    step = kFixedStep;
        uint32_t maxEnemies = kMaxEnemies;
        uint32_t maxProjectiles = kMaxProjectiles;
        uint32_t maxOrbs = kMaxOrbs;

        Vector3  gridOrigin;
        float    cellSize = 2.0f;

        uint32_t gridW = 0;
        uint32_t gridD = 0;
        uint32_t seed = 0;
        uint32_t playerAlive = 1;
    };
    static_assert(sizeof(FrameCB) == 64, "SwarmFrameCB layout mismatch");

    // ============================================================
    // 生成用の定数（b1）
    // ============================================================
    struct SpawnCB
    {
        uint32_t requestCount = 0;
        uint32_t scanStart = 0;    // 毎フレーム回して探索開始位置をずらす
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(SpawnCB) == 16, "SwarmSpawnCB layout mismatch");
    // ============================================================
// 雑魚 AI の調整値（b1）
// ChaseAISystem の public メンバをそのまま持ってきた。
// 雑魚は全員同じ体格なので半径もここ（本体の 48B を崩さない）
// ============================================================
    struct AICB
    {
        float separationRadius = 1.2f;
        float separationPower = 4.0f;
        float avoidPower = 2.0f;        // 2026-10-20：6 → 2。流場が木を避けるので、追いかける速度（3.5）を上回る押しは要らない（木の隣の台地の角へ押し込んでいた）
        float lookAhead = 0.35f;        // 秒。速度 × これ で先読み距離

        float groundY = 0.9f;
        float enemyRadius = 0.4f;       // カプセルの半径
        float enemyCapsuleHalf = 0.5f;  // 直線部分の半分
        float velocityLag = 10.0f;      // 速度の追従係数。大きいほど機敏（∞ = 即時）

        float maxSpeedMul = 1.5f;       // 合成速度の上限 = moveSpeed × これ
        float turnSpeed = 12.0f;        // 旋回の上限（rad/s）
        float playerPushOut = 10.0f;    // プレイヤーに食い込んだ時に押し戻す強さ（1/s）
        float contactDamage = 15.0f;    // 接触1回のダメージ（Megabonk 1 面の雑魚 8〜25）

        float attackInterval = 1.0f;    // 同じ雑魚が次に殴れるまでの秒数
        float playerCapsuleHalf = 0.5f; // プレイヤーカプセルの直線部の半分。シーンが毎フレーム入れる
        float hitStun = 0.08f;          // 被弾で止まる秒数（HitCS が animIndex=2 を立て、MoveCS が数える）
        float hitFlash = 3.0f;          // 被弾直後の頂点色の倍率。1 へ減衰。Bloom で光る
    };
    static_assert(sizeof(AICB) == 64, "SwarmAICB layout mismatch");

    // ============================================================
    // 自爆兵の調整値（ContactCS は b3、雑魚の VS は b5）
    // 爆発のダメージはプレイヤーだけ。雑魚には入らない（見た目の範囲は威力 0）
    // ============================================================
    struct BomberCB
    {
        float    fuseTime = 1.0f;       // 点火から爆発までの秒数
        float    triggerMargin = 0.15f; // 接触（半径の和）+ これ以内で点火
        float    blastRadius = 2.5f;    // 爆発の瞬間にプレイヤー（カプセル）がこの中なら被弾
        float    blastDamage = 30.0f;     // Megabonk の Boomer と同じ

        uint32_t blastArea = 0;         // 爆発の見た目に出す範囲（AreaDef の番号）。0 = 出さない
        float    swell = 0.35f;         // VS: 爆発直前の膨らみ（1 + これ 倍まで）
        float    flashGain = 4.0f;      // VS: 点滅の明るさ（Bloom で光る）
        float    _pad = 0.0f;

        // ---- エリート（kEnemyKindElite）。種類の規則を読む pass はこの CB を持っているので相乗りする ----
        float    eliteScale = 2.2f;       // 体格（モデル・当たり / 接触の半径・HP 条の高さ）
        float    eliteDamageMul = 40.0f / 15.0f;   // 接触ダメージの倍率（Megabonk の小ボス 40 / 雑魚 15）
        float    eliteExpMul = 20.0f;     // 落とす経験値オーブの倍率（1 個で雑魚 20 体分）
        float    _elitePad = 0.0f;

        // ---- 面の Boss（kEnemyKindBoss）----
        float    bossScale = 3.0f;           // 4 だと目の前に来た時に画面の大半を塞ぐ
        float    bossDamageMul = 40.0f / 15.0f;   // Megabonk の面 Boss 22〜40 / 雑魚 15
        float    bossExpMul = 100.0f;
        float    _bossPad = 0.0f;

        // ---- 最終ウェーブの幽霊（kEnemyKindGhost）----
        float    ghostHover = 0.7f;          // VS: 地面からこれだけ浮く（判定の位置は地面のまま）
        float    ghostAlpha = 0.55f;         // VS: 頂点 alpha（alpha blend で描く）
        float    ghostGlow = 1.6f;           // VS: 青白の色に掛ける HDR の倍率
        float    ghostDamageMul = 1.5f;      // 接触ダメージの倍率（2026-10-18 に 1.0 → 1.5：最終ウェーブで死ななかった）

        // ---- スプリッター（kEnemyKindSplitter）と分裂体（kEnemyKindSplitling）。2026-10-03 ----
        float    splitterScale = 1.15f;      // 少し大きい
        float    splitterDamageMul = 1.0f;
        float    splitterExpMul = 1.5f;      // HP は雑魚の 2 倍、分裂体の分も足すと雑魚 2.4 体分
        float    _splitterPad = 0.0f;
        float    splitlingScale = 0.6f;
        float    splitlingDamageMul = 0.5f;
        float    splitlingExpMul = 0.3f;
        float    _splitlingPad = 0.0f;

        // ---- Boss の突進（2026-10-07、BossAttacks が毎フレーム書く）----
        // bossChargeOn = 1 の間、Boss は流れ場・追跡を無視して (dirX, dirZ) × bossChargeSpeed で真っ直ぐ動く
        // （速さ 0 = その場で溜め）。壁は今まで通り止める。プレイヤーにぶつかっても止まらない（突き抜ける）
        float    bossChargeDirX = 0.0f;
        float    bossChargeDirZ = 1.0f;
        float    bossChargeSpeed = 0.0f;
        float    bossChargeOn = 0.0f;

        // ---- 重装兵（kEnemyKindBrute。2026-10-07 夜）----
        float    bruteScale = 1.3f;          // 体格（モデル・当たり / 接触の半径）
        float    bruteDamageMul = 1.6f;      // 接触ダメージ（Megabonk の Goblin Tank 30 / 雑魚 15 の手前）
        float    bruteExpMul = 3.0f;         // HP が雑魚の 3 倍なので経験も 3 倍
        float    _brutePad = 0.0f;

        // ---- 突撃兵（kEnemyKindCharger。2026-10-08）----
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

        // ---- 盾兵（kEnemyKindShield。2026-10-08）----
        float    shieldScale = 1.1f;
        float    shieldDamageMul = 1.2f;
        float    shieldExpMul = 2.0f;
        float    shieldArmor = 5.0f;          // 1 発（弾 1 発・範囲の 1 tick）毎にこれだけ引く
        float    shieldMinFrac = 0.2f;        // ただし元のダメージのこの割合は必ず通る

        // ---- 凍結（アイスランス。2026-10-08）----
        float    freezeBigMul = 0.5f;         // エリート・Boss の凍結の長さに掛ける
        float    freezeImmunity = 3.0f;       // 解けてからこの秒数は凍らない（凍らせ続けられないように）
        float    freezeTint = 1.0f;           // VS: 凍った体の氷色の強さ（0 = 色を変えない）

        // ---- トゲ玉（kEnemyKindSpikeBall。2026-10-17）----
        float    spikeBallScale = 1.4f;       // 体格（当たり / 接触の半径。見た目の半径は SwarmSystem::spikeBallLook.radius × これ）
        float    spikeBallDamageMul = 1.0f;   // 接触ダメージ（弾き飛ばしが本体）
        float    spikeBallExpMul = 4.0f;      // HP 60 = 雑魚 4 倍
        // MoveCS：体格 1 の玉が 1m 転がる毎に回る角度（rad）= spikeBallLook.rollRate / radius。SwarmSystem が毎ステップ入れる（2026-10-25、元 _pad）
        float    spikeBallRollPerMeter = 1.0f / 0.6f;

        // ---- 砂潜り（kEnemyKindBurrower。2026-10-17）----
        float    burrowScale = 1.0f;
        float    burrowDamageMul = 2.0f;      // 顔を出した時の一噛み（1 回だけ）
        float    burrowExpMul = 2.0f;
        float    burrowSurfaceDist = 2.0f;    // プレイヤーがこの距離に入ったら顔を出す（m）
        float    burrowSurfaceTime = 3.0f;    // 出ている秒 = 潜りのクールダウン（噛んだ後もここまで外で動かず、撃てる。ユーザー 10-17「潜行要 CD」）
        float    burrowMinBuried = 1.5f;      // 潜ってからこの秒数は出ない
        float    burrowBuriedSpeedMul = 1.3f; // 砂の下の速さ
        float    burrowSurfaceSpeedMul = 0.8f;// 出ている時の速さ
        float    burrowBiteRadius = 2.4f;     // 出た時の一噛みの届く範囲（体の中心から m。surfaceDist より広く = 止まっていれば必ず当たる）
        float    burrowBiteDelay = 0.3f;      // 出てから噛むまでの秒（体が出て来る間。ここで離れれば避けられる）
        float    _burrowPad[2] = {};

        // ---- 砂嵐の術師（kEnemyKindSandMage）と砂旋（kEnemyKindTornado）。2026-10-17 ----
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

        // ---- 黒幽霊（kEnemyKindDarkGhost。2026-10-18）。HP・速さは MobSpawner の darkGhost* ----
        float    darkGhostScale = 1.3f;       // 体格（白幽霊は 1）
        float    darkGhostDamageMul = 3.0f;   // 接触ダメージの倍率（白幽霊は ghostDamageMul）
        float    darkGhostExpMul = 10.0f;     // 倒せた時の経験値（HP 40 倍なので滅多に無い）
        float    darkGhostAlpha = 0.85f;      // VS: 頂点 alpha（白幽霊より濃い）

        // ---- 突撃爆弾兵（kEnemyKindDashBomber。2026-10-18）。HP・平時の速さは MobSpawner の m_DashBomber* ----
        float    dashBomberScale = 1.0f;
        float    dashBomberDamageMul = 1.2f;  // 爆発のダメージ = 自爆兵の爆発 × これ
        float    dashBomberExpMul = 1.5f;     // 点火前に倒せた時の経験値
        float    dashBomberTrigger = 9.0f;    // プレイヤーがこの距離（m）に入ると点火して突っ込む
        float    dashBomberFuse = 1.0f;       // 溜めが終わってから爆発までの秒 = 突っ込む時間（12 m/s × 1.0 = 12m、点火距離 9m を走り抜ける）。触れればその場で爆発
        float    dashBomberDashSpeed = 12.0f; // 突っ込む速さ m/s（突撃兵 14、プレイヤーのスライディング 13 なら振り切れる）
        float    dashBomberBlastRadius = 2.5f;
        float    dashBomberWindup = 0.45f;    // 点火してから走り出すまでの溜め秒（その場で赤く点滅。2026-10-20）

        // ---- 面毎の Boss（2026-10-24。BossAttacks::Configure / Output::bossBurrow をシーンが毎フレーム書く）----
        float    bossTint[3] = { 1.1f, 0.5f, 1.5f };   // 雑魚の網の色に掛ける（草原 = 紫、砂漠 = 砂岩）
        float    bossBurrow = 0.0f;           // 0 = 地上、1 = 砂の下（砂の守護者の潜砂。AICS が Boss の fuse へ -burrow で写す）

        // ---- 呪いの鎧（kEnemyKindArmor / ArmorRevived。2026-10-25）。HP・速さ・蘇るまでの秒は MobSpawner ----
        float    armorScale = 1.25f;
        float    armorDamageMul = 1.5f;
        float    armorExpMul = 1.0f;          // 1 回目の死（崩れるだけ）
        float    armorRevivedExpMul = 2.5f;   // 蘇った鎧を倒した時

        // ---- 石像ガーゴイル（kEnemyKindGargoyle。2026-10-25）----
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
        float    gargoyleDiveMin = 4.5f;      // 飛んでいる間はプレイヤーからこれ〜diveRange × 0.9 の間を回る（真上から急降下すると頭の上を素通りした）

        // ---- 死霊術師（kEnemyKindNecro）と骸骨（kEnemyKindSkeleton）。2026-10-25 ----
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

        // ---- 鎖の番人（kEnemyKindWarden。2026-10-25）。鎖そのもの（飛ぶ速さ・当たり・引き寄せ）は CPU（シーン）----
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
        // ---- 面の固定エリート（kEnemyKindEliteA / EliteB。2026-10-25）。体格は面毎（EliteAttacks::Configure が入れる）----
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
    };
    static_assert(sizeof(BomberCB) == 752, "SwarmBomberCB layout mismatch");

    // 固定エリートへの指示（BomberCB::eliteCmd。HLSL の SWARM_ELITE_* と同じ値）
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

    // ============================================================
    // 固定エリートの様子（GPU → CPU。SwarmEnemyKindList2CS が毎フレーム書き、staging でリードバック。2026-10-25）
    // 先頭 16B = 数、以降 32B × kMaxEliteInfo（槽・種類・HP・最大 HP（固定小数）・位置・fuse）
    // ============================================================
    constexpr uint32_t kMaxEliteInfo = 4;
    struct EliteInfo
    {
        uint32_t slot = 0;
        uint32_t kind = 0;
        uint32_t hp = 0;
        uint32_t maxHp = 0;
        float    pos[3] = {};
        float    fuse = 0.0f;
    };
    static_assert(sizeof(EliteInfo) == 32, "SwarmEliteInfo layout mismatch");
    struct EliteInfoBlock
    {
        uint32_t count = 0;
        uint32_t _pad[3] = {};
        EliteInfo e[kMaxEliteInfo];
    };
    static_assert(sizeof(EliteInfoBlock) == 16 + 32 * kMaxEliteInfo, "SwarmEliteInfoBlock layout mismatch");

    // ============================================================
    // 分裂の環（GPU → CPU）。SwarmCorpseTrackCS がスプリッターの死を見つけたら 1 件書く（先頭 16B = 今までの総数、
    // 以降 16B × kMaxSplitEvents）。CPU は誘発の環と同じくリードバックして差分を取り、MobSpawner が分裂体を湧かせる
    // ============================================================
    constexpr uint32_t kMaxSplitEvents = 128;
    struct SplitEvent
    {
        Vector3  position;    // 死んだ所（敵の位置 = 地面 + groundY）
        uint32_t kind = 0;    // 種類：kEnemyKindSplitter = スプリッターの死（分裂体を出す）、kEnemyKindSandMage = 術師の詠唱（砂旋を出す。2026-10-17）
    };
    static_assert(sizeof(SplitEvent) == 16, "SwarmSplitEvent layout mismatch");

    // ============================================================
    // ダメージの数字（GPU → CPU。2026-10-17）。SwarmDamageTextCS が槽毎の hp の減りをまとめて 1 件書く
    // （先頭 16B = 総数、以降 16B × kMaxDamageTexts）。CPU は分裂の環と同じくリードバックし、UI/DamageNumbers が描く
    // ============================================================
    constexpr uint32_t kMaxDamageTexts = 512;
    struct DamageText
    {
        Vector3  position;    // 頭の上
        uint32_t packed = 0;  // 下 24 bit = 量 × 10、kDtxPoison / kDtxKill
    };
    static_assert(sizeof(DamageText) == 16, "SwarmDamageText layout mismatch");
    constexpr uint32_t kDtxAmountMask = 0x00FFFFFFu;
    constexpr uint32_t kDtxPoison = 1u << 24;   // 大半が毒素の削り → 緑
    constexpr uint32_t kDtxKill = 1u << 25;     // その敵が死んだ → 大きく
    constexpr uint32_t kDtxElementShift = 26u;   // bits 26〜29 = 最後に当てた物の属性（DamageElement）
    // ダメージの属性（ProjectileProfile / AreaProfile の element。数字の色だけに使う。順番は HLSL の SwarmCommon と同じ）
    enum class DamageElement : uint32_t { None = 0, Fire, Ice, Lightning, Arcane, Earth, Poison, Wind, Light, Count };
    inline const char* DamageElementName(uint32_t e)
    {
        static const char* kNames[] = { "none", "fire", "ice", "lightning", "arcane", "earth", "poison", "wind", "light" };
        return e < (uint32_t)DamageElement::Count ? kNames[e] : "?";
    }

    // ============================================================
    // Boss の様子（GPU → CPU。SwarmEnemyCompactCS が毎フレーム書き、staging でリードバック）。
    // ============================================================
    // 局所の風源（2026-10-19、ユーザー「風そのものが風の係数を持って世界の風に影響する」）。
    // SwarmWindSourceCS が毎フレーム生きている砂旋から kMaxWindSources 枠の表に書く（空き = radius 0）。
    // 草（GrassVS）・落ち葉（DebrisUpdateCS）・粒子（ParticleUpdateCS）が WindSources.hlsli の式で世界の風に足す。
    // 2026-10-20：一方向へ押す分（pushDir × push、中心で最大・縁で 0）も持てる = 突風（前へ吹く渦）。
    // CPU の風源（SwarmSystem::AddWindBurst）も同じ CS が表に足す。
    // HLSL の WindSource と同じ並び（48B）
    // ============================================================
    constexpr uint32_t kMaxWindSources = 16;
    struct WindSource
    {
        float pos[3];      // 足元の中心
        float radius;      // 届く距離 m（0 = 空き）
        float swirl;       // 芯の縁での接線の速さ m/s（上から見て反時計回り）
        float updraft;     // 芯の近くの上昇の速さ m/s
        float inflow;      // 縁で中心へ向かう速さ m/s
        float fade;        // 0〜1（生まれ / 消え）
        float pushDir[2];  // 押す向き（xz 単位ベクトル。0 = 押さない）
        float push;        // 中心での押す速さ m/s
        float _pad;
    };
    static_assert(sizeof(WindSource) == 48, "WindSource layout mismatch");

    // Boss HP 条・画面外の目印・倒したかの判定に使う（リードバックなので 2〜3 フレーム古い）
    // ============================================================
    struct BossInfo
    {
        uint32_t alive = 0;    // 生きている Boss の数
        uint32_t hp = 0;       // 固定小数（HpFromFixed で戻す）。複数居たら最後に書いた 1 体
        uint32_t maxHp = 0;
        uint32_t ghosts = 0;      // 生きている白幽霊の数（2026-10-18。CompactCS が空いていた語に数える。MobSpawner の幽霊の上限用）
        float    pos[3] = {};
        uint32_t darkGhosts = 0;  // 生きている黒幽霊の数（同上）
    };
    static_assert(sizeof(BossInfo) == 32, "SwarmBossInfo layout mismatch");

    // ============================================================
    // プレイヤーが受けた打撃の向き（GPU → CPU、2026-10-01 ノックバック用）。
    // SwarmContactCS が殴られた / 爆発を受けた度に「敵（爆心）→ プレイヤー」の単位ベクトル × 1000 と回数を足す。
    // counters と同じく GPU 上で永久に累積し、CPU は前回値との差分を取る（消さない。読み損ねても取りこぼさない）
    // ============================================================
    struct PlayerHitInfo
    {
        int32_t  meleeX = 0, meleeZ = 0;   // 近接の向きの和（× kHitDirScale）
        uint32_t meleeCount = 0;
        int32_t  blastX = 0, blastZ = 0;   // 爆発の向きの和
        uint32_t blastCount = 0;
        int32_t  spikeX = 0, spikeZ = 0;   // トゲ玉の向きの和（2026-10-17。爆発より遠く高く飛ぶ）
        uint32_t spikeCount = 0;
        int32_t  windX = 0, windZ = 0;     // 砂旋の向きの和（2026-10-17。高く吹き上げる）
        uint32_t windCount = 0;
    };
    static_assert(sizeof(PlayerHitInfo) == 48, "SwarmPlayerHitInfo layout mismatch");
    constexpr float kHitDirScale = 1000.0f;   // = SwarmContactCS の HIT_DIR_SCALE

    // ============================================================
   // 経験値オーブの調整値（b2）
   // HitCS（生成）と OrbMoveCS（吸引・取得）の両方が読む
   // ============================================================
    struct OrbCB
    {
        float attractRadius = 5.0f;   // ここに入ると吸い寄せ開始（Megabonk の拾う範囲 5）
        float pickupRadius = 0.6f;    // ここまで来たら取得
        float accel = 30.0f;          // 吸い寄せの加速度
        float maxSpeed = 18.0f;

        float amount = 10.0f;         // 1個あたりの経験値（今は全敵共通）
        float orbY = 0.5f;            // オーブの浮遊高さ
        float _pad[2] = {};
    };
    static_assert(sizeof(OrbCB) == 32, "SwarmOrbCB layout mismatch");

    // ============================================================
    // 光線（カプセル型の範囲）の起点 / 終点（b3、AreaTickCS）。CPU が毎フレーム書く。
    // start.w = 半径、end.w = 1 の間だけ生きる（0 にすると GPU 側の範囲が消える）
    // ============================================================
    struct BeamCB
    {
        DirectX::SimpleMath::Vector4 start[kMaxBeams];
        DirectX::SimpleMath::Vector4 end[kMaxBeams];
    };
    static_assert(sizeof(BeamCB) == 128, "SwarmBeamCB layout mismatch");

    // ============================================================
    // 光線の標的（b4、SwarmBeamTargetCS、2026-10-01）。チャンネル毎:
    //   origin.w = asfloat(cmd)（0 = 未使用 / 1 = 開始: seek に一番近い敵を捕まえる / 2 = 追跡: 捕まえた敵が
    //   死んだら・遠くへ行ったら、射程内で光線の向きに一番近い敵へ乗り換える）、dir.w = 射程、seek.w = asfloat(serial)
    // 結果は 32B × チャンネル（slot, serial, valid, pad, pos.xyz, pad）を staging でリードバック
    // ============================================================
    //   3 = 連鎖（2026-10-20、連鎖のルーン × アークボルト）: seek（届いた敵の位置）から SWARM_CHAIN_RADIUS 内で
    //   一番近い「別の」敵（dir.xyz = asfloat(除外する slot × 3) = この連鎖で既に届いた敵）。答えは同じ 32B（valid = 0 なら居ない）
    enum : uint32_t { kBeamTargetIdle = 0, kBeamTargetStart = 1, kBeamTargetTrack = 2, kBeamTargetChain = 3 };
    constexpr uint32_t kBeamNoSlot = 0xFFFFFFFFu;
    struct BeamTargetCB
    {
        DirectX::SimpleMath::Vector4 origin[kMaxBeams];
        DirectX::SimpleMath::Vector4 dir[kMaxBeams];
        DirectX::SimpleMath::Vector4 seek[kMaxBeams];
    };
    static_assert(sizeof(BeamTargetCB) == 192, "SwarmBeamTargetCB layout mismatch");
    struct BeamTarget
    {
        uint32_t slot = 0xFFFFFFFFu;
        uint32_t serial = 0;
        uint32_t valid = 0;
        uint32_t _pad = 0;
        float    pos[3] = {};
        float    _pad2 = 0.0f;
    };
    static_assert(sizeof(BeamTarget) == 32, "SwarmBeamTarget layout mismatch");
    // ============================================================
  // 転送回収用（b1）
  // 生成キューの [offset, offset+count) を、プレイヤーから minDist より
  // 遠い活き雑魚へ上書きする
  // ============================================================
    struct RecycleCB
    {
        uint32_t count = 0;
        uint32_t offset = 0;
        float    minDistSq = 0.0f;
        uint32_t _pad = 0;
    };
    static_assert(sizeof(RecycleCB) == 16, "SwarmRecycleCB layout mismatch");
}
