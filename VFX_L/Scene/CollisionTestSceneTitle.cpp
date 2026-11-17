// ============================================================
// CollisionTestSceneTitle.cpp
// タイトル画面（TitleScene = m_TitleScene。背景は遊ぶ場所そのもの）。
//   ・地形は決まった seed の 3 つのマップ（kTitleSeed）。草原・砂漠・遺跡の場面を巡回する（CollisionTestSceneTitleShow.cpp）。箱・門・磁石・HUD なし
//   ・プレイヤーは開始地点の近くの、周りが開けた平らな所に立つ（台地の上・崖の際・山頂・洞窟は避ける）
//   ・戦いのスローモーション：場面毎の魔法を背嚢に並べ（無敵・MP 無限）、周り全部から雑魚（一撃で砕ける）を寄せ続け、
//     自動で撃って吹き飛ばす。体力バーは出さない。
//     背嚢にメテオがあれば、さらに kMeteorEvery 秒ごとにプレイヤーの周りの群れへメテオを落とす（いつ見ても爆発が開いている）。
//     戦い（物理・アニメ・杖・GPU・粒子・範囲のエフェクト）は kSlowMo 倍の時間で進め、カメラは実時間でプレイヤーの周りを回る。
//     操作は回さない（体は一番近い敵へ向ける）。湧きは MobSpawner でなく SpawnTitleEnemies
//   ・光と天候は場面毎（WeatherSystem の時刻を固定し、砂漠・遺跡は出来事を起こす）
//   ・プレイヤーは画面の右寄り、左は名前とメニュー（TitleScreenUI）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Scene/RunResult.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Player/PlayerStatsComponent.h"
#include "Swarm/ProjectileProfile.h"
#include "Audio/AudioSystem.h"
#include "Core/Application.h"
#include "Core/VideoCapture.h"

#include <Windows.h>

namespace
{
    constexpr float kSunFromCamDeg = 45.0f;  // 太陽を最初のカメラの側からどれだけ横へずらすか（正面から照らすと平たい）

    // ---- 戦いのスローモーション ----
    constexpr float kSlowMo = 0.22f;            // 戦いの時間の進み（実時間に掛ける）
    constexpr float kDemoMinDist = 8.0f;        // 敵を出す所：プレイヤーから（m）。カメラの反対側の弧
    constexpr float kDemoMaxDist = 15.0f;
    constexpr uint32_t kDemoMaxAlive = 45;      // 場に居る数の上限
    constexpr float kDemoSpawnInterval = 0.25f; // 秒（戦いの時間）
    constexpr int   kDemoBatch = 3;             // 1 回に出す数
    constexpr float kDemoSpawnArcDeg = 220.0f;  // 出す所の弧の広さ（カメラの反対側を中心に）
    constexpr float kDemoHp = 5.0f;             // 雑魚の HP：何が当たっても一撃で砕ける（砕けて飛ぶ破片がスローで見える）
    constexpr float kDemoSpeedMul = 0.55f;      // 雑魚の速さ（普段の湧きの値に掛ける）。遅くして、プレイヤーに届く前に砕ける
    constexpr float kDemoTurnRate = 300.0f;     // 体を敵へ向ける速さ（度 / 秒、戦いの時間）
    constexpr float kMeteorEvery = 0.55f;       // 群れへメテオを落とす間隔（秒、戦いの時間 = 実時間で約 2.5 秒）
    constexpr float kMeteorMinDist = 3.0f;      // 落とす所：プレイヤーから（m）
    constexpr float kMeteorMaxDist = 8.0f;

    float Rand01()
    {
        static std::mt19937 rng(2026);
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    }
}

// ============================================================
// Init の終わり（地形・プレイヤー・天候ができた後）
// ============================================================
void CollisionTestScene::InitTitle()
{
    auto& gfx = Application::Get().GetGraphics();
    m_Mobs.Director().enabled = false;   // 湧かない
    InitTitleDemo();
    InitTitleShow();   // 3 つの地形・最初の場面（立つ所・光・天候・魔法・敵）

    // ---- カメラ・メニュー・曲 ----
    m_TitleTime = 0.0f;
    SetCamera(&m_TitleCam);
    UpdateTitleCamera(0.0f);
    m_TitleUI.Initialize(gfx.GetDevice(), gfx.GetContext(), m_ScreenW, m_ScreenH);
    AudioSystem::Get().PlayMusic("title");   // 戦闘・リザルトから戻った時は前の曲から交差フェード
}

