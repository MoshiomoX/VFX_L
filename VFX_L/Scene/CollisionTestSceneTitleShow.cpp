// ============================================================
// CollisionTestSceneTitleShow.cpp
// タイトルの場面の巡回とマウスの風。
//   場面：草原・黄昏（竜巻・突風・メテオで枯れ草が燃え広がる）→ 砂漠・砂嵐（竜巻が砂を巻き上げ、砂嵐の術師が砂旋を放つ）
//   → 遺跡・夜（瘴気をメテオ・光線で吹き払い、渦が瘴気を巻き込む）。kTitleVignetteTime 秒毎に暗転して次へ。
//   地形はステージ選択と同じく 3 つとも最初に建てて、切替は出し入れだけ（BuildSelectWorlds / ActivateSelectWorld）。
//   マウス：画面の下の地面に、動かした向き・速さの風源を出す（草・落ち葉・砂・瘴気・粒子が流れる）。
//   パッド：メニューを動かした瞬間にプレイヤーの足元から外へ一吹き
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Component/TransformComponent.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Manager/InputManager.h"
#include "World/StageConfig.h"

#include <Windows.h>

namespace
{
    constexpr float kTitleVignetteTime = 20.0f;   // 1 つの場面の長さ（実時間の秒）
    constexpr float kCurtainOut = 0.4f;           // 暗くなる秒
    constexpr float kCurtainIn = 0.6f;            // 明るくなる秒

    // 場面：マップ・時刻（WeatherSystem の 0 昼 〜 1 夕 〜 1.1 夜）・天候の出来事を起こすか・背嚢・敵
    struct Vignette
    {
        int   stage;
        float timeOfDay;
        bool  weatherEvent;
        ItemID spells[5];
        int   spellCount;
        ItemID runes[4];
        int   runeTargets[4];   // spells の何番目を狙うか
        int   runeCount;
        int   sandMages;        // 砂旋を放つ術師の数
    };
    const Vignette kVignettes[] = {
        // 草原・黄昏：竜巻・突風で草と落ち葉、メテオで枯れ草に火
        { 1, 1.0f, false,
          { ItemID::Meteor, ItemID::Twister, ItemID::Gust, ItemID::Fireball, ItemID::GoldenArrow }, 5,
          { ItemID::SplitRune, ItemID::DoubleCastRune, ItemID::Magnifier, ItemID::HasteRune }, { 0, 0, 1, 2 }, 4, 0 },
        // 砂漠・砂嵐：竜巻が砂面を巻き上げ、メテオが穴をあけ、氷の棘が砂を濡らす
        { 2, 0.35f, true,
          { ItemID::Meteor, ItemID::Twister, ItemID::IceSpike, ItemID::StoneShot, ItemID::Unknown }, 4,
          { ItemID::SplitRune, ItemID::Magnifier, ItemID::DoubleCastRune, ItemID::HasteRune }, { 0, 1, 1, 2 }, 4, 3 },
        // 遺跡・夜：光線・メテオが瘴気を吹き払い、渦が瘴気を巻き込む。雷
        { 3, 1.1f, true,
          { ItemID::Beam, ItemID::Meteor, ItemID::VoidVortex, ItemID::ArcBolt, ItemID::Unknown }, 4,
          { ItemID::HasteRune, ItemID::SplitRune, ItemID::Magnifier, ItemID::DoubleCastRune }, { 0, 1, 2, 2 }, 4, 0 },
    };
    constexpr int kVignetteCount = (int)(sizeof(kVignettes) / sizeof(kVignettes[0]));

    float ShowRand01()
    {
        static unsigned s = 777u;
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / 16777216.0f;
    }
}

// ============================================================
// 最初：3 つの地形を建てて 1 つ目の場面
// ============================================================
void CollisionTestScene::InitTitleShow()
{
    BuildSelectWorlds();   // Debug で +2.5 秒ほど（地形 2 つ分）
    m_TitleShow = TitleShowState{};
    StartTitleVignette(0);
}

