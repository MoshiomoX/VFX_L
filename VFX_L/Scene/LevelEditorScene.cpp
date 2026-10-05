// ============================================================
// LevelEditorScene.cpp
// ============================================================
#include "Scene/LevelEditorScene.h"
#include "Core/Application.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Transform.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "Debug/DebugManager.h"
#include "Debug/Gizmo.h"
#include "Debug/AutoTest/AutoTestMapEdit.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

using namespace DirectX::SimpleMath;
namespace fs = std::filesystem;

namespace
{
    // 素材を探すフォルダ（中の .fbx を全部拾う）。素材を増やす時はここに足す。
    // pack は一覧の一番上の段の名前
    // defaultScale は置く時の既定倍率（実寸 m に直した後に掛ける）。
    //   KayKit Forest は実寸どおり。Kenney Retro Fantasy は壁・床が 1m 角の部品なので、
    //   2 倍にして 1 部品 = 地形の 1 マス（2m）に揃える（1m だと人物 1.6〜1.8m に対して膝の高さ）
    struct PropFolder
    {
        const char* path;
        const char* pack;
        float defaultScale;
    };
    const PropFolder kPropFolders[] =
    {
        { "Assets/Model/KayKit_Forest/fbx/",       "Forest (KayKit)",               1.0f },
        { "Assets/Model/Kenney_RetroFantasy/fbx/", "Retro Fantasy (Kenney, pixel)", 2.0f },
    };

    // 照明: 戦闘シーン（CollisionTestScene）の既定と同じ Unity 風の太陽
    const Vector3 kSunColor = { 1.0f, 0.957f, 0.839f };
    const Vector3 kAmbientSky = { 0.40f, 0.44f, 0.50f };
    const Vector3 kAmbientGround = { 0.22f, 0.20f, 0.18f };

    // "Tree_1_A_Color1" → "Tree_1_A"（KayKit の色違い番号は一覧では要らない）
    std::string DisplayName(std::string stem)
    {
        const std::string suffix = "_Color1";
        if (stem.size() > suffix.size()
            && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
            stem.erase(stem.size() - suffix.size());
        return stem;
    }

    bool ContainsNoCase(const std::string& s, const char* sub)
    {
        if (!sub || !*sub) return true;
        std::string a = s, b = sub;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return a.find(b) != std::string::npos;
    }

    // 光線 × 軸平行の箱（スラブ法）。当たれば入口の t（光線の始点が箱の中なら 0）
    bool RayBox(const Vector3& ro, const Vector3& rd, const Vector3& bmin, const Vector3& bmax, float& tHit)
    {
        const float o[3] = { ro.x, ro.y, ro.z };
        const float d[3] = { rd.x, rd.y, rd.z };
        const float lo[3] = { bmin.x, bmin.y, bmin.z };
        const float hi[3] = { bmax.x, bmax.y, bmax.z };
        float t0 = 0.0f, t1 = FLT_MAX;
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(d[i]) < 1e-8f)
            {
                if (o[i] < lo[i] || o[i] > hi[i]) return false;
                continue;
            }
            float ta = (lo[i] - o[i]) / d[i];
            float tb = (hi[i] - o[i]) / d[i];
            if (ta > tb) std::swap(ta, tb);
            t0 = (std::max)(t0, ta);
            t1 = (std::min)(t1, tb);
            if (t0 > t1) return false;
        }
        tHit = t0;
        return true;
    }

    // unit = モデルのファイル単位 → m（Model::GetFileUnitScale）。o.scale はその上に掛ける
    void FillTransform(Transform& t, const LevelObject& o, float unit)
    {
        const float s = o.scale * unit;
        t.SetPosition(o.position);
        t.SetRotation(o.rotation);
        t.SetScale(Vector3(s, s, s));
    }

    // 保存名に使える文字だけ残す（英数字 _ -）
    std::string SanitizeName(const char* s)
    {
        std::string out;
        for (const char* p = s; *p; ++p)
        {
            const unsigned char c = (unsigned char)*p;
            if (std::isalnum(c) || c == '_' || c == '-') out.push_back((char)c);
        }
        return out;
    }

    float WrapDeg(float d)
    {
        d = std::fmod(d, 360.0f);
        return (d < 0.0f) ? d + 360.0f : d;
    }
}