// ============================================================
// 立つ所とカメラの向きを選んでプレイヤーを置く（タイトル・ステージ選択で共用）
// ============================================================
void CollisionTestScene::PlaceTitleStand()
{
    // ---- 立つ所とカメラの向きを一緒に選ぶ：カメラから見てプレイヤーの向こう（背景）が一番開けている組 ----
    // 背景の開け具合 = 画面の幅（カメラの振れも込みで ±54°）の中で、6〜55m 先の地面がカメラの目の高さから
    // どれだけ持ち上がって見えるか（傾き）の最大。小さいほど近くに台地の壁・山頂の崖・洞窟の岩山が立たない。
    // 外周の碗の斜面（55m より先）はどの向きにも同じように立つので数えない（霧で遠い山に見せる）。
    // 立つ所は平原（山頂・洞窟の外）で、7x7 マスが歩ける（木・岩が近くに無い）、3x3 がほぼ平ら、カメラの所の地面が目より低い
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    const int cx = W / 2, cz = D / 2;
    std::vector<uint8_t> zoned((size_t)W * D, 0);
    for (int i : m_TerrainLayout.summitCells) if (i >= 0 && i < W * D) zoned[(size_t)i] = 1;
    for (int i : m_TerrainLayout.mineCells)   if (i >= 0 && i < W * D) zoned[(size_t)i] = 1;

    // 見た目の高さ：洞窟の坑・岩の壁は高さ場では 0 / −10m だが、上に岩の屋根（roofTop）が載っていて山に見える
    const bool hasZones = m_TerrainMap.zone.size() == (size_t)W * D && m_TerrainMap.caveRing.size() == (size_t)W * D;
    auto lookHeight = [&](const Vector3& q)
        {
            if (hasZones)
            {
                int gx, gz;
                m_Grid.WorldToCell(q, gx, gz);
                if (gx >= 0 && gz >= 0 && gx < W && gz < D)
                {
                    const size_t i = (size_t)gz * W + gx;
                    if (m_TerrainMap.zone[i] == 2 || m_TerrainMap.caveRing[i]) return m_TerrainConfig.roofTop;
                }
            }
            return m_Grid.SampleHeight(q.x, q.z);
        };
    auto backgroundRise = [&](const Vector3& feet, float camYawDeg)
        {
            const float eye = feet.y + kTitleCamHeight;
            float worst = -1.0e9f;
            for (float off = -54.0f; off <= 54.0f; off += 18.0f)
            {
                const float a = DirectX::XMConvertToRadians(camYawDeg + off);
                const Vector3 v(-std::sin(a), 0.0f, -std::cos(a));   // カメラ → プレイヤー → 背景
                for (float d = 6.0f; d <= 55.0f; d += 3.0f)
                {
                    const Vector3 q = feet + v * d;
                    worst = (std::max)(worst, (lookHeight(q) - eye) / d);
                }
            }
            return worst;
        };

    Vector3 best = m_Grid.CellToWorld(cx, cz);
    best.y = m_Grid.SampleHeight(best.x, best.z);
    float camYaw = 0.0f, bestScore = 1.0e9f;
    for (int z = cz - 15; z <= cz + 15; z += 1)
        for (int x = cx - 15; x <= cx + 15; x += 1)
        {
            if (x < 3 || z < 3 || x >= W - 3 || z >= D - 3 || zoned[(size_t)z * W + x]) continue;
            bool open = true;   // 周り 9x9 マス（±8m）が歩ける：群れが囲める・回るカメラの手前に木や岩が立たない
            for (int dz = -4; dz <= 4 && open; ++dz)
                for (int dx = -4; dx <= 4 && open; ++dx)
                    open = m_Grid.IsDryWalkable(x + dx, z + dz) && !zoned[(size_t)(z + dz) * W + (x + dx)];
            if (!open) continue;
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const Vector3 c = m_Grid.CellToWorld(x + dx, z + dz);
                    const float h = m_Grid.SampleHeight(c.x, c.z);
                    lo = (std::min)(lo, h);
                    hi = (std::max)(hi, h);
                }
            if (hi - lo > 0.6f) continue;
            // 周り 12m（±6 マス）に段差が無い：台地・高台の上や、崖の際に立たない（緩い丘は 2.5m まで）
            lo = 1.0e9f; hi = -1.0e9f;
            for (int dz = -6; dz <= 6; dz += 2)
                for (int dx = -6; dx <= 6; dx += 2)
                {
                    const Vector3 c = m_Grid.CellToWorld(x + dx, z + dz);
                    const float h = m_Grid.SampleHeight(c.x, c.z);
                    lo = (std::min)(lo, h);
                    hi = (std::max)(hi, h);
                }
            if (hi - lo > 2.5f) continue;

            Vector3 feet = m_Grid.CellToWorld(x, z);
            feet.y = m_Grid.SampleHeight(feet.x, feet.z);
            const float nearCenter = 0.002f * std::sqrt((float)((x - cx) * (x - cx) + (z - cz) * (z - cz)));
            for (int i = 0; i < 24; ++i)
            {
                const float yawDeg = 15.0f * (float)i;
                const float a = DirectX::XMConvertToRadians(yawDeg);
                const Vector3 cam = feet + Vector3(std::sin(a), 0.0f, std::cos(a)) * kTitleCamDistance;
                if (m_Grid.SampleHeight(cam.x, cam.z) > feet.y + 1.0f) continue;   // カメラが丘に埋まる
                const float score = backgroundRise(feet, yawDeg) + nearCenter;
                if (score < bestScore) { bestScore = score; best = feet; camYaw = yawDeg; }
            }
        }
    m_TitleFocus = best;
    const float bestH = best.y;
    std::cout << "[Title] stand (" << best.x << ", " << best.y << ", " << best.z << ") camYaw " << camYaw
        << " background rise " << bestScore << std::endl;

    // m_TitleFacing = カメラの真ん中の向き（プレイヤーから見たカメラの方角）。体の向きは UpdateTitleDemo が敵へ回す
    m_TitleFacing = camYaw;

    if (m_Registry.IsValid(m_Player) && m_Registry.Has<TransformComponent>(m_Player))
    {
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        float lift = 1.3f;
        if (m_Registry.Has<PlayerStatsComponent>(m_Player))
        {
            const auto& st = m_Registry.Get<PlayerStatsComponent>(m_Player);
            lift = st.height * 0.5f + st.radius + 0.05f;
        }
        tf.position = { best.x, bestH + lift, best.z };
        tf.rotation.y = camYaw + 180.0f;   // 最初は画面の奥を向く（あとは一番近い敵へ向く）
        if (m_Registry.Has<RigidbodyComponent>(m_Player))
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        if (m_Registry.Has<HealthComponent>(m_Player))
            m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    }

}

