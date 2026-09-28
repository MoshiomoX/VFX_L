// ============================================================
// SceneLighting.cpp
// ============================================================
#include "Graphics/Light/SceneLighting.h"
#include "Graphics/Light/PointLightManager.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/SkyRenderer.h"
#include "World/GridWorld.h"
#include "Camera/CameraBase.h"
#include "Debug/DebugManager.h"
#include "Debug/Gizmo.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX::SimpleMath;

// ============================================================
// 太陽の向き
// Unity の回転（X = pitch, Y = yaw, Z = 0）で前方 (0,0,1) を回したのと同じ。
// 既定の (50, +30) は Unity の新規シーンの Directional Light (50, -30) を
// 右手系へ写した物（X が鏡写し。画面上の当たり方が同じになる）
// ============================================================
Vector3 SceneLighting::SunDirection() const
{
    const float p = DirectX::XMConvertToRadians(m_SunPitch);
    const float y = DirectX::XMConvertToRadians(m_SunYaw);
    Vector3 d(std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p));
    d.Normalize();
    return d;
}

bool SceneLighting::Init(ID3D11Device* device)
{
    // TEMP-TEST: 自己テストで太陽の高さを変える（影の長さを見る）
    char env[16] = {};
    if (GetEnvironmentVariableA("VFXL_SUN_PITCH", env, sizeof(env)) > 0)
        m_SunPitch = (float)atof(env);
    return m_Sky.Initialize(device);
}

void SceneLighting::Apply(Renderer& renderer) const
{
    renderer.SetDirectionalLight(SunDirection(),
        { m_LightColor[0], m_LightColor[1], m_LightColor[2] },
        m_LightIntensity);
    renderer.SetAmbientHemisphere(
        { m_AmbientSky[0], m_AmbientSky[1], m_AmbientSky[2] },
        { m_AmbientGround[0], m_AmbientGround[1], m_AmbientGround[2] });

    const float* fog = m_FogUseHorizon ? m_SkyHorizon : m_FogColor;
    renderer.SetFog({ fog[0], fog[1], fog[2] }, m_FogStart, m_FogEnd, m_FogOn ? m_FogMax : 0.0f);
    renderer.SetAlbedoSrgb(m_AlbedoSrgb);
}

