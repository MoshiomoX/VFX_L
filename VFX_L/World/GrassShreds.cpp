// ============================================================
// GrassShreds.cpp
// ============================================================
#include "World/GrassShreds.h"
#include "World/LocalWind.h"
#include "World/GridWorld.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/VFXId.h"
#include "imgui.h"

using DirectX::SimpleMath::Vector3;

void GrassShreds::Update(float dt, const LocalWind& wind, const GridWorld& grid, bool grassOn,
    const Vector3& tint, AreaVFXPlayer& vfx, const VFXContext& ctx)
{
    const Settings& s = m_Settings;
    for (Emitter& e : m_Emitters)
        e.matched = false;

    if (s.enabled && grassOn)
    {
        const float matchD2 = s.matchDistance * s.matchDistance;
        for (const Swarm::WindSource& w : wind.sources)
        {
            if (w.radius <= 0.0f || w.fade <= 0.01f) continue;
            const float speed = (std::max)(w.swirl, w.push);
            const float k = w.fade * std::clamp((speed - s.minWind) / (std::max)(s.fullWind - s.minWind, 0.1f), 0.0f, 1.0f);
            if (k <= 0.0f) continue;
            Vector3 pos(w.pos[0], 0.0f, w.pos[2]);
            pos.y = grid.SampleHeight(pos.x, pos.z);

            // 前のフレームの発生源のうち一番近い物（まだ対応の付いていない物）
            int best = -1;
            float bestD2 = matchD2;
            for (int i = 0; i < (int)m_Emitters.size(); ++i)
            {
                const Emitter& e = m_Emitters[i];
                if (e.matched) continue;
                const float dx = e.pos.x - pos.x, dz = e.pos.z - pos.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 < bestD2) { bestD2 = d2; best = i; }
            }
            if (best < 0)
            {
                Emitter e;
                // 止めるのは表から消えた時。長さは止め忘れた時の保険
                e.handle = vfx.Play(VFXDatabase::GetFile(VFXId::GrassShred), pos, 3600.0f, false, ctx, tint);
                if (e.handle == 0) continue;
                if (s.refRadius > 0.0f) vfx.ScaleInstance(e.handle, w.radius / s.refRadius);
                m_Emitters.push_back(e);
                best = (int)m_Emitters.size() - 1;
            }
            Emitter& e = m_Emitters[best];
            e.matched = true;
            e.unseen = 0.0f;
            e.pos = pos;
            vfx.SetInstance(e.handle, pos, pos);
            vfx.SetPresence(e.handle, k, false);
        }
    }

    // 表から消えた風源の分：出すのを止め、少し待って（リードバックの抜け）から片付ける。出ている粒子は自然に消える
    for (int i = (int)m_Emitters.size() - 1; i >= 0; --i)
    {
        Emitter& e = m_Emitters[i];
        if (e.matched) continue;
        e.unseen += dt;
        vfx.SetPresence(e.handle, 0.0f, true);
        if (e.unseen > s.lingerTime || !s.enabled || !grassOn)
        {
            vfx.StopInstance(e.handle);
            m_Emitters.erase(m_Emitters.begin() + i);
        }
    }
}

void GrassShreds::Clear(AreaVFXPlayer& vfx)
{
    for (const Emitter& e : m_Emitters)
        vfx.StopInstance(e.handle);
    m_Emitters.clear();
}

void GrassShreds::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Grass Shreds (wind sources)")) return;
    auto& s = m_Settings;
    ImGui::Checkbox("Enabled##gshred", &s.enabled);
    ImGui::SameLine();
    ImGui::Text("emitters %d", ActiveCount());
    ImGui::DragFloatRange2("Wind min / full##gshred", &s.minWind, &s.fullWind, 0.1f, 0.0f, 40.0f, "%.1f m/s");
    ImGui::DragFloat("Ref Radius##gshred", &s.refRadius, 0.1f, 0.5f, 20.0f, "%.1f m");
    ImGui::DragFloat("Match Distance##gshred", &s.matchDistance, 0.1f, 0.1f, 10.0f, "%.1f m");
    ImGui::DragFloat("Linger##gshred", &s.lingerTime, 0.01f, 0.0f, 2.0f, "%.2f s");
    ImGui::TextDisabled("looks: VFXData/GrassShred.json (tinted per stage, swirled by the wind sources)");
}
