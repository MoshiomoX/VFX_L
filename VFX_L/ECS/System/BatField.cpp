// ============================================================
// BatField.cpp
// ============================================================
#include "ECS/System/BatField.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Mesh/Vertex3D.h"
#include "Graphics/Model/Model.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Light/PointLightManager.h"

#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 平らな多角形（XZ、法線 +Y）を根元の点から扇に三角形に分ける
    std::shared_ptr<Model> MakeFan(ID3D11Device* device, const std::vector<Vector3>& rim)
    {
        std::vector<VERTEX_3D> v;
        std::vector<unsigned int> idx;
        for (const auto& p : rim)
        {
            VERTEX_3D q;
            q.position = p;
            q.normal = Vector3::Up;
            q.tangent = Vector3::UnitX;
            q.uv = Vector2(p.x, p.z);
            q.color = Vector4(1, 1, 1, 1);
            v.push_back(q);
        }
        for (unsigned int i = 1; i + 1 < (unsigned int)rim.size(); ++i)
            idx.insert(idx.end(), { 0u, i, i + 1 });
        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, v, idx)) return nullptr;
        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }
}

void BatField::EnsureModels()
{
    if (m_Tried) return;
    m_Tried = true;
    ID3D11Device* device = ResourceManager::Get().GetDevice();
    if (!device) return;
    m_Sphere = PrimitiveBuilder::CreateSphere(device, 0.5f, { 1, 1, 1, 1 }, 12);
    // 右の翼：根元 (0,0,0) から、前縁が先端へ、後縁は指の骨の間が 3 つえぐれた膜
    m_Wing = MakeFan(device, {
        { 0.00f, 0.0f,  0.00f },
        { 0.00f, 0.0f,  0.10f },
        { 0.18f, 0.0f,  0.13f },
        { 0.36f, 0.0f,  0.10f },
        { 0.46f, 0.0f,  0.02f },
        { 0.40f, 0.0f, -0.10f },
        { 0.33f, 0.0f, -0.05f },
        { 0.26f, 0.0f, -0.16f },
        { 0.19f, 0.0f, -0.08f },
        { 0.11f, 0.0f, -0.17f },
        { 0.04f, 0.0f, -0.10f },
    });
    // 耳：XY の三角（前を向く）
    m_Ear = MakeFan(device, { { -0.025f, 0.0f, 0.0f }, { 0.025f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.07f } });
}

