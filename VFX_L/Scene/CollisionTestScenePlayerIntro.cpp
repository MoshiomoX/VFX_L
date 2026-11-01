// ============================================================
// CollisionTestScenePlayerIntro.cpp
// プレイヤーの登場（2026-11-01。ユーザーの選択：魔法陣の転送 / 約 2 秒 / カメラを寄せてから戻す）。毎面の始めに 1 回。
//   0.00  足元に金の魔法陣が開き、光が集まる（プレイヤーはまだ居ない）
//   0.75  光の柱が立ち、プレイヤーが閃光の中に現れる（体の金色が引いていく）。草が倒れ、カメラが揺れる
//   2.00  カメラが元の位置に戻り、HUD が出て操作できるようになる
// この間は戦いを進めない（時間・湧き・操作は止める）。景色の更新はタイトルと同じ UpdateTitleWorld
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/TransformComponent.h"
#include "VFX_Editor/VFXId.h"

#include <Windows.h>

namespace
{
    constexpr float kFeetToCentre = 0.92f;  // 足元 → プレイヤーの位置（カプセルの中心）
    constexpr float kTotalTime = 2.0f;
    constexpr float kRevealAt = 0.75f;     // 光の柱が立って現れる時刻
    constexpr float kFlashTime = 0.7f;     // 体の金色が引くまで
    constexpr float kMaxStep = 1.0f / 30.0f;   // 読み込み直後の長いフレームで演出を飛ばさない
    constexpr float kCamBackFrom = 0.45f;  // カメラが引き始める時刻
    constexpr float kPillarAt = 0.61f;     // 空から光が落ち始める（kRevealAt に地面へ届く）
    constexpr float kPillarHeight = 24.0f;
    constexpr float kCamDistance = 8.5f;   // 魔法陣（直径 5.7m）が画面に収まる近さ
    constexpr float kCamPitch = 30.0f;
    constexpr float kCamYawOffset = -30.0f;
    constexpr float kCircleSize = 2.2f;    // 足元の魔法陣の大きさの倍率

    float Smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    float Mix(float a, float b, float t) { return a + (b - a) * t; }
}

void CollisionTestScene::StartPlayerIntro()
{
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<SkinnedAnimComponent>(m_Player)) return;
    m_PlayerIntroT = 0.0f;
    m_PlayerIntroShown = false;
    m_PlayerIntroLog = GetEnvironmentVariableA("VFXL_PLAYER_INTRO", nullptr, 0) > 0;   // TEMP-TEST

    // 出生の高さから落ちてくるとカメラも一緒に下がるので、始めから地面に立たせる
    if (const Vector3* pp = PlayerPos())
    {
        m_IntroFeet = Vector3(pp->x, m_Grid.SampleHeight(pp->x, pp->z), pp->z);
        m_Registry.Get<TransformComponent>(m_Player).position.y = m_IntroFeet.y + kFeetToCentre;
    }

    auto& cam = m_Camera.Camera();
    m_IntroCamDistance = cam.distance;
    m_IntroCamPitch = cam.GetPitch();
    m_IntroCamYaw = cam.GetYaw();
    cam.avoidOcclusion = false;
    cam.distance = kCamDistance;
    cam.SetPitch(kCamPitch);
    cam.SetYaw(m_IntroCamYaw + kCamYawOffset);
    cam.SnapToTarget();

    m_GameUI.hideAll = true;
    m_Registry.Get<SkinnedAnimComponent>(m_Player).visible = false;

    m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::PlayerArrive), m_IntroFeet, 2.0f, false, m_VFXContext);
    m_IntroPillarFx = 0;
    AudioSystem::Get().Play("beam_charge");
}