// ============================================================
// Init / Shutdown
// ============================================================
void LevelEditorScene::Init()
{
    auto* device = Application::Get().GetGraphics().GetDevice();

    UpdateScreenSize();   // カメラの投影もここで作る
    m_Camera.Place({ 0.0f, 12.0f, -18.0f }, 0.0f, 35.0f);
    SetCamera(&m_Camera);

    // 地面: 上面を y = -0.01 に置く（DebugManager の格子線 y = 0 とちらつかないよう少し下げる）
    m_Ground = PrimitiveBuilder::CreateBox(device, { kGroundHalf, 0.05f, kGroundHalf },
        { 0.42f, 0.55f, 0.33f, 1.0f });

    ScanAssets();
    RefreshLevelList();
    NewLevel();
    RerollGhost();

    std::cout << "[LevelEditorScene] Init: " << m_Assets.size() << " assets, "
        << m_LevelFiles.size() << " saved levels" << std::endl;
}

void LevelEditorScene::Shutdown()
{
    if (m_MapEdit.IsActive()) DebugManager::Get().SetShowGrid(true);   // 地図モードで消した参照格子を戻す
    m_MapEdit.Shutdown();
    m_Models.clear();
    m_Ground.reset();
}

void LevelEditorScene::UpdateScreenSize()
{
    auto& gfx = Application::Get().GetGraphics();
    const float w = gfx.GetWidth();
    const float h = gfx.GetHeight();
    if (w <= 0.0f || h <= 0.0f) return;
    if (w == m_ScreenW && h == m_ScreenH) return;
    m_ScreenW = w;
    m_ScreenH = h;
    m_Camera.Init(45.0f, w / h, 0.1f, 2000.0f);
}

// ============================================================
// 素材
// ============================================================
void LevelEditorScene::ScanAssets()
{
    m_Assets.clear();
    for (const PropFolder& pf : kPropFolders)
    {
        const char* folder = pf.path;
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(folder, ec))
        {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (e.is_regular_file() && ext == ".fbx") files.push_back(e.path());
        }
        if (ec) std::cout << "[LevelEditorScene] asset folder not found: " << folder << std::endl;
        std::sort(files.begin(), files.end());   // 同じ種類が並ぶように

        for (const auto& f : files)
        {
            PropAsset a;
            a.path = std::string(folder) + f.filename().string();
            a.name = DisplayName(f.stem().string());
            a.pack = pf.pack;
            a.defaultScale = pf.defaultScale;
            // 名前の最初の区切りまで（KayKit は "Tree_1_A"、Kenney は "wall-fortified-door"）
            const size_t sep = a.name.find_first_of("_-");
            a.group = (sep == std::string::npos) ? a.name : a.name.substr(0, sep);
            m_Assets.push_back(std::move(a));
        }
    }
}

std::shared_ptr<Model> LevelEditorScene::GetModel(const std::string& path)
{
    auto it = m_Models.find(path);
    if (it != m_Models.end()) return it->second;   // 読めなかった物も nullptr で覚えておく（毎フレーム読み直さない）

    auto model = ResourceManager::Get().LoadModel(path);
    if (model)
    {
        const Vector3 size = model->GetBoundsMax() - model->GetBoundsMin();
        const float u = model->GetFileUnitScale();
        std::cout << "[LevelEditorScene] loaded " << path << " unit=" << u << " size(m)=("
            << size.x * u << ", " << size.y * u << ", " << size.z * u << ")" << std::endl;
    }
    m_Models[path] = model;
    return model;
}

