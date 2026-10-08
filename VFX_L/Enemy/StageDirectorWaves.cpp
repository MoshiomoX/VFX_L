// ============================================================
// StageDirectorWaves.cpp
// StageDirector のうち、湧きの波・最終ウェーブの予告・画面の真ん中の案内（2026-10-07。StageDirector.cpp が 430 行あるので分けた）。
//   ・SpawnWaves（押し寄せ → 一息 → 普段）の倍率を MobSpawner::waveRateMul へ、方向を SpawnDirector の arc* へ
//   ・時間切れの finalWarnTime 秒前：「最終ウェーブまで あと…」の案内と、湧きを 1 → finalWarnRateMul 倍へ
//   ・時間切れの後の湧きは finalSpawnRateStart から finalRampTime 秒で finalSpawnRate へ（以前は 20 体/秒へ一気に）
// ============================================================
#include "Enemy/StageDirector.h"
#include "Enemy/MobSpawner.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"

void StageDirector::SetAnnounce(const wchar_t* text, float seconds)
{
    m_AnnounceText = text;
    m_AnnounceLeft = seconds;
    m_AnnounceAge = 0.0f;
}

const wchar_t* StageDirector::Announce(float& alpha, float& age) const
{
    alpha = 0.0f;
    age = m_AnnounceAge;
    if (!m_AnnounceText || m_AnnounceLeft <= 0.0f) return nullptr;
    // 出る時 0.15 秒、消える時 0.6 秒のフェード
    const float in = (std::min)(1.0f, m_AnnounceAge / 0.15f);
    const float out = (std::min)(1.0f, m_AnnounceLeft / 0.6f);
    alpha = (std::min)(in, out);
    return m_AnnounceText;
}

void StageDirector::UpdateWaves(float runTime, float dt, MobSpawner& mobs)
{
    if (m_AnnounceLeft > 0.0f)
    {
        m_AnnounceLeft -= dt;
        m_AnnounceAge += dt;
    }

    auto& director = mobs.Director();
    waves.Update(runTime, stageTime);
    if (waves.JustStarted())
    {
        SetAnnounce(L"敵の群れが押し寄せてくる！", 3.5f);
        AudioSystem::Get().Play("wave_warn");
        m_Event = "surge";
    }

    float rateMul = waves.RateMul();
    const bool timed = stageTime > 0.0f;
    if (timed && runTime >= stageTime)
        rateMul = 1.0f;   // 最終ウェーブの湧きは finalSpawnRate が決める
    else if (timed && finalWarnTime > 0.0f && runTime >= stageTime - finalWarnTime)
    {
        // 予告：時間切れへ向けて湧きを少しずつ上げる（崖を坂にする）
        const float t = 1.0f - (stageTime - runTime) / finalWarnTime;
        rateMul = (std::max)(rateMul, 1.0f + (finalWarnRateMul - 1.0f) * t);
        if (!m_FinalWarned)
        {
            m_FinalWarned = true;
            swprintf_s(m_AnnounceBuf, L"最終ウェーブまで あと%d秒", (int)(finalWarnTime + 0.5f));
            SetAnnounce(m_AnnounceBuf, 4.0f);
            AudioSystem::Get().Play("wave_warn");
            m_Event = "final warning";
        }
    }
    else
        m_FinalWarned = false;   // パネルで制限時間を延ばした時

    mobs.waveRateMul = rateMul;
    const bool arc = waves.ArcActive() && !(timed && runTime >= stageTime);
    director.arcYaw = waves.ArcYaw();
    director.arcHalf = arc ? waves.arcHalfDeg * 0.0174532925f : 3.14159265f;
}

void StageDirector::DrawWavesImGui(float runTime)
{
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1), "Spawn Waves (surge / lull)");
    waves.DrawImGui(runTime);
    ImGui::DragFloat("Final Warn (s before)", &finalWarnTime, 1.0f, 0.0f, 300.0f);
    ImGui::DragFloat("Final Warn Spawn x", &finalWarnRateMul, 0.05f, 1.0f, 10.0f);
    ImGui::DragFloat("Final Spawn Start / s", &finalSpawnRateStart, 0.5f, 0.0f, 200.0f);
    ImGui::DragFloat("Final Ramp (s)", &finalRampTime, 1.0f, 0.0f, 600.0f);
}