void SceneLighting::ClearFog(Renderer& renderer)
{
    renderer.SetFog({ 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f, 0.0f);
    renderer.SetAlbedoSrgb(false);
}

// ============================================================
// 空（画面全体。場面の描画の一番最初）
// 太陽のにじみと円盤は平行光の向きと色から
// ============================================================
void SceneLighting::DrawSky(ID3D11DeviceContext* context, CameraBase* camera) const
{
    if (!m_SkyOn) return;
    SkyRenderer::Params p;
    p.zenith = { m_SkyZenith[0], m_SkyZenith[1], m_SkyZenith[2] };
    p.horizon = { m_SkyHorizon[0], m_SkyHorizon[1], m_SkyHorizon[2] };
    p.below = { m_SkyBelow[0], m_SkyBelow[1], m_SkyBelow[2] };
    p.sunDir = SunDirection();
    p.sunColor = Vector3(m_LightColor[0], m_LightColor[1], m_LightColor[2]) * m_LightIntensity;
    p.zenithCurve = m_SkyCurve;
    p.sunGlow = m_SunGlow;
    p.sunGlowPower = m_SunGlowPower;
    p.sunDisk = m_SunDisk;
    m_Sky.Render(context, camera, p);
}

// ============================================================
// 場景光源: 点光源表へ積む
// 表は Application が毎フレーム頭で空にする。Upload（UpdateGameplay の
// CollectLights か SceneBase::Render）より前に積めば、そのフレームから効く
// ============================================================
void SceneLighting::SubmitPointLights() const
{
    if (!m_SceneLightOn) return;
    PointLightManager::Get().Add(m_SceneLightPos,
        Vector3(m_SceneLightColor[0], m_SceneLightColor[1], m_SceneLightColor[2]),
        m_SceneLightRadius, m_SceneLightIntensity);
}

void SceneLighting::DrawMarkers(const Vector3* player, const GridWorld& grid,
    CameraBase* camera, bool allowGizmo)
{
    if (player) DrawSunMarker(*player);
    DrawSceneLightGizmo(grid, camera, allowGizmo);
}

// ============================================================
// 太陽の目印（Unity の Directional Light のギズモと同じ考え）
// 平行光に位置は無いので、玩家の頭上に「光がどこから来るか」を描く:
//   光線に垂直な円 = 太陽、円から出る平行な短い線 = 光線、
//   中心から頭へ届く矢印 = この向きで当たっている
// ============================================================
void SceneLighting::DrawSunMarker(const Vector3& player) const
{
    if (!m_ShowSunMarker) return;

    const Vector3 dir = SunDirection();
    const Vector3 head = player + Vector3(0.0f, 2.0f, 0.0f);
    constexpr float kArrowLen = 1.8f;   // カメラが低くても太陽の円が画面に入る長さ
    const Vector3 sun = head - dir * kArrowLen;

    // 円を張る 2 軸（光線に垂直）。真上から照らす時は Y との外積が潰れるので X を使う
    Vector3 u = dir.Cross(Vector3::UnitY);
    if (u.LengthSquared() < 1e-4f) u = Vector3::UnitX;
    u.Normalize();
    Vector3 v = dir.Cross(u);
    v.Normalize();

    auto& dbg = DebugManager::Get();
    const Color col(m_LightColor[0], m_LightColor[1], m_LightColor[2], 1.0f);

    constexpr float kR = 0.35f;
    constexpr int kSeg = 24;
    Vector3 prev = sun + u * kR;
    for (int i = 1; i <= kSeg; ++i)
    {
        const float a = DirectX::XM_2PI * (float)i / (float)kSeg;
        const Vector3 p = sun + (u * std::cos(a) + v * std::sin(a)) * kR;
        dbg.AddDebugLine(prev, p, col);
        prev = p;
    }

    // 円周から平行な光線 4 本（平行光なので全部同じ向き）
    for (int k = 0; k < 4; ++k)
    {
        const float a = DirectX::XM_PIDIV2 * (float)k;
        const Vector3 o = (u * std::cos(a) + v * std::sin(a)) * kR;
        dbg.AddDebugLine(sun + o, sun + o + dir * 0.6f, col);
    }

    // 中心から頭への矢印
    dbg.AddDebugLine(sun, head, col);
    dbg.AddDebugLine(head, head - dir * 0.3f + u * 0.12f, col);
    dbg.AddDebugLine(head, head - dir * 0.3f - u * 0.12f, col);
}

// ============================================================
// 場景光源: 3D ギズモ（左ドラッグで位置）と目印
// 目印 = 電球の小球 + 真下への線 + 光の届く球（半径 R）が地面を切る円。
// この円の外は場景光源では一切照らされない。
// ※ギズモで動かした位置は次のフレームの SubmitPointLights から効く（1 フレーム遅れ）
// ============================================================
void SceneLighting::DrawSceneLightGizmo(const GridWorld& grid, CameraBase* camera, bool allowGizmo)
{
    if (!camera) return;

    // 掴んでいる状態の追跡はここで進むので、ギズモを出さないフレームも呼ぶ
    Gizmo::BeginFrame(camera->GetViewMatrix(), camera->GetProjectionMatrix());

    // 切っている時はギズモも目印も出さない
    if (!m_SceneLightOn) return;

    // 背包・升級・呪文書を開いている間は出さない（左クリックをそちらと取り合う）
    if (m_SceneLightGizmo && allowGizmo)
    {
        Gizmo::Options opt;
        opt.label = "Light";
        Gizmo::Translate("scene_light", m_SceneLightPos, opt);
    }

    if (!m_SceneLightMarker) return;

    auto& dbg = DebugManager::Get();
    const Color bulb(m_SceneLightColor[0], m_SceneLightColor[1], m_SceneLightColor[2], 1.0f);
    const Color dim(m_SceneLightColor[0] * 0.6f, m_SceneLightColor[1] * 0.6f, m_SceneLightColor[2] * 0.6f, 1.0f);

    dbg.DrawWireSphere(m_SceneLightPos, 0.25f, bulb);

    const float groundY = grid.SampleHeight(m_SceneLightPos.x, m_SceneLightPos.z);
    const Vector3 foot(m_SceneLightPos.x, groundY, m_SceneLightPos.z);
    dbg.AddDebugLine(m_SceneLightPos, foot, dim);

    // 照射範囲と地面の交円（光源が地面から R 以上離れていれば地面には届かない）
    const float h = m_SceneLightPos.y - groundY;
    const float R = m_SceneLightRadius;
    if (std::fabs(h) >= R) return;

    const float r = std::sqrt(R * R - h * h);
    const float y = groundY + 0.05f;   // 地面と重なってちらつかないよう少し浮かす
    constexpr int kSeg = 48;
    Vector3 prev(foot.x + r, y, foot.z);
    for (int i = 1; i <= kSeg; ++i)
    {
        const float a = DirectX::XM_2PI * (float)i / (float)kSeg;
        const Vector3 p(foot.x + r * std::cos(a), y, foot.z + r * std::sin(a));
        dbg.AddDebugLine(prev, p, dim);
        prev = p;
    }
}

// ============================================================
// ImGui: Lighting 面板
// ============================================================
void SceneLighting::DrawImGui(const Vector3* player)
{
    if (!ImGui::CollapsingHeader("Lighting"))
        return;

    // ---- 太陽（平行光）= 主光 ----
    ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.8f, 1.0f), "Sun (directional, main light)");
    ImGui::SliderFloat("Pitch (deg)", &m_SunPitch, 0.0f, 90.0f, "%.0f");   // 0 = 真横、90 = 真上から
    ImGui::SliderFloat("Yaw (deg)", &m_SunYaw, -180.0f, 180.0f, "%.0f");
    ImGui::ColorEdit3("Light Color", m_LightColor);
    ImGui::DragFloat("Intensity", &m_LightIntensity, 0.02f, 0.0f, 5.0f);
    ImGui::ColorEdit3("Ambient Sky", m_AmbientSky);
    ImGui::ColorEdit3("Ambient Ground", m_AmbientGround);
    ImGui::Checkbox("Sun Marker", &m_ShowSunMarker);
    ImGui::SameLine();
    if (ImGui::Button("Reset (Unity default)"))
    {
        m_SunPitch = kSunPitchDefault;
        m_SunYaw = kSunYawDefault;
        m_LightColor[0] = 1.0f; m_LightColor[1] = 0.957f; m_LightColor[2] = 0.839f;
        m_LightIntensity = 1.0f;
    }

    // ---- 場景光源（位置あり。既定は切）----
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "Scene Point Light (positioned, local)");
    ImGui::Checkbox("Enabled##scene_light", &m_SceneLightOn);
    ImGui::SameLine();
    ImGui::Checkbox("Gizmo##scene_light", &m_SceneLightGizmo);
    ImGui::SameLine();
    ImGui::Checkbox("Marker##scene_light", &m_SceneLightMarker);
    ImGui::DragFloat3("Position##scene_light", &m_SceneLightPos.x, 0.05f);
    ImGui::ColorEdit3("Color##scene_light", m_SceneLightColor);
    ImGui::DragFloat("Radius##scene_light", &m_SceneLightRadius, 0.1f, 0.5f, 100.0f);
    ImGui::DragFloat("Intensity##scene_light", &m_SceneLightIntensity, 0.02f, 0.0f, 20.0f);
    if (ImGui::Button("Move Above Player##scene_light") && player)
        m_SceneLightPos = *player + Vector3(0.0f, 4.0f, 0.0f);
    ImGui::TextDisabled("Gizmo: left-drag the handles (camera is right-drag)");

    // ---- 空と霧（線形 HDR）----
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Sky & Fog");
    ImGui::Checkbox("Sky##sky", &m_SkyOn);
    ImGui::ColorEdit3("Zenith##sky", m_SkyZenith, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit3("Horizon##sky", m_SkyHorizon, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::ColorEdit3("Below##sky", m_SkyBelow, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Zenith Curve##sky", &m_SkyCurve, 0.1f, 2.0f);
    ImGui::DragFloat("Sun Glow##sky", &m_SunGlow, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Sun Glow Power##sky", &m_SunGlowPower, 0.5f, 1.0f, 256.0f);
    ImGui::DragFloat("Sun Disk##sky", &m_SunDisk, 0.1f, 0.0f, 20.0f);
    ImGui::Checkbox("sRGB Textures##sky", &m_AlbedoSrgb);
    ImGui::SameLine();
    ImGui::TextDisabled("(model albedo decoded to linear)");
    ImGui::Checkbox("Fog##fog", &m_FogOn);
    ImGui::SameLine();
    ImGui::Checkbox("Fog = Horizon##fog", &m_FogUseHorizon);
    if (!m_FogUseHorizon)
        ImGui::ColorEdit3("Fog Color##fog", m_FogColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::DragFloatRange2("Fog Start/End (m)##fog", &m_FogStart, &m_FogEnd, 0.5f, 0.0f, 1000.0f);
    ImGui::SliderFloat("Fog Max##fog", &m_FogMax, 0.0f, 1.0f);
}
