// ============================================================
// ProjectileEditorScene.cpp
// 投射物の飛び方の編集シーン
// ============================================================
#include "Scene/ProjectileEditorScene.h"
#include "Core/Application.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "Debug/DebugManager.h"
#include "Debug/Gizmo.h"
#include "Manager/InputManager.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Material/Material.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>

using namespace DirectX::SimpleMath;

namespace
{
    // 3 次ベジェ（SwarmCommon.hlsli の SwarmBezier と同じ）
    Vector3 Bezier(const Vector3 p[4], float t)
    {
        const float u = 1.0f - t;
        return p[0] * (u * u * u) + p[1] * (3.0f * u * u * t)
            + p[2] * (3.0f * u * t * t) + p[3] * (t * t * t);
    }

    // 水平な輪
    void DrawRing(const Vector3& c, float radius, const Color& col)
    {
        auto& dbg = DebugManager::Get();
        const int kSeg = 40;
        Vector3 prev = c + Vector3(radius, 0, 0);
        for (int i = 1; i <= kSeg; ++i)
        {
            const float a = 6.2831853f * (float)i / (float)kSeg;
            const Vector3 cur = c + Vector3(std::cos(a) * radius, 0, std::sin(a) * radius);
            dbg.AddDebugLine(prev, cur, col);
            prev = cur;
        }
    }

    // 円盤の判定範囲：上下の輪 + 4 本の柱
    void DrawDisc(const Vector3& c, float radius, float halfHeight, const Color& col)
    {
        auto& dbg = DebugManager::Get();
        const Vector3 up(0, halfHeight, 0);
        DrawRing(c, radius, col);
        DrawRing(c + up, radius, col);
        DrawRing(c - up, radius, col);
        for (int i = 0; i < 4; ++i)
        {
            const float a = 1.5707963f * (float)i;
            const Vector3 o(std::cos(a) * radius, 0, std::sin(a) * radius);
            dbg.AddDebugLine(c + o - up, c + o + up, col);
        }
    }

    void DrawCross(const Vector3& p, float size, const Color& col)
    {
        auto& dbg = DebugManager::Get();
        dbg.AddDebugLine(p - Vector3(size, 0, 0), p + Vector3(size, 0, 0), col);
        dbg.AddDebugLine(p - Vector3(0, size, 0), p + Vector3(0, size, 0), col);
        dbg.AddDebugLine(p - Vector3(0, 0, size), p + Vector3(0, 0, size), col);
    }
}

// ============================================================
// Init
// ============================================================
void ProjectileEditorScene::Init()
{
    std::cout << "[ProjectileEditorScene] Init" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();
    auto* context = Application::Get().GetGraphics().GetContext();

    // ---------- Camera：銃口の後ろ上から標的の方を見る ----------
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
    m_Camera.SetPosition({ 0.0f, 14.0f, -22.0f });
    m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
    SetCamera(&m_Camera);

    // ---------- 粒子（弾の見た目は全部ここから出る）----------
    if (!m_ParticleSystem.Initialize(device, context, 100000))
    {
        std::cout << "[Error] ParticleSystem init failed" << std::endl;
        return;
    }
    m_ParticleSystem.SetCamera(&m_Camera);
    m_ParticleTexture = ResourceManager::Get().LoadTexture(Res::Tex::ParticleSheet);
    if (m_ParticleTexture)
        m_ParticleSystem.SetTexture(m_ParticleTexture);

    // ---------- 地形：全面歩ける平地 ----------
    // 弾は歩けないセルに入ると消える。編集中に壁で消えると紛らわしいので壁は置かない
    m_Grid.Init(100, 100);

    // ---------- GPU gameplay ----------
    Material::InitDefaultTextures(device);
    m_Swarm.SetParticleSystem(&m_ParticleSystem);
    if (!m_Swarm.Initialize(device, context))
    {
        std::cout << "[Error] SwarmSystem init failed" << std::endl;
        return;
    }
    m_Swarm.UploadTerrain(m_Grid);
    m_Swarm.BuildVFXTable();

    // 標的は殴ってこない（銃口 = GPU 側の玩家位置なので、放っておくと接触判定が走る）
    m_Swarm.GetAIParams().contactDamage = 0.0f;
    m_Muzzle.y = m_Swarm.GetAIParams().groundY;   // 雑魚の胴の高さで水平に撃つ

    // ---------- 範囲攻撃の見た目（CPU 側の VFX。Mesh entry も出せるように MeshRenderer も持つ）----------
    if (!m_MeshRenderer.Initialize(device))
        std::cout << "[Error] VFXMeshRenderer init failed" << std::endl;
    m_VFXContext.particleSystem = &m_ParticleSystem;
    m_VFXContext.meshRenderer = &m_MeshRenderer;
    m_AreaMarker.y = m_Swarm.GetAIParams().groundY;

    // ---------- プロファイル ----------
    // 先に範囲：投射物の「命中で出す範囲」が範囲の番号を引くため
    AreaProfileDB::LoadAll();
    SelectArea(AreaProfileDB::Count() > 1 ? 1 : 0);
    RefreshVfxFiles();
    ProjectileProfileDB::LoadAll();
    PushMotions();
    SelectProfile(ProjectileProfileDB::Count() > 1 ? 1 : 0);

    RespawnTargets();

    std::cout << "[ProjectileEditorScene] Init complete" << std::endl;
}

