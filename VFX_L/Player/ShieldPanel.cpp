// ============================================================
// ShieldPanel.cpp
// ============================================================
#include "Player/ShieldPanel.h"
#include "Player/ShieldComponent.h"
#include "Player/PlayerStateSystem.h"
#include "imgui.h"

void ShieldPanel::Draw(Registry& reg, Entity player)
{
    if (!reg.IsValid(player) || !reg.Has<ShieldComponent>(player)) return;
    auto& sh = reg.Get<ShieldComponent>(player);

    char buf[64];
    sprintf_s(buf, "shield %.0f / %.0f", sh.current, sh.max);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.75f, 1.0f, 1.0f));
    ImGui::ProgressBar((sh.max > 0.0f) ? sh.current / sh.max : 0.0f, ImVec2(-1, 0), buf);
    ImGui::PopStyleColor();

    if (!ImGui::TreeNode("Shield"))
        return;
    ImGui::DragFloat("Shield Max", &sh.max, 1.0f, 0.0f, 5000.0f);
    ImGui::DragFloat("Shield Current", &sh.current, 1.0f, 0.0f, sh.max);
    ImGui::DragFloat("Recharge Delay (s)", &sh.rechargeDelay, 0.05f, 0.0f, 30.0f);
    ImGui::DragFloat("Refill Time (s)", &sh.refillTime, 0.05f, 0.0f, 30.0f);
    if (sh.Full())            ImGui::Text("full");
    else if (sh.Recharging()) ImGui::Text("recharging");
    else                      ImGui::Text("waiting %.1f s", sh.rechargeDelay - sh.sinceHit);
    ImGui::Text("since hit %.1f s   hits %u   breaks %u", sh.sinceHit, sh.hits, sh.breaks);
    // 被弾の窓口と同じ経路で当てる（無敵時間中は弾かれる）
    if (ImGui::Button("Hit 10##shield")) PlayerStateSystem::TryApplyHit(reg, player, 10.0f);
    ImGui::SameLine();
    if (ImGui::Button("Hit 1000##shield")) PlayerStateSystem::TryApplyHit(reg, player, 1000.0f);
    ImGui::SameLine();
    if (ImGui::Button("Refill##shield")) sh.current = sh.max;
    ImGui::TreePop();
}
