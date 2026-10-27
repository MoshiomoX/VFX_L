// ============================================================
// CollisionTestSceneTitle.cpp
// タイトル画面（TitleScene = m_TitleScene、2026-10-09 ユーザー：「タイトルとリザルトが不格好」→ 背景は遊ぶ場所そのもの）。
//   ・地形は決まった seed の草原（kTitleSeed）。箱・門・磁石・HUD なし
//   ・プレイヤーは開始地点の近くの、周りが開けた平らな所に立つ（台地の上・崖の際・山頂・洞窟は避ける）
//   ・戦いのスローモーション（同日ユーザー：「立っているだけでは意味が分からない」→「一番派手な場面のスローモーション + カメラの回転」）：
//     背嚢にメテオ・魔導光線などを並べ（無敵・MP 無限）、周り全部から雑魚（一撃で砕ける）を寄せ続け、自動で撃って吹き飛ばす。体力バーは出さない。
//     さらに kMeteorEvery 秒ごとにプレイヤーの周りの群れへメテオを落とす（いつ見ても爆発が開いている）。
//     戦い（物理・アニメ・杖・GPU・粒子・範囲のエフェクト）は kSlowMo 倍の時間で進め、カメラは実時間でプレイヤーの周りを回る。
//     操作は回さない（体は一番近い敵へ向ける）。湧きは MobSpawner でなく SpawnTitleEnemies
//   ・午後遅くの光（天候は照明を上書きしない）、天候の出来事なし
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
    constexpr float kTitleFov = 40.0f;
    constexpr float kCamDistance = 10.0f;     // プレイヤーからカメラまで（水平、m）
    constexpr float kCamHeight = 5.8f;        // 足元からのカメラの高さ。見下ろす：目の高さだと外周の碗の斜面（34m）がどの向きにも背景を塞ぐ
    constexpr float kLookHeight = 0.9f;      // 見る所の高さ（足元から）
    constexpr float kFrameShift = 2.2f;      // 見る所をカメラの左へずらす（m）= プレイヤーが画面の右寄りに立つ
    constexpr float kOrbitSpeedDeg = 9.0f;   // カメラが回る速さ（度 / 実時間の秒。40 秒で 1 周）
    constexpr float kSunFromCamDeg = 45.0f;  // 太陽を最初のカメラの側からどれだけ横へずらすか（正面から照らすと平たい）

    // ---- 戦いのスローモーション ----
    constexpr float kSlowMo = 0.22f;            // 戦いの時間の進み（実時間に掛ける）
    constexpr float kDemoMinDist = 8.0f;        // 敵を出す所：プレイヤーから（m）。周り 360 度
    constexpr float kDemoMaxDist = 15.0f;
    constexpr uint32_t kDemoMaxAlive = 45;      // 場に居る数の上限
    constexpr float kDemoSpawnInterval = 0.25f; // 秒（戦いの時間）
    constexpr int   kDemoBatch = 3;             // 1 回に出す数
    constexpr int   kDemoPrewarm = 40;          // 最初から出しておく数（始めから囲まれている）
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
    PlaceTitleStand();
    ApplyTitleLight(StageConfig::Get(1).light);
    InitTitleDemo();

    // ---- カメラ・メニュー・曲 ----
    m_TitleTime = 0.0f;
    SetCamera(&m_TitleCam);
    UpdateTitleCamera(0.0f);
    m_TitleUI.Initialize(gfx.GetDevice(), gfx.GetContext(), m_ScreenW, m_ScreenH);
    AudioSystem::Get().PlayMusic("title");   // 戦闘・リザルトから戻った時は前の曲から交差フェード
}