void ProjectileEditorScene::Shutdown()
{
    m_AreaVFX.StopAll();
    m_Swarm.Shutdown();
    std::cout << "[ProjectileEditorScene] Shutdown" << std::endl;
}

// ============================================================
// 編集中の値を GPU へ。飛んでいる弾も次のステップから新しい型で動く
// （曲線の形は生成時・再捕捉時に組むので、形の変更は次の弾から）
// ============================================================
void ProjectileEditorScene::PushMotions()
{
    // 範囲の雛形も一緒に上げる（弾の命中で出す範囲。投射物の hitArea がこの表の番号を指す）
    m_Swarm.SetAreaDefs(AreaProfileDB::BuildDefs(m_Swarm.GetVFXTable()));

    // 最後の行は乱数曲線の試射用（プロファイルは kScratchRow より手前まで）
    std::vector<Swarm::Motion> rows = ProjectileProfileDB::BuildMotions();
    rows.resize(Swarm::kMaxMotions);
    rows[ProjectileProfileDB::kScratchRow] = m_RandomMotion;
    m_Swarm.SetMotions(rows);
}

void ProjectileEditorScene::SelectProfile(int index)
{
    if (index < 0 || index >= ProjectileProfileDB::Count()) index = 0;
    m_Selected = index;
    m_Dirty = false;

    const std::string& name = ProjectileProfileDB::At(index).name;
    strncpy_s(m_NameBuf, sizeof(m_NameBuf), name.c_str(), _TRUNCATE);
}

// ============================================================
// 標的を並べ直す。銃口の正面（+Z）に横一列
// ============================================================
void ProjectileEditorScene::RespawnTargets()
{
    m_Swarm.KillAll();   // 弾も消える

    const float y = m_Swarm.GetAIParams().groundY;
    const int n = (std::max)(1, m_TargetCount);
    for (int i = 0; i < n; ++i)
    {
        const float f = (n == 1) ? 0.0f : ((float)i / (float)(n - 1) - 0.5f);
        // 真ん中を少し手前にして「一番近い 1 体」がはっきり決まるようにする
        const float z = m_Muzzle.z + m_TargetDistance + std::abs(f) * 3.0f;
        m_Swarm.SpawnEnemy({ m_Muzzle.x + f * m_TargetSpread, y, z }, m_TargetHp, m_TargetSpeed);
    }
    m_RespawnTimer = 0.0f;
}

// ============================================================
// 1 発撃つ。狙いは玩家（= 銃口）に一番近い雑魚。武器の自動照準と同じ
// ============================================================
void ProjectileEditorScene::Fire(bool randomCurve)
{
    ProjectileProfile& p = ProjectileProfileDB::At(m_Selected);

    Vector3 dir(0, 0, 1);
    Vector3 tp, tv;
    float dist = 0.0f;
    const bool hasTarget = m_Swarm.GetNearestEnemy(tp, tv, dist);
    if (hasTarget)
    {
        Vector3 d = tp - m_Muzzle;
        if (d.LengthSquared() > 1e-6f) { d.Normalize(); dir = d; }
    }

    // ---- 乱数曲線：予備行に今回だけの制御点を入れて、その行で撃つ ----
    uint32_t motionRow = (uint32_t)m_Selected;
    bool mirror = false;
    if (randomCurve)
    {
        static std::mt19937 rng{ std::random_device{}() };
        auto rnd = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

        ProjectileProfile r = p;
        // 直進のプロファイルを選んでいても曲線で撃つ（それを見るためのボタンなので）
        if (r.mode == Swarm::MotionMode::Straight) r.mode = Swarm::MotionMode::CurveOnce;
        r.c1 = { rnd(0.15f, 0.45f), rnd(-m_RandSide, m_RandSide), rnd(0.0f, m_RandUp) };
        r.c2 = { rnd(0.55f, 0.85f), rnd(-m_RandSide, m_RandSide), rnd(0.0f, m_RandUp) };

        m_RandomMotion = r.ToMotion();
        PushMotions();
        motionRow = (uint32_t)ProjectileProfileDB::kScratchRow;

        if (hasTarget)
        {
            r.BuildPreview(m_Muzzle, tp, 1.0f, m_LastRandomCurve);
            m_LastRandomTimer = 1.5f;
        }
    }
    else
    {
        mirror = ProjectileProfileDB::NextMirror(m_Selected);
    }

    const VFXId vfx = (VFXDatabase::Count() > 0)
        ? VFXDatabase::At((std::min)(m_VfxIndex, VFXDatabase::Count() - 1))
        : VFXId::None;

    m_Swarm.SpawnProjectile(vfx, m_Muzzle, dir * p.previewSpeed,
        p.previewDamage, p.previewRadius, p.previewLifetime,
        motionRow, mirror);
}

