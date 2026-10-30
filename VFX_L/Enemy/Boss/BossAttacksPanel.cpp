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
    ImGui::Checkbox("Enrage Roar##boss", &enrageRoar);   // 狂暴の咆哮（2026-10-29）
    ImGui::SameLine();
    ImGui::Text("%s", m_EnrageRoarDone ? "(done this fight)" : "");
    ImGui::DragFloat("Roar Time (s)##boss", &enrageRoarTime, 0.05f, 0.05f, 5.0f);
    ImGui::DragFloat("Roar Ring (m)##boss", &enrageRingMax, 0.5f, 1.0f, 40.0f);
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
    ImGui::SameLine();
    if (ImGui::Button("Antler Toss Now")) QueueMove(BossMove::AntlerToss);
    ImGui::SameLine();
    if (ImGui::Button("Antler Bolt Now")) QueueMove(BossMove::AntlerBolt);
    if (ImGui::Button("Dust Devils Now")) QueueMove(BossMove::DustDevils);
    ImGui::SameLine();
    if (ImGui::Button("Djinn Breath Now")) QueueMove(BossMove::DjinnBreath);
    ImGui::SameLine();
    if (ImGui::Button("Sand Shift Now")) QueueMove(BossMove::DjinnShift);
    ImGui::SameLine();
    if (ImGui::Button("Sand Fist Now")) QueueMove(BossMove::SandFist);
    ImGui::SameLine();
    if (ImGui::Button("Djinn Phase 2 Now")) ForceDjinnPhase2();
    if (ImGui::Button("Curse Orbs Now")) QueueMove(BossMove::CurseOrbs);
    ImGui::SameLine();
    if (ImGui::Button("Raise Dead Now")) QueueMove(BossMove::RaiseDead);
    ImGui::SameLine();
    if (ImGui::Button("Shadow Step Now")) QueueMove(BossMove::ShadowStep);
    ImGui::SameLine();
    if (ImGui::Button("Curse Floor Now")) QueueMove(BossMove::CurseFloor);
    if (ImGui::Button("Beast Dive Now")) QueueMove(BossMove::BeastDive);
    ImGui::SameLine();
    if (ImGui::Button("Petrify Breath Now")) QueueMove(BossMove::PetrifyBreath);
    ImGui::SameLine();
    if (ImGui::Button("Wing Gust Now")) QueueMove(BossMove::WingGust);
    ImGui::SameLine();
    if (ImGui::Button("Tail Sweep Now")) QueueMove(BossMove::TailSweep);
    ImGui::SameLine();
    if (ImGui::Button("Beast Phase 2 Now")) ForceDjinnPhase2();
    if (ImGui::Button("Sky Crush Now")) QueueMove(BossMove::SkyCrush);
    ImGui::SameLine();
    if (ImGui::Button("Feather Shards Now")) QueueMove(BossMove::FeatherShards);

    // 面毎の Boss（StageDef::boss）。ここで替えると技の有り / 無し・召喚の種類・体の色が入れ替わる
    int type = (int)m_Type;
    const char* types[] = { "Stone Giant (grassland)", "Sand Guardian (desert)", "Scorpion (desert 2)", "Thunder Stag (grassland 3)", "Crypt Knight (ruins 1)", "Werewolf (grassland 2)", "Sand Djinn (desert 3)", "Bone King (ruins 3)", "Stone Beast (ruins 2)" };
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
        ImGui::DragInt("Charges When Enraged", &chargeCountEnraged, 0.1f, 1, 8);
        ImGui::DragFloat("Re-windup (s)", &chargeRewindup, 0.05f, 0.05f, 3.0f);
        ImGui::TreePop();
    }
    // 雷鳴の雄鹿の専用の技（2026-10-29）
    if (ImGui::TreeNode("Thunder Stag: Antler Toss / Antler Bolt"))
    {
        ImGui::SeparatorText("Antler Toss (front fan, launches high)");
        ImGui::DragFloat("Toss Windup (s)", &tossWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Toss Radius (m)", &tossRadius, 0.1f, 0.5f, 15.0f);
        ImGui::DragFloat("Toss Half Angle (deg)", &tossHalfDeg, 1.0f, 5.0f, 90.0f);
        ImGui::DragFloat("Toss Dodge Height (m)", &tossDodgeHeight, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Toss Damage (base)", &tossDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Toss Rest (s)", &tossRest, 0.05f, 0.05f, 3.0f);
        ImGui::Checkbox("Toss Warning Fan", &tossWarn);
        ImGui::SeparatorText("Antler Bolt (lightning along a fan of bands)");
        ImGui::DragFloat("Bolt Windup (s)", &boltWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragInt("Bolts", &boltCount, 0.1f, 1, 9);
        ImGui::DragInt("Bolts Enraged", &boltCountEnraged, 0.1f, 1, 9);
        ImGui::DragFloat("Bolt Fan Step (deg)", &boltFanDeg, 0.5f, 1.0f, 60.0f);
        ImGui::DragFloat("Bolt Length (m)", &boltLength, 0.5f, 2.0f, 40.0f);
        ImGui::DragFloat("Bolt Width (m)", &boltWidth, 0.05f, 0.2f, 5.0f);
        ImGui::DragFloat("Bolt Damage (base)", &boltDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Bolt Rest (s)", &boltRest, 0.05f, 0.05f, 3.0f);
        ImGui::Checkbox("Test: treat as enraged", &testEnraged);
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
    if (ImGui::TreeNode("Sand Djinn: Dust Devils / Breath / Shift / Fist"))
    {
        DjinnTune& d = djinn;
        ImGui::Text("phase %d  hover %.2f m  devils %d  pose %.2f", m_Djinn.phase2 ? 2 : 1, m_Djinn.hoverBase, (int)m_Djinn.devils.size(), m_Djinn.pose);
        ImGui::SeparatorText("Floating (phase 1)");
        ImGui::DragFloat("Hover (m)", &d.hover, 0.05f, 0.0f, 6.0f);
        ImGui::DragFloat("Land Time (s)", &d.landTime, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat2("Keep Min / Max (m)", &d.keepMin, 0.1f, 0.0f, 40.0f);
        ImGui::SeparatorText("Dust Devils");
        ImGui::DragInt("Count", &d.devilCount, 1, 1, 8);
        ImGui::DragInt("Count Phase 2", &d.devilCountPhase2, 1, 1, 8);
        ImGui::DragFloat("Cast Time (s)", &d.devilCastTime, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Devil Windup (s)", &d.devilWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Devil Life (s)", &d.devilLife, 0.1f, 0.5f, 20.0f);
        ImGui::DragFloat2("Ring Radius Start / End (m)", &d.devilRadiusStart, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Spin (deg/s)", &d.devilSpinDeg, 1.0f, -360.0f, 360.0f);
        ImGui::DragFloat("Devil Hit Radius (m)", &d.devilHitRadius, 0.05f, 0.2f, 6.0f);
        ImGui::DragFloat("Devil Damage (base)", &d.devilDamage, 0.5f, 0.0f, 500.0f);
        ImGui::SeparatorText("Breath");
        ImGui::DragFloat("Breath Windup (s)", &d.breathWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Breath Time (s)", &d.breathTime, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat("Breath Radius (m)", &d.breathRadius, 0.1f, 1.0f, 30.0f);
        ImGui::DragFloat("Breath Half Angle (deg)", &d.breathHalfDeg, 0.5f, 5.0f, 90.0f);
        ImGui::DragFloat("Breath Turn (deg/s)", &d.breathTurnDeg, 1.0f, 0.0f, 180.0f);
        ImGui::DragFloat("Breath Tick (s)", &d.breathTick, 0.01f, 0.05f, 2.0f);
        ImGui::DragFloat("Breath Damage (base)", &d.breathDamage, 0.5f, 0.0f, 500.0f);
        ImGui::SeparatorText("Sand Shift");
        ImGui::DragFloat("Shift Sink (s)", &d.shiftSink, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Shift Speed (m/s)", &d.shiftSpeed, 0.5f, 1.0f, 60.0f);
        ImGui::DragFloat("Shift Behind (m)", &d.shiftBehind, 0.1f, 1.0f, 20.0f);
        ImGui::DragFloat("Shift Rise (s)", &d.shiftRise, 0.05f, 0.05f, 3.0f);
        ImGui::SeparatorText("Sand Fist (phase 2)");
        ImGui::DragFloat("Fist Windup (s)", &d.fistWindup, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Fist Radius (m)", &d.fistRadius, 0.1f, 1.0f, 20.0f);
        ImGui::DragFloat("Fist Half Angle (deg)", &d.fistHalfDeg, 0.5f, 5.0f, 90.0f);
        ImGui::DragFloat("Fist Damage (base)", &d.fistDamage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Bone King: Curse Orbs / Raise Dead / Shadow Step / Curse Floor"))
    {
        KingTune& k = king;
        ImGui::Text("orbs %d  curse %s r %.1f strength %.2f", (int)m_King.orbs.size(), m_King.curse.active ? "on" : "off", m_King.curse.radius, m_King.curse.strength);
        ImGui::DragFloat("Hover (m)##king", &k.hover, 0.05f, 0.0f, 6.0f);
        ImGui::DragFloat2("Keep Min / Max (m)##king", &k.keepMin, 0.1f, 0.0f, 40.0f);
        ImGui::SeparatorText("Curse Orbs");
        ImGui::DragFloat("Orb Windup (s)", &k.orbWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragInt("Orb Count", &k.orbCount, 1, 1, 12);
        ImGui::DragInt("Orb Count Enraged", &k.orbCountEnraged, 1, 1, 16);
        ImGui::DragFloat("Orb Fan (deg)", &k.orbFanDeg, 0.5f, 0.0f, 90.0f);
        ImGui::DragFloat("Orb Speed (m/s)", &k.orbSpeed, 0.1f, 0.5f, 20.0f);
        ImGui::DragFloat("Orb Turn (deg/s)", &k.orbTurnDeg, 1.0f, 0.0f, 360.0f);
        ImGui::DragFloat("Orb Life (s)", &k.orbLife, 0.1f, 0.5f, 30.0f);
        ImGui::DragFloat("Orb Hit Radius (m)", &k.orbHitRadius, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Orb Damage (base)", &k.orbDamage, 0.5f, 0.0f, 500.0f);
        ImGui::SeparatorText("Raise Dead");
        ImGui::DragFloat("Raise Windup (s)", &k.raiseWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Raise Radius (m)", &k.raiseRadius, 0.5f, 1.0f, 60.0f);
        ImGui::DragInt("Raise Max", &k.raiseMax, 1, 0, 40);
        ImGui::DragInt("Raise Min", &k.raiseMin, 1, 0, 40);
        ImGui::SeparatorText("Shadow Step");
        ImGui::DragFloat("Step Sink (s)", &k.stepSink, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Step Speed (m/s)", &k.stepSpeed, 0.5f, 1.0f, 60.0f);
        ImGui::DragFloat("Step Dist (m)", &k.stepDist, 0.1f, 1.0f, 30.0f);
        ImGui::DragFloat("Step Turn (deg)", &k.stepTurnDeg, 1.0f, 0.0f, 180.0f);
        ImGui::DragFloat("Step Rise (s)", &k.stepRise, 0.05f, 0.05f, 3.0f);
        ImGui::SeparatorText("Curse Floor");
        ImGui::DragFloat("Curse Windup (s)", &k.curseWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Curse Radius (m)", &k.curseRadius, 0.1f, 1.0f, 20.0f);
        ImGui::TextDisabled("(the look BossCurseFloor.json is made for 7 m)");
        ImGui::DragFloat("Curse Duration (s)", &k.curseDuration, 0.1f, 0.5f, 30.0f);
        ImGui::SliderFloat("Curse Run Speed x", &k.curseSlow, 0.0f, 1.0f);
        ImGui::DragFloat("Curse Pull Edge (m/s)", &k.cursePullEdge, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Curse Pull Centre (m/s)", &k.cursePullCenter, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Curse Tick (s)", &k.curseTick, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Curse Tick Damage (base)", &k.curseTickDamage, 0.5f, 0.0f, 200.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Stone Beast: Dive / Petrify Breath / Wing Gust / Tail Sweep"))
    {
        BeastTune& s = beast;
        ImGui::Text("phase %d  hover %.2f  wing %.2f  spin %.2f  petrify %.2f  stone %.2f", m_Beast.phase2 ? 2 : 1, m_Beast.hoverBase, m_Beast.wing,
            m_Beast.spin, m_Beast.petrify, m_Beast.stoneLeft);
        ImGui::DragFloat("Hover (m)##beast", &s.hover, 0.05f, 0.0f, 8.0f);
        ImGui::DragFloat("Land Time (s)##beast", &s.landTime, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat2("Keep Min / Max (m)##beast", &s.keepMin, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Flap Speed (rad/s)", &s.flapSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::SeparatorText("Dive");
        ImGui::DragFloat("Dive Windup (s)", &s.diveWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Dive Speed (m/s)", &s.diveSpeed, 0.5f, 1.0f, 80.0f);
        ImGui::DragFloat("Dive Overshoot (m)", &s.diveOvershoot, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat2("Dive Length Min / Max (m)", &s.diveMinLength, 0.1f, 1.0f, 60.0f);
        ImGui::DragFloat("Dive Width (m)", &s.diveWidth, 0.05f, 0.2f, 10.0f);
        ImGui::DragFloat("Dive Hit Radius (m)", &s.diveHitRadius, 0.05f, 0.2f, 10.0f);
        ImGui::DragFloat("Dive Low Hover (m)", &s.diveLowHover, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Dive Damage (base)", &s.diveDamage, 0.5f, 0.0f, 500.0f);
        ImGui::SeparatorText("Petrify Breath");
        ImGui::DragFloat("Breath Windup (s)##beast", &s.breathWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Breath Time (s)##beast", &s.breathTime, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat("Breath Radius (m)##beast", &s.breathRadius, 0.1f, 1.0f, 30.0f);
        ImGui::DragFloat("Breath Half (deg)##beast", &s.breathHalfDeg, 0.5f, 1.0f, 89.0f);
        ImGui::DragFloat("Breath Turn (deg/s)##beast", &s.breathTurnDeg, 1.0f, 0.0f, 180.0f);
        ImGui::DragFloat("Breath Damage (base)##beast", &s.breathDamage, 0.5f, 0.0f, 200.0f);
        ImGui::DragFloat("Petrify per Hit", &s.petrifyPerHit, 0.01f, 0.0f, 1.0f);
        ImGui::SliderFloat("Petrify Slow", &s.petrifySlow, 0.0f, 1.0f);
        ImGui::DragFloat("Petrify Decay (/s)", &s.petrifyDecay, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Petrify Lock (s)", &s.petrifyLockTime, 0.05f, 0.0f, 10.0f);
        ImGui::SliderFloat("Petrify Lock Run x", &s.petrifyLockSlow, 0.0f, 1.0f);
        ImGui::SeparatorText("Wing Gust");
        ImGui::DragFloat("Gust Windup (s)", &s.gustWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Gust Radius (m)", &s.gustRadius, 0.1f, 1.0f, 40.0f);
        ImGui::DragFloat("Gust Half (deg)", &s.gustHalfDeg, 0.5f, 1.0f, 89.0f);
        ImGui::DragFloat("Gust Damage (base)", &s.gustDamage, 0.5f, 0.0f, 200.0f);
        ImGui::SeparatorText("Tail Sweep");
        ImGui::DragFloat("Sweep Windup (s)", &s.sweepWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Sweep Radius (m)", &s.sweepRadius, 0.1f, 1.0f, 20.0f);
        ImGui::DragFloat("Sweep Time (s)", &s.sweepTime, 0.02f, 0.1f, 3.0f);
        ImGui::DragFloat("Sweep Dodge Height (m)", &s.sweepDodgeHeight, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Sweep Damage (base)", &s.sweepDamage, 0.5f, 0.0f, 500.0f);
        ImGui::SeparatorText("Sky Crush");
        ImGui::DragFloat("Crush Rise (s)", &s.crushRise, 0.05f, 0.05f, 5.0f);
        ImGui::DragFloat("Crush High (m)", &s.crushHigh, 0.5f, 2.0f, 60.0f);
        ImGui::DragFloat("Crush Track (s)", &s.crushTrack, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Crush Follow (m/s)", &s.crushFollowSpeed, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Crush Lock (s)", &s.crushLock, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Crush Fall (s)", &s.crushFall, 0.02f, 0.05f, 3.0f);
        ImGui::DragFloat("Crush Radius (m)", &s.crushRadius, 0.1f, 0.5f, 15.0f);
        ImGui::DragFloat("Crush Damage (base)", &s.crushDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Crush Rest (s)", &s.crushRest, 0.05f, 0.0f, 5.0f);
        ImGui::SeparatorText("Feather Shards");
        ImGui::Text("shards in the air %d", (int)m_Beast.shards.size());
        ImGui::DragFloat("Shard Windup (s)", &s.shardWindup, 0.05f, 0.05f, 5.0f);
        ImGui::DragInt("Shard Count", &s.shardCount, 1, 1, 21);
        ImGui::DragInt("Shard Count Enraged", &s.shardCountEnraged, 1, 1, 25);
        ImGui::DragFloat("Shard Spacing (deg)", &s.shardSpacingDeg, 0.5f, 1.0f, 60.0f);
        ImGui::DragInt("Shard Volleys", &s.shardVolleys, 1, 1, 6);
        ImGui::DragFloat("Shard Volley Gap (s)", &s.shardVolleyGap, 0.05f, 0.1f, 3.0f);
        ImGui::DragFloat("Shard Speed (m/s)", &s.shardSpeed, 0.5f, 1.0f, 60.0f);
        ImGui::DragFloat("Shard Range (m)", &s.shardRange, 0.5f, 2.0f, 60.0f);
        ImGui::DragFloat("Shard Hit Radius (m)", &s.shardHitRadius, 0.05f, 0.05f, 3.0f);
        ImGui::DragFloat("Shard Damage (base)", &s.shardDamage, 0.5f, 0.0f, 200.0f);
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