// ============================================================
// Update
// ============================================================
void LevelEditorScene::Update(float dt)
{
    SceneBase::Update(dt);
    UpdateScreenSize();
    if (m_StatusTimer > 0.0f) m_StatusTimer -= dt;

    m_Camera.Update(dt);
    Gizmo::BeginFrame(m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix());

    // ---- 戦闘の地図モード（Scene/MapEditMode）：操作も表示もそちらへ任せる。カメラ・素材一覧・置く物の向き / 大きさはこのシーンの物 ----
    if (MapEditAutoTest::Enabled()) MapEditAutoTest::Update(m_MapEdit, m_Camera, dt);   // TEMP-TEST
    if (m_MapEdit.IsActive())
    {
        MapEditMode::PlaceRequest req;
        if (m_PlaceAsset >= 0)
        {
            req.path = &m_Assets[m_PlaceAsset].path;
            req.scale = m_GhostScale;
            req.yawDeg = m_GhostYaw;
        }
        if (m_MapEdit.Update(m_Camera, req, m_Snap)) RerollGhost();
        if (m_MapEdit.ConsumeStopPlacing()) m_PlaceAsset = -1;
        // 置いている最中の R は次に置く物を回す
        if (m_PlaceAsset >= 0 && !ImGui::GetIO().WantTextInput && !m_Camera.IsLooking()
            && InputManager::Get().GetKeyTrigger('R'))
            RotateStep(InputManager::Get().GetKeyPress(VK_SHIFT) ? -1.0f : 1.0f);
        DrawUI();
        return;
    }

    UpdateEditing();

    // ---- 線の目印: 選択中は黄、幽霊は水色 ----
    if (m_Selected >= 0)
    {
        const LevelObject& o = m_Level.objects[m_Selected];
        if (auto m = GetModel(o.model)) DrawBox(*m, WorldOf(o), Color(1.0f, 0.85f, 0.2f, 1.0f));
    }
    if (m_PlaceAsset >= 0 && m_GhostValid)
    {
        if (auto m = GetModel(m_Assets[m_PlaceAsset].path))
        {
            LevelObject g;
            g.position = m_GhostPos;
            g.rotation = { 0.0f, m_GhostYaw, 0.0f };
            g.model = m_Assets[m_PlaceAsset].path;
            g.scale = m_GhostScale;
            DrawBox(*m, WorldOf(g), Color(0.3f, 0.85f, 1.0f, 1.0f));
        }
    }

    DrawUI();
}

void LevelEditorScene::UpdateEditing()
{
    const ImGuiIO& io = ImGui::GetIO();
    auto& input = InputManager::Get();

    // ---- 選択中の物の移動ギズモ（置いている最中は出さない）----
    // クリック処理より先に呼ぶ: 取っ手の上なら下の物を選び直さない
    if (m_PlaceAsset < 0 && m_Selected >= 0)
    {
        Gizmo::Options opt;
        opt.snap = m_Snap;
        if (Gizmo::Translate("level_editor_selected", m_Level.objects[m_Selected].position, opt))
            MarkDirty();
    }

    // ---- 幽霊（次に置く物）をマウスの下の地面へ ----
    m_GhostValid = false;
    if (m_PlaceAsset >= 0 && !io.WantCaptureMouse && !m_Camera.IsLooking())
    {
        Vector3 hit;
        if (MouseOnGround(hit))
        {
            m_GhostPos = Snap(hit);
            m_GhostValid = true;
        }
    }

    // ---- 左クリック: 置く / 選ぶ ----
    const bool mouseFree = !io.WantCaptureMouse && !Gizmo::IsHovering() && !Gizmo::IsUsing()
        && !m_Camera.IsLooking();
    if (mouseFree && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (m_PlaceAsset >= 0)
        {
            if (m_GhostValid) PlaceGhost();
        }
        else
        {
            Select(PickObject());
        }
    }

    // ---- ショートカット（文字入力中・見回し中は無し。Esc はアプリ終了なので使わない）----
    if (io.WantTextInput || m_Camera.IsLooking()) return;
    const bool ctrl = input.GetKeyPress(VK_CONTROL);
    const bool shift = input.GetKeyPress(VK_SHIFT);

    if (input.GetKeyTrigger('X'))
    {
        if (m_PlaceAsset >= 0) m_PlaceAsset = -1;   // 置くのを止める
        else Select(-1);                            // 選択を外す
    }
    if (input.GetKeyTrigger(VK_DELETE)) DeleteSelected();
    if (ctrl && input.GetKeyTrigger('D')) DuplicateSelected();
    if (ctrl && input.GetKeyTrigger('S')) SaveLevel();
    if (!ctrl && input.GetKeyTrigger('F')) FocusSelected();
    if (!ctrl && input.GetKeyTrigger('R')) RotateStep(shift ? -1.0f : 1.0f);
}

