// ============================================================
// CastCircles.cpp
// ============================================================
#include "ECS/System/CastCircles.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Mesh/Vertex3D.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "UI/UIDeco.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr size_t kMaxHands = 16;

    float EaseOut(float t) { t = std::clamp(t, 0.0f, 1.0f); const float u = 1.0f - t; return 1.0f - u * u * u; }

    // 局所の +Y（板の法線）を n へ向ける行列（行ベクトルの決まり：行 0 = X の行き先、行 1 = Y、行 2 = Z）
    Matrix FaceTo(const Vector3& n)
    {
        Vector3 y = n;
        if (y.LengthSquared() < 1e-6f) y = Vector3::Up;
        y.Normalize();
        Vector3 x = Vector3::Up.Cross(y);
        if (x.LengthSquared() < 1e-4f) x = Vector3::UnitX;
        x.Normalize();
        const Vector3 z = x.Cross(y);
        return Matrix(x, y, z);
    }
}

void CastCircles::EnsureResources()
{
    if (m_Ready) return;
    m_Ready = true;
    ID3D11Device* device = ResourceManager::Get().GetDevice();
    if (!device) return;

    // 1m 四方の板（XZ、法線 +Y）。両面で描くので巻き順は気にしない
    std::vector<VERTEX_3D> v(4);
    const Vector3 p[4] = { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { -0.5f, 0.0f, 0.5f } };
    const Vector2 uv[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
    for (int i = 0; i < 4; ++i)
    {
        v[i].position = p[i];
        v[i].normal = Vector3::Up;
        v[i].tangent = Vector3::UnitX;
        v[i].uv = uv[i];
        v[i].color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    const std::vector<unsigned int> idx = { 0, 2, 1, 0, 3, 2 };
    auto mesh = std::make_shared<Mesh>();
    if (!mesh->Create(device, v, idx)) return;
    m_Quad = std::make_shared<Model>();
    m_Quad->AddSubMesh(mesh);
}

void CastCircles::OnCast(const Vector3& pos, const Vector3& dir, const Vector3& color)
{
    m_SinceCast = 0.0f;
    m_TargetColor = color;
    if (m_FootAct <= 0.01f) m_FootColor = color;   // 消えていた所から出る時は最初からその色
    m_FootPulse = 1.0f;

    if (!handEnabled) return;
    if (m_Hands.size() >= kMaxHands) m_Hands.erase(m_Hands.begin());
    Hand h;
    h.pos = pos;
    h.dir = dir;
    h.color = color;
    h.spin0 = std::fmod(m_Clock * 97.0f, 6.2831853f);   // 毎回違う角度から回り始める
    m_Hands.push_back(h);
}

void CastCircles::Update(float dt, const Vector3& feet, bool visible)
{
    m_Clock += dt;
    m_Feet = feet;
    m_SinceCast += dt;

    // 足元：撃った直後は濃くなり、holdTime を過ぎたら消えていく
    const bool on = visible && footEnabled && m_SinceCast < holdTime;
    if (on) m_FootAct = (std::min)(1.0f, m_FootAct + dt / (std::max)(fadeIn, 0.01f));
    else    m_FootAct = (std::max)(0.0f, m_FootAct - dt / (std::max)(fadeOut, 0.01f));
    m_FootSpin += DirectX::XMConvertToRadians(footSpinDeg) * dt;
    m_FootPulse = (std::max)(0.0f, m_FootPulse - dt * 6.0f);
    m_FootColor = Vector3::Lerp(m_FootColor, m_TargetColor, (std::min)(1.0f, dt * 8.0f));

    for (auto& h : m_Hands) h.age += dt;
    m_Hands.erase(std::remove_if(m_Hands.begin(), m_Hands.end(),
        [&](const Hand& h) { return h.age >= handLife; }), m_Hands.end());
}

void CastCircles::Submit(VFXMeshRenderer& renderer)
{
    EnsureResources();
    if (!m_Quad) return;
    const auto& tex = UIDeco::Tex();

    VFXMeshDrawItem item;
    item.model = m_Quad.get();
    item.blend = 0;   // 加算
    item.twoSided = true;
    item.params.noVertexColor = 1.0f;

    // ---- 足元：外の花の魔法陣 + 内側に逆回りの星 ----
    if (m_FootAct > 0.001f && tex.circleFlower)
    {
        const float a = m_FootAct * m_FootAct * (3.0f - 2.0f * m_FootAct);
        const float s = footSize * (1.0f + pulse * m_FootPulse) * (0.85f + 0.15f * a);
        const Matrix at = Matrix::CreateTranslation(m_Feet + Vector3(0.0f, footLift, 0.0f));

        item.mainTex = tex.circleFlower.get();
        item.params.tint = Vector4(m_FootColor.x, m_FootColor.y, m_FootColor.z, footAlpha * a);
        item.params.intensity = footIntensity;
        item.world = Matrix::CreateScale(s, 1.0f, s) * Matrix::CreateRotationY(m_FootSpin) * at;
        renderer.Submit(item);

        if (tex.circleStar)
        {
            item.mainTex = tex.circleStar.get();
            item.params.tint.w = footAlpha * a * 0.8f;
            item.world = Matrix::CreateScale(s * 0.62f, 1.0f, s * 0.62f) * Matrix::CreateRotationY(-m_FootSpin * 1.6f)
                * Matrix::CreateTranslation(m_Feet + Vector3(0.0f, footLift + 0.01f, 0.0f));
            renderer.Submit(item);
        }
    }

    // ---- 手の前：ぱっと開いて、後半で消える ----
    if (tex.circleStar)
        for (const auto& h : m_Hands)
        {
            const float t = h.age / (std::max)(handLife, 0.01f);
            const float s = handSize * (0.3f + 0.7f * EaseOut(t * 2.5f));
            const float a = 1.0f - std::clamp((t - 0.45f) / 0.55f, 0.0f, 1.0f);
            Vector3 dir = h.dir;
            if (dir.LengthSquared() < 1e-6f) dir = Vector3::UnitZ;
            dir.Normalize();
            item.mainTex = tex.circleStar.get();
            item.params.tint = Vector4(h.color.x, h.color.y, h.color.z, a);
            item.params.intensity = handIntensity;
            item.world = Matrix::CreateScale(s, 1.0f, s)
                * Matrix::CreateRotationY(h.spin0 + DirectX::XMConvertToRadians(handSpinDeg) * h.age)
                * FaceTo(dir) * Matrix::CreateTranslation(h.pos + dir * handOffset);
            renderer.Submit(item);
        }
}

void CastCircles::DrawImGui()
{
    ImGui::Checkbox("Foot circle", &footEnabled); ImGui::SameLine();
    ImGui::Checkbox("Hand circle", &handEnabled);
    ImGui::DragFloat("Foot size (m)", &footSize, 0.05f, 0.5f, 8.0f);
    ImGui::DragFloat("Foot lift (m)", &footLift, 0.01f, 0.0f, 1.0f);
    ImGui::SliderFloat("Foot alpha", &footAlpha, 0.0f, 1.0f);
    ImGui::DragFloat("Foot intensity", &footIntensity, 0.05f, 0.0f, 6.0f);
    ImGui::DragFloat("Foot spin (deg/s)", &footSpinDeg, 1.0f, -360.0f, 360.0f);
    ImGui::DragFloat("Foot hold (s)", &holdTime, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Foot fade out (s)", &fadeOut, 0.01f, 0.01f, 3.0f);
    ImGui::DragFloat("Hand size (m)", &handSize, 0.01f, 0.1f, 3.0f);
    ImGui::DragFloat("Hand life (s)", &handLife, 0.01f, 0.05f, 1.5f);
    ImGui::DragFloat("Hand intensity", &handIntensity, 0.05f, 0.0f, 6.0f);
}