// ============================================================
// 立つ所とカメラの向きを選んでプレイヤーを置く（タイトル・ステージ選択で共用。2026-10-21 に InitTitle から分けた）
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
            const float eye = feet.y + kCamHeight;
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
                    open = m_Grid.IsWalkable(x + dx, z + dz) && !zoned[(size_t)(z + dz) * W + (x + dx)];
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
                const Vector3 cam = feet + Vector3(std::sin(a), 0.0f, std::cos(a)) * kCamDistance;
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
// カメラ：プレイヤーの周りを実時間で回る（戦いはスローモーション）。斜め上から見下ろし、プレイヤーは画面の右寄り
// ============================================================
void CollisionTestScene::UpdateTitleCamera(float dt)
{
    (void)dt;
    const float aspect = (m_ScreenH > 0.0f) ? m_ScreenW / m_ScreenH : 16.0f / 9.0f;
    m_TitleCam.Init(kTitleFov, aspect, 0.1f, 2000.0f);

    const float yaw = DirectX::XMConvertToRadians(m_TitleFacing + kOrbitSpeedDeg * m_TitleTime);
    const Vector3 feet = m_TitleFocus;
    Vector3 pos = feet + Vector3(std::sin(yaw), 0.0f, std::cos(yaw)) * kCamDistance;
    pos.y = feet.y + kCamHeight;
    pos.y = (std::max)(pos.y, m_Grid.SampleHeight(pos.x, pos.z) + 0.6f);   // 丘の斜面に潜らない

    // 見る所：足元の少し上を、左へずらして（プレイヤーが画面の右寄りに立つ）
    Vector3 fwd = feet - pos;
    fwd.y = 0.0f;
    fwd.Normalize();
    const Vector3 right = fwd.Cross(Vector3::Up);   // CameraBase::GetRight と同じ向き
    Vector3 look = feet - right * kFrameShift;
    look.y = feet.y + kLookHeight;
    m_TitleCam.LookAt(pos, look, Vector3::Up);
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
        m_WeaponSystem.Update(m_Registry, gdt, m_CollisionSystem);
        m_PlayerVFX.Update(m_Registry, m_Player, gdt, 0.0f, m_WeaponSystem.GetCastFx());   // 詠唱の光（タイトルでも）
        for (const auto& g : m_WeaponSystem.GetGustFx())
        {
            m_Grass.AddGust(g.pos, g.radius, g.dir, g.length);
            m_Debris.AddGust(g.pos, g.radius, g.dir, g.length);
            AddGustWindBurst(g.pos, g.dir, g.length, g.radius);
        }
    }
    m_PlayerAnimSystem.Update(m_Registry, gdt);
    m_SkinnedAnimSystem.Update(m_Registry, gdt);
    m_ClothChainSystem.Update(m_Registry, gdt);
    const Vector3* pp = PlayerPos();
    if (pp)
    {
        const bool grounded = m_Registry.Has<RigidbodyComponent>(m_Player) && m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        m_Grass.Update(gdt, *pp, grounded, false);
    }
    m_GrassFire.Tick(gdt);
    m_Debris.Tick(gdt);
    m_Water.Tick(gdt);
    UpdateWindToys(gdt);   // 転がり草・タンポポ（2026-10-22）
    m_ManaSystem.Update(m_Registry, gdt);

    UpdateTitleCamera(dt);

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
    m_Weather.Update(dt, 0.0f, m_TitleCam.GetPosition(), m_TitleCam.GetForward());
    m_AreaVFX.Update(gdt, pp ? *pp : Vector3::Zero);
    m_ParticleSystem.Flush(gdt, m_TotalTime);
    m_Swarm.CollectLights();
}

// ============================================================
// Update の代わり：景色（スローモーションの戦い）+ メニュー
// ============================================================
void CollisionTestScene::UpdateTitle(float dt)
{
    UpdateTitleWorld(dt, true);

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
        sm.RequestChangeScene(SceneType::STAGE_SELECT);   // マップ × Tier を選んでから出発（2026-10-21、Megabonk に倣う）
        break;
    case TitleScreenUI::kTraining:
        sm.RequestChangeScene(SceneType::SPELL_LAB);
        break;
    case TitleScreenUI::kQuit:
        PostQuitMessage(0);   // 窓の × と同じ終わり方（Esc での即終了は 2026-10-04 に外した）
        break;
    default:
        break;
    }
}