bool LevelEditorScene::MouseOnGround(Vector3& hit)
{
    Vector3 ro, rd;
    Gizmo::GetMouseRay(ro, rd);
    if (rd.y > -1e-4f) return false;   // 地面の方を向いていない
    const float t = -ro.y / rd.y;
    hit = ro + rd * t;
    return std::fabs(hit.x) <= kGroundHalf && std::fabs(hit.z) <= kGroundHalf;
}

// 光線をそれぞれの物の空間へ移して、モデルの包囲箱と当てる（回転・拡縮込みで正しい）。
// 方向は正規化し直さないので、t は世界の光線と同じ尺度 → 一番近い物が取れる
int LevelEditorScene::PickObject()
{
    Vector3 ro, rd;
    Gizmo::GetMouseRay(ro, rd);

    int best = -1;
    float bestT = FLT_MAX;
    for (int i = 0; i < (int)m_Level.objects.size(); ++i)
    {
        const LevelObject& o = m_Level.objects[i];
        auto model = GetModel(o.model);
        if (!model) continue;

        const Matrix inv = WorldOf(o).Invert();
        const Vector3 lo = Vector3::Transform(ro, inv);
        const Vector3 ld = Vector3::TransformNormal(rd, inv);
        float t = 0.0f;
        if (RayBox(lo, ld, model->GetBoundsMin(), model->GetBoundsMax(), t) && t < bestT)
        {
            bestT = t;
            best = i;
        }
    }
    return best;
}

Vector3 LevelEditorScene::Snap(const Vector3& p) const
{
    if (m_Snap <= 0.0f) return p;
    return { std::round(p.x / m_Snap) * m_Snap, p.y, std::round(p.z / m_Snap) * m_Snap };
}

float LevelEditorScene::UnitOf(const std::string& path)
{
    auto m = GetModel(path);
    return m ? m->GetFileUnitScale() : 1.0f;
}

Matrix LevelEditorScene::WorldOf(const LevelObject& o)
{
    Transform t;
    FillTransform(t, o, UnitOf(o.model));
    return t.GetWorldMatrix();
}

// ============================================================
// 編集
// ============================================================
void LevelEditorScene::PlaceGhost()
{
    if (m_PlaceAsset < 0 || m_PlaceAsset >= (int)m_Assets.size()) return;

    LevelObject o;
    o.model = m_Assets[m_PlaceAsset].path;
    o.position = m_GhostPos;
    o.rotation = { 0.0f, m_GhostYaw, 0.0f };
    o.scale = m_GhostScale;
    m_Level.objects.push_back(o);
    m_Selected = (int)m_Level.objects.size() - 1;   // 置くのを止めた時にそのまま動かせる
    MarkDirty();
    RerollGhost();
}

