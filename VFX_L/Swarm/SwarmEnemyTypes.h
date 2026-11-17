// ============================================================
// SwarmEnemyTypes.h
// 雑魚の本体・種類・描画リスト・種類ごとの状態・砕け散りの環。
// HLSL 側は Shader/Common/SwarmEnemyTypes.hlsli（SwarmEnemy / SwarmEnemyExtra / SwarmCorpse）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

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
        uint32_t animIndex = 0;   // 0=待機 1=歩行 kAnimStun(2)=被弾（HitCS が書く。animTime が経過秒。VS が閃光に使う）
        // 接触攻撃の残り待ち時間（ContactCS だけが書く。0 以上）
        float    attackCooldown = 0.0f;
    };
    static_assert(sizeof(Enemy) == 48, "SwarmEnemy layout mismatch");

    // ============================================================
    // 雑魚の生成依頼（SpawnEnemyCS / RecycleCS が読み、本体と並行バッファへ分けて書く）。
    // HLSL の SwarmEnemySpawn と同じ並び（32B）
    // ============================================================
    struct EnemySpawn
    {
        Vector3  position;
        uint32_t hp = 0;           // 固定小数（HpToFixed）
        float    moveSpeed = 3.5f;
        float    yaw = 0.0f;       // 砂旋の飛ぶ向きなど。他は次の AI のステップが決める
        uint32_t kind = 0;         // kEnemyKind*（EnemyExtra::kind へ）
        uint32_t _pad = 0;
    };
    static_assert(sizeof(EnemySpawn) == 32, "SwarmEnemySpawn layout mismatch");

    // ============================================================
    // 雑魚の被弾の記録（スロットと同じ添字の並行バッファ enemyHitInfo。HLSL の SwarmEnemyHitInfo と同じ並び、16B）。
    // GPU だけが読み書きする（HitCS / AreaDamageCS が書き、DamageTextCS と AreaDamageCS が読む）
    // ============================================================
    struct EnemyHitInfo
    {
        uint32_t lastHit = 0;       // 最後の命中（とどめ以外）の属性 + 吸血の印
        uint32_t killElement = 0;   // とどめの一撃の属性
        float    bladeTime = 0.0f;  // 最後に回る剣が当たった時刻
        float    iceTime = 0.0f;    // 最後に氷の範囲が当たった時刻
    };
    static_assert(sizeof(EnemyHitInfo) == 16, "SwarmEnemyHitInfo layout mismatch");

    // ============================================================
    // 雑魚の種類（Enemy 本体の 48B は変えず、スロットと同じ添字の並行バッファに持つ）
    //   生成依頼では EnemySpawn::kind で運ぶ（SpawnEnemyCS / RecycleCS が並行バッファへ移す）
    // ============================================================
    constexpr uint32_t kEnemyKindMob = 0;      // 普通の雑魚（接触で殴る）
    constexpr uint32_t kEnemyKindBomber = 1;   // 自爆兵（接触で点火 → fuseTime 秒後に爆発）
    constexpr uint32_t kEnemyKindElite = 2;    // エリート（大きい雑魚。KindCB の elite* で体格・接触ダメージ・経験値を倍にする）
    constexpr uint32_t kEnemyKindBoss = 3;     // 面の Boss（KindCB の boss*。怯まない。HP と位置は BossInfo で CPU へ）
    constexpr uint32_t kEnemyKindGhost = 4;    // 最終ウェーブの幽霊：速い、壁も台地も素通り、半透明の青白
    constexpr uint32_t kEnemyKindSplitter = 5; // スプリッター：死ぬと小さい分裂体を 3 体出す（SwarmCorpseTrackCS → 敵の出来事の環 → MobSpawner）
    constexpr uint32_t kEnemyKindSplitling = 6;// 分裂体：スプリッターの小さい子。もう分裂しない
    constexpr uint32_t kEnemyKindBrute = 7;    // 重装兵：HP 3 倍・遅い・大きい
    // 突撃兵：近付くと止まって溜め（地面に赤い予告の帯）→ 向きを固定して一直線に突進 → 息切れで立ち止まる。
    // 段階は EnemyExtra::fuse に持つ（SwarmEnemyAICS が書く。HLSL の SwarmChargerPhase）
    constexpr uint32_t kEnemyKindCharger = 8;
    // 盾兵：受ける 1 発毎のダメージから KindCB::shieldArmor を引く（最低 shieldMinFrac 倍は通る）。
    // 毒の沼・光線の細かい tick はほとんど通らず、一発の重い魔法がよく効く。身体の前に塔の盾（SwarmShieldVS）
    constexpr uint32_t kEnemyKindShield = 9;
    // トゲ玉（砂漠の敵）：厚くて遅い転がるトゲの玉。触れたプレイヤーを弾き飛ばす（PlayerHitInfo の spike）。
    // 怯まない・突風で押せない。雑魚のメッシュは描かず SwarmSpikeBallVS が球 + トゲを描く。HP・速さは MobSpawner::m_SpikeBall*
    constexpr uint32_t kEnemyKindSpikeBall = 10;
    // 砂潜り（砂漠の敵）：砂の下を進む（砂の山だけ見える・当たらない・噛まない・狙われない）、プレイヤーの
    // 足元で顔を出し、burrowBiteDelay 秒後に周り burrowBiteRadius を 1 回噛んで（ダメージ ×2 + 小さなノックバック）、
    // burrowSurfaceTime 秒でまた潜る（出ている間は動かない・追わない）。EnemyExtra.fuse > 0 = 出ている残り秒、<= 0 = -(潜っている秒)
    constexpr uint32_t kEnemyKindBurrower = 11;
    // 砂嵐の術師（砂漠の敵）：プレイヤーから距離を取り
    // （sandMageKeepMin〜Max の間に立つ）、sandMageWindup 秒溜めて砂旋（kEnemyKindTornado）を 1 つ放つ。放つ瞬間は AICS が
    // 敵の出来事の環（enemyEvents）に書き、CPU（MobSpawner::SpawnFromEvents）がプレイヤーへ向けて湧かせる。EnemyExtra.fuse > 0 = 溜めの経過秒、< 0 = クールダウンの残り
    constexpr uint32_t kEnemyKindSandMage = 12;
    // 砂旋：敵の「弾」を敵の 1 種類として実装した物。yaw の向きへ moveSpeed で一直線（幽霊と同じく壁も台地も素通り）、
    // 撃っても当たらない・狙われない・HP バー無し、触れたプレイヤーを高く吹き飛ばす（PlayerHitInfo の wind）、tornadoLife 秒で消える
    // （ContactCS が DEAD にする。撃破に数えない・経験値無し・砕け散らない、死亡の土煙だけ出る）。体は描かない
    // （見えるのは足元の砂埃 SwarmTornadoEmitCS と、風源 SwarmWindSourceCS で巻かれる草・粒子）
    constexpr uint32_t kEnemyKindTornado = 13;
    // 黒幽霊：最終ウェーブの時間切れ
    // finalDarkGhostStart 秒後から出る、幽霊の上位。HP = 雑魚 × darkGhostHpMul（普通のビルドでは倒せない）、速さ = 雑魚 × darkGhostSpeedMul
    // （走りより速く、スライディングなら振り切れる）、体格 darkGhostScale、接触 × darkGhostDamageMul + 爆発級のノックバック。
    // 白幽霊と同じく壁も台地も素通り・怯まない・凍らない・遅くならない・突風で飛ばない（SwarmIsGhost）。描画は幽霊のリスト（VS が種類で色を変える）
    constexpr uint32_t kEnemyKindDarkGhost = 14;
    // 突撃爆弾兵：プレイヤーが dashBomberTrigger 内に入ると自分で点火し、
    // その瞬間の向き（Enemy.yaw）を固定 → dashBomberWindup 秒その場で溜め → 固定した向きへ dashBomberDashSpeed で一直線に突っ込む
    // （向きを固定するので横へ一歩で躱せる）。
    // 触れた瞬間、または溜めの後 dashBomberFuse 秒で爆発（半径 dashBomberBlastRadius、
    // 自爆兵の爆発 × dashBomberDamageMul + 爆発級のノックバック）。**点火中は無敵**（自爆兵も同じ。SwarmInvulnerable：撃っても当たらない・
    // 狙われない・怯まない・凍らない）。EnemyExtra.fuse > 0 = 点火からの秒（AICS が数える）。皮は Kenney Blocky H（灰色ロボット）
    constexpr uint32_t kEnemyKindDashBomber = 15;
    // ---- 遺跡の敵（呪いの鎧・石像ガーゴイル・死霊術師・鎖の番人）----
    // 呪いの鎧：硬くて遅い。1 回目の死では砕けず崩れ落ち（SwarmCorpseTrackCS が敵の出来事の環に書く）、CPU（MobSpawner）が
    // armorReviveDelay 秒後に同じ所へ「蘇った鎧」を HP 半分で湧かせる。蘇った鎧は普通に砕ける
    constexpr uint32_t kEnemyKindArmor = 16;
    constexpr uint32_t kEnemyKindArmorRevived = 17;
    // 石像ガーゴイル：プレイヤーの近くに石像（動かない・撃っても当たらない・狙われない）で湧き、近付かれる / 時間が経つと
    // 目覚めて飛ぶ（壁も台地も素通り、高さ gargoyleHover）→ 近くで向きを固定して急降下（爆発級のノックバック）→ 着地して
    // しばらく地上で殴れる → また飛ぶ。EnemyExtra.fuse（AICS が唯一の書き手）：<= 0 眠り（-経過秒）、(0, wakeTime) 目覚め、
    // [wakeTime, SWARM_GARGOYLE_DIVE) 飛行、>= SWARM_GARGOYLE_DIVE 急降下（溜め → 降下 → 着地）
    constexpr uint32_t kEnemyKindGargoyle = 18;
    // 死霊術師：砂嵐の術師と同じく距離を取って溜め、詠唱の瞬間に敵の出来事の環に書く → CPU が近くの最近の死体の所から
    // 骸骨（kEnemyKindSkeleton）を起こす（死体が無ければ自分の周り）
    constexpr uint32_t kEnemyKindNecro = 19;
    constexpr uint32_t kEnemyKindSkeleton = 20;   // 速くて脆い・経験値少し（死霊術師が起こす）
    // 鎖の番人：中距離で止まり、向きを固定して溜め（地面に予告の帯 = 突撃兵と同じ VS）→ 溜めの終わりに敵の出来事の環に
    // 「種類 | 向き << 16」を書く → CPU（シーン）が鎖の鉤を投げ、当たったプレイヤーを番人の前へ引き寄せる
    constexpr uint32_t kEnemyKindWarden = 21;
    // ---- 面の固定エリート（各面 2 体、どれも「予告を避ける」型）----
    // A = 近接型（草原 大斧の戦鬼 / 砂漠 砂塵の騎兵 / 遺跡 影の剣士）、B = 遠距離型（森の弓手 / 炎砂の術師 / 呪いの司書。
    // KindCB::fixedEliteKeepMin〜Max に立つ）。どの個体かは面で決まり、皮と飾りは面毎に描画リストへ入れる（Enemy/EliteLooks.cpp）。
    // 技の流れ・当たり・予告・アフィックス（疾風 / 鉄壁 / 雷光 / 狂暴）は CPU（Enemy/EliteAttacks）が持ち、KindCB::eliteCmd / eliteMove で
    // 「立つ・決めた向きへ走る・瞬間移動・隠れる・盾で半減・怒りで大きく」を GPU に伝える。生死・HP・位置は EliteInfo で CPU へ
    constexpr uint32_t kEnemyKindEliteA = 22;
    constexpr uint32_t kEnemyKindEliteB = 23;
    inline bool IsFixedElite(uint32_t kind) { return kind == kEnemyKindEliteA || kind == kEnemyKindEliteB; }
    // 敵の出来事（EnemyEvent::kind）の下 16 bit = 種類を取り出すマスク。上 16 bit は出来事の中身（ガーゴイルは下の 3 つ、番人は向き）
    constexpr uint32_t kEnemyEventKindMask = 0xFFFFu;
    constexpr uint32_t kGargoyleEventWake = 0u;   // ガーゴイル：目覚めた
    constexpr uint32_t kGargoyleEventDive = 1u;   // 急降下を始めた（溜めの頭）
    constexpr uint32_t kGargoyleEventLand = 2u;   // 着地した
    // 描画リストの数（種類毎にテクスチャを替えて描く）。エリートは雑魚と同じメッシュ・テクスチャなので雑魚のリストで描き、
    // 大きさと色は VS が種類を見て変える
    constexpr uint32_t kEnemyKinds = 18;
    // 描画リストの添字（種類 → リスト。エリートは雑魚のリスト、分裂体はスプリッターのリスト。
    // Boss は自分のリスト（面毎の皮と飾り。SwarmSystem::SetBossLook））
    constexpr uint32_t kDrawListMob = 0;
    constexpr uint32_t kDrawListBomber = 1;
    constexpr uint32_t kDrawListSplitter = 2;
    constexpr uint32_t kDrawListBrute = 3;    // 重装兵（緑のオーク）
    constexpr uint32_t kDrawListCharger = 4;  // 突撃兵（黒い忍者）
    constexpr uint32_t kDrawListShield = 5;   // 盾兵（紺の警官）
    constexpr uint32_t kDrawListBurrower = 6; // 砂潜り（自分のテクスチャ + 砂の山）
    constexpr uint32_t kDrawListSandMage = 7; // 砂嵐の術師（自分のテクスチャ。砂旋は何も描かないので描画リストを持たない）
    constexpr uint32_t kDrawListDashBomber = 8; // 突撃爆弾兵（灰色ロボット）
    constexpr uint32_t kDrawListBoss = 9;     // 面の Boss（皮は面毎、体の後に飾りも描く）
    // 遺跡の敵。生きている一覧は SwarmEnemyKindList2CS、砕け中の一覧は SwarmCorpseList2CS
    // （Boss・砂嵐の術師・突撃爆弾兵の砕け中もこちら = 自分の皮で砕ける）
    constexpr uint32_t kDrawListArmor = 10;   // 呪いの鎧（蘇った鎧も）
    constexpr uint32_t kDrawListGargoyle = 11;
    constexpr uint32_t kDrawListNecro = 12;
    constexpr uint32_t kDrawListSkeleton = 13;
    constexpr uint32_t kDrawListWarden = 14;
    // 面の固定エリート。生きている一覧は SwarmEnemyKindList2CS、砕け中は SwarmCorpseList3CS
    constexpr uint32_t kDrawListEliteA = 15;
    constexpr uint32_t kDrawListEliteB = 16;
    constexpr uint32_t kDrawListGhost = 17;   // 最後に alpha blend で描く（必ず最後の番号。白幽霊と黒幽霊の両方）

    // EnemyExtra::fuse の意味は種類ごとに違う（書き手は SwarmEnemyAICS。自爆兵 / 突撃爆弾兵の爆発だけ SwarmContactCS）。
    // HLSL の SwarmEnemyExtra の上の表と一致させること：
    //   自爆兵         : 点火からの秒。0 = 未点火（点火した瞬間に 1 ステップ分が入る）
    //   突撃兵         : < 0 = 次の突進までの待ち（0 へ数え上げる）、0 = 追いかけ中、> 0 = 溜め始めてからの秒（SwarmChargerPhase）
    //   砂潜り         : > 0 = 地上にいる残り秒、<= 0 = -(潜っている秒)
    //   砂嵐の術師 / 死霊術師 / 鎖の番人 : 0 = 準備完了、> 0 = 溜めの経過秒、< 0 = クールダウンの残り
    //   砂旋           : 0 = 生成直後、> 0 = 残り秒、< 0 = 寿命切れ
    //   石像ガーゴイル : <= 0 = 眠り（-経過秒）、(0, wakeTime) = 目覚め中、[wakeTime, SWARM_GARGOYLE_DIVE) = 飛行、
    //                    >= SWARM_GARGOYLE_DIVE = 急降下（fuse - SWARM_GARGOYLE_DIVE = 急降下に入ってからの秒）
    //   突撃爆弾兵     : > 0 = 点火からの秒、0 = 未点火
    //   Boss           : -burrow（0..1。KindCB::bossBurrow を AICS が写す。< -0.5 = 砂の下）
    //   固定エリート   : -hide（0..1。KindCB::eliteMove の w を AICS が写す。< -0.5 = 狙われない）
    //   その他         : 使わない（0）
    struct EnemyExtra
    {
        uint32_t kind = kEnemyKindMob;
        float    fuse = 0.0f;   // 種類ごとの状態（上の表）
    };
    static_assert(sizeof(EnemyExtra) == 8, "SwarmEnemyExtra layout mismatch");

    // ============================================================
    // 敵の状態（並行バッファ enemyStatus、float4。xy = 減速、zw = 凍結）
    //   x = 減速の残り秒、y = 減速の強さ（毒の沼。SwarmAreaDamageCS が書く）
    //   z = 凍結の残り秒（> 0 の間は動かない・殴らない）
    //   w = 凍結中：凍結の長さ（VS の出始め / 溶け際の演出用）、解けた後：再凍結できない残り秒
    // 凍結の依頼は SwarmHitCS が Enemy::animIndex = kAnimFreezeRequest、animTime = 秒 で置き、
    // 次のステップの SwarmEnemyAICS が受け取る（Enemy の 48B は変えない）
    // ============================================================
    constexpr uint32_t kAnimFreezeRequest = 3u;
    constexpr uint32_t kAnimStun = 2u;   // Enemy::animIndex：被弾の硬直（HLSL の SWARM_ANIM_STUN）

    // ============================================================
    // 死んだ敵の砕け散り
    //   SwarmCorpseTrackCS が「前のフレームは生きていて今は DEAD」の槽を見つけ、その時の位置・向き・種類を
    //   環（kMaxCorpses 枠、先頭は RAW の通し番号）へ 1 つ書く。描画（SwarmCorpseVS）は部品毎に
    //   (seed, 経過時間) の式で飛び散らせる。状態は持たない。HLSL の SwarmCorpse / SWARM_MAX_CORPSES と一致
    // ============================================================
    constexpr uint32_t kMaxCorpses = 512;
    struct Corpse
    {
        Vector3  position;          // 死んだ時の位置（雑魚の position = 地面 + centerAboveFeet）
        float    yaw = 0.0f;
        float    dir[2] = {};       // 飛ばす向き（xz の単位。プレイヤーから離れる向き）
        float    birth = 0.0f;      // 死んだ時刻（SwarmSystem の時計 m_AnimClock）。0 = 空き
        uint32_t kind = 0;
        uint32_t seed = 0;
        uint32_t _pad0 = 0;         // 未使用（16 バイト境界合わせ）。常に 0
        float    _pad[2] = {};
    };
    static_assert(sizeof(Corpse) == 48, "SwarmCorpse layout mismatch");
}