// ============================================================
// 戦いの見本：背嚢に見栄えのする魔法を並べる（shapes 自動テストと同じ並び。全マスに枠）。
//   メテオ（ファイアボール + ストーンショット）・魔導光線（ホーミングボルト + アークボルト）・毒・黄金の矢
// 無敵・MP 無限。曲はタイトルのまま（爆発・命中の音だけ鳴らす）。最初から囲まれているよう近めに出しておく
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
    if (m_Registry.Has<BackpackComponent>(m_Player))
    {
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        while (!bp.items.empty())
            BackpackLogic::Remove(bp, (int)bp.items.size() - 1);

        struct P { ItemID id; int r, c; };
        const P layout[] = {
            { ItemID::Meteor, 2, 2 },        // 十字
            { ItemID::Fireball, 0, 3 },      // メテオの上の腕の右
            { ItemID::StoneShot, 3, 1 },     // メテオの左の腕の下
            { ItemID::Beam, 6, 5 },          // 横 3
            { ItemID::HomingBolt, 5, 4 },    // 光線の上
            { ItemID::ArcBolt, 7, 6 },       // 光線の下
            { ItemID::Poison, 1, 7 },        // 猛毒の沼（2026-10-17 から上級魔法）：(0,7)(1,6)(1,7)(1,8)
            { ItemID::GoldenArrow, 3, 8 },
            { ItemID::Swamp, 2, 7 },         // (2,7)(2,8)：上 = 沼 (1,7)(1,8)
            { ItemID::Spore, 2, 6 },         // 縦 2 (2,6)(3,6)：上 = 沼 (1,6)
        };
        int index[(int)std::size(layout)];
        for (int i = 0; i < (int)std::size(layout); ++i)
            index[i] = BackpackLogic::Place(bp, layout[i].id, layout[i].r, layout[i].c, 0);

        // ---- 機能ルーン（同日ユーザー：「魔法を挟んだだけ。機能型の魔法も足して、もっと派手に」）----
        // メテオは分裂（一度に輪になって落ちる）+ 二重詠唱 + 拡大鏡（大きい爆発）、弾は分裂で扇に、光線は加速で回数を増やす。
        // 置き場所は総当たり：空いたマス × 4 向きに仮に置き、狙いの魔法に影響が届く所のうち、
        // 影響を受ける魔法の数が一番多い所に残す（届かなければ置かない）
        struct R { ItemID rune; int target; };   // target = layout の番号
        const R runes[] = {
            { ItemID::SplitRune, 0 }, { ItemID::DoubleCastRune, 0 }, { ItemID::Magnifier, 0 },
            { ItemID::SplitRune, 4 }, { ItemID::SplitRune, 7 }, { ItemID::HasteRune, 3 },
            { ItemID::Magnifier, 6 }, { ItemID::DoubleCastRune, 5 }, { ItemID::HasteRune, 1 },
            { ItemID::SplitRune, 2 },
        };
        auto influences = [&](int runeIdx, int item)
            {
                if (item < 0) return false;
                const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, item);
                return std::find(inf.begin(), inf.end(), runeIdx) != inf.end();
            };
        int placedRunes = 0;
        for (const R& rr : runes)
        {
            const int target = index[rr.target];
            if (target < 0) continue;
            int bestR = -1, bestC = -1, bestRot = 0, bestCount = 0;
            for (int r = 0; r < BackpackComponent::GRID; ++r)
                for (int c = 0; c < BackpackComponent::GRID; ++c)
                    for (int rot = 0; rot < 4; ++rot)
                    {
                        if (!BackpackLogic::CanPlace(bp, rr.rune, r, c, rot)) continue;
                        const int ri = BackpackLogic::Place(bp, rr.rune, r, c, rot);
                        if (ri < 0) continue;
                        int count = 0;
                        if (influences(ri, target))
                            for (int k = 0; k < (int)std::size(layout); ++k) count += influences(ri, index[k]) ? 1 : 0;
                        BackpackLogic::Remove(bp, ri);   // 一番後ろなので他の番号は変わらない
                        if (count > bestCount) { bestCount = count; bestR = r; bestC = c; bestRot = rot; }
                    }
            if (bestCount > 0 && BackpackLogic::Place(bp, rr.rune, bestR, bestC, bestRot) >= 0) ++placedRunes;
        }
        std::cout << "[Title] runes placed " << placedRunes << " / " << std::size(runes) << std::endl;
        bp.dirty = true;

        // TEMP-TEST: VFXL_TITLE_LOG=1 で並べた物（場所・受けている影響の数）を autotest.log へ
        char env[8] = {};
        if (GetEnvironmentVariableA("VFXL_TITLE_LOG", env, sizeof(env)) > 0)
        {
            char line[128];
            snprintf(line, sizeof(line), "title runes placed %d / %d", placedRunes, (int)std::size(runes));
            AutoTestLog(line);
            for (int i = 0; i < (int)bp.items.size(); ++i)
            {
                const PlacedItem& it = bp.items[i];
                std::string inf;
                for (int k : BackpackLogic::GetInfluencers(bp, i)) inf += " " + std::to_string((int)bp.items[k].id);
                snprintf(line, sizeof(line), "title item %d id %d at %d,%d rot %d influencers [%s ]", i, (int)it.id, it.row, it.col, it.rotation, inf.c_str());
                AutoTestLog(line);
            }
        }
    }
    m_TitleSpawnTimer = 0.0f;
    m_TitleMeteorTimer = 0.3f;
    SpawnTitleEnemies(kDemoPrewarm, 6.0f, kDemoMaxDist);
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
    m_TitleMeteorTimer -= dt;
    if (m_TitleMeteorTimer <= 0.0f && m_Registry.Has<TransformComponent>(m_Player))
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
            impact.y = m_Grid.SampleHeight(impact.x, impact.z) + m_Swarm.GetAIParams().groundY;   // 普段の標的（雑魚の高さ）と同じ
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
        if (m_Swarm.GetNearestEnemy(np, nv, nd) && nd < 25.0f)
        {
            const Vector3 d = np - tf.position;
            if (d.x * d.x + d.z * d.z > 0.01f) target = DirectX::XMConvertToDegrees(std::atan2(d.x, d.z));
        }
        const float diff = std::remainder(target - tf.rotation.y, 360.0f);
        const float step = kDemoTurnRate * dt;
        tf.rotation.y = std::remainder(tf.rotation.y + std::clamp(diff, -step, step), 360.0f);
    }
}

// プレイヤーの周り 360 度（minDist〜maxDist m）の歩けるマスに雑魚を出す。HP は一撃で砕ける kDemoHp、速さは普段 × kDemoSpeedMul
void CollisionTestScene::SpawnTitleEnemies(int count, float minDist, float maxDist)
{
    const Vector3* pp = PlayerPos();
    if (!pp) return;
    const float gy = m_Swarm.GetAIParams().groundY;
    for (int i = 0; i < count; ++i)
        for (int tries = 0; tries < 6; ++tries)
        {
            const float yaw = Rand01() * DirectX::XM_2PI;
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