void LevelEditorScene::RerollGhost()
{
    if (m_RandomYaw)
        m_GhostYaw = std::uniform_real_distribution<float>(0.0f, 360.0f)(m_Rng);
    const float lo = (std::min)(m_ScaleMin, m_ScaleMax);
    const float hi = (std::max)(m_ScaleMin, m_ScaleMax);
    m_GhostScale = m_RandomScale ? std::uniform_real_distribution<float>(lo, hi)(m_Rng) : 1.0f;
    // 素材パックの既定倍率（Kenney Retro は 1m 単位の部品なので 2 倍で 1 マス = 2m に揃える）
    if (m_PlaceAsset >= 0 && m_PlaceAsset < (int)m_Assets.size())
        m_GhostScale *= m_Assets[m_PlaceAsset].defaultScale;
}

void LevelEditorScene::Select(int index)
{
    m_Selected = (index >= 0 && index < (int)m_Level.objects.size()) ? index : -1;
}

void LevelEditorScene::DeleteSelected()
{
    if (m_Selected < 0) return;
    m_Level.objects.erase(m_Level.objects.begin() + m_Selected);
    m_Selected = -1;
    MarkDirty();
}

void LevelEditorScene::DuplicateSelected()
{
    if (m_Selected < 0) return;
    LevelObject copy = m_Level.objects[m_Selected];
    copy.position.x += (m_Snap > 0.0f) ? (std::max)(m_Snap, 1.0f) : 1.0f;   // 重ならないよう横へずらす
    m_Level.objects.push_back(copy);
    m_Selected = (int)m_Level.objects.size() - 1;
    MarkDirty();
}

void LevelEditorScene::FocusSelected()
{
    if (m_Selected < 0) return;
    const LevelObject& o = m_Level.objects[m_Selected];
    auto model = GetModel(o.model);
    if (!model)
    {
        m_Camera.Focus(o.position, 1.0f);
        return;
    }
    const Vector3 center = Vector3::Transform(model->GetBoundsCenter(), WorldOf(o));
    const float radius = (model->GetBoundsMax() - model->GetBoundsMin()).Length() * 0.5f
        * o.scale * model->GetFileUnitScale();
    m_Camera.Focus(center, radius);
}

void LevelEditorScene::RotateStep(float sign)
{
    if (m_PlaceAsset >= 0)
    {
        m_GhostYaw = WrapDeg(m_GhostYaw + sign * m_RotateStep);
        return;
    }
    if (m_Selected < 0) return;
    float& yaw = m_Level.objects[m_Selected].rotation.y;
    yaw = WrapDeg(yaw + sign * m_RotateStep);
    MarkDirty();
}

// ============================================================
// 保存
// ============================================================
void LevelEditorScene::NewLevel()
{
    m_Level = LevelData();
    m_Level.name = "untitled";
    strcpy_s(m_NameBuf, m_Level.name.c_str());
    m_Selected = -1;
    m_Dirty = false;
}

void LevelEditorScene::SaveLevel()
{
    const std::string name = SanitizeName(m_NameBuf);
    if (name.empty())
    {
        SetStatus("Name is empty (use letters, digits, _ or -)");
        return;
    }
    strcpy_s(m_NameBuf, name.c_str());
    m_Level.name = name;
    if (LevelIO::Save(m_Level))
    {
        m_Dirty = false;
        SetStatus("Saved " + name + ".json");
        RefreshLevelList();
    }
    else
    {
        SetStatus("Save failed: " + name);
    }
}

void LevelEditorScene::LoadLevel(const std::string& name)
{
    LevelData loaded;
    if (!LevelIO::Load(name, loaded))
    {
        SetStatus("Load failed: " + name);
        return;
    }
    m_Level = std::move(loaded);
    m_Level.name = name;
    strcpy_s(m_NameBuf, name.c_str());
    m_Selected = -1;
    m_Dirty = false;
    SetStatus("Loaded " + name + " (" + std::to_string(m_Level.objects.size()) + " objects)");
}

void LevelEditorScene::RefreshLevelList()
{
    m_LevelFiles = LevelIO::List();
}

void LevelEditorScene::SetStatus(const std::string& msg)
{
    m_Status = msg;
    m_StatusTimer = 4.0f;
    std::cout << "[LevelEditorScene] " << msg << std::endl;
}