// ============================================================
// Update
// ============================================================
void ProjectileEditorScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_TotalTime += dt;

    // ---- 1 / 2 キーで頁を切り替える。C キーで範囲を 1 回出す（文字入力中は無効）----
    if (!ImGui::GetIO().WantTextInput)
    {
        auto& input = InputManager::Get();
        if (input.GetKeyTrigger('1')) m_TabRequest = 0;
        if (input.GetKeyTrigger('2')) m_TabRequest = 1;
        if (m_Tab == 1 && input.GetKeyTrigger('C')) CastArea();
    }

    // ---- 範囲の頁：自動で出す + 輪の目安の時計 ----
    if (m_Tab == 1 && m_AreaAutoCast && m_AreaSelected > 0)
    {
        m_AreaCastTimer -= dt;
        if (m_AreaCastTimer <= 0.0f)
        {
            m_AreaCastTimer = (std::max)(AreaProfileDB::At(m_AreaSelected).previewInterval, 0.1f);
            CastArea();
        }
    }
    for (auto& la : m_LiveAreas)
    {
        if (la.follow) { la.center.x = m_Muzzle.x; la.center.z = m_Muzzle.z; }
        la.flash -= dt;
        la.tickTimer -= dt;
        if (la.tickTimer <= 0.0f && la.timeLeft > 0.0f)
        {
            la.flash = 0.12f;
            la.tickTimer += (std::max)(la.tickInterval, 0.02f);
        }
        la.timeLeft -= dt;
    }
    m_LiveAreas.erase(std::remove_if(m_LiveAreas.begin(), m_LiveAreas.end(),
        [](const LiveArea& la) { return la.timeLeft <= 0.0f && la.flash <= 0.0f; }), m_LiveAreas.end());

    // ---- 試射（投射物の頁を開いている時だけ）----
    if (m_Tab == 0 && m_AutoFire)
    {
        m_FireTimer -= dt;
        if (m_FireTimer <= 0.0f)
        {
            m_FireTimer = (std::max)(m_FireInterval, 0.05f);
            m_VolleyLeft = (std::max)(1, m_Volley);
            m_VolleyTimer = 0.0f;
            m_NextVolleyRandom = m_RandomEachShot;
        }
    }

    // ---- R キー：乱数曲線で 1 volley（文字入力中は無効）----
    if (m_Tab == 0 && !ImGui::GetIO().WantTextInput && InputManager::Get().GetKeyTrigger('R'))
    {
        m_VolleyLeft = (std::max)(1, m_Volley);
        m_VolleyTimer = 0.0f;
        m_NextVolleyRandom = true;
    }

    // 1 フレームに撃つのは 1 発まで。乱数曲線は予備行を 1 発ごとに書き換えるので、
    // 同じフレームに 2 発撃つと両方が後の曲線になってしまう
    if (m_VolleyLeft > 0)
    {
        m_VolleyTimer -= dt;
        if (m_VolleyTimer <= 0.0f)
        {
            Fire(m_NextVolleyRandom);
            --m_VolleyLeft;
            m_VolleyTimer = m_VolleyDelay;
        }
    }

    // ---- 標的が全滅したら少し待って並べ直す ----
    // aliveEnemies は回読で 1〜2 フレーム古い。湧かせた直後の 0 を全滅と誤認しないよう、
    // 0 が一定時間続いた時だけ並べ直す
    if (m_AutoRespawn)
    {
        if (m_Swarm.GetCounters().aliveEnemies == 0 && m_Swarm.GetPendingEnemySpawns() == 0)
            m_RespawnTimer += dt;
        else
            m_RespawnTimer = 0.0f;

        if (m_RespawnTimer > 1.0f)
            RespawnTargets();
    }

    // ---- GPU gameplay → 粒子 の順（本番と同じ）----
    m_Swarm.Flush(m_Muzzle, 0.4f, true, dt, m_TotalTime);
    m_AreaVFX.Update(dt, m_Muzzle);   // emitter を積むので粒子の Flush より前
    m_ParticleSystem.Flush(dt, m_TotalTime);

    m_LastRandomTimer = (std::max)(0.0f, m_LastRandomTimer - dt);

    DrawGuides();
    DrawGizmos();
    DrawUI();
}

