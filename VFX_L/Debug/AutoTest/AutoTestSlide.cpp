// ============================================================
// AutoTestSlide.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=slide
// VFXL_BATTLE_AUTOTEST=slide：一番長い下り坂の上にプレイヤーを置き、走る → 滑る → 跳ぶ を入力の代わりに流して
// 0.1 秒毎の速さ・足元の傾き・状態を記録する。続けて平地でも滑る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSlide final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 滑りの自動テスト（VFXL_BATTLE_AUTOTEST=slide）
// 2 秒: 一番長い下り坂の上へ（高さ図を 1m 刻みで見て、連続して下る距離が一番長い所）。
//       0.4 秒走ってから滑る（坂で速くなるはず）→ 2.4 秒で跳ぶ（水平の速さが残るはず）
// 次: 平地（進む先 12m が同じ高さ）へ。0.4 秒走ってから滑る（押し出し → 摩擦で減って立つ）
// 0.1 秒毎に 水平の速さ・足元の傾き・接地・滑り中か・高さ を autotest.log へ
// ============================================================
void AutoTestSlide::Run(float dt)
{
    static Vector3 s_Dir(0.0f, 0.0f, 1.0f);
    static Vector3 s_SlopeStart, s_SlopeDir(0.0f, 0.0f, 1.0f);   // 1) で見つけた坂（最後の全景用）
    static float s_Phase = 0.0f;
    static float s_Log = 0.0f;
    auto& pcs = m_PlayerControlSystem;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;   // 三択で止めない

    const int W = m_Grid.Width(), D = m_Grid.Depth();
    auto walkableAt = [&](const Vector3& p) { int gx, gz; m_Grid.WorldToCell(p, gx, gz); return m_Grid.IsWalkable(gx, gz); };
    auto place = [&](const Vector3& p, const Vector3& dir)
        {
            tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.2f, p.z);
            rb.velocity = Vector3::Zero;
            st.slideActive = false;
            s_Dir = dir;
            m_Camera.Camera().SetYaw(DirectX::XMConvertToDegrees(std::atan2(-dir.x, dir.z)));   // 進行方向を映す
            m_Camera.Camera().SnapToTarget();
            s_Phase = 0.0f;
            s_Log = 0.0f;
        };
    char line[200];

    // ---- 1) 坂を探して置く ----
    if (m_AutoStep == 0 && m_AutoTime >= 2.0f)
    {
        float best = 0.0f;
        int bestSteps = 0;
        Vector3 start, dir(0, 0, 1);
        for (int gz = 2; gz < D - 2; ++gz)
            for (int gx = 2; gx < W - 2; ++gx)
            {
                if (!m_Grid.IsWalkable(gx, gz)) continue;
                const Vector3 c = m_Grid.CellToWorld(gx, gz);
                for (int k = 0; k < 8; ++k)
                {
                    const float a = k * DirectX::XM_PIDIV4;
                    const Vector3 d(std::sin(a), 0.0f, std::cos(a));
                    float prev = m_Grid.SampleHeight(c.x, c.z), drop = 0.0f;
                    int steps = 0;
                    for (int s = 1; s <= 40; ++s)   // 大きい丘の坂は 20m 前後
                    {
                        const Vector3 p = c + d * (float)s;
                        if (!walkableAt(p)) break;
                        const float h = m_Grid.SampleHeight(p.x, p.z);
                        const float dh = prev - h;
                        if (dh < 0.15f || dh > 0.9f) break;   // 1m で約 9〜42 度の下り
                        drop += dh; prev = h; ++steps;
                    }
                    if (drop > best) { best = drop; bestSteps = steps; start = c; dir = d; }
                }
            }
        place(start, dir);
        s_SlopeStart = start;
        s_SlopeDir = dir;
        snprintf(line, sizeof(line), "slide A: slope start %.1f,%.1f dir %.2f,%.2f drops %.2f m over %d m",
            start.x, start.z, dir.x, dir.z, best, bestSteps);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    // ---- 3) 平地を探して置く ----
    else if (m_AutoStep == 2)
    {
        Vector3 start, dir(0, 0, 1);
        bool found = false;
        for (int r = 3; r < W / 2 && !found; ++r)   // 中央から外へ
            for (int gz = D / 2 - r; gz <= D / 2 + r && !found; ++gz)
                for (int gx = W / 2 - r; gx <= W / 2 + r && !found; ++gx)
                {
                    if (gx < 2 || gz < 2 || gx >= W - 2 || gz >= D - 2 || !m_Grid.IsWalkable(gx, gz)) continue;
                    const Vector3 c = m_Grid.CellToWorld(gx, gz);
                    const float h0 = m_Grid.SampleHeight(c.x, c.z);
                    for (int k = 0; k < 8 && !found; k += 2)
                    {
                        const float a = k * DirectX::XM_PIDIV4;
                        const Vector3 d(std::sin(a), 0.0f, std::cos(a));
                        bool ok = true;
                        for (int s = 1; s <= 12 && ok; ++s)
                        {
                            const Vector3 p = c + d * (float)s;
                            ok = walkableAt(p) && std::fabs(m_Grid.SampleHeight(p.x, p.z) - h0) < 0.02f;
                        }
                        if (ok) { found = true; start = c; dir = d; }
                    }
                }
        place(start, dir);
        snprintf(line, sizeof(line), "slide B: flat start %.1f,%.1f dir %.2f,%.2f found %d", start.x, start.z, dir.x, dir.z, found ? 1 : 0);
        AutoTestLog(line);
        m_AutoStep = 3;
    }
    // ---- 5) 一番高いマス（高台の 2 段目の上など）に立たせ、35m 引いて 3 方向から全景を撮らせる ----
    else if (m_AutoStep == 4)
    {
        float best = -1.0f;
        Vector3 top;
        for (int gz = 2; gz < D - 2; ++gz)
            for (int gx = 2; gx < W - 2; ++gx)
            {
                if (!m_Grid.IsWalkable(gx, gz)) continue;
                const Vector3 c = m_Grid.CellToWorld(gx, gz);
                const float h = m_Grid.SampleHeight(c.x, c.z);
                if (h > best) { best = h; top = c; }
            }
        // 下りの向き：4 方向のうち、歩けて高さが上がらない（1m で 0.9m 以下の下り）まま一番遠くまで行ける向き
        Vector3 down(1.0f, 0.0f, 0.0f);
        int reach = 0;
        for (int k = 0; k < 4; ++k)
        {
            const Vector3 d = (k == 0) ? Vector3(1, 0, 0) : (k == 1) ? Vector3(-1, 0, 0) : (k == 2) ? Vector3(0, 0, 1) : Vector3(0, 0, -1);
            float prev = best;
            int n = 0;
            for (int s = 1; s <= 80; ++s)
            {
                const Vector3 p = top + d * (float)s;
                if (!walkableAt(p)) break;
                const float h = m_Grid.SampleHeight(p.x, p.z);
                if (h > prev + 0.05f || prev - h > 0.9f) break;
                prev = h;
                n = s;
            }
            if (n > reach) { reach = n; down = d; }
        }
        s_SlopeDir = down;   // 以降は全景用に使う
        place(top, down);
        auto& cam = m_Camera.Camera();
        cam.SetPitch(15.0f);
        cam.distance = 45.0f;
        cam.avoidOcclusion = false;
        snprintf(line, sizeof(line), "slide view: highest %.1f m at %.1f,%.1f, walks down %.0f,%.0f for %d m",
            best, top.x, top.z, down.x, down.z, reach);
        AutoTestLog(line);
        m_AutoStep = 5;
    }
    else if (m_AutoStep == 5)
    {
        // 下りの向きと直角に、左から横顔（1.5 秒）→ 右から見下ろし（1.5 秒。崖の影が地面に落ちるのを見る）
        s_Phase += dt;
        const float side = (s_Phase < 1.5f) ? 1.0f : -1.0f;
        m_Camera.Camera().SetYaw(DirectX::XMConvertToDegrees(std::atan2(-s_SlopeDir.z * side, -s_SlopeDir.x * side)));
        m_Camera.Camera().SetPitch(side > 0.0f ? 15.0f : 50.0f);
        if (s_Phase >= 3.0f) { AutoTestLog("slide done"); m_AutoStep = 6; }
    }

    if (m_AutoStep != 1 && m_AutoStep != 3)
    {
        pcs.testInput = false;
        return;
    }

    // ---- 入力の代わり: 決めた向きへ走る / 滑る / 跳ぶ ----
    s_Phase += dt;
    const auto& cam = m_Camera.Camera();
    Vector3 camF = cam.GetForward(); camF.y = 0; camF.Normalize();
    Vector3 camR = cam.GetRight();   camR.y = 0; camR.Normalize();
    pcs.testInput = true;
    pcs.testMove = Vector2(s_Dir.Dot(camR), s_Dir.Dot(camF));
    pcs.testJump = false;
    if (m_AutoStep == 1)
    {
        pcs.testSlide = (s_Phase >= 0.4f && s_Phase < 3.4f);
        if (s_Phase >= 3.4f && s_Phase - dt < 3.4f) pcs.testJump = true;
    }
    else
        pcs.testSlide = (s_Phase >= 0.4f && s_Phase < 3.0f);

    s_Log += dt;
    if (s_Log >= 0.1f)
    {
        s_Log = 0.0f;
        const float hs = std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z);
        const float slope = DirectX::XMConvertToDegrees(std::acos((std::min)(1.0f, rb.groundNormal.y)));
        snprintf(line, sizeof(line), "slide t %.1f speed %.2f vy %.2f slope %.0f grounded %d sliding %d input %d y %.2f | cam speed %.1f fov %.1f +dist %.2f",
            s_Phase, hs, rb.velocity.y, slope, rb.isGrounded ? 1 : 0, st.slideActive ? 1 : 0, pcs.testSlide ? 1 : 0, tf.position.y,
            m_Camera.Camera().GetSpeed(), m_Camera.Camera().GetEffectiveFov(), m_Camera.Camera().GetSpeedExtraDistance());
        AutoTestLog(line);
    }

    const float end = (m_AutoStep == 1) ? 4.6f : 3.8f;
    if (s_Phase >= end)
    {
        if (m_AutoStep == 1) m_AutoStep = 2;
        else m_AutoStep = 4;
    }
}

REGISTER_BATTLE_AUTOTEST("slide", AutoTestSlide)