void BatField::Submit(VFXMeshRenderer& renderer, const std::vector<WeaponSystem::Bat>& bats)
{
    if (!enabled || bats.empty()) return;
    EnsureModels();
    if (!m_Sphere || !m_Wing || !m_Ear) return;

    for (const auto& b : bats)
    {
        // ---- 向き：前 = 飛ぶ向き（少しだけ上下も）。翼は羽ばたき ----
        Vector3 f = b.vel;
        if (f.LengthSquared() < 1e-4f) f = Vector3::UnitZ;
        f.y *= 0.4f;
        f.Normalize();
        Vector3 r = Vector3::Up.Cross(f);
        if (r.LengthSquared() < 1e-4f) r = Vector3::UnitX;
        r.Normalize();
        const Vector3 u = f.Cross(r);
        const Matrix rot(r, u, f);
        const Matrix world = Matrix::CreateScale(scale) * rot * Matrix::CreateTranslation(b.pos);
        const float flap = std::sin(b.age * flapFreq + b.seed) * flapAmp + 0.15f;
        // 出た直後 0.12 秒で大きくなる
        const float grow = (std::min)(b.age / 0.12f, 1.0f);
        const Matrix appear = Matrix::CreateScale(0.3f + 0.7f * grow);

        VFXMeshDrawItem item;
        item.twoSided = true;
        item.params.noVertexColor = 1.0f;

        // ---- 血の赤の細い尾：通った位置をつないだ細い棒（頭が太く、後ろへ細く薄く）----
        item.model = m_Sphere.get();
        item.blend = 1;
        item.params.shade = 0.0f;
        item.params.intensity = trailIntensity;
        {
            Vector3 prev = b.pos;
            const int n = b.trailN;
            for (int t = 0; t < n; ++t)
            {
                const Vector3 p = b.trail[t];
                Vector3 seg = prev - p;
                const float len = seg.Length();
                if (len > 1e-3f)
                {
                    seg /= len;
                    const float k = 1.0f - (float)t / (float)WeaponSystem::Bat::kTrail;   // 頭 1 → 尻尾 0
                    const float w = trailWidth * (0.25f + 0.75f * k);
                    Vector3 side = Vector3::Up.Cross(seg);
                    if (side.LengthSquared() < 1e-4f) side = Vector3::UnitX;
                    side.Normalize();
                    const Vector3 up2 = seg.Cross(side);
                    item.params.tint = Vector4(trailColor.x, trailColor.y, trailColor.z, trailAlpha * k * grow * (b.returning && b.heal > 0.0f ? 1.0f : 0.75f));
                    item.world = Matrix::CreateScale(w, w, len * 1.15f) * Matrix(side, up2, seg)
                        * Matrix::CreateTranslation((prev + p) * 0.5f);
                    renderer.Submit(item);
                }
                prev = p;
            }
        }
        // 噛んで戻る蝙蝠は胸に光る血の玉を抱えている（加算）
        if (b.returning && b.heal > 0.0f)
        {
            item.blend = 0;
            item.params.intensity = returnGlow;
            item.params.tint = Vector4(1.0f, 0.05f, 0.12f, 0.9f);
            item.world = Matrix::CreateScale(0.06f) * Matrix::CreateTranslation(0.0f, -0.07f, 0.05f) * world;
            renderer.Submit(item);
        }

        // ---- 体と頭（半透明の処理で不透明に。固定の光で陰影）----
        item.blend = 1;
        item.params.shade = 1.0f;
        item.params.intensity = 1.0f;
        item.params.tint = Vector4(bodyColor.x, bodyColor.y, bodyColor.z, 1.0f);
        item.world = Matrix::CreateScale(0.13f, 0.11f, 0.22f) * appear * world;
        renderer.Submit(item);
        item.world = Matrix::CreateScale(0.12f) * Matrix::CreateTranslation(0.0f, 0.04f, 0.15f) * appear * world;
        renderer.Submit(item);

        // ---- 耳：頭の上に 2 枚、少し外へ ----
        item.model = m_Ear.get();
        for (float side : { -1.0f, 1.0f })
        {
            item.world = Matrix::CreateRotationX(-1.2f) * Matrix::CreateRotationZ(side * 0.35f)
                * Matrix::CreateTranslation(side * 0.035f, 0.08f, 0.15f) * appear * world;
            renderer.Submit(item);
        }

        // ---- 翼：右は付け根から +X、左は左右反転。前の軸（Z）回りに羽ばたく ----
        item.model = m_Wing.get();
        item.params.tint = Vector4(wingColor.x, wingColor.y, wingColor.z, 0.96f);
        item.params.shade = 0.6f;
        item.world = Matrix::CreateRotationZ(flap) * Matrix::CreateTranslation(0.05f, 0.02f, 0.0f) * appear * world;
        renderer.Submit(item);
        item.world = Matrix::CreateScale(-1.0f, 1.0f, 1.0f) * Matrix::CreateRotationZ(-flap)
            * Matrix::CreateTranslation(-0.05f, 0.02f, 0.0f) * appear * world;
        renderer.Submit(item);

        // ---- 光る目（加算）----
        item.model = m_Sphere.get();
        item.blend = 0;
        item.params.shade = 0.0f;
        item.params.intensity = 1.0f;
        item.params.tint = Vector4(eyeColor.x, eyeColor.y, eyeColor.z, 1.0f);
        for (float side : { -1.0f, 1.0f })
        {
            item.world = Matrix::CreateScale(0.03f) * Matrix::CreateTranslation(side * 0.035f, 0.06f, 0.21f) * appear * world;
            renderer.Submit(item);
        }
    }
}

void BatField::SubmitLights(const std::vector<WeaponSystem::Bat>& bats) const
{
    if (!enabled || !lights) return;
    for (const auto& b : bats)
    {
        const float grow = (std::min)(b.age / 0.12f, 1.0f);
        const float k = (b.returning && b.heal > 0.0f) ? lightReturnMul : 1.0f;
        PointLightManager::Get().Add(b.pos, lightColor, lightRadius, lightIntensity * k * grow);
    }
}

void BatField::DrawImGui()
{
    ImGui::Checkbox("Lights##bats", &lights);
    ImGui::ColorEdit3("Light color##bats", &lightColor.x);
    ImGui::DragFloat("Light radius##bats", &lightRadius, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Light intensity##bats", &lightIntensity, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Light (returning) x##bats", &lightReturnMul, 0.05f, 0.0f, 5.0f);
    ImGui::Checkbox("Enabled##bats", &enabled);
    ImGui::DragFloat("Scale##bats", &scale, 0.01f, 0.2f, 5.0f);
    ImGui::DragFloat("Flap freq (rad/s)##bats", &flapFreq, 0.5f, 0.0f, 60.0f);
    ImGui::DragFloat("Flap amp (rad)##bats", &flapAmp, 0.01f, 0.0f, 1.6f);
    ImGui::ColorEdit3("Body##bats", &bodyColor.x);
    ImGui::ColorEdit3("Wing##bats", &wingColor.x);
    ImGui::ColorEdit3("Eye (HDR)##bats", &eyeColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Trail##bats", &trailColor.x);
    ImGui::DragFloat("Trail width (m)##bats", &trailWidth, 0.001f, 0.0f, 0.3f);
    ImGui::DragFloat("Trail alpha##bats", &trailAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Trail intensity##bats", &trailIntensity, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat("Blood orb glow##bats", &returnGlow, 0.05f, 0.0f, 5.0f);
}