// ============================================================
// 曲線・制御点・銃口・地面の格子
// ============================================================
void ProjectileEditorScene::DrawGuides()
{
    auto& dbg = DebugManager::Get();
    const float groundY = m_Swarm.GetAIParams().groundY;

    if (m_ShowGrid)
    {
        const Color col(0.22f, 0.24f, 0.28f, 1.0f);
        const float half = 30.0f, stepSize = 5.0f;
        const float y = groundY - 0.9f;
        for (float v = -half; v <= half + 0.01f; v += stepSize)
        {
            dbg.AddDebugLine({ v, y, -half }, { v, y, half }, col);
            dbg.AddDebugLine({ -half, y, v }, { half, y, v }, col);
        }
    }

    DrawCross(m_Muzzle, 0.5f, Color(0.3f, 1.0f, 0.5f, 1.0f));

    // ---- 範囲：生きている物（tick の瞬間だけ白）と、次に出る場所（暗い輪）----
    for (const auto& la : m_LiveAreas)
        DrawDisc(la.center, la.radius, la.halfHeight,
            (la.flash > 0.0f) ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(1.0f, 0.55f, 0.15f, 1.0f));

    if (m_Tab == 1 && m_AreaSelected > 0)
    {
        const AreaProfile& ap = AreaProfileDB::At(m_AreaSelected);
        Vector3 c = m_AreaMarker, tp0, tv0;
        float d0 = 0.0f;
        bool ok = true;
        if (m_AreaPlace == 0) { ok = m_Swarm.GetNearestEnemy(tp0, tv0, d0); c = tp0; }
        else if (m_AreaPlace == 1) c = m_Muzzle;
        if (ok) DrawRing(c, ap.radius, Color(0.45f, 0.30f, 0.15f, 1.0f));
        return;   // 範囲の頁では曲線を出さない
    }

    // ---- 直前の乱数曲線（少しの間だけ）----
    if (m_LastRandomTimer > 0.0f)
    {
        const Color col(1.0f, 0.35f, 0.9f, 1.0f);
        const int kSeg = 32;
        Vector3 prev = m_LastRandomCurve[0];
        for (int i = 1; i <= kSeg; ++i)
        {
            const Vector3 cur = Bezier(m_LastRandomCurve, (float)i / (float)kSeg);
            dbg.AddDebugLine(prev, cur, col);
            prev = cur;
        }
    }

    if (!m_ShowCurve) return;

    Vector3 tp, tv;
    float dist = 0.0f;
    if (!m_Swarm.GetNearestEnemy(tp, tv, dist)) return;

    const ProjectileProfile& p = ProjectileProfileDB::At(m_Selected);

    if (p.mode == Swarm::MotionMode::Straight)
    {
        dbg.AddDebugLine(m_Muzzle, tp, Color(1.0f, 0.9f, 0.3f, 1.0f));
        return;
    }

    auto drawSide = [&](float sign, const Color& curveCol, const Color& handleCol)
        {
            Vector3 cp[4];
            p.BuildPreview(m_Muzzle, tp, sign, cp);

            const int kSeg = 32;
            Vector3 prev = cp[0];
            for (int i = 1; i <= kSeg; ++i)
            {
                const Vector3 cur = Bezier(cp, (float)i / (float)kSeg);
                dbg.AddDebugLine(prev, cur, curveCol);
                prev = cur;
            }
            dbg.AddDebugLine(cp[0], cp[1], handleCol);
            dbg.AddDebugLine(cp[3], cp[2], handleCol);
            DrawCross(cp[1], 0.25f, handleCol);
            DrawCross(cp[2], 0.25f, handleCol);
        };

    if (p.mirror != ProjectileProfile::Mirror::Fixed)
        drawSide(-1.0f, Color(0.45f, 0.40f, 0.20f, 1.0f), Color(0.25f, 0.30f, 0.40f, 1.0f));
    drawSide(+1.0f, Color(1.0f, 0.9f, 0.3f, 1.0f), Color(0.4f, 0.7f, 1.0f, 1.0f));
}

// ============================================================
// 3D ギズモ：銃口と、曲線の 2 つの制御点をマウスで掴んで動かす。
// 制御点は世界座標で動かし、(along, side, up) へ戻してプロファイルに書く
// ============================================================
void ProjectileEditorScene::DrawGizmos()
{
    if (!GetCamera()) return;
    Gizmo::BeginFrame(GetCamera()->GetViewMatrix(), GetCamera()->GetProjectionMatrix());
    if (!m_ShowGizmos) return;

    Gizmo::Options opt;
    opt.snap = m_GizmoSnap;

    opt.label = "Muzzle";
    Gizmo::Translate("proj_editor_muzzle", m_Muzzle, opt);

    // ---- 範囲の頁：目印を掴んで、出す場所を決める ----
    if (m_Tab == 1)
    {
        if (m_AreaPlace == 2)
        {
            opt.label = "Area";
            Gizmo::Translate("proj_editor_area_marker", m_AreaMarker, opt);
        }
        return;
    }

    // ---- 制御点（曲線の型で、標的が居る時だけ）----
    if (m_Selected == 0) return;   // 組み込みの直進は編集不可
    ProjectileProfile& p = ProjectileProfileDB::At(m_Selected);
    if (p.mode == Swarm::MotionMode::Straight) return;

    Vector3 tp, tv;
    float dist = 0.0f;
    if (!m_Swarm.GetNearestEnemy(tp, tv, dist)) return;

    Vector3 cp[4];
    p.BuildPreview(m_Muzzle, tp, 1.0f, cp);

    opt.sizePixels = 70.0f;
    bool changed = false;

    opt.label = "C1";
    if (Gizmo::Translate("proj_editor_c1", cp[1], opt))
    {
        p.SetControlFromWorld(1, m_Muzzle, tp, 1.0f, cp[1]);
        changed = true;
    }
    opt.label = "C2";
    if (Gizmo::Translate("proj_editor_c2", cp[2], opt))
    {
        p.SetControlFromWorld(2, m_Muzzle, tp, 1.0f, cp[2]);
        changed = true;
    }

    if (changed)
    {
        m_Dirty = true;
        PushMotions();
    }
}

