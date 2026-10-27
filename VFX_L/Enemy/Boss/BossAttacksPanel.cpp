// ============================================================
// BossAttacksPanel.cpp
// BossAttacks の調整パネル（Enemies パネルの「Boss Attacks」）。2026-10-24 に BossAttacks.cpp から分けた（技が 8 つに増えたので）
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
#include "Swarm/SwarmTypes.h"
#include "imgui.h"

void BossAttacks::DrawImGui()
{
    ImGui::TextColored(ImVec4(0.85f, 0.4f, 1.0f, 1), "Boss Attacks");
    ImGui::Text("move %s (%.1f s)  next in %.1f s  rings %d  wave %s  | volleys %u blasts %u",
        BossMoveName(m_Move), m_MoveTime, m_Gap, (int)m_Rings.size(), m_WaveOn ? "on" : "off", volleys, blasts);
    ImGui::Checkbox("Enabled##boss", &enabled);
    ImGui::DragFloat("Body Scale##boss", &bodyScale, 0.05f, 1.0f, 8.0f);   // Boss 毎（Configure が入れる。狼人 3.8）
    ImGui::DragFloat("First Delay (s)##boss", &firstDelay, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Gap (s)##boss", &gap, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Gap Enraged (s)##boss", &gapEnraged, 0.1f, 0.0f, 60.0f);
    ImGui::SliderFloat("Enrage HP Ratio##boss", &enrageHpRatio, 0.0f, 1.0f);
    ImGui::SliderFloat("Combo Chance (enraged)##boss", &comboChance, 0.0f, 1.0f);
    ImGui::DragFloat("Range (m)##boss", &range, 0.5f, 1.0f, 200.0f);
    if (ImGui::Button("Slam Now")) QueueMove(BossMove::Slam);
    ImGui::SameLine();
    if (ImGui::Button("Rock Rain Now")) QueueMove(BossMove::RockRain);
    ImGui::SameLine();
    if (ImGui::Button("Shockwave Now")) QueueMove(BossMove::Shockwave);
    ImGui::SameLine();
    if (ImGui::Button("Charge Now")) QueueMove(BossMove::Charge);
    ImGui::SameLine();
    if (ImGui::Button("Summon Now")) QueueMove(BossMove::Summon);
    if (ImGui::Button("Sand Spear Now")) QueueMove(BossMove::SandSpear);
    ImGui::SameLine();
    if (ImGui::Button("Burrow Now")) QueueMove(BossMove::Burrow);
    ImGui::SameLine();
    if (ImGui::Button("Quicksand Now")) QueueMove(BossMove::Quicksand);
    ImGui::SameLine();
    if (ImGui::Button("Sting Now")) QueueMove(BossMove::Sting);
    ImGui::SameLine();
    if (ImGui::Button("Claw Sweep Now")) QueueMove(BossMove::ClawSweep);
    ImGui::SameLine();
    if (ImGui::Button("Chain Pull Now")) QueueMove(BossMove::ChainPull);
    if (ImGui::Button("Pounce Now")) QueueMove(BossMove::Pounce);
    ImGui::SameLine();
    if (ImGui::Button("Claw Combo Now")) QueueMove(BossMove::ClawCombo);
    ImGui::SameLine();
    if (ImGui::Button("Howl Now")) QueueMove(BossMove::Howl);

    // 面毎の Boss（StageDef::boss）。ここで替えると技の有り / 無し・召喚の種類・体の色が入れ替わる
    int type = (int)m_Type;
    const char* types[] = { "Stone Giant (grassland)", "Sand Guardian (desert)", "Scorpion (desert 2)", "Thunder Stag (grassland 3)", "Crypt Knight (ruins 1)", "Werewolf (grassland 2)" };
    if (ImGui::Combo("Boss Type##boss", &type, types, (int)BossType::Count))
        Configure((BossType)type);
    ImGui::ColorEdit3("Body Tint##boss", &bodyTint.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::Text("quicksand %s  r %.1f  strength %.2f  burrow sink %.2f",
        m_SandField.active ? "on" : "off", m_SandField.radius, m_SandField.strength, m_BurrowSink);

    if (ImGui::TreeNode("Choosing (BossBrain)"))
    {
        brain.DrawImGui();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Slam"))
    {
        ImGui::DragInt("Rings", &count, 1, 1, 12);
        ImGui::DragFloat("Spacing (s)", &spacing, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("Warn Time (s)", &warnTime, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Radius (m)", &radius, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Damage (base)", &damage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Rock Rain"))
    {
        ImGui::DragInt("Rocks", &rockCount, 1, 1, 40);
        ImGui::DragFloat("Spacing (s)", &rockSpacing, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Warn Time (s)", &rockWarn, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Radius (m)", &rockRadius, 0.05f, 0.3f, 10.0f);
        ImGui::DragFloat("Spread (m)", &rockSpread, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Damage (base)", &rockDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Fall Time (s)", &rockFallTime, 0.01f, 0.0f, 2.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Shockwave"))
    {
        ImGui::DragFloat("Windup (s)", &waveWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Windup Ring (m)", &waveWindupRadius, 0.1f, 0.5f, 20.0f);
        ImGui::DragFloat("Speed (m/s)", &waveSpeed, 0.1f, 1.0f, 60.0f);
        ImGui::DragFloat("Max Radius (m)", &waveMaxRadius, 0.5f, 2.0f, 100.0f);
        ImGui::DragFloat("Band (m)", &waveBand, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat("Dodge Height (m)", &waveDodgeHeight, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Damage (base)", &waveDamage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Charge"))
    {
        ImGui::DragFloat("Windup (s)", &chargeWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Speed (m/s)", &chargeSpeed, 0.5f, 1.0f, 80.0f);
        ImGui::DragFloat("Length (m)", &chargeLength, 0.5f, 1.0f, 100.0f);
        ImGui::DragFloat("Lane Ring (m)", &chargeLaneRadius, 0.05f, 0.2f, 10.0f);
        ImGui::DragFloat("Lane Spacing (m)", &chargeLaneSpacing, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Hit Radius (m)", &chargeHitRadius, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Damage (base)", &chargeDamage, 0.5f, 0.0f, 500.0f);
        ImGui::SeparatorText("Thunder Stag (lightning along the lane)");
        ImGui::Checkbox("Stag Thunder", &stagThunder);
        ImGui::DragFloat("Thunder Warn (s)", &stagThunderWarn, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Thunder Damage (base)", &stagThunderDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Thunder Radius (m)", &stagThunderRadius, 0.05f, 0.3f, 6.0f);
        ImGui::DragFloat("Head Toss (s)", &stagTossTime, 0.01f, 0.02f, 1.0f);
        ImGui::DragFloat("Head Recover (s)", &stagRecoverTime, 0.01f, 0.02f, 2.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Summon"))
    {
        ImGui::DragFloat("Windup (s)", &summonWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragInt("Count", &summonCount, 1, 1, 30);
        ImGui::DragInt("Count Enraged", &summonCountEnraged, 1, 1, 30);
        for (int k = 0; k < kSummonPicks; ++k)
        {
            ImGui::PushID(k);
            int kind = (int)summonPicks[k].kind;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputInt("##kind", &kind)) summonPicks[k].kind = (uint32_t)std::clamp(kind, 0, (int)Swarm::kEnemyKindDashBomber);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Kind / Weight", &summonPicks[k].weight, 0.01f, 0.0f, 10.0f);
            ImGui::PopID();
        }
        ImGui::TextDisabled("kinds: 0 mob 1 bomber 10 spike ball 11 burrower 12 sand mage");
        ImGui::DragFloat2("Ring Min / Max (m)", &summonRingMin, 0.1f, 0.5f, 30.0f);
        ImGui::DragFloat("Spawn Ring (m)", &summonRingRadius, 0.05f, 0.2f, 5.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Sand Spear (sand guardian)"))
    {
        ImGui::DragFloat("Windup (s)", &spearWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Length (m)", &spearLength, 0.5f, 2.0f, 60.0f);
        ImGui::DragFloat("Speed (m/s)", &spearSpeed, 0.5f, 1.0f, 80.0f);
        ImGui::DragFloat("Lane Radius (m)", &spearLaneRadius, 0.05f, 0.2f, 5.0f);
        ImGui::DragFloat("Lane Spacing (m)", &spearLaneSpacing, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Dodge Height (m)", &spearDodgeHeight, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Damage (base)", &spearDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragInt("Lines Enraged", &spearLinesEnraged, 1, 1, 7);
        ImGui::DragFloat("Fan (deg)", &spearFanDeg, 0.5f, 0.0f, 90.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Burrow (sand guardian)"))
    {
        ImGui::DragFloat("Sink (s)", &burrowSinkTime, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Speed (m/s)", &burrowSpeed, 0.1f, 1.0f, 40.0f);
        ImGui::DragFloat("Max Travel (s)", &burrowMaxTravel, 0.1f, 0.2f, 15.0f);
        ImGui::DragFloat("Stop Dist (m)", &burrowStopDist, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Warn (s)", &burrowWarn, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Rise (s)", &burrowRiseTime, 0.05f, 0.05f, 2.0f);
        ImGui::DragFloat("Radius (m)", &burrowRadius, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Damage (base)", &burrowDamage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Scorpion: Sting / Claw Sweep"))
    {
        ImGui::Text("poison left %.1f s", m_PoisonLeft);
        ImGui::DragFloat("Sting Windup (s)", &stingWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Sting Speed (m/s)", &stingSpeed, 0.5f, 1.0f, 40.0f);
        ImGui::DragFloat("Sting Length (m)", &stingLength, 0.5f, 1.0f, 30.0f);
        ImGui::DragFloat("Sting Band Width (m)", &stingWidth, 0.1f, 0.5f, 8.0f);
        ImGui::DragFloat("Sting Hit Radius (m)", &stingHitRadius, 0.05f, 0.5f, 8.0f);
        ImGui::DragFloat("Sting Damage (base)", &stingDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Sting Rest (s)", &stingRest, 0.05f, 0.0f, 3.0f);
        ImGui::DragFloat("Poison Time (s)", &stingPoisonTime, 0.1f, 0.0f, 30.0f);
        ImGui::SliderFloat("Poison Slow (run x)", &stingPoisonSlow, 0.1f, 1.0f);
        ImGui::DragFloat("Claw Windup (s)", &clawWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Claw Radius (m)", &clawRadius, 0.1f, 1.0f, 15.0f);
        ImGui::DragFloat("Claw Half Angle (deg)", &clawHalfDeg, 1.0f, 10.0f, 90.0f);
        ImGui::DragFloat("Claw Dodge Height (m)", &clawDodgeHeight, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Claw Damage (base)", &clawDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Claw Rest (s)", &clawRest, 0.05f, 0.0f, 3.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Crypt Knight: Chain Pull / revive"))
    {
        ImGui::Text("enraged %s (forced %s)", m_WasAlive ? "?" : "-", forceEnraged ? "yes" : "no");
        ImGui::DragFloat("Chain Windup (s)", &chainWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Chain Fan (deg)", &chainFanDeg, 0.5f, 0.0f, 45.0f);
        ImGui::DragFloat("Chain Range (m)", &chainRange, 0.5f, 2.0f, 40.0f);
        ImGui::DragFloat("Chain Band Width (m)", &chainWidth, 0.05f, 0.3f, 5.0f);
        ImGui::DragFloat("Chain Damage (base)", &chainDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Chain Pull Stop (m)", &chainPullStop, 0.1f, 0.5f, 10.0f);
        ImGui::DragFloat("Chain Rest (s)", &chainRest, 0.05f, 0.0f, 3.0f);
        ImGui::TextDisabled("revive: StageDirector bossLives / bossReviveDelay / bossReviveHpFrac (Stage section)");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Werewolf: Pounce / Claw Combo / Howl"))
    {
        ImGui::Text("howl haste left %.1f s  enraged %d", m_HowlHasteLeft, m_LastEnraged ? 1 : 0);
        ImGui::DragFloat("Pounce Windup (s)", &pounceWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Pounce Air (s)", &pounceAirTime, 0.05f, 0.1f, 3.0f);
        ImGui::DragFloat("Pounce Height (m)", &pounceHeight, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Pounce Radius (m)", &pounceRadius, 0.1f, 0.5f, 10.0f);
        ImGui::DragFloat("Pounce Damage (base)", &pounceDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Pounce Rest (s)", &pounceRest, 0.05f, 0.0f, 3.0f);
        ImGui::DragFloat("Claw Windup (s)", &clawComboWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Claw Dash Speed (m/s)", &clawDashSpeed, 0.5f, 1.0f, 40.0f);
        ImGui::DragFloat("Claw Dash Length (m)", &clawDashLength, 0.5f, 1.0f, 30.0f);
        ImGui::DragFloat("Claw Hit Radius (m)", &clawComboHitRadius, 0.05f, 0.5f, 8.0f);
        ImGui::DragFloat("Claw Damage (base)", &clawComboDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragInt("Claw Count", &clawComboCount, 1, 1, 9);
        ImGui::DragInt("Claw Count Enraged", &clawComboCountEnraged, 1, 1, 9);
        ImGui::DragFloat("Howl Windup (s)", &howlWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Howl Haste (s)", &howlHasteTime, 0.5f, 0.0f, 60.0f);
        ImGui::SliderFloat("Howl Gap x", &howlGapMul, 0.1f, 1.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Quicksand (sand guardian)"))
    {
        ImGui::DragFloat("Windup (s)", &sandWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Radius (m)", &sandRadius, 0.1f, 1.0f, 30.0f);
        ImGui::DragFloat("Duration (s)", &sandDuration, 0.1f, 0.5f, 30.0f);
        ImGui::DragFloat("Fade (s)", &sandFadeTime, 0.05f, 0.05f, 5.0f);
        ImGui::SliderFloat("Run Speed x", &sandSlow, 0.0f, 1.0f);
        ImGui::DragFloat("Pull at Edge (m/s)", &sandPullEdge, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Pull at Centre (m/s)", &sandPullCenter, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Dead Radius (m)", &sandDeadRadius, 0.05f, 0.0f, 5.0f);
        ImGui::TreePop();
    }
}