// ============================================================
// 午後遅くの光：草原の昼の照明から、太陽を低く暖かく、霧を遠く薄く。太陽はカメラの側の斜めから
// ============================================================
void CollisionTestScene::ApplyTitleLight(const SceneLighting::Preset& base)
{
    // 天候は照明を上書きしない・出来事を起こさない
    m_Weather.driveLighting = false;
    m_Weather.eventsEnabled = false;
    {
        SceneLighting::Preset p = base;
        p.sunPitch = 30.0f;
        // 光の進む向き（水平）= カメラ → プレイヤーを横へ回した向き。SceneLighting::SunDirection の yaw と同じ決まり
        p.sunYaw = m_TitleFacing + 180.0f + kSunFromCamDeg;
        p.lightColor = { 1.0f, 0.86f, 0.66f };
        p.lightIntensity *= 1.05f;
        p.skyZenith = { 0.12f, 0.30f, 0.70f };
        p.skyHorizon = { 0.95f, 0.72f, 0.48f };
        p.sunGlow = 0.5f;
        // 霧：外周の碗の斜面（崖の絵、高さ 34m）がどの向きにも背景に立つので、遠くを地平線の色へ溶かして「遠い山」に見せる
        p.fogUseHorizon = true;
        p.fogStart = 45.0f;
        p.fogEnd = 115.0f;
        p.fogMax = 0.8f;
        m_Lighting.ApplyPreset(p);
    }
}