// ============================================================
// UI
// ============================================================
void ProjectileEditorScene::DrawUI()
{
    ImGui::Begin("Projectile Editor");

    // 開いている頁だけが自動で撃つ（m_Tab）
    if (ImGui::BeginTabBar("##editor_tabs"))
    {
        if (ImGui::BeginTabItem("Projectile (1)", nullptr,
            (m_TabRequest == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
        {
            m_Tab = 0;
            DrawProjectileTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Area: explosion / circle (2)", nullptr,
            (m_TabRequest == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
        {
            m_Tab = 1;
            DrawAreaTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    m_TabRequest = -1;

    DrawCommonUI();
    ImGui::End();
}

// ============================================================
// 投射物の頁
// ============================================================
void ProjectileEditorScene::DrawProjectileTab()
{
    // ---------- プロファイル一覧 ----------
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Profiles");
    if (ImGui::BeginListBox("##profiles", ImVec2(-1, 110)))
    {
        for (int i = 0; i < ProjectileProfileDB::Count(); ++i)
        {
            const auto& pr = ProjectileProfileDB::At(i);
            std::string label = pr.name + (i == 0 ? "  (built-in)" : "");
            if (i == m_Selected && m_Dirty) label += " *";
            if (ImGui::Selectable(label.c_str(), i == m_Selected))
                SelectProfile(i);
        }
        ImGui::EndListBox();
    }

    if (ImGui::Button("New"))
    {
        ProjectileProfile np;
        np.mode = Swarm::MotionMode::CurveOnce;
        np.name = "Projectile" + std::to_string(ProjectileProfileDB::Count());
        const int idx = ProjectileProfileDB::Add(np);
        if (idx >= 0) { PushMotions(); SelectProfile(idx); m_Dirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && m_Selected >= 0)
    {
        ProjectileProfile np = ProjectileProfileDB::At(m_Selected);
        np.name += "_copy";
        const int idx = ProjectileProfileDB::Add(np);
        if (idx >= 0) { PushMotions(); SelectProfile(idx); m_Dirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload All"))
    {
        ProjectileProfileDB::LoadAll();
        PushMotions();
        SelectProfile((std::min)(m_Selected, ProjectileProfileDB::Count() - 1));
    }
    ImGui::Separator();

    // ---------- 選択中のプロファイル ----------
    ProjectileProfile& p = ProjectileProfileDB::At(m_Selected);
    const bool builtin = (m_Selected == 0);
    bool changed = false;

    if (builtin)
        ImGui::TextDisabled("built-in straight shot: not editable. Press New.");

    ImGui::BeginDisabled(builtin);

    if (ImGui::InputText("Name", m_NameBuf, sizeof(m_NameBuf)))
    {
        p.name = m_NameBuf;
        m_Dirty = true;
    }

    const char* modeNames[] = {
        "Straight (no curve)",
        "Curve, lock once (target dies -> fly straight)",
        "Full tracking (target dies -> find another)" };
    int mode = (int)p.mode;
    if (ImGui::Combo("Mode", &mode, modeNames, 3))
    {
        p.mode = (Swarm::MotionMode)mode;
        changed = true;
    }

    if (p.mode != Swarm::MotionMode::Straight)
    {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Curve (x = along, y = side, z = up; side/up are x distance)");
        changed |= ImGui::DragFloat3("Control 1", &p.c1.x, 0.01f, -2.0f, 2.0f);
        changed |= ImGui::DragFloat3("Control 2", &p.c2.x, 0.01f, -2.0f, 2.0f);

        const char* mirrorNames[] = { "Fixed side", "Alternate left/right", "Random" };
        int mirror = (int)p.mirror;
        if (ImGui::Combo("Mirror", &mirror, mirrorNames, 3))
        {
            p.mirror = (ProjectileProfile::Mirror)mirror;
            changed = true;
        }

        if (ImGui::Button("Preset: Side Arc")) { p.c1 = { 0.30f, 0.35f, 0.0f }; p.c2 = { 0.70f, 0.35f, 0.0f }; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Lob")) { p.c1 = { 0.25f, 0.0f, 0.45f }; p.c2 = { 0.75f, 0.0f, 0.45f }; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("S-Curve")) { p.c1 = { 0.30f, 0.45f, 0.0f }; p.c2 = { 0.70f, -0.45f, 0.0f }; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Line")) { p.c1 = { 0.33f, 0.0f, 0.0f }; p.c2 = { 0.66f, 0.0f, 0.0f }; changed = true; }
    }

    if (p.mode == Swarm::MotionMode::Track)
        changed |= ImGui::DragFloat("Retarget Radius (0 = unlimited)", &p.retargetRadius, 0.5f, 0.0f, 200.0f);

    // ---------- 命中した場所に出す範囲（爆発など）----------
    {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "On hit");
        const int cur = AreaProfileDB::IndexOf(p.hitArea);
        if (ImGui::BeginCombo("Hit Area", cur > 0 ? AreaProfileDB::At(cur).name.c_str() : "(none)"))
        {
            if (ImGui::Selectable("(none)", cur == 0)) { p.hitArea.clear(); changed = true; }
            for (int i = 1; i < AreaProfileDB::Count(); ++i)
                if (ImGui::Selectable(AreaProfileDB::At(i).name.c_str(), i == cur))
                {
                    p.hitArea = AreaProfileDB::At(i).name;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (cur > 0)
        {
            changed |= ImGui::Checkbox("Also when it expires / hits a wall", &p.hitAreaOnExpire);
            const AreaProfile& ha = AreaProfileDB::At(cur);
            if (AreaProfileDB::FindVFXId(ha.vfxFile) == VFXId::None)
                ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                    "damage works, but no visuals: '%s' is not in VFXDatabase.\n"
                    "Hit areas are born on the GPU, which can only emit registered VFX.",
                    ha.vfxFile.empty() ? "(no vfx)" : ha.vfxFile.c_str());
        }
    }

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Test shot (editor only; in game the item decides)");
    changed |= ImGui::DragFloat("Speed", &p.previewSpeed, 0.1f, 0.5f, 100.0f);
    changed |= ImGui::DragFloat("Lifetime", &p.previewLifetime, 0.05f, 0.1f, 30.0f);
    changed |= ImGui::DragFloat("Radius", &p.previewRadius, 0.01f, 0.05f, 3.0f);
    changed |= ImGui::DragFloat("Damage", &p.previewDamage, 0.5f, 0.0f, 1000.0f);

    if (changed)
    {
        m_Dirty = true;
        PushMotions();
    }

    if (ImGui::Button("Save"))
    {
        if (ProjectileProfileDB::Save(m_Selected)) m_Dirty = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("-> %s%s.json", ProjectileProfileDB::kDir, p.name.c_str());

    ImGui::EndDisabled();
    ImGui::Separator();

    // ---------- 試射 ----------
    if (ImGui::CollapsingHeader("Firing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Auto Fire", &m_AutoFire);
        ImGui::SameLine();
        if (ImGui::Button("Fire Once")) { m_VolleyLeft = (std::max)(1, m_Volley); m_VolleyTimer = 0.0f; m_NextVolleyRandom = m_RandomEachShot; }

        // ---- 乱数曲線 ----
        if (ImGui::Button("Fire Random Curve (R)")) { m_VolleyLeft = (std::max)(1, m_Volley); m_VolleyTimer = 0.0f; m_NextVolleyRandom = true; }
        ImGui::SameLine();
        ImGui::Checkbox("Random every shot", &m_RandomEachShot);
        ImGui::DragFloat("Random Side (+/-)", &m_RandSide, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Random Up (0..)", &m_RandUp, 0.01f, 0.0f, 2.0f);
        ImGui::TextDisabled("pink line = last random curve. the profile itself is not changed");
        ImGui::DragFloat("Interval", &m_FireInterval, 0.01f, 0.05f, 5.0f);
        ImGui::SliderInt("Shots per Volley", &m_Volley, 1, 8);
        ImGui::DragFloat("Volley Delay", &m_VolleyDelay, 0.005f, 0.0f, 1.0f);
        if (ImGui::DragFloat3("Muzzle", &m_Muzzle.x, 0.1f)) {}

        if (VFXDatabase::Count() > 0)
        {
            m_VfxIndex = (std::min)(m_VfxIndex, VFXDatabase::Count() - 1);
            const char* cur = VFXDatabase::GetPath(VFXDatabase::At(m_VfxIndex));
            if (ImGui::BeginCombo("VFX", cur ? cur : "(none)"))
            {
                for (int i = 0; i < VFXDatabase::Count(); ++i)
                {
                    const char* path = VFXDatabase::GetPath(VFXDatabase::At(i));
                    if (ImGui::Selectable(path ? path : "(none)", i == m_VfxIndex))
                        m_VfxIndex = i;
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox("Show Projectile Spheres", &m_ShowDebugSpheres);
    }
}

// ============================================================
// 範囲攻撃の頁
// ============================================================
void ProjectileEditorScene::SelectArea(int index)
{
    if (index < 0 || index >= AreaProfileDB::Count()) index = 0;
    m_AreaSelected = index;
    m_AreaDirty = false;
    strncpy_s(m_AreaNameBuf, sizeof(m_AreaNameBuf), AreaProfileDB::At(index).name.c_str(), _TRUNCATE);
}

void ProjectileEditorScene::RefreshVfxFiles()
{
    m_VfxFiles.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(AreaProfileDB::kVfxDir, ec)) return;
    for (const auto& e : fs::directory_iterator(AreaProfileDB::kVfxDir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json")
            m_VfxFiles.push_back(e.path().filename().string());
    std::sort(m_VfxFiles.begin(), m_VfxFiles.end());
}

void ProjectileEditorScene::CastArea()
{
    if (m_AreaSelected <= 0) return;
    const AreaProfile& ap = AreaProfileDB::At(m_AreaSelected);

    Vector3 center = m_AreaMarker;
    bool atCaster = false;
    if (m_AreaPlace == 0)
    {
        Vector3 tv; float d = 0.0f;
        if (!m_Swarm.GetNearestEnemy(center, tv, d)) return;   // 標的が居ない
    }
    else if (m_AreaPlace == 1)
    {
        center = m_Muzzle;   // 銃口 = GPU 側の玩家位置。追従の確認はここで
        atCaster = true;
    }

    const Swarm::Area a = ap.MakeArea(center, atCaster);
    const bool follow = (a.flags & Swarm::kAreaFollowPlayer) != 0;
    m_Swarm.SpawnArea(a);
    m_AreaVFX.Play(ap.vfxFile, center, a.timeLeft, follow, m_VFXContext);

    LiveArea la;
    la.center = center;
    la.radius = a.radius;
    la.halfHeight = a.halfHeight;
    la.timeLeft = a.timeLeft;
    la.tickInterval = a.tickInterval;
    la.follow = follow;
    m_LiveAreas.push_back(la);
}

void ProjectileEditorScene::DrawAreaTab()
{
    // ---------- 一覧 ----------
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Area Profiles");
    if (ImGui::BeginListBox("##areas", ImVec2(-1, 110)))
    {
        for (int i = 1; i < AreaProfileDB::Count(); ++i)
        {
            std::string label = AreaProfileDB::At(i).name;
            if (i == m_AreaSelected && m_AreaDirty) label += " *";
            if (ImGui::Selectable(label.c_str(), i == m_AreaSelected))
                SelectArea(i);
        }
        ImGui::EndListBox();
    }

    if (ImGui::Button("New Explosion"))
    {
        AreaProfile np;
        np.kind = AreaProfile::Kind::OneShot;
        np.name = "Explosion" + std::to_string(AreaProfileDB::Count());
        const int idx = AreaProfileDB::Add(np);
        if (idx >= 0) { PushMotions(); SelectArea(idx); m_AreaDirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("New Circle"))
    {
        AreaProfile np;
        np.kind = AreaProfile::Kind::Lasting;
        np.name = "Circle" + std::to_string(AreaProfileDB::Count());
        np.radius = 4.0f;
        np.damage = 5.0f;
        np.duration = 4.0f;
        np.tickInterval = 0.5f;
        np.stun = false;          // 持続で硬直を入れると中の敵が固まり続ける
        np.previewInterval = 5.0f;
        const int idx = AreaProfileDB::Add(np);
        if (idx >= 0) { PushMotions(); SelectArea(idx); m_AreaDirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate##area") && m_AreaSelected > 0)
    {
        AreaProfile np = AreaProfileDB::At(m_AreaSelected);
        np.name += "_copy";
        const int idx = AreaProfileDB::Add(np);
        if (idx >= 0) { PushMotions(); SelectArea(idx); m_AreaDirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload All##area"))
    {
        AreaProfileDB::LoadAll();
        m_AreaVFX.ClearTemplates();   // VFX の json を編集し直した時もこれで読み直す
        RefreshVfxFiles();
        PushMotions();
        SelectArea((std::min)(m_AreaSelected, AreaProfileDB::Count() - 1));
    }
    ImGui::Separator();

    if (m_AreaSelected <= 0)
    {
        ImGui::TextDisabled("no area profile yet. Press New Explosion / New Circle.");
        return;
    }

    // ---------- 選択中 ----------
    AreaProfile& p = AreaProfileDB::At(m_AreaSelected);
    bool changed = false;

    if (ImGui::InputText("Name##area", m_AreaNameBuf, sizeof(m_AreaNameBuf)))
    {
        p.name = m_AreaNameBuf;
        m_AreaDirty = true;
        PushMotions();   // 投射物の hitArea は名前で引くので、表を作り直す
    }

    const char* kindNames[] = { "One shot (explosion)", "Lasting (circle / burning ground)" };
    int kind = (int)p.kind;
    if (ImGui::Combo("Kind", &kind, kindNames, 2))
    {
        p.kind = (AreaProfile::Kind)kind;
        changed = true;
    }

    changed |= ImGui::DragFloat("Radius##area", &p.radius, 0.05f, 0.2f, 60.0f);
    changed |= ImGui::DragFloat("Half Height", &p.halfHeight, 0.05f, 0.1f, 30.0f);
    if (p.kind == AreaProfile::Kind::OneShot)
    {
        changed |= ImGui::DragFloat("Damage##area", &p.damage, 0.5f, 0.0f, 100000.0f);
        changed |= ImGui::DragFloat("Linger (sec, visuals only)", &p.duration, 0.01f, 0.05f, 10.0f);
    }
    else
    {
        changed |= ImGui::DragFloat("Damage per Tick", &p.damage, 0.5f, 0.0f, 100000.0f);
        changed |= ImGui::DragFloat("Duration (sec)", &p.duration, 0.05f, 0.1f, 120.0f);
        changed |= ImGui::DragFloat("Tick Interval", &p.tickInterval, 0.01f, 0.02f, 10.0f);
        const float ticks = std::floor(p.duration / (std::max)(p.tickInterval, 0.02f)) + 1.0f;
        ImGui::TextDisabled("about %.0f ticks, %.0f damage in total, %.1f dps",
            ticks, ticks * p.damage, p.damage / (std::max)(p.tickInterval, 0.02f));
    }
    changed |= ImGui::Checkbox("Follow caster (when cast on the player)", &p.followCaster);
    changed |= ImGui::Checkbox("Hit stun on damage", &p.stun);
    if (p.stun && p.kind == AreaProfile::Kind::Lasting)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "stun + short tick interval = enemies stay frozen inside");

    // ---------- 見た目 ----------
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Visuals (made in the VFX Editor, F2)");
    if (ImGui::BeginCombo("VFX##area", p.vfxFile.empty() ? "(none)" : p.vfxFile.c_str()))
    {
        if (ImGui::Selectable("(none)", p.vfxFile.empty())) { p.vfxFile.clear(); changed = true; }
        for (const auto& f : m_VfxFiles)
            if (ImGui::Selectable(f.c_str(), f == p.vfxFile)) { p.vfxFile = f; changed = true; }
        ImGui::EndCombo();
    }
    if (!p.vfxFile.empty())
    {
        if (AreaProfileDB::FindVFXId(p.vfxFile) == VFXId::None)
            ImGui::TextDisabled("not in VFXDatabase: fine for items, but invisible as a projectile Hit Area");
        else
            ImGui::TextDisabled("in VFXDatabase: also works as a projectile Hit Area (particles only)");
    }
    ImGui::TextDisabled("the VFX is not scaled by Radius: author it at the size you want");

    if (changed)
    {
        m_AreaDirty = true;
        PushMotions();
    }

    if (ImGui::Button("Save##area"))
    {
        if (AreaProfileDB::Save(m_AreaSelected)) m_AreaDirty = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("-> %s%s.json", AreaProfileDB::kDir, p.name.c_str());
    ImGui::Separator();

    // ---------- 試し撃ち ----------
    if (ImGui::CollapsingHeader("Casting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Auto Cast", &m_AreaAutoCast);
        ImGui::SameLine();
        if (ImGui::Button("Cast Once (C)")) CastArea();
        ImGui::DragFloat("Interval##area", &p.previewInterval, 0.05f, 0.1f, 30.0f);

        const char* placeNames[] = { "At nearest target", "At muzzle (= the player)", "At marker (drag the gizmo)" };
        ImGui::Combo("Where", &m_AreaPlace, placeNames, 3);
        if (m_AreaPlace == 1)
            ImGui::TextDisabled("move the Muzzle gizmo while a circle is up to check Follow caster");
        ImGui::TextDisabled("orange = live area, white flash = damage tick, dark ring = next cast");
    }
}

// ============================================================
// 両方の頁で共通：標的・表示・状態
// ============================================================
void ProjectileEditorScene::DrawCommonUI()
{
    // ---------- 標的 ----------
    if (ImGui::CollapsingHeader("Targets", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderInt("Count", &m_TargetCount, 1, 30);
        ImGui::DragFloat("Distance", &m_TargetDistance, 0.2f, 2.0f, 60.0f);
        ImGui::DragFloat("Spread", &m_TargetSpread, 0.2f, 0.0f, 60.0f);
        ImGui::DragFloat("HP", &m_TargetHp, 1.0f, 1.0f, 100000.0f);
        ImGui::DragFloat("Move Speed (0 = still)", &m_TargetSpeed, 0.05f, 0.0f, 10.0f);
        ImGui::Checkbox("Auto Respawn", &m_AutoRespawn);
        ImGui::SameLine();
        if (ImGui::Button("Respawn Now")) RespawnTargets();
        ImGui::TextDisabled("low HP: targets die mid-flight, shows what each mode does then");
    }

    // ---------- 表示 / 状態 ----------
    if (ImGui::CollapsingHeader("View"))
    {
        ImGui::Checkbox("Show Curve", &m_ShowCurve);
        ImGui::SameLine();
        ImGui::Checkbox("Show Gizmos", &m_ShowGizmos);
        ImGui::DragFloat("Gizmo Snap (0 = off, Ctrl = free)", &m_GizmoSnap, 0.05f, 0.0f, 5.0f);
        ImGui::SameLine();
        ImGui::Checkbox("Show Grid", &m_ShowGrid);
        ImGui::DragFloat3("Light Dir", m_LightDir, 0.02f, -1.0f, 1.0f);
    }

    const auto& c = m_Swarm.GetCounters();
    ImGui::Separator();
    ImGui::Text("alive targets %u   alive projectiles %u   kills %u",
        c.aliveEnemies, c.aliveProjectiles, c.killCount);
    ImGui::Text("alive areas %u   ticking this step %u   area vfx %zu",
        c.aliveAreas, c.tickingAreas, m_AreaVFX.GetActiveCount());
    ImGui::Text("swarm flush %.3f ms   substeps %d", m_Swarm.GetFlushMs(), m_Swarm.GetLastSubSteps());
}

// ============================================================
// Render：雑魚（標的）→ 弾の位置の球 → 粒子
// ============================================================
void ProjectileEditorScene::Render(Renderer& renderer)
{
    renderer.SetDirectionalLight(
        { m_LightDir[0], m_LightDir[1], m_LightDir[2] },
        { 1.0f, 1.0f, 1.0f }, 1.0f);
    renderer.SetAmbientColor({ 0.25f, 0.25f, 0.25f });

    SceneBase::Render(renderer);

    m_Swarm.Render(GetCamera(), renderer.GetLightData());
    if (m_ShowDebugSpheres)
        m_Swarm.RenderDebug(GetCamera());

    // 法環などの VFX Mesh（光を当てない。深度は読むだけ）。粒子の前
    m_MeshRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());

    m_ParticleSystem.SetCamera(GetCamera());
    m_ParticleSystem.SetLight(renderer.GetLightData());
    m_ParticleSystem.Render();
}
