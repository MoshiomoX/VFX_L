// ============================================================
// ShieldBubble.cpp
// ============================================================
#include "ECS/System/ShieldBubble.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/Model.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char* kModel = "Assets/VFX/Mesh/fanghuzhao2.FBX";
}

void ShieldBubble::Update(float dt, float shieldNow, bool hit, bool visible)
{
    m_Time += dt;
    m_Hit = (std::max)(0.0f, m_Hit - ((hitTime > 0.0f) ? dt / hitTime : 1.0f));
    if (hit) m_Hit = 1.0f;

    const bool show = visible && shieldNow > 0.0f;
    if (show && !m_Shown) m_Appear = 0.0f;   // 0 から戻った（開始時・回復の始まり）→ 膨らみながら出す
    m_Shown = show;
    if (m_Shown)
        m_Appear = (std::min)(1.0f, m_Appear + ((appearTime > 0.0f) ? dt / appearTime : 1.0f));
}

void ShieldBubble::Submit(VFXMeshRenderer& renderer, const Vector3& pos)
{
    if (!enabled || !m_Shown) return;
    if (!m_Loaded)
    {
        m_Model = ResourceManager::Get().LoadModel(kModel);
        m_Loaded = true;
    }
    if (!m_Model) return;

    // 出る時は 0.85 倍から膨らむ。受けた瞬間は少し膨らんで赤く、明るく
    const float grow = 1.0f - (1.0f - m_Appear) * (1.0f - m_Appear);   // ease-out
    const float s = (0.85f + 0.15f * grow) * (1.0f + hitScale * m_Hit);
    const float pulse = 1.0f + breathe * std::sin(m_Time * breatheSpeed);
    const Vector3 col = Vector3::Lerp(color, hitColor, m_Hit);
    const float a = (alpha + (hitAlpha - alpha) * m_Hit) * pulse * grow;
    const float yaw = DirectX::XMConvertToRadians(spinDeg * m_Time);

    VFXMeshDrawItem item;
    item.model = m_Model.get();
    item.params.tint = Vector4(col.x, col.y, col.z, a);
    item.params.intensity = intensity;
    item.params.noVertexColor = 1.0f;   // fanghuzhao2 の頂点色（材質の濃い青、赤 0）を掛けない。掛けると赤が出ない
    item.blend = blend;
    item.twoSided = true;

    // 上の半球と、上下を返した下の半球（継ぎ目はカプセルの中心の少し下）
    const Matrix at = Matrix::CreateTranslation(pos + Vector3(0.0f, offsetY, 0.0f));
    const Matrix upper = Matrix::CreateScale(scaleXZ * s, scaleUp * s, scaleXZ * s) * Matrix::CreateRotationY(yaw) * at;
    const Matrix lower = Matrix::CreateScale(scaleXZ * s, scaleDown * s, scaleXZ * s)
        * Matrix::CreateFromYawPitchRoll(yaw, DirectX::XM_PI, 0.0f) * at;
    auto submitBoth = [&](VFXMeshDrawItem& it)
        {
            it.world = upper; renderer.Submit(it);
            it.world = lower; renderer.Submit(it);
        };
    submitBoth(item);

    // 受けた瞬間：加算だけだと草の緑に赤が足されて黄色・白に寄るので、半透明の赤を 1 枚重ねて下の色を赤へ引く
    if (m_Hit > 0.0f && hitFill > 0.0f)
    {
        VFXMeshDrawItem fill = item;
        fill.blend = 1;
        fill.params.tint = Vector4(hitColor.x, hitColor.y, hitColor.z, hitFill * m_Hit);
        fill.params.intensity = 1.0f;
        submitBoth(fill);
    }
}

void ShieldBubble::DrawImGui()
{
    ImGui::Checkbox("Shield bubble (while shield > 0)", &enabled);
    if (!ImGui::TreeNode("Bubble look"))
        return;
    ImGui::ColorEdit3("Color##bubble", &color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Alpha##bubble", &alpha, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Intensity##bubble", &intensity, 0.02f, 0.0f, 10.0f);
    ImGui::Combo("Blend##bubble", &blend, "Additive\0Alpha\0");
    ImGui::DragFloat("Spin deg/s##bubble", &spinDeg, 0.5f, -360.0f, 360.0f);
    ImGui::DragFloat("Breathe##bubble", &breathe, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Breathe speed##bubble", &breatheSpeed, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Appear time##bubble", &appearTime, 0.01f, 0.0f, 3.0f);
    ImGui::ColorEdit3("Hit color##bubble", &hitColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Hit time##bubble", &hitTime, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Hit alpha##bubble", &hitAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Hit scale##bubble", &hitScale, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Hit fill##bubble", &hitFill, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Scale XZ##bubble", &scaleXZ, 0.0005f, 0.0f, 1.0f, "%.4f");
    ImGui::DragFloat("Scale up##bubble", &scaleUp, 0.0005f, 0.0f, 1.0f, "%.4f");
    ImGui::DragFloat("Scale down##bubble", &scaleDown, 0.0005f, 0.0f, 1.0f, "%.4f");
    ImGui::DragFloat("Offset Y##bubble", &offsetY, 0.01f, -2.0f, 2.0f);
    if (ImGui::Button("Test hit flash##bubble")) m_Hit = 1.0f;
    ImGui::TreePop();
}