// ============================================================
// Update の代わり（操作・湧き・武器・UI・音の選曲は回さない）
// ============================================================
void CollisionTestScene::UpdateTitleWorld(float dt, bool demo)
{
    SceneBase::Update(dt);
    m_TitleTime += dt;   // カメラ・メニューは実時間
    UpdateScreenSize();

    // 戦いはスローモーション：物理・アニメ・杖・GPU・粒子・範囲のエフェクト・草の風を全部 gdt で進める（ステージ選択は等速、見本なし）
    const float gdt = dt * (demo ? kSlowMo : 1.0f);
    m_TotalTime += gdt;

    m_Lighting.SubmitPointLights();
    SubmitTorchLights();
    m_SpellFx.Bats().SubmitLights(m_WeaponSystem.GetBats());   // 吸血コウモリの弱い赤い光

    // ---- 背嚢 → 杖（最初のフレームに組み上がる）----
    m_BackpackAggregate.Update(m_Registry);

    // ---- 体：物理 → 状態 → 杖（自動で撃つ）→ アニメ・マント・草。操作は回さない ----
    m_CollisionSystem.Update(m_Registry);
    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, gdt, m_CollisionSystem);
    m_PlayerStateSystem.Update(m_Registry, gdt);
    if (demo)
    {
        UpdateTitleDemo(gdt);   // 敵を寄せる・メテオを落とす・体を敵へ向ける・MP を満たす
        m_WeaponSystem.lineWaveSpeed = m_SpellFx.IceSpikes().waveSpeed;   // アイススパイクの範囲を見た目のトゲと同じ時刻に出す
        m_WeaponSystem.lineRiseDelay = m_SpellFx.IceSpikes().riseTime * 0.6f;
        m_WeaponSystem.Update(m_Registry, gdt, m_CollisionSystem);
        m_SpellFx.Update(gdt, m_WeaponSystem.GetTriggeredFx(),   // アイススパイクの氷のトゲ
            [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
        m_PlayerVFX.Update(m_Registry, m_Player, gdt, 0.0f, m_WeaponSystem.GetCastFx());   // 詠唱の光（タイトルでも）
        for (const auto& g : m_WeaponSystem.GetGustFx())
        {
            m_Grass.AddGust(g.pos, g.radius, g.dir, g.length);
            m_Debris.AddGust(g.pos, g.radius, g.dir, g.length);
            if (!g.noWind) AddGustWindBurst(g.pos, g.dir, g.length, g.radius, true);
        }
    }
    if (demo) UpdateTitleTornadoes(gdt);   // 砂嵐の術師の砂旋
    m_PlayerAnimSystem.Update(m_Registry, gdt);
    m_SkinnedAnimSystem.Update(m_Registry, gdt);
    m_ClothChainSystem.Update(m_Registry, gdt);
    const Vector3* pp = PlayerPos();
    if (pp)
    {
        const bool grounded = m_Registry.Has<RigidbodyComponent>(m_Player) && m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        m_Grass.Update(gdt, *pp, grounded, false);
    }
    m_GrassFire.Tick(demo ? dt : gdt);   // タイトルの火は実時間で広げる（スローのままだと炎が育たない）
    m_Sand.Tick(gdt);
    m_Miasma.Tick(gdt);
    m_Debris.Tick(gdt);
    m_WindTrees.Tick(gdt);
    m_Water.Tick(gdt);
    UpdateWindToys(gdt);   // 転がり草・タンポポ
    m_ManaSystem.Update(m_Registry, gdt);

    if (IsMenuScene()) UpdateTitleCamera(dt);   // プレイヤーの登場でも使う（カメラは演出が動かす）

    // ---- GPU（雑魚・弾・範囲）・天候の光・粒子・音（曲は選ばない）----
    // 被弾・経験値のリードバックは受け取らない（プレイヤーは傷付かない・育たない）
    PushWind();   // 風と草の火の入力（Flush の前）
    if (pp)
    {
        float radius = 0.4f;
        if (m_Registry.Has<ColliderComponent>(m_Player)) radius = m_Registry.Get<ColliderComponent>(m_Player).radius;
        m_Swarm.Flush(*pp, radius, true, gdt, m_TotalTime);
    }
    m_Audio.Update(gdt, m_Swarm, m_Registry, m_Player, BattleAudio::State{});
    m_Weather.Update(dt, 0.0f, GetCamera()->GetPosition(), GetCamera()->GetForward());
    m_AreaVFX.Update(gdt, pp ? *pp : Vector3::Zero);
    m_ParticleSystem.Flush(gdt, m_TotalTime);
    m_Swarm.CollectLights();
}

// ============================================================
// Update の代わり：景色（スローモーションの戦い）+ メニュー
// ============================================================
void CollisionTestScene::UpdateTitle(float dt)
{
    // 右下の魔法の表が組み替えるのは景色のプレイヤーの背嚢
    m_TitleUI.SetBackpack(m_Registry.IsValid(m_Player) && m_Registry.Has<BackpackComponent>(m_Player) ? &m_Registry.Get<BackpackComponent>(m_Player) : nullptr);
    UpdateTitleCameraInput(dt);  // 右ドラッグ・ホイールのカメラ（風より先：回している間は吹かない）
    UpdateTitleCursorWind(dt);   // マウスの風（次の Flush で効く）
    UpdateTitleWorld(dt, true);
    UpdateTitleShow(dt);         // 場面の巡回

    // TEMP-TEST: 動画の書き出し（VFXL_CAPTURE、自動テスト無し）：黒からの出だしから VFXL_TITLE_SECONDS 秒（既定 8）書き出して閉じる。
    // メニューと操作の案内は TitleScreenUI が描かない（作品紹介の動画のタイトル）
    if (VideoCapture::Get().Enabled())
    {
        static float s_Rec = 0.0f;
        static bool s_Closed = false;
        char env[16] = {};
        const float len = GetEnvironmentVariableA("VFXL_TITLE_SECONDS", env, sizeof(env)) > 0 ? (float)atof(env) : 8.0f;
        if (!s_Closed)
        {
            VideoCapture::Get().SetRecording(true);
            s_Rec += VideoCapture::Get().FrameDt();
            if (s_Rec >= len)
            {
                s_Closed = true;
                VideoCapture::Get().Finish();
                PostQuitMessage(0);
            }
        }
        m_TitleUI.Update(dt);   // 出だしのフェード・名前の動きは進める（選択は捨てる）
        return;
    }

    // ---- メニュー ----
    auto& sm = Application::Get().GetGame().GetSceneManager();
    int picked = m_TitleUI.Update(dt);
    if (m_TitleUI.ConsumeSpellEdited()) m_TitleShow.userEdited = true;
    {
        // TEMP-TEST: VFXL_TITLE_AUTOPICK=<0..1> で 4 秒後にその項目を選ぶ（タイトル → 戦闘 / 実験場の切替の確認。キーを送らない）
        static bool s_Done = false;
        char env[8] = {};
        if (!s_Done && m_TitleTime > 4.0f && GetEnvironmentVariableA("VFXL_TITLE_AUTOPICK", env, sizeof(env)) > 0)
        {
            s_Done = true;
            picked = std::clamp(atoi(env), 0, 1);
        }
    }
    switch (picked)
    {
    case TitleScreenUI::kStart:
        g_RunCarry.Reset();
        sm.RequestChangeScene(SceneType::STAGE_SELECT);   // マップ × Tier を選んでから出発
        break;
    case TitleScreenUI::kTraining:
        sm.RequestChangeScene(SceneType::SPELL_LAB);
        break;
    case TitleScreenUI::kQuit:
        PostQuitMessage(0);   // 窓の × と同じ終わり方
        break;
    default:
        break;
    }
}

// ============================================================
// 戦いの見本の共通の準備：無敵・MP 無限・体力バーと着弾の輪を出さない。曲はタイトルのまま（爆発・命中の音だけ鳴らす）
// ============================================================
void CollisionTestScene::InitTitleDemo()
{
    m_Audio.musicAuto = false;
    m_Swarm.hpBar.enabled = false;   // 体力バーは出さない（群れが赤い棒だらけに見える）
    m_Swarm.dropRing.enabled = false;   // メテオの着弾の警告の輪も出さない（スローだと長く残って地面が黄色い円だらけ）
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
    // 背嚢の魔法と最初の敵は場面毎（StartTitleVignette）
}

// ============================================================
// 毎フレーム（dt = 戦いの時間）：MP を満たす・敵を寄せ続ける・群れへメテオを落とす・体を一番近い敵へ向ける
// ============================================================
void CollisionTestScene::UpdateTitleDemo(float dt)
{
    if (!m_Registry.IsValid(m_Player)) return;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.current = mp.max;
    }

    m_TitleSpawnTimer -= dt;
    if (m_TitleSpawnTimer <= 0.0f)
    {
        m_TitleSpawnTimer = kDemoSpawnInterval;
        if (m_Swarm.GetCounters().aliveEnemies < kDemoMaxAlive)
            SpawnTitleEnemies(kDemoBatch, kDemoMinDist, kDemoMaxDist);
    }

    // ---- 群れへメテオ（杖のメテオとは別。誘発のメテオと同じく着弾点を決めて落とす）----
    // 右下の表でメテオを外したら落とさない
    const bool hasMeteor = m_Registry.Has<BackpackComponent>(m_Player)
        && BackpackLogic::CountPlaced(m_Registry.Get<BackpackComponent>(m_Player), ItemID::Meteor) > 0;
    m_TitleMeteorTimer -= dt;
    if (hasMeteor && m_TitleMeteorTimer <= 0.0f && m_Registry.Has<TransformComponent>(m_Player))
    {
        m_TitleMeteorTimer = kMeteorEvery;
        static const int s_Meteor = ProjectileProfileDB::IndexOf("Meteor");
        if (s_Meteor > 0)
        {
            const ProjectileProfile& prof = ProjectileProfileDB::At(s_Meteor);
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            const float a = Rand01() * DirectX::XM_2PI;
            const float r = kMeteorMinDist + (kMeteorMaxDist - kMeteorMinDist) * Rand01();
            const Vector3 dir(std::sin(a), 0.0f, std::cos(a));   // プレイヤー → 着弾点（石はプレイヤーの側から飛んで来る）
            Vector3 impact = pp + dir * r;
            impact.y = m_Grid.SampleHeight(impact.x, impact.z) + m_Swarm.GetAIParams().centerAboveFeet;   // 普段の標的（雑魚の高さ）と同じ
            m_Swarm.SpawnProjectile(prof.ResolveVFX(), impact, dir * prof.speed, prof.damage, prof.radius, prof.lifetime,
                (uint32_t)s_Meteor, false, 0, true);
            AudioSystem::Get().Play(prof.castSound);
        }
    }

    if (m_Registry.Has<TransformComponent>(m_Player))
    {
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        float target = tf.rotation.y;   // 敵が居なければそのまま
        Vector3 np, nv;
        float nd = 0.0f;
        if (m_Swarm.GetAimTarget(np, nv, nd) && nd < 25.0f)
        {
            const Vector3 d = np - tf.position;
            if (d.x * d.x + d.z * d.z > 0.01f) target = DirectX::XMConvertToDegrees(std::atan2(d.x, d.z));
        }
        const float diff = std::remainder(target - tf.rotation.y, 360.0f);
        const float step = kDemoTurnRate * dt;
        tf.rotation.y = std::remainder(tf.rotation.y + std::clamp(diff, -step, step), 360.0f);
    }
}

