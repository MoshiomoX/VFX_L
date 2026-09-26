// ============================================================
// FollowCamera.cpp
// ============================================================
#include "Camera/FollowCamera.h"
#include "Manager/InputManager.h"
#include "Debug/DebugManager.h"
#include <cmath>
#include <algorithm>

namespace
{
    // 臨界減衰ばね（Unity の SmoothDamp と同じ式）。dt が変わっても同じ動きになる
    float SmoothDamp(float cur, float target, float& vel, float smoothTime, float dt)
    {
        if (dt <= 0.0f) return cur;
        smoothTime = (std::max)(0.0001f, smoothTime);
        const float omega = 2.0f / smoothTime;
        const float x = omega * dt;
        const float k = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        const float change = cur - target;
        const float temp = (vel + omega * change) * dt;
        vel = (vel - omega * temp) * k;
        float out = target + (change + temp) * k;

        // 行き過ぎたら止める（ばねが目標を越えて戻ってくる揺れを出さない）
        if ((target - cur > 0.0f) == (out > target))
        {
            out = target;
            vel = 0.0f;
        }
        return out;
    }

    // なめらかな疑似乱数（-1..1 程度）。周波数の違う sin を重ねて周期を見えにくくする
    float ShakeNoise(float t, float seed)
    {
        return 0.5f * std::sin(t * 1.00f + seed)
            + 0.3f * std::sin(t * 2.31f + seed * 1.7f)
            + 0.2f * std::sin(t * 4.13f + seed * 2.9f);
    }
}

void FollowCamera::AddTrauma(float amount)
{
    m_Trauma = std::clamp(m_Trauma + amount, 0.0f, 1.0f);
}

// ============================================================
// 遮蔽の距離：5 本の射線のうち一番近い当たり
// 1 本だけだと、柱の角をかすめる時にカメラの端が壁へめり込む
// ============================================================
float FollowCamera::ProbeDistance(const Vector3& from, const Vector3& dir, float maxDist,
    const Vector3& right, const Vector3& up) const
{
    const Vector3 offsets[5] = {
        Vector3::Zero, right * probeRadius, -right * probeRadius, up * probeRadius, -up * probeRadius
    };

    float best = maxDist;
    for (const auto& o : offsets)
    {
        float hit = 0.0f;
        if (m_Probe(from + o, dir, maxDist, hit) && hit < best)
            best = hit;
    }
    return best;
}