// ============================================================
// Render
// ============================================================
void LevelEditorScene::Render(Renderer& renderer)
{
    // 太陽（Unity の Directional Light を右手系へ写した向き。CollisionTestScene::SunDirection と同じ式）
    const float p = DirectX::XMConvertToRadians(m_SunPitch);
    const float y = DirectX::XMConvertToRadians(m_SunYaw);
    Vector3 sunDir(std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p));
    sunDir.Normalize();
    renderer.SetDirectionalLight(sunDir, kSunColor, 1.0f);
    renderer.SetAmbientHemisphere(kAmbientSky, kAmbientGround);

    SceneBase::Render(renderer);   // カメラを渡し、点光源表を上げる

    if (m_MapEdit.IsActive())
    {
        m_MapEdit.Render(renderer);
        return;
    }

    if (m_Ground)
    {
        Transform g;
        g.SetPosition({ 0.0f, -0.06f, 0.0f });
        m_Ground->Draw(renderer, &g);
    }

    for (const auto& o : m_Level.objects)
    {
        auto model = GetModel(o.model);
        if (!model) continue;
        Transform t;
        FillTransform(t, o, model->GetFileUnitScale());
        model->Draw(renderer, &t);
    }

    // 次に置く物をマウスの下に本物の姿で
    if (m_PlaceAsset >= 0 && m_GhostValid)
    {
        if (auto model = GetModel(m_Assets[m_PlaceAsset].path))
        {
            LevelObject g;
            g.position = m_GhostPos;
            g.rotation = { 0.0f, m_GhostYaw, 0.0f };
            g.scale = m_GhostScale;
            Transform t;
            FillTransform(t, g, model->GetFileUnitScale());
            model->Draw(renderer, &t);
        }
    }
}

// 回転込みの包囲箱を線で（12 本）
void LevelEditorScene::DrawBox(const Model& model, const Matrix& world, const Color& color)
{
    const Vector3 lo = model.GetBoundsMin();
    const Vector3 hi = model.GetBoundsMax();
    Vector3 c[8];
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 local((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
        c[i] = Vector3::Transform(local, world);
    }
    static const int kEdges[12][2] =
    {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },   // x 方向
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },   // y 方向
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },   // z 方向
    };
    auto& dbg = DebugManager::Get();
    for (const auto& e : kEdges)
        dbg.AddDebugLine(c[e[0]], c[e[1]], color);
}

// ============================================================
// UI
// ============================================================
void LevelEditorScene::DrawUI()
{
    // ---- モードの切り替え：素材を並べる（json）/ 戦闘の地図（.vmap）----
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f - 170.0f, vp->Pos.y + 40.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Editor Mode", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        int mode = m_MapEdit.IsActive() ? 1 : 0;
        ImGui::RadioButton("Prop Level (.json)", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Battle Map (.vmap)", &mode, 1);
        if ((mode == 1) != m_MapEdit.IsActive())
        {
            m_MapEdit.SetActive(mode == 1);
            // y = 0 の参照格子は起伏のある地図の中を横切るので、地図モードでは消す
            DebugManager::Get().SetShowGrid(mode != 1);
        }
        ImGui::End();
    }
    DrawAssetWindow();
    if (m_MapEdit.IsActive()) m_MapEdit.DrawWindow(m_Camera);
    else DrawLevelWindow();
}

