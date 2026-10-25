// ============================================================
// IceSpikeField.cpp
// ============================================================
#include "ECS/System/IceSpikeField.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr size_t kMaxSpikes = 240;  // 同時に出ているトゲ（10m の線で約 19 本 x 12 回分）。超えたら古い物から消す

    float Clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }
}

void IceSpikeField::EnsureModel()
{
    if (m_Tried) return;
    m_Tried = true;
    m_Model = ResourceManager::Get().LoadModelAuto(modelPath ? modelPath : Res::VFX::IceSpikes).staticModel;
    if (!m_Model) return;
    // 塊の底の中心を根元にして、高さを測る（Y が上。凍った敵の足元の氷と同じ扱い）
    const Vector3 bmin = m_Model->GetBoundsMin(), bmax = m_Model->GetBoundsMax();
    m_ModelHeight = (std::max)(bmax.y - bmin.y, 1e-4f);
    m_Anchor = Vector3((bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f);
}

// ============================================================
// 並べる：足元から end まで spacing 毎に 1 本、左右交互に（1 本毎に入れ替わる）。足元から順に出る（delay = 負の age）。
// 先へ行くほど少し高く。根元は 1 本ずつ地面の高さ。横へずらした所が線の真ん中の地面と maxStep 以上違えば出さない
// ============================================================
void IceSpikeField::Spawn(const Vector3& start, const Vector3& end, float scaleIn, const HeightFn& heightAt)
{
    if (!enabled) return;
    Vector3 dir(end.x - start.x, 0.0f, end.z - start.z);
    const float length = dir.Length();
    if (length < 1e-3f) return;
    dir /= length;
    const Vector3 right(dir.z, 0.0f, -dir.x);
    const float scale = (std::max)(scaleIn, 0.1f);
    const float step = (std::max)(spacing * scale, 0.1f);
    auto groundAt = [&](const Vector3& p) { return heightAt ? heightAt(p.x, p.z) : start.y + (end.y - start.y) * (p - start).Length() / length; };

    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float sideSign = (u01(m_Rng) < 0.5f) ? -1.0f : 1.0f;   // 最初の 1 本が左右どちらか
    for (float along = startGap; along <= length; along += step, sideSign = -sideSign)
    {
        const float t = along / length;
        const float a = along + (u01(m_Rng) - 0.5f) * step * 0.3f;
        const Vector3 center = start + dir * a;
        Vector3 p = center + right * (sideSign * (sideMin + (sideMax - sideMin) * u01(m_Rng)) * scale);
        const float h = groundAt(p);
        if (std::fabs(h - groundAt(center)) > maxStep) continue;   // 横が崖・台地
        p.y = h;

        Spike s;
        s.base = p;
        s.yaw = u01(m_Rng) * DirectX::XM_2PI;
        // 前へ倒す（線の向き）。少しだけ外側（ずらした側）へも
        Vector3 lean = dir + right * (sideSign * 0.35f + (u01(m_Rng) - 0.5f) * 0.3f);
        lean.Normalize();
        s.tiltAxis = Vector3(lean.z, 0.0f, -lean.x);   // 右向きの軸で回すと上が lean の方へ倒れる
        s.tilt = DirectX::XMConvertToRadians(tiltDeg + (u01(m_Rng) * 2.0f - 1.0f) * tiltJitterDeg);
        s.height = (heightMin + (heightMax - heightMin) * u01(m_Rng)) * (1.0f + (growFar - 1.0f) * t) * scale;
        s.age = -(along / (std::max)(waveSpeed, 0.1f));
        m_Spikes.push_back(s);
    }
    if (m_Spikes.size() > kMaxSpikes)
        m_Spikes.erase(m_Spikes.begin(), m_Spikes.begin() + (m_Spikes.size() - kMaxSpikes));
}

void IceSpikeField::Update(float dt)
{
    const float life = riseTime + holdTime + sinkTime;
    for (auto& s : m_Spikes) s.age += dt;
    m_Spikes.erase(std::remove_if(m_Spikes.begin(), m_Spikes.end(),
        [&](const Spike& s) { return s.age >= life; }), m_Spikes.end());
}

// ============================================================
// 描く：突き出る（地面の下から押し上がり、一瞬伸びる）→ 残る → 沈みながら透けていく
// ============================================================
void IceSpikeField::Submit(VFXMeshRenderer& renderer)
{
    if (m_Spikes.empty()) return;
    EnsureModel();
    if (!m_Model) return;

    VFXMeshDrawItem item;
    item.model = m_Model.get();
    item.blend = blend;   // 1 = 半透明（遠い順に並べて描かれる）
    item.twoSided = false;
    item.params.noVertexColor = 1.0f;   // 材質の色が頂点色に焼かれている FBX（色は color で決める）
    item.params.shade = shade;
    item.params.toon = toon ? 1.0f : 0.0f;
    item.params.intensity = intensity;

    for (const auto& s : m_Spikes)
    {
        if (s.age < 0.0f) continue;
        float rise = 1.0f, grow = 1.0f, fade = 1.0f, down = 0.0f;
        if (s.age < riseTime)
        {
            const float t = Clamp01(s.age / (std::max)(riseTime, 1e-3f));
            const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            rise = e;
            grow = 1.0f + overshoot * std::sin(DirectX::XM_PI * t);
        }
        else if (s.age > riseTime + holdTime)
        {
            const float u = Clamp01((s.age - riseTime - holdTime) / (std::max)(sinkTime, 1e-3f));
            down = u * u;
            fade = 1.0f - u;
        }
        const float scale = s.height / m_ModelHeight;
        const float y = -sink - s.height * ((1.0f - rise) + down);
        item.world = Matrix::CreateTranslation(-m_Anchor)
            * Matrix::CreateScale(scale, scale * grow, scale)
            * Matrix::CreateRotationY(s.yaw)
            * Matrix::CreateFromAxisAngle(s.tiltAxis, s.tilt)
            * Matrix::CreateTranslation(s.base + Vector3(0.0f, y, 0.0f));
        item.params.tint = Vector4(color.x, color.y, color.z, alpha * fade);
        renderer.Submit(item);
    }
}

void IceSpikeField::DrawImGui()
{
    ImGui::Checkbox("Enabled##icespike", &enabled);
    ImGui::Text("Active spikes: %d", ActiveCount());
    ImGui::DragFloat("Start gap (m)", &startGap, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Spacing (m)", &spacing, 0.01f, 0.1f, 3.0f);
    ImGui::DragFloat("Side min (m)", &sideMin, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Side max (m)", &sideMax, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Wave speed (m/s)", &waveSpeed, 0.5f, 1.0f, 100.0f);
    ImGui::DragFloat("Height min (m)", &heightMin, 0.01f, 0.1f, 4.0f);
    ImGui::DragFloat("Height max (m)", &heightMax, 0.01f, 0.1f, 4.0f);
    ImGui::DragFloat("Far height x", &growFar, 0.01f, 0.5f, 3.0f);
    ImGui::DragFloat("Tilt (deg)", &tiltDeg, 0.5f, -60.0f, 60.0f);
    ImGui::DragFloat("Tilt jitter (deg)", &tiltJitterDeg, 0.5f, 0.0f, 45.0f);
    ImGui::DragFloat("Max step (m)", &maxStep, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Rise (s)", &riseTime, 0.005f, 0.01f, 1.0f);
    ImGui::DragFloat("Hold (s)", &holdTime, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Sink (s)", &sinkTime, 0.01f, 0.01f, 2.0f);
    ImGui::DragFloat("Bury (m)", &sink, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Overshoot", &overshoot, 0.01f, 0.0f, 1.0f);
    ImGui::ColorEdit3("Color (linear)", &color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Alpha", &alpha, 0.0f, 1.0f);
    ImGui::DragFloat("Intensity", &intensity, 0.01f, 0.0f, 4.0f);
    ImGui::SliderFloat("Shade", &shade, 0.0f, 1.0f);
    ImGui::Checkbox("Toon glass bands", &toon);
    if (ImGui::Button("Clear##icespike")) Clear();
}