void FollowCamera::Update(float dt)
{
    // デバッグカメラ操作中は視点入力を受け付けない。
    // マウスはシーンが捕獲している間だけ使う（カーソルを出している時は UI・ImGui の操作用。
    // 捕獲中は ImGui にマウスが渡らないので、WantCaptureMouse は見なくてよい）
    bool acceptInput = !DebugManager::Get().IsUsingDebugCamera();

    if (acceptInput)
    {
        auto& input = InputManager::Get();

        float yawDelta = 0.0f;
        float pitchDelta = 0.0f;   // 正 = 見下ろす方向

        // --- 右スティック ---
        Vector2 rs = input.GetPadRightStick();
        yawDelta += rs.x * stickSensitivity * dt;
        pitchDelta -= rs.y * stickSensitivity * dt;   // 上に倒す = 見上げる

        // --- マウス（捕獲中はボタンを押さなくても常に回る。普通の TPS）---
        if (input.IsMouseCaptured())
        {
            auto md = input.GetMouseLookDelta();
            yawDelta += md.x * mouseSensitivity;
            pitchDelta += md.y * mouseSensitivity;    // 下に動かす = 見下ろす
        }

        if (invertY) pitchDelta = -pitchDelta;

        m_Yaw += yawDelta;
        m_Pitch += pitchDelta;
        m_Pitch = std::clamp(m_Pitch, pitchMin, pitchMax);

        // yaw を -180〜180 に丸める（数値の肥大を防ぐ）
        if (m_Yaw > 180.0f) m_Yaw -= 360.0f;
        if (m_Yaw < -180.0f) m_Yaw += 360.0f;
    }

    // --- yaw / pitch からカメラの前方向を作る ---
    float yawRad = DirectX::XMConvertToRadians(m_Yaw);
    float pitchRad = DirectX::XMConvertToRadians(m_Pitch);

    Vector3 forward;
    forward.x = -std::sin(yawRad) * std::cos(pitchRad);
    forward.y = -std::sin(pitchRad);              // pitch 正 = 下向き
    forward.z = std::cos(yawRad) * std::cos(pitchRad);
    forward.Normalize();

    // 画面の右・上（CameraBase::GetRight と同じ向きの取り方）
    const Vector3 worldUp(0.0f, 1.0f, 0.0f);
    Vector3 right = forward.Cross(worldUp);
    right.Normalize();
    Vector3 up = right.Cross(forward);
    up.Normalize();

    // ============================================================
    // 平滑追従：追従点をばねで追わせる
    // ============================================================
    const float gap = (m_FollowTarget - m_Pivot).Length();
    if (m_Snap || !smoothFollow || gap > snapDistance)
    {
        m_Pivot = m_FollowTarget;
        m_PivotVel = Vector3::Zero;
        if (m_Snap)
        {
            m_CurDistance = distance;
            m_DistVel = 0.0f;
            m_Snap = false;
        }
    }
    else
    {
        m_Pivot.x = SmoothDamp(m_Pivot.x, m_FollowTarget.x, m_PivotVel.x, followSmoothTime, dt);
        m_Pivot.z = SmoothDamp(m_Pivot.z, m_FollowTarget.z, m_PivotVel.z, followSmoothTime, dt);
        m_Pivot.y = SmoothDamp(m_Pivot.y, m_FollowTarget.y, m_PivotVel.y, verticalSmoothTime, dt);
    }

    // 体の芯（肩ずらし前の注視点）
    const Vector3 head = m_Pivot + Vector3(0.0f, height, 0.0f);

    // ============================================================
    // 肩ずらし：注視点ごと横へ。横に壁があればその手前まで縮める
    // （注視点が壁の中に入ると、後ろ向きの射線が壁を素通りする）
    // ============================================================
    float shoulder = shoulderOffset;
    const bool probe = avoidOcclusion && m_Probe;
    if (probe && std::abs(shoulder) > 1e-3f)
    {
        const float sign = (shoulder > 0.0f) ? 1.0f : -1.0f;
        float hit = 0.0f;
        if (m_Probe(head, right * sign, std::abs(shoulder) + probeRadius, hit))
            shoulder = sign * (std::max)(0.0f, hit - probeRadius);
    }
    const Vector3 lookAt = head + right * shoulder;

    // ============================================================
    // 遮蔽回避：注視点から後ろへ射線。壁・床に当たったらその手前まで寄る
    //   寄る時は即座（めり込みを 1 フレームも見せない）
    //   戻る時はばね（障害物の縁を行き来した時にぱたぱたさせない）
    // ============================================================
    float want = distance;
    m_Occluded = false;
    if (probe)
    {
        const float reach = distance + probeRadius;
        const float hit = ProbeDistance(lookAt, -forward, reach, right, up);
        if (hit < reach)
        {
            want = (std::max)(minDistance, hit - probeRadius);
            m_Occluded = true;
        }
    }

    if (want < m_CurDistance)
    {
        m_CurDistance = want;
        m_DistVel = 0.0f;
    }
    else
    {
        m_CurDistance = SmoothDamp(m_CurDistance, want, m_DistVel, returnSmoothTime, dt);
    }

    const Vector3 pos = lookAt - forward * m_CurDistance;
    LookAt(pos, lookAt, worldUp);

    // ============================================================
    // 画面の揺れ：trauma を減らしつつ、trauma² に比例した首振り
    // ============================================================
    m_Trauma = (std::max)(0.0f, m_Trauma - traumaDecay * dt);
    m_ShakeTime += dt;

    if (shakeEnabled && m_Trauma > 0.0f)
    {
        const float s = m_Trauma * m_Trauma;
        const float t = m_ShakeTime * shakeFrequency;
        SetViewShake(
            shakeMaxYaw * s * ShakeNoise(t, 1.3f),
            shakeMaxPitch * s * ShakeNoise(t, 7.1f),
            shakeMaxRoll * s * ShakeNoise(t, 13.7f));
    }
    else
    {
        SetViewShake(0.0f, 0.0f, 0.0f);
    }
}