// ============================================================
// 場面を始める：前の戦いを全部片付け、地形を出し、光・天候・魔法・敵を整える
// ============================================================
void CollisionTestScene::StartTitleVignette(int index)
{
    index = ((index % kVignetteCount) + kVignetteCount) % kVignetteCount;
    const Vignette& v = kVignettes[index];
    m_TitleShow.index = index;
    m_TitleShow.time = 0.0f;
    m_TitleShow.ignites.clear();

    // ---- 前の場面の片付け（敵・弾・範囲・竜巻・光線・エフェクト）----
    m_Swarm.KillAll();
    m_Swarm.ClearProjectiles();
    m_Swarm.ClearAreas();
    m_WeaponSystem.ClearActive();
    m_SandTornadoes.Clear(m_AreaVFX);
    m_SandTornadoKillSerial = m_Swarm.GetKillAllCount();
    m_AreaVFX.StopAll();

    // ---- 地形（出し入れ）・立つ所 ----
    ActivateSelectWorld(v.stage);

    // ---- 光と天候：時刻を決め、砂漠・遺跡は出来事（砂嵐 / 濃霧と雷）をすぐ強く ----
    const StageDef& def = StageConfig::Get(v.stage);
    m_Weather.Init(def, 600.0f, m_Lighting, m_AreaVFX, &m_VFXContext, &m_Swarm, 1);
    m_Weather.driveLighting = true;
    m_Weather.eventsEnabled = false;
    m_Weather.timeOverride = v.timeOfDay;
    m_Weather.fadeIn = 1.5f;
    if (v.weatherEvent) m_Weather.TriggerNow(1.0e4f);

    // ---- 魔法 ----
    // 右下の表で組み替えていたらそのまま（どの場面でも組んだ魔法で戦う）
    if (!m_TitleShow.userEdited)
        BuildTitleBackpack(v.spells, v.spellCount, v.runes, v.runeTargets, v.runeCount);

    // ---- 敵：最初から囲まれているように。砂漠は離れて立つ術師（死なない）----
    m_TitleSpawnTimer = 0.0f;
    m_TitleMeteorTimer = 0.3f;
    SpawnTitleEnemies(40, 6.0f, 15.0f);
    if (v.sandMages > 0)
    {
        const float gy = m_Swarm.GetAIParams().groundY;
        float hp = 15.0f, speed = 3.0f;
        m_Mobs.KindStats(Swarm::kEnemyKindSandMage, hp, speed);
        for (int i = 0; i < v.sandMages; ++i)
        {
            // カメラの反対側（画面の奥）に扇形に
            const float yaw = DirectX::XMConvertToRadians(m_TitleFacing + 180.0f + (i - (v.sandMages - 1) * 0.5f) * 35.0f);
            const float dist = 11.0f + 2.0f * ShowRand01();
            const float x = m_TitleFocus.x + std::sin(yaw) * dist, z = m_TitleFocus.z + std::cos(yaw) * dist;
            if (!m_Grid.IsWalkableAt(Vector3(x, 0.0f, z))) continue;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 1.0e6f, speed, Swarm::kEnemyKindSandMage);
        }
    }

    // ---- 草原：見える所の枯れ草に先に火を付けておく（スローの戦いでも炎が見えるように）----
    if (m_GrassFire.Active())
    {
        const float back = DirectX::XMConvertToRadians(m_TitleFacing + 180.0f);
        for (int tries = 0; tries < 60 && m_TitleShow.ignites.size() < 4; ++tries)
        {
            const float yaw = back + (ShowRand01() - 0.5f) * 2.2f;
            const float dist = 6.0f + 12.0f * ShowRand01();
            const Vector3 p(m_TitleFocus.x + std::sin(yaw) * dist, 0.0f, m_TitleFocus.z + std::cos(yaw) * dist);
            if (m_GrassFire.IsDry(p.x, p.z)) m_TitleShow.ignites.push_back(p);
        }
    }
}