void LevelEditorScene::DrawAssetWindow()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 10.0f, vp->Pos.y + 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level Assets");

    // ---- 置き方 ----
    if (m_PlaceAsset >= 0)
    {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Placing: %s", m_Assets[m_PlaceAsset].name.c_str());
        ImGui::TextDisabled("LMB on ground: place   R: rotate   X: stop");
        if (ImGui::Button("Stop Placing (X)")) m_PlaceAsset = -1;
    }
    else
    {
        ImGui::TextDisabled("Pick an asset, then left-click the ground");
    }

    {
        static const float kSnaps[] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.0f };
        static const char* kSnapNames[] = { "Off", "0.25 m", "0.5 m", "1 m", "2 m (grid cell)" };
        int cur = 0;
        for (int i = 0; i < 5; ++i) if (std::fabs(m_Snap - kSnaps[i]) < 1e-4f) cur = i;
        if (ImGui::Combo("Snap", &cur, kSnapNames, 5)) m_Snap = kSnaps[cur];
    }
    ImGui::DragFloat("Rotate Step (R)", &m_RotateStep, 1.0f, 1.0f, 180.0f, "%.0f deg");
    if (ImGui::Checkbox("Random Yaw", &m_RandomYaw)) RerollGhost();
    if (ImGui::Checkbox("Random Scale", &m_RandomScale)) RerollGhost();
    if (m_RandomScale)
        ImGui::DragFloatRange2("Scale Range", &m_ScaleMin, &m_ScaleMax, 0.01f, 0.1f, 5.0f);

    ImGui::Separator();
    ImGui::InputTextWithHint("##filter", "filter (e.g. tree, rock)", m_Filter, sizeof(m_Filter));
    ImGui::Text("%d assets", (int)m_Assets.size());

    // ---- 一覧（素材パック → 名前の頭 の 2 段。同じ素材をもう一度押すと置くのを止める）----
    // 種類の段は既定で閉じる（Retro Fantasy だけで 20 近くある）。絞り込み中は当たりのある段を開く
    const bool filtering = m_Filter[0] != '\0';
    ImGui::BeginChild("asset_list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (size_t p = 0; p < m_Assets.size();)
    {
        const std::string& pack = m_Assets[p].pack;
        size_t packEnd = p;
        while (packEnd < m_Assets.size() && m_Assets[packEnd].pack == pack) ++packEnd;

        ImGui::PushID(pack.c_str());
        if (ImGui::TreeNodeEx(pack.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = p; i < packEnd;)
            {
                const std::string& group = m_Assets[i].group;
                size_t end = i;
                while (end < packEnd && m_Assets[end].group == group) ++end;

                bool any = !filtering;
                for (size_t k = i; k < end && !any; ++k)
                    any = ContainsNoCase(m_Assets[k].name, m_Filter);

                if (any)
                {
                    if (filtering) ImGui::SetNextItemOpen(true);
                    if (ImGui::TreeNode(group.c_str()))
                    {
                        for (size_t k = i; k < end; ++k)
                        {
                            const PropAsset& a = m_Assets[k];
                            if (!ContainsNoCase(a.name, m_Filter)) continue;
                            const bool on = (m_PlaceAsset == (int)k);
                            if (ImGui::Selectable(a.name.c_str(), on))
                            {
                                m_PlaceAsset = on ? -1 : (int)k;
                                if (m_PlaceAsset >= 0)
                                {
                                    GetModel(a.path);   // 最初の 1 回はここで読む（置く瞬間に止まらないよう）
                                    RerollGhost();
                                }
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                i = end;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        p = packEnd;
    }
    ImGui::EndChild();
    ImGui::End();
}

void LevelEditorScene::DrawLevelWindow()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 350.0f, vp->Pos.y + 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    // ---- ファイル ----
    ImGui::InputText("Name", m_NameBuf, sizeof(m_NameBuf));
    if (ImGui::Button("Save (Ctrl+S)")) SaveLevel();
    ImGui::SameLine();
    if (ImGui::Button("New"))
    {
        if (m_Dirty) m_Pending = Pending::New;   // 確認窓は DrawDiscardPopup が開く
        else NewLevel();
    }
    ImGui::SameLine();
    if (m_Dirty) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "* unsaved");
    else ImGui::TextDisabled("saved");

    if (ImGui::TreeNode("Load"))
    {
        if (ImGui::SmallButton("Refresh")) RefreshLevelList();
        if (m_LevelFiles.empty()) ImGui::TextDisabled("(no files in %s)", LevelIO::kDir);
        for (const auto& name : m_LevelFiles)
        {
            if (ImGui::Selectable(name.c_str(), name == m_Level.name))
            {
                if (m_Dirty) { m_Pending = Pending::Load; m_PendingName = name; }
                else LoadLevel(name);
            }
        }
        ImGui::TreePop();
    }
    DrawDiscardPopup();

    if (m_StatusTimer > 0.0f) ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", m_Status.c_str());

    // ---- 選択中の物 ----
    ImGui::Separator();
    if (m_Selected >= 0)
    {
        LevelObject& o = m_Level.objects[m_Selected];
        ImGui::Text("Selected #%d: %s", m_Selected, DisplayName(fs::path(o.model).stem().string()).c_str());
        if (ImGui::DragFloat3("Position", &o.position.x, 0.05f)) MarkDirty();
        if (ImGui::DragFloat3("Rotation", &o.rotation.x, 1.0f, -360.0f, 360.0f, "%.0f")) MarkDirty();
        if (ImGui::DragFloat("Scale", &o.scale, 0.01f, 0.05f, 20.0f)) MarkDirty();
        if (ImGui::Button("Focus (F)")) FocusSelected();
        ImGui::SameLine();
        if (ImGui::Button("Duplicate (Ctrl+D)")) DuplicateSelected();
        ImGui::SameLine();
        if (ImGui::Button("Delete (Del)")) DeleteSelected();
    }
    else
    {
        ImGui::TextDisabled("Nothing selected (LMB on an object)");
    }

    // ---- 置いた物の一覧 ----
    ImGui::Separator();
    ImGui::Text("Objects: %d", (int)m_Level.objects.size());
    ImGui::BeginChild("object_list", ImVec2(0, 220.0f), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)m_Level.objects.size(); ++i)
    {
        const std::string label = std::to_string(i) + "  "
            + DisplayName(fs::path(m_Level.objects[i].model).stem().string());
        ImGui::PushID(i);
        if (ImGui::Selectable(label.c_str(), i == m_Selected)) Select(i);
        ImGui::PopID();
    }
    ImGui::EndChild();

    // ---- 照明 / 操作説明 ----
    if (ImGui::CollapsingHeader("Lighting"))
    {
        ImGui::SliderFloat("Sun Pitch", &m_SunPitch, 0.0f, 90.0f, "%.0f");
        ImGui::SliderFloat("Sun Yaw", &m_SunYaw, -180.0f, 180.0f, "%.0f");
    }
    if (ImGui::CollapsingHeader("Controls"))
    {
        ImGui::BulletText("RMB drag: look,  RMB + WASD/QE: fly,  Shift: fast");
        ImGui::BulletText("Wheel: move forward / back");
        ImGui::BulletText("LMB: place (asset picked) / select object");
        ImGui::BulletText("Gizmo: drag the arrows / planes to move");
        ImGui::BulletText("R / Shift+R: rotate,  F: focus");
        ImGui::BulletText("Ctrl+D: duplicate,  Del: delete");
        ImGui::BulletText("X: stop placing / deselect,  Ctrl+S: save");
        ImGui::BulletText("Esc quits the app (engine-wide)");
    }

    ImGui::End();
}

// 未保存の変更を捨てるかの確認。OpenPopup と BeginPopupModal は同じ ID の場所で
// 呼ぶ必要があるので（TreeNode の中で開くと ID がずれる）、ここで両方やる
void LevelEditorScene::DrawDiscardPopup()
{
    const char* kId = "Discard changes?";
    if (m_Pending != Pending::None && !ImGui::IsPopupOpen(kId))
        ImGui::OpenPopup(kId);
    if (!ImGui::BeginPopupModal(kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("The current level has unsaved changes.");
    if (ImGui::Button("Discard"))
    {
        if (m_Pending == Pending::New) NewLevel();
        else if (m_Pending == Pending::Load) LoadLevel(m_PendingName);
        m_Pending = Pending::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_Pending = Pending::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
