// ============================================================
// BladeField.cpp
// ============================================================
#include "ECS/System/BladeField.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Mesh/Vertex3D.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "Swarm/AreaProfile.h"
#include "Item/ItemDatabase.h"

#include "imgui.h"

using namespace DirectX::SimpleMath;

const BladeField::ModelInfo* BladeField::Find(const std::string& path)
{
    ModelInfo& m = m_Models[path];
    if (!m.tried)
    {
        m.tried = true;
        m.model = ResourceManager::Get().LoadModelAuto(path).staticModel;
        if (m.model)
        {
            const Vector3 bmin = m.model->GetBoundsMin(), bmax = m.model->GetBoundsMax();
            m.length = (std::max)(bmax.y - bmin.y, 1e-4f);
            m.hilt = Vector3((bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f);
            m.middle = (bmin + bmax) * 0.5f;
        }
    }
    return m.model ? &m : nullptr;
}

// 扇形の帯（XZ 平面、半径 inner〜1）。角度 0（+Z）が剣、-span へ向かって後ろ。
// 頂点色の a：長さ方向は剣の側 1 → 後ろ 0、幅方向は剣先の少し内側が一番明るく、両縁は柔らかく
Model* BladeField::Arc(float inner, float spanDeg)
{
    const int key = (int)std::lround(inner * 100.0f) * 1000 + (int)std::lround(spanDeg);
    auto it = m_Arcs.find(key);
    if (it != m_Arcs.end()) return it->second.get();
    std::shared_ptr<Model>& slot = m_Arcs[key];
    ID3D11Device* device = ResourceManager::Get().GetDevice();
    if (!device) return nullptr;

    constexpr int kSeg = 32;
    const float w = 1.0f - inner;
    const float rows[4] = { inner, inner + w * 0.55f, 1.0f - w * 0.08f, 1.0f };
    const float rowAlpha[4] = { 0.0f, 0.55f, 1.0f, 0.0f };
    const float span = DirectX::XMConvertToRadians(spanDeg);
    std::vector<VERTEX_3D> v;
    std::vector<unsigned int> idx;
    for (int s = 0; s <= kSeg; ++s)
    {
        const float t = (float)s / (float)kSeg;
        const float a = -t * span;
        const float along = std::pow(1.0f - t, 1.6f);
        for (int r = 0; r < 4; ++r)
        {
            VERTEX_3D q;
            q.position = Vector3(std::sin(a) * rows[r], 0.0f, std::cos(a) * rows[r]);
            q.normal = Vector3::Up;
            q.tangent = Vector3::UnitX;
            q.uv = Vector2(t, (float)r / 3.0f);
            q.color = Vector4(1.0f, 1.0f, 1.0f, along * rowAlpha[r]);
            v.push_back(q);
        }
        if (s == 0) continue;
        for (int r = 0; r < 3; ++r)
        {
            const unsigned int a0 = (unsigned)((s - 1) * 4 + r), b0 = (unsigned)(s * 4 + r);
            idx.insert(idx.end(), { a0, b0, a0 + 1, a0 + 1, b0, b0 + 1 });
        }
    }
    auto mesh = std::make_shared<Mesh>();
    if (!mesh->Create(device, v, idx)) return nullptr;
    slot = std::make_shared<Model>();
    slot->AddSubMesh(mesh);
    return slot.get();
}

void BladeField::Submit(VFXMeshRenderer& renderer, const std::vector<WeaponSystem::BladeSet>& sets, float lift)
{
    if (!enabled) return;
    for (const auto& b : sets)
    {
        if (b.profile <= 0 || b.presence <= 0.0f) continue;
        const AreaProfile& ap = AreaProfileDB::At(b.profile);
        const ModelInfo* mi = Find(ap.bladeModel);
        if (!mi) continue;

        const ItemCommon* ic = ItemDatabase::GetCommon(b.id);
        const Vector4 base = ic ? ic->color : Vector4(0.6f, 0.8f, 1.0f, 1.0f);
        const Vector3 body = Vector3::Lerp(Vector3(base.x, base.y, base.z), Vector3::One, whiten);
        const float p = b.presence * b.presence * (3.0f - 2.0f * b.presence);
        // 出る時は柄の方から伸び、消える時は細くなる
        const float s = (b.length / mi->length) * (0.35f + 0.65f * p);
        const float sideSign = b.spin >= 0.0f ? 1.0f : -1.0f;
        const float tilt = DirectX::XMConvertToRadians(tiltDeg);

        auto basis = [&](float ang)
        {
            const Vector3 out(std::sin(ang), 0.0f, std::cos(ang));
            const Vector3 back = Vector3(-std::cos(ang), 0.0f, std::sin(ang)) * sideSign;   // 回る向きの逆（曲がった刃の先は後ろへ）
            const Vector3 ex = back * std::cos(tilt) + Vector3::UnitY * std::sin(tilt);
            const Vector3 ez = -back * std::sin(tilt) + Vector3::UnitY * std::cos(tilt);
            return Matrix(ex, out, ez);
        };
        auto place = [&](float ang, int k) -> Vector3
        {
            const Vector3 out(std::sin(ang), 0.0f, std::cos(ang));
            const float y = lift + bob * std::sin(b.age * 5.0f + (float)k * 2.1f);
            return b.center + out * (b.radius - b.length * (0.35f + 0.65f * p)) + Vector3(0.0f, y, 0.0f);
        };

        // ---- 刀光の帯（形は剣の長さの比と帯の長さで決まる。出た直後は回った分だけ見せる）----
        Model* arc = nullptr;
        float arcAlpha = 0.0f;
        if (slashLight)
        {
            const float spanDeg = std::clamp(DirectX::XMConvertToDegrees(std::abs(b.spin)) * slashTime, 40.0f, 160.0f);
            arc = Arc(std::clamp((b.radius - b.length) / (std::max)(b.radius, 1e-3f), 0.05f, 0.95f), spanDeg);
            const float swept = b.age * std::abs(b.spin) / DirectX::XMConvertToRadians(spanDeg);
            arcAlpha = slashAlpha * p * (std::min)(swept, 1.0f);
        }
        const Vector3 arcColor = Vector3::Lerp(Vector3(base.x, base.y, base.z), Vector3::One, slashWhiten);

        VFXMeshDrawItem item;
        item.model = mi->model.get();
        item.twoSided = true;
        item.params.noVertexColor = 1.0f;
        for (int k = 0; k < b.count; ++k)
        {
            const float ang = b.angle + DirectX::XM_2PI * (float)k / (float)b.count;
            const Matrix rot = basis(ang);
            const Vector3 hilt = place(ang, k);

            if (arc && arcAlpha > 0.0f)
            {
                VFXMeshDrawItem band;
                band.model = arc;
                band.blend = 0;   // 加算
                band.twoSided = true;
                band.params.noVertexColor = 0.0f;   // 頂点色の a で後ろへ消す
                band.params.intensity = slashIntensity;
                band.params.tint = Vector4(arcColor.x, arcColor.y, arcColor.z, arcAlpha);
                // 逆回りは X を反転（帯は剣の後ろへ伸びる）
                band.world = Matrix::CreateScale(b.radius * sideSign, 1.0f, b.radius) * Matrix::CreateRotationY(ang)
                    * Matrix::CreateTranslation(b.center + Vector3(0.0f, lift, 0.0f));
                renderer.Submit(band);
            }

            // ---- 本体：半透明のガラス ----
            item.blend = 1;
            item.params.toon = 1.0f;
            item.params.shade = 1.0f;
            item.params.intensity = bodyIntensity;
            item.params.tint = Vector4(body.x, body.y, body.z, bodyAlpha * p);
            item.world = Matrix::CreateTranslation(-mi->hilt) * Matrix::CreateScale(s) * rot * Matrix::CreateTranslation(hilt);
            renderer.Submit(item);

            // ---- 光の殻と残像：加算。剣の真ん中を中心に少し大きく ----
            item.blend = 0;
            item.params.toon = 0.0f;
            item.params.shade = 0.4f;
            item.params.intensity = shellIntensity;
            for (int t = 0; t <= trailCount; ++t)
            {
                const float a = ang - sideSign * DirectX::XMConvertToRadians(trailStepDeg) * (float)t;
                const Matrix r = (t == 0) ? rot : basis(a);
                const Vector3 h = (t == 0) ? hilt : place(a, k);
                const Vector3 mid = h + Vector3::Transform(mi->middle - mi->hilt, Matrix::CreateScale(s) * r);
                const float alpha = (t == 0) ? shellAlpha : trailAlpha * (1.0f - (float)t / (float)(trailCount + 1));
                item.params.tint = Vector4(base.x, base.y, base.z, alpha * p);
                item.world = Matrix::CreateTranslation(-mi->middle) * Matrix::CreateScale(s * shellScale) * r * Matrix::CreateTranslation(mid);
                renderer.Submit(item);
            }
        }
    }
}

void BladeField::DrawImGui()
{
    ImGui::Checkbox("Enabled##blades", &enabled);
    ImGui::DragFloat("Tilt (deg)##blades", &tiltDeg, 0.5f, -90.0f, 90.0f);
    ImGui::DragFloat("Body alpha##blades", &bodyAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Body intensity##blades", &bodyIntensity, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("Whiten##blades", &whiten, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Shell scale##blades", &shellScale, 0.01f, 1.0f, 2.0f);
    ImGui::DragFloat("Shell alpha##blades", &shellAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Shell intensity##blades", &shellIntensity, 0.01f, 0.0f, 6.0f);
    ImGui::SliderInt("Trail count##blades", &trailCount, 0, 10);
    ImGui::DragFloat("Trail step (deg)##blades", &trailStepDeg, 0.5f, 0.0f, 60.0f);
    ImGui::DragFloat("Trail alpha##blades", &trailAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Bob (m)##blades", &bob, 0.005f, 0.0f, 0.5f);
    ImGui::Checkbox("Slash light##blades", &slashLight);
    ImGui::DragFloat("Slash length (s of spin)##blades", &slashTime, 0.005f, 0.02f, 0.5f);
    ImGui::DragFloat("Slash alpha##blades", &slashAlpha, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Slash intensity##blades", &slashIntensity, 0.05f, 0.0f, 6.0f);
    ImGui::DragFloat("Slash whiten##blades", &slashWhiten, 0.01f, 0.0f, 1.0f);
}