void CollisionTestScene::UpdatePlayerIntro(float dt)
{
    const float step = (std::min)(dt, kMaxStep);
    const float before = m_PlayerIntroT;
    m_PlayerIntroT += step;
    const float t = m_PlayerIntroT;

    UpdateTitleWorld(step, false);
    m_Camera.UpdateMouseCapture(false);   // 操作は無いが、カーソルは始めから出さない
    m_PlayerVFX.SetIntroCircle(Smooth(t / 0.3f) * (1.0f - Smooth((t - kRevealAt) / 0.9f)), kCircleSize);
    m_PlayerVFX.Update(m_Registry, m_Player, step, 0.0f, {});

    // ---- 空から光が落ちる（根元 = 空、先端 = 足元。伸びる時間で地面へ届く）----
    if (before < kPillarAt && t >= kPillarAt)
        m_IntroPillarFx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::PlayerArrivePillar),
            m_IntroFeet + Vector3(0.0f, kPillarHeight, 0.0f), 1.0f, false, m_VFXContext);
    if (m_IntroPillarFx)
        m_AreaVFX.SetInstance(m_IntroPillarFx, m_IntroFeet + Vector3(0.0f, kPillarHeight, 0.0f), m_IntroFeet);

    // ---- 現れる ----
    auto& anim = m_Registry.Get<SkinnedAnimComponent>(m_Player);
    if (t >= kRevealAt && !m_PlayerIntroShown)
    {
        m_PlayerIntroShown = true;
        anim.visible = true;
        AudioSystem::Get().Play("mana_surge");
        m_Camera.Camera().RaiseTrauma(0.22f);
        m_Grass.AddGust(m_IntroFeet, 4.0f);
        m_Debris.AddGust(m_IntroFeet, 4.0f);
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::LandDustHeavy), m_IntroFeet, 1.0f, false, m_VFXContext, m_PlayerVFX.dustTint);   // 光の着地で石が跳ねる
    }
    if (m_PlayerIntroShown)
    {
        const float k = 1.0f - Smooth((t - kRevealAt) / kFlashTime);
        anim.hitFlash = k;
        anim.hitFlashColor = Vector3(1.6f, 1.25f, 0.5f);
    }

    // ---- カメラ：寄った所から元へ戻る ----
    auto& cam = m_Camera.Camera();
    const float back = Smooth((t - kCamBackFrom) / (kTotalTime - kCamBackFrom));
    cam.distance = Mix(kCamDistance, m_IntroCamDistance, back);
    cam.SetPitch(Mix(kCamPitch, m_IntroCamPitch, back));
    cam.SetYaw(Mix(m_IntroCamYaw + kCamYawOffset, m_IntroCamYaw, back));
    cam.SnapToTarget();
    m_Camera.Update(step, PlayerPos());

    // TEMP-TEST: 自動の撮影
    if (m_PlayerIntroLog)
        for (float at : { 0.35f, 0.72f, 0.82f, 1.05f, 1.5f, 1.95f })
            if (before < at && t >= at)
            {
                char line[48];
                snprintf(line, sizeof(line), "playerintro look t%.2f", at);
                AutoTestLog(line);
            }

    // ---- 終わり：操作を返す ----
    if (t >= kTotalTime)
    {
        EndPlayerIntro();
        if (m_PlayerIntroLog) AutoTestLog("playerintro done");
    }
}

// 途中で面を読み直した時もここでカメラと HUD を元へ戻す
void CollisionTestScene::EndPlayerIntro()
{
    m_PlayerIntroT = -1.0f;
    auto& cam = m_Camera.Camera();
    cam.distance = m_IntroCamDistance;
    cam.SetPitch(m_IntroCamPitch);
    cam.SetYaw(m_IntroCamYaw);
    cam.avoidOcclusion = true;
    m_GameUI.hideAll = false;
    m_PlayerVFX.SetIntroCircle(0.0f, 1.0f);
    if (m_Registry.IsValid(m_Player) && m_Registry.Has<SkinnedAnimComponent>(m_Player))
    {
        auto& anim = m_Registry.Get<SkinnedAnimComponent>(m_Player);
        anim.visible = true;
        anim.hitFlash = 0.0f;
    }
}