// ============================================================
// 毎フレーム（実時間）：場面の時間・暗転・切替・火付け
// ============================================================
void CollisionTestScene::UpdateTitleShow(float dt)
{
    // TEMP-TEST: VFXL_TITLE_SHOTS=1（VFXL_SELF_SHOT と一緒に）：場面毎に 2 枚、マウスで名前・地面を掃いた所、
    // 最後に「はじめる」で名前が吹き飛ぶ所を撮って閉じる（マウスは画面上の位置を代わりに入れる）
    {
        static const bool s_On = [] { char e[8] = {}; return GetEnvironmentVariableA("VFXL_TITLE_SHOTS", e, sizeof(e)) > 0; }();
        static float s_T = 0.0f;
        static int s_Next = 0;
        if (s_On)
        {
            s_T += dt;
            const float lt = m_TitleShow.time;
            const int vi = m_TitleShow.index;
            auto shot = [&](const char* name) { char line[96]; snprintf(line, sizeof(line), "title look %s", name); AutoTestLog(line); };
            // 0〜2：場面毎（切替の 1 周目だけ）
            struct S { int v; float t; const char* name; };
            static const S kShots[] = {
                { 0, 5.0f, "v0_grass_a" }, { 0, 8.45f, "logo_sweep" }, { 0, 9.3f, "logo_after" }, { 0, 10.6f, "wind_sweep" },
                { 0, 14.0f, "v0_grass_b" }, { 1, 5.0f, "v1_desert_a" }, { 1, 14.0f, "v1_desert_b" },
                { 2, 5.0f, "v2_ruins_a" }, { 2, 14.0f, "v2_ruins_b" } };
            if (s_Next < (int)(sizeof(kShots) / sizeof(kShots[0])) && kShots[s_Next].v == vi && lt >= kShots[s_Next].t)
                shot(kShots[s_Next++].name);
            if (vi == 0 && !m_TitleShow.switched)
            {
                if (lt >= 8.0f && lt < 8.8f)   // 名前の上を左から右へ
                {
                    const float u = (lt - 8.0f) / 0.8f;
                    InputManager::Get().OnMouseMove((int)(m_ScreenW * (0.05f + 0.40f * u)), (int)(m_ScreenH * 0.17f));
                }
                if (lt >= 10.0f && lt < 11.0f)   // 景色の地面を左から右へ
                {
                    const float u = (lt - 10.0f) / 1.0f;
                    InputManager::Get().OnMouseMove((int)(m_ScreenW * (0.55f + 0.35f * u)), (int)(m_ScreenH * 0.72f));
                }
                // 右下の表：1 つ目の魔法を右クリックで外し、候補から魔導光線をドラッグして空いた所へ置く
                if (m_Registry.Has<BackpackComponent>(m_Player))
                {
                    auto& in = InputManager::Get();
                    const auto& tb = m_TitleUI.TestTable();
                    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
                    static int s_Step = 0;
                    auto move = [&](const Vector2& p) { in.OnMouseMove((int)p.x, (int)p.y); };
                    if (s_Step == 0 && lt >= 14.3f && !bp.items.empty())
                    {
                        for (int r = 0; r < BackpackComponent::GRID && s_Step == 0; ++r)
                            for (int c = 0; c < BackpackComponent::GRID && s_Step == 0; ++c)
                                if (BackpackLogic::GetItemAt(bp, r, c) == 0) { move(tb.TestCellCenter(r, c)); s_Step = 1; }
                    }
                    else if (s_Step == 1 && lt >= 14.45f) { in.DebugMouseButton(1, 1); s_Step = 2; }
                    else if (s_Step == 2 && lt >= 14.6f) { in.DebugMouseButton(1, 0); s_Step = 3; }
                    else if (s_Step == 3 && lt >= 14.8f)
                    {
                        Vector2 p;
                        if (tb.TestPaletteCenter(ItemID::Beam, p)) move(p);
                        s_Step = 4;
                    }
                    else if (s_Step == 4 && lt >= 14.95f) { in.DebugMouseButton(0, 1); s_Step = 5; }
                    else if (s_Step == 5 && lt >= 15.1f)
                    {
                        for (int r = 0; r < BackpackComponent::GRID && s_Step == 5; ++r)
                            for (int c = 0; c < BackpackComponent::GRID && s_Step == 5; ++c)
                                if (BackpackLogic::CanPlace(bp, ItemID::Beam, r, c, 0)) { move(tb.TestCellCenter(r, c)); s_Step = 6; }
                    }
                    else if (s_Step == 6 && lt >= 15.3f) { shot("table_drag"); s_Step = 7; }
                    else if (s_Step == 7 && lt >= 15.45f) { in.DebugMouseButton(0, 0); s_Step = 8; }
                    else if (s_Step == 8 && lt >= 15.6f)
                    {
                        in.DebugMouseButton(0, -1);
                        in.DebugMouseButton(1, -1);
                        int beams = 0;
                        for (const auto& it : bp.items) beams += (it.id == ItemID::Beam) ? 1 : 0;
                        char line[96];
                        snprintf(line, sizeof(line), "title table items %d beams %d", (int)bp.items.size(), beams);
                        AutoTestLog(line);
                        s_Step = 9;
                    }
                    else if (s_Step == 9 && lt >= 17.5f)
                    {
                        char line[64];
                        snprintf(line, sizeof(line), "title table edited %d", m_TitleShow.userEdited ? 1 : 0);
                        AutoTestLog(line);
                        shot("table_after");
                        s_Step = 10;
                    }
                }
            }
            if (vi == 0 && m_TitleShow.switched && lt >= 3.0f)
            {
                static float s_Pick = -1.0f;
                if (s_Pick < 0.0f) { s_Pick = s_T; m_TitleUI.TestPick(0); }
                if (s_T - s_Pick >= 0.2f && s_Next == 9) { shot("burst1"); ++s_Next; }
                if (s_T - s_Pick >= 0.45f && s_Next == 10) { shot("burst2"); ++s_Next; }
                if (s_T - s_Pick >= 0.6f) { AutoTestLog("title done"); PostQuitMessage(0); }
            }
        }
    }

    m_TitleShow.time += dt;
    float curtain = 0.0f;
    if (m_TitleShow.time >= kTitleVignetteTime)
    {
        StartTitleVignette(m_TitleShow.index + 1);   // 真っ暗の間に入れ替える（地形の出し入れで少し止まる）
        m_TitleShow.switched = true;
        curtain = 1.0f;
    }
    else if (m_TitleShow.time > kTitleVignetteTime - kCurtainOut)
        curtain = (m_TitleShow.time - (kTitleVignetteTime - kCurtainOut)) / kCurtainOut;
    else if (m_TitleShow.switched && m_TitleShow.time < kCurtainIn)
        curtain = 1.0f - m_TitleShow.time / kCurtainIn;   // 切替の後（最初の場面は TitleScreenUI が黒から出す）
    m_TitleUI.SetCurtain(curtain);

    if (!m_TitleShow.ignites.empty())
    {
        const Vector3 p = m_TitleShow.ignites.back();
        m_TitleShow.ignites.pop_back();
        m_GrassFire.Ignite(p, 2.5f);
    }
}

