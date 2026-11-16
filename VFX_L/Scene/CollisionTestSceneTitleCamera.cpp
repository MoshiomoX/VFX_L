// ============================================================
// CollisionTestSceneTitleCamera.cpp
// タイトル画面と選択画面のカメラ。プレイヤーの周りをゆっくり回る。
//   ・タイトルだけマウスで動かせる：右ドラッグで回す（縦は高さ）、ホイールで寄る / 引く。
//     右下の表の上では効かない。放して少し経つと自動の回転へ戻り、高さと距離も元へ戻る
//   ・プレイヤーは画面の右寄り（見る所をカメラの左へずらす）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Manager/InputManager.h"
#include "Core/DevUI.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kTitleFov = 40.0f;
    constexpr float kLookHeight = 0.9f;
    constexpr float kFrameShift = 2.2f;       // 見る所をカメラの左へずらす = プレイヤーが画面の右寄り
    constexpr float kOrbitSpeedDeg = 9.0f;    // 自動の回転（度 / 実時間の秒）

    constexpr float kDragYawPerPx = 0.25f;    // 右ドラッグ横：度 / ピクセル
    constexpr float kDragHeightPerPx = 0.004f; // 右ドラッグ縦：高さの倍率 / ピクセル
    constexpr float kZoomPerNotch = 0.9f;     // ホイール 1 目盛りの距離の倍率
    constexpr float kHeightMin = 0.3f, kHeightMax = 1.8f;
    constexpr float kZoomMin = 0.55f, kZoomMax = 1.8f;
    constexpr float kReturnDelay = 3.0f;      // 触らなくなってから自動へ戻るまでの秒
    constexpr float kReturnRate = 1.2f;       // 高さ・距離を元へ戻す速さ（1 / 秒）
}

float CollisionTestScene::TitleCamYawDeg() const
{
    return m_TitleFacing + kOrbitSpeedDeg * m_TitleTime + m_TitleCamCtl.yawDeg;
}

void CollisionTestScene::UpdateTitleCameraInput(float dt)
{
    auto& c = m_TitleCamCtl;
    if (!m_TitleScene) { c = {}; return; }

    auto& input = InputManager::Get();
    const bool free = !m_TitleUI.MouseOverTable() && !DevUI::WantMouse();
    bool touched = false;

    if (input.GetMouseTrigger(1) && free && !input.IsUsingPad()) c.dragging = true;
    if (c.dragging && !input.GetMousePress(1)) c.dragging = false;
    if (c.dragging)
    {
        const DirectX::XMFLOAT2 d = input.GetMouseDelta();
        c.yawDeg -= d.x * kDragYawPerPx;
        c.height = std::clamp(c.height + d.y * kDragHeightPerPx, kHeightMin, kHeightMax);
        touched = true;
    }

    const float wheel = input.GetMouseWheel();
    if (wheel != 0.0f && free)
    {
        c.zoom = std::clamp(c.zoom * std::pow(kZoomPerNotch, wheel), kZoomMin, kZoomMax);
        touched = true;
    }

    c.idle = touched ? 0.0f : c.idle + dt;
    if (c.dragging || c.idle < kReturnDelay)
    {
        c.yawDeg -= kOrbitSpeedDeg * dt;   // 触っている間は自動の回転を止める
    }
    else
    {
        const float k = 1.0f - std::exp(-kReturnRate * dt);
        c.height += (1.0f - c.height) * k;
        c.zoom += (1.0f - c.zoom) * k;
    }
}

void CollisionTestScene::UpdateTitleCamera(float dt)
{
    (void)dt;
    const float aspect = (m_ScreenH > 0.0f) ? m_ScreenW / m_ScreenH : 16.0f / 9.0f;
    m_TitleCam.Init(kTitleFov, aspect, 0.1f, 2000.0f);
    const auto& c = m_TitleCamCtl;
    const float yaw = DirectX::XMConvertToRadians(TitleCamYawDeg());
    const Vector3 feet = m_TitleFocus;
    Vector3 pos = feet + Vector3(std::sin(yaw), 0.0f, std::cos(yaw)) * (kTitleCamDistance * c.zoom);
    pos.y = feet.y + kTitleCamHeight * c.height * c.zoom;
    pos.y = (std::max)(pos.y, m_Grid.SampleHeight(pos.x, pos.z) + 0.6f);   // 丘の斜面に潜らない
    Vector3 fwd = feet - pos; fwd.y = 0.0f; fwd.Normalize();
    const Vector3 right = fwd.Cross(Vector3::Up);   // CameraBase::GetRight と同じ向き
    Vector3 look = feet - right * (kFrameShift * c.zoom);
    look.y = feet.y + kLookHeight;
    m_TitleCam.LookAt(pos, look, Vector3::Up);
}
