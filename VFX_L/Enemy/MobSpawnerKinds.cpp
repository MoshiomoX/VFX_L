// ============================================================
// MobSpawnerKinds.cpp
// 突撃兵・盾兵（2026-10-08）の湧きの割合と、Enemies パネルの段（突撃兵・盾兵・凍結の GPU 定数）。
// MobSpawner.cpp が 330 行を超えていたので、新しい種類の分はこちらに置く
// ============================================================
#include "Enemy/MobSpawner.h"
#include "Swarm/SwarmSystem.h"
#include "imgui.h"

// 面毎の割合（start 前は 0、rampEnd まで直線、以降そのまま）
float MobSpawner::KindRamp::At(float runTime) const
{
    if (runTime < start) return 0.0f;
    const float span = (std::max)(1.0f, rampEnd - start);
    const float t = (std::min)(1.0f, (runTime - start) / span);
    return ratioStart + (ratioEnd - ratioStart) * t;
}

void MobSpawner::DrawChargerShieldImGui(SwarmSystem& swarm)
{
    auto& bomb = swarm.GetBomberParams();
    auto drawRamp = [](const char* label, KindRamp& r)
        {
            ImGui::PushID(label);
            ImGui::DragFloat("Start (s)", &r.start, 5.0f, 0.0f, 1.0e9f);
            ImGui::DragFloat("Ratio Start", &r.ratioStart, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Ratio End", &r.ratioEnd, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Ramp End (s)", &r.rampEnd, 5.0f, 0.0f, 3600.0f);
            ImGui::PopID();
        };

    // ---- 突撃兵：溜め（予告の帯）→ 突進 → 息切れ ----
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1), "Charger (GPU)");
    ImGui::Text("ratio now %.2f", m_ChargerRatio);
    drawRamp("charger", chargerMix);
    ImGui::DragFloat("Charger HP", &m_ChargerHp, 1.0f, 1.0f, 2000.0f);
    ImGui::DragFloat("Charger Speed", &m_ChargerSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Charger Scale", &bomb.chargerScale, 0.01f, 0.2f, 3.0f);
    ImGui::DragFloat("Charger Damage x", &bomb.chargerDamageMul, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Charger Dash Damage x", &bomb.chargerDashDamageMul, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Charger Exp x", &bomb.chargerExpMul, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Wind-up (s)", &bomb.chargerWindup, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Dash Speed", &bomb.chargerDashSpeed, 0.5f, 1.0f, 60.0f);
    ImGui::DragFloat("Dash Distance", &bomb.chargerDashDist, 0.5f, 1.0f, 60.0f);
    ImGui::DragFloat("Recover (s)", &bomb.chargerRecover, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat("Cooldown (s)", &bomb.chargerCooldown, 0.1f, 0.0f, 30.0f);
    ImGui::DragFloat("Trigger Min (m)", &bomb.chargerMinDist, 0.5f, 0.0f, 60.0f);
    ImGui::DragFloat("Trigger Max (m)", &bomb.chargerMaxDist, 0.5f, 0.0f, 60.0f);
    ImGui::DragFloat("Wind-up Glow", &bomb.chargerGlow, 0.1f, 1.0f, 10.0f);
    auto& line = swarm.chargeLine;   // 予告の帯（色は straight alpha）
    ImGui::Checkbox("Dash Band", &line.enabled);
    ImGui::DragFloat("Band Edge Width", &line.edgeWidth, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Band Lift", &line.lift, 0.005f, 0.0f, 0.3f);
    ImGui::ColorEdit4("Band Fill", &line.fill.x);
    ImGui::ColorEdit4("Band Edge", &line.edge.x);
    ImGui::ColorEdit4("Band Back", &line.back.x);
    if (ImGui::Button("Spawn 5 Chargers Nearby")) QueueDebugChargers(5);
    ImGui::Separator();

    // ---- 盾兵：1 発毎に装甲を引く + 身体の前の塔の盾 ----
    ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1), "Shield Bearer (GPU)");
    ImGui::Text("ratio now %.2f", m_ShieldRatio);
    drawRamp("shield", shieldMix);
    ImGui::DragFloat("Shield HP", &m_ShieldHp, 1.0f, 1.0f, 2000.0f);
    ImGui::DragFloat("Shield Speed", &m_ShieldSpeed, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Shield Scale", &bomb.shieldScale, 0.01f, 0.2f, 3.0f);
    ImGui::DragFloat("Shield Damage x", &bomb.shieldDamageMul, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Shield Exp x", &bomb.shieldExpMul, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Armor (per hit)", &bomb.shieldArmor, 0.5f, 0.0f, 100.0f);
    ImGui::SliderFloat("Min Damage Fraction", &bomb.shieldMinFrac, 0.0f, 1.0f, "%.2f");
    auto& look = swarm.shieldLook;
    ImGui::Checkbox("Show Shield Mesh", &look.enabled);
    ImGui::DragFloat("Mesh Height (m)", &look.height, 0.01f, 0.1f, 4.0f);
    ImGui::DragFloat3("Hold (right/up/fwd)", &look.hold.x, 0.01f, -2.0f, 3.0f);
    ImGui::DragFloat("Mesh Yaw (deg)", &look.yawDeg, 1.0f, -180.0f, 180.0f);
    ImGui::DragFloat("Walk Bob (m)", &look.bob, 0.005f, 0.0f, 0.3f);
    ImGui::ColorEdit3("Shield Tint", &look.tint.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    if (ImGui::Button("Spawn 5 Shield Bearers Nearby")) QueueDebugShields(5);
    ImGui::Separator();

    // ---- 凍結（アイスランス）----
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1), "Freeze (GPU)");
    ImGui::DragFloat("Elite / Boss Freeze x", &bomb.freezeBigMul, 0.05f, 0.0f, 1.0f);
    ImGui::DragFloat("Re-freeze Immunity (s)", &bomb.freezeImmunity, 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Frozen Body Tint", &bomb.freezeTint, 0.05f, 0.0f, 2.0f);
    auto& ice = swarm.iceLook;
    ImGui::Checkbox("Show Ice Mesh", &ice.enabled);
    ImGui::DragFloat("Ice Height (m)", &ice.height, 0.01f, 0.1f, 4.0f);
    ImGui::DragFloat("Ice Spread (m)", &ice.spread, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Ice Sink (m)", &ice.sink, 0.01f, -1.0f, 1.0f);
    ImGui::ColorEdit3("Ice Color", &ice.color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Ice Alpha", &ice.alpha, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
}