// ============================================================
// 砂嵐の術師：湧きは止めたまま MobSpawner を回して詠唱から砂旋を出し、柱（SandTornadoFx）を同じ直線で動かす
// ============================================================
void CollisionTestScene::UpdateTitleTornadoes(float dt)
{
    const Vector3* pp = PlayerPos();
    if (!pp) return;
    m_Mobs.Update(m_Grid, *pp, dt, 0.0f, m_Swarm);
    std::vector<MobSpawner::TornadoBirth> births;
    m_Mobs.ConsumeTornadoBirths(births);
    for (const MobSpawner::TornadoBirth& b : births)
        m_SandTornadoes.Spawn(m_AreaVFX, m_VFXContext, b.pos, b.yaw, b.speed, m_Swarm.GetBomberParams().tornadoLife);
    m_SandTornadoes.Update(dt, m_AreaVFX, [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
}

// ============================================================
// マウスの下の地面へ風源（動かした向き・速さ）。パッドはメニューを動かした瞬間に足元から一吹き
// ============================================================
void CollisionTestScene::UpdateTitleCursorWind(float dt)
{
    if (dt <= 0.0f) return;
    auto& input = InputManager::Get();

    // ---- パッド：メニューの項目を動かした ----
    const int menu = m_TitleUI.MenuCursor();
    if (menu != m_TitleShow.lastMenu)
    {
        if (m_TitleShow.lastMenu >= 0 && input.IsUsingPad())
        {
            SwarmSystem::WindBurst b;
            b.pos = m_TitleFocus;
            b.radius = 6.0f;
            b.swirl = 4.0f;
            b.updraft = 2.0f;
            b.inflow = -4.0f;   // 外へ
            b.life = 0.6f;
            m_Swarm.AddWindBurst(b);
            m_Grass.AddGust(m_TitleFocus, 4.5f);
            m_Debris.AddGust(m_TitleFocus, 4.5f);
        }
        m_TitleShow.lastMenu = menu;
    }
    if (input.IsUsingPad() || m_TitleUI.MouseOverTable()) { m_TitleShow.hasCursor = false; return; }   // 表を触っている間は吹かない

    // ---- マウスの下の地面：画面の点から光線を出し、地面より下に入った所 ----
    const DirectX::XMFLOAT2 mp = input.GetMousePos();
    if (m_ScreenW <= 0.0f || m_ScreenH <= 0.0f) return;
    const float nx = mp.x / m_ScreenW * 2.0f - 1.0f;
    const float ny = 1.0f - mp.y / m_ScreenH * 2.0f;
    const DirectX::SimpleMath::Matrix inv = (m_TitleCam.GetViewMatrix() * m_TitleCam.GetProjectionMatrix()).Invert();
    const Vector3 a = Vector3::Transform(Vector3(nx, ny, 0.0f), inv);
    const Vector3 b = Vector3::Transform(Vector3(nx, ny, 1.0f), inv);
    Vector3 dir = b - a;
    dir.Normalize();
    bool hit = false;
    float lo = 0.0f, hi = 0.0f;
    for (float t = 0.5f; t < 90.0f; t += 0.5f)
    {
        const Vector3 p = a + dir * t;
        if (p.y <= m_Grid.SampleHeight(p.x, p.z)) { hit = true; lo = t - 0.5f; hi = t; break; }
    }
    if (!hit) { m_TitleShow.hasCursor = false; return; }
    for (int i = 0; i < 6; ++i)
    {
        const float mid = (lo + hi) * 0.5f;
        const Vector3 p = a + dir * mid;
        if (p.y <= m_Grid.SampleHeight(p.x, p.z)) hi = mid; else lo = mid;
    }
    const Vector3 ground = a + dir * hi;

    if (!m_TitleShow.hasCursor || (ground - m_TitleShow.cursorGround).Length() > 8.0f)
    {
        m_TitleShow.cursorGround = ground;
        m_TitleShow.cursorVel = Vector3::Zero;
        m_TitleShow.hasCursor = true;
        return;
    }
    Vector3 v = (ground - m_TitleShow.cursorGround) / dt;
    v.y = 0.0f;
    m_TitleShow.cursorVel += (v - m_TitleShow.cursorVel) * (std::min)(1.0f, dt * 12.0f);
    m_TitleShow.cursorGround = ground;

    const float speed = m_TitleShow.cursorVel.Length();
    if (speed < 1.5f) return;
    Swarm::WindSource w = {};
    w.pos[0] = ground.x; w.pos[1] = ground.y; w.pos[2] = ground.z;
    w.radius = 3.5f;
    w.swirl = 2.0f;
    w.updraft = 1.2f;
    w.inflow = 0.0f;
    w.fade = (std::min)(1.0f, speed / 6.0f);
    w.pushDir[0] = m_TitleShow.cursorVel.x / speed;
    w.pushDir[1] = m_TitleShow.cursorVel.z / speed;
    w.push = (std::min)(speed * 0.45f, 12.0f);
    m_Swarm.AddFrameWindSource(w);
}

// ============================================================
// 背嚢：全部のマスに 3x3 の枠、魔法は空いた所へ順に、ルーンは狙いの魔法に届く所のうち影響の多い所へ
// ============================================================
void CollisionTestScene::BuildTitleBackpack(const ItemID* spells, int spellCount, const ItemID* runes, const int* runeTargets, int runeCount)
{
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    bp.frames.clear();
    BackpackLogic::RebuildFrameOccupancy(bp);
    for (int r = 0; r < BackpackComponent::GRID; ++r)
        for (int c = 0; c < BackpackComponent::GRID; ++c)
            BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
    while (!bp.items.empty())
        BackpackLogic::Remove(bp, (int)bp.items.size() - 1);

    // 魔法：一つおきに空けて並べる（ルーンを隣に置く隙間）
    std::vector<int> index(spellCount, -1);
    for (int i = 0; i < spellCount; ++i)
    {
        for (int r = (i % 2) * 4; r < BackpackComponent::GRID && index[i] < 0; r += 2)
            for (int c = 1; c < BackpackComponent::GRID && index[i] < 0; c += 3)
                for (int rot = 0; rot < 4 && index[i] < 0; ++rot)
                    if (BackpackLogic::CanPlace(bp, spells[i], r, c, rot))
                        index[i] = BackpackLogic::Place(bp, spells[i], r, c, rot);
        for (int r = 0; r < BackpackComponent::GRID && index[i] < 0; ++r)   // 隙間が無ければどこでも
            for (int c = 0; c < BackpackComponent::GRID && index[i] < 0; ++c)
                for (int rot = 0; rot < 4 && index[i] < 0; ++rot)
                    if (BackpackLogic::CanPlace(bp, spells[i], r, c, rot))
                        index[i] = BackpackLogic::Place(bp, spells[i], r, c, rot);
    }

    auto influences = [&](int runeIdx, int item)
        {
            if (item < 0) return false;
            const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, item);
            return std::find(inf.begin(), inf.end(), runeIdx) != inf.end();
        };
    for (int k = 0; k < runeCount; ++k)
    {
        const int target = (runeTargets[k] >= 0 && runeTargets[k] < spellCount) ? index[runeTargets[k]] : -1;
        if (target < 0) continue;
        int bestR = -1, bestC = -1, bestRot = 0, bestCount = 0;
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                for (int rot = 0; rot < 4; ++rot)
                {
                    if (!BackpackLogic::CanPlace(bp, runes[k], r, c, rot)) continue;
                    const int ri = BackpackLogic::Place(bp, runes[k], r, c, rot);
                    if (ri < 0) continue;
                    int count = 0;
                    if (influences(ri, target))
                        for (int s = 0; s < spellCount; ++s) count += influences(ri, index[s]) ? 1 : 0;
                    BackpackLogic::Remove(bp, ri);   // 一番後ろなので他の番号は変わらない
                    if (count > bestCount) { bestCount = count; bestR = r; bestC = c; bestRot = rot; }
                }
        if (bestCount > 0) BackpackLogic::Place(bp, runes[k], bestR, bestC, bestRot);
    }
    bp.dirty = true;
}