// プレイヤーの周り（カメラの反対側の kDemoSpawnArcDeg 度、minDist〜maxDist m）の歩けるマスに雑魚を出す。HP は一撃で砕ける kDemoHp、速さは普段 × kDemoSpeedMul
void CollisionTestScene::SpawnTitleEnemies(int count, float minDist, float maxDist)
{
    const Vector3* pp = PlayerPos();
    if (!pp) return;
    const float gy = m_Swarm.GetAIParams().centerAboveFeet;
    // カメラの反対側（画面の奥）の弧から出す：手前に出るとメニューの文字に重なる
    const float camYaw = DirectX::XMConvertToRadians(TitleCamYawDeg());
    for (int i = 0; i < count; ++i)
        for (int tries = 0; tries < 6; ++tries)
        {
            const float yaw = camYaw + DirectX::XM_PI + (Rand01() - 0.5f) * DirectX::XMConvertToRadians(kDemoSpawnArcDeg);
            const float dist = minDist + (maxDist - minDist) * Rand01();
            const float x = pp->x + std::sin(yaw) * dist;
            const float z = pp->z + std::cos(yaw) * dist;
            if (!m_Grid.IsWalkableAt(Vector3(x, 0.0f, z))) continue;
            float hp = 15.0f, speed = 3.5f;
            m_Mobs.KindStats(Swarm::kEnemyKindMob, hp, speed);
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), kDemoHp, speed * kDemoSpeedMul, Swarm::kEnemyKindMob);
            break;
        }
}
