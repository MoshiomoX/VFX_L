// ============================================================
// FlowDebugView.cpp
// 流れの見える化の窓（風・雑魚の経路の流れ場・群れの実際の動き）。説明は FlowDebugView.h
// ============================================================
#include "Debug/FlowDebugView.h"
#include "Core/DevUI.h"
#include "Manager/InputManager.h"
#include "Swarm/SwarmSystem.h"
#include "World/GridWorld.h"
#include "World/LocalWind.h"
#include "World/WindState.h"

#include "imgui.h"
#include <Windows.h>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    // 窓の中の矢印：真ん中 c、画面での向き (sx, sy)（単位）、長さ len px。下地が色付きなので黒い縁を先に描く
    void ArrowPx(ImDrawList* dl, ImVec2 c, float sx, float sy, float len, ImU32 col, float thick)
    {
        const ImVec2 tail(c.x - sx * len * 0.5f, c.y - sy * len * 0.5f);
        const ImVec2 tip(c.x + sx * len * 0.5f, c.y + sy * len * 0.5f);
        const float h = (std::max)(len * 0.38f, 3.5f), w = h * 0.55f;
        const ImVec2 back(tip.x - sx * h, tip.y - sy * h);
        const ImVec2 l(back.x - sy * w, back.y + sx * w), r(back.x + sy * w, back.y - sx * w);
        const ImU32 edge = IM_COL32(0, 0, 0, 170);
        dl->AddLine(tail, back, edge, thick + 2.0f);
        dl->AddTriangle(tip, l, r, edge, 2.0f);
        dl->AddLine(tail, back, col, thick);
        dl->AddTriangleFilled(tip, l, r, col);
    }

    ImU32 Rgb(float r, float g, float b, float a = 1.0f)
    {
        auto c = [](float v) { return (int)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return IM_COL32(c(r), c(g), c(b), c(a));
    }

    // 青 → 白 → 赤（t = 0〜1）。dim = 明るさの倍率（矢印が見えるように下地は暗め）
    ImU32 Heat(float t, float dim)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const Vector3 cyan(0.05f, 0.75f, 1.0f), mid(0.9f, 0.9f, 0.9f), red(1.0f, 0.12f, 0.08f);
        const Vector3 c = (t < 0.5f) ? Vector3::Lerp(cyan, mid, t * 2.0f) : Vector3::Lerp(mid, red, t * 2.0f - 1.0f);
        return Rgb(c.x * dim, c.y * dim, c.z * dim);
    }
}

bool FlowDebugView::HandleKey()
{
    if (!DevUI::kEnabled) return false;
    if (!InputManager::Get().GetKeyTrigger(VK_F8)) return false;
    m_Mode = (Mode)(((int)m_Mode + 1) % (int)Mode::Count);
    m_CrowdTimer = 0.0f;
    std::cout << "[FlowDebug] " << ModeName() << std::endl;
    return true;
}

const char* FlowDebugView::ModeName() const
{
    switch (m_Mode)
    {
    case Mode::Wind:  return "風（世界の風 + 風源）";
    case Mode::Path:  return "経路の流れ場（雑魚が向かう向き）";
    case Mode::Crowd: return "群れの動き（今の速度の平均）";
    default:          return "切";
    }
}

// 雑魚を読み戻して、プレイヤーの周りの 2m のマス毎に速度を足す（crowdInterval 秒毎）
void FlowDebugView::ReadCrowd(const Inputs& in)
{
    int cx = 0, cz = 0;
    in.grid->WorldToCell(*in.player, cx, cz);
    const int n = (int)std::ceil(range / GridWorld::kCellSize) + 1;
    m_CrowdN = n * 2 + 1;
    m_CrowdX0 = cx - n;
    m_CrowdZ0 = cz - n;
    m_CrowdSum.assign((size_t)m_CrowdN * m_CrowdN, Vector2::Zero);
    m_CrowdCount.assign((size_t)m_CrowdN * m_CrowdN, 0);
    m_CrowdDots.clear();
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    if (!in.swarm || !in.swarm->DebugReadEnemies(en, st)) return;
    for (size_t i = 0; i < en.size() && i < st.size(); ++i)
    {
        if (st[i] == Swarm::kStateDead) continue;
        int gx = 0, gz = 0;
        in.grid->WorldToCell(en[i].position, gx, gz);
        const int lx = gx - m_CrowdX0, lz = gz - m_CrowdZ0;
        if (lx < 0 || lz < 0 || lx >= m_CrowdN || lz >= m_CrowdN) continue;
        const size_t k = (size_t)lz * m_CrowdN + lx;
        m_CrowdSum[k] += Vector2(en[i].velocity.x, en[i].velocity.z);
        ++m_CrowdCount[k];
        m_CrowdDots.emplace_back(en[i].position.x, en[i].position.z);
    }
}

void FlowDebugView::Draw(const Inputs& in)
{
    if (!DevUI::Active() || m_Mode == Mode::Off || !in.player || !in.grid) return;

    if (m_Mode == Mode::Crowd)
    {
        m_CrowdTimer -= in.dt;
        if (m_CrowdTimer <= 0.0f) { m_CrowdTimer = crowdInterval; ReadCrowd(in); }
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float size = panelSize * (vp->Size.y / 900.0f);
    const ImVec2 o(vp->Pos.x + 16.0f, vp->Pos.y + (vp->Size.y - size) * 0.5f);
    const ImVec2 e(o.x + size, o.y + size);
    const float px = in.player->x, pz = in.player->z;
    const float pxPerM = size / (range * 2.0f);
    // 世界 → 窓（右上のミニマップと同じ：+z が上、+x が左）。向きのベクトル (dx, dz) は画面で (-dx, -dz)
    auto toPanel = [&](float wx, float wz) { return ImVec2(o.x + size * 0.5f - (wx - px) * pxPerM, o.y + size * 0.5f - (wz - pz) * pxPerM); };

    dl->AddRectFilled(ImVec2(o.x - 4.0f, o.y - 26.0f), ImVec2(e.x + 4.0f, e.y + 4.0f), IM_COL32(8, 10, 14, 215), 6.0f);
    char title[128];
    snprintf(title, sizeof(title), "F8  %s", ModeName());
    dl->AddText(ImVec2(o.x + 2.0f, o.y - 22.0f), IM_COL32(255, 225, 120, 255), title);
    dl->PushClipRect(o, e, true);

    const float cs = GridWorld::kCellSize;
    const float cellPx = cs * pxPerM;
    int cx = 0, cz = 0;
    in.grid->WorldToCell(*in.player, cx, cz);
    const int n = (int)std::ceil(range / cs) + 1;
    const int side = n * 2 + 1;
    auto inGrid = [&](int gx, int gz) { return gx >= 0 && gz >= 0 && gx < in.grid->Width() && gz < in.grid->Depth(); };

    // 下地の青 → 赤（2026-10-28 ユーザー：「風の強さ・場の高低で青赤の差を」）：
    //   風の時 = 風速 0 → windHeatMax m/s、それ以外 = 窓の中の歩ける所の高さの低 → 高
    std::vector<Vector3> wind;   // 風の時だけ：マス毎の風（xz）
    std::vector<float> value((size_t)side * side, 0.0f);
    float lo = 1e9f, hi = -1e9f;
    for (int iz = 0; iz < side; ++iz)
        for (int ix = 0; ix < side; ++ix)
        {
            const int gx = cx - n + ix, gz = cz - n + iz;
            const Vector3 c = in.grid->CellToWorld(gx, gz);
            float& v = value[(size_t)iz * side + ix];
            if (m_Mode == Mode::Wind && in.wind)
            {
                Vector3 w = in.wind->Velocity(in.windTime, c.x, c.z);
                if (in.local) { const Vector3 l = in.local->Air(c.x, c.z); w.x += l.x; w.z += l.z; }
                w.y = 0.0f;
                wind.push_back(w);
                v = std::sqrt(w.x * w.x + w.z * w.z);
            }
            else if (inGrid(gx, gz) && in.grid->IsWalkable(gx, gz))
            {
                v = in.grid->SampleHeight(c.x, c.z);
                lo = (std::min)(lo, v);
                hi = (std::max)(hi, v);
            }
        }
    if (m_Mode == Mode::Wind) { lo = 0.0f; hi = windHeatMax; }
    else if (hi < lo) { lo = 0.0f; hi = 1.0f; }
    else if (hi - lo < 1.0f) { const float mid = (lo + hi) * 0.5f; lo = mid - 0.5f; hi = mid + 0.5f; }

    for (int iz = 0; iz < side; ++iz)
        for (int ix = 0; ix < side; ++ix)
        {
            const int gx = cx - n + ix, gz = cz - n + iz;
            if (!inGrid(gx, gz)) continue;
            const Vector3 c = in.grid->CellToWorld(gx, gz);
            const ImVec2 p = toPanel(c.x, c.z);
            ImU32 col;
            if (!in.grid->IsWalkable(gx, gz))
                col = IM_COL32(40, 40, 44, 255);                     // 塞がっている（木・岩・崖の外）
            else if (in.grid->IsWater(gx, gz) && m_Mode != Mode::Wind)
                col = IM_COL32(20, 40, 120, 255);                    // 水（高さの青と分けて濃い紺）
            else
                col = Heat((value[(size_t)iz * side + ix] - lo) / (hi - lo), heatDim);
            dl->AddRectFilled(ImVec2(p.x - cellPx * 0.5f + 0.5f, p.y - cellPx * 0.5f + 0.5f), ImVec2(p.x + cellPx * 0.5f - 0.5f, p.y + cellPx * 0.5f - 0.5f), col);
        }

    const float thick = (std::max)(1.5f, size / 220.0f);
    if (m_Mode == Mode::Wind && in.wind)
    {
        // マスの真ん中毎の白い矢印（強さは下地の色）。長さ = 0.12 マス / (m/s)（最低 0.3 マス、最大 1 マス）
        for (int iz = 0; iz < side; ++iz)
            for (int ix = 0; ix < side; ++ix)
            {
                const Vector3& w = wind[(size_t)iz * side + ix];
                const float speed = value[(size_t)iz * side + ix];
                if (speed < 0.05f) continue;
                const Vector3 c = in.grid->CellToWorld(cx - n + ix, cz - n + iz);
                ArrowPx(dl, toPanel(c.x, c.z), -w.x / speed, -w.z / speed, cellPx * std::clamp(0.3f + speed * 0.12f, 0.3f, 1.0f),
                    IM_COL32(255, 255, 255, 255), thick);
            }
    }
    else if (m_Mode == Mode::Path && in.swarm)
    {
        const FlowField& flow = in.swarm->GetFlowField();
        if (flow.IsBuilt())
        {
            const auto& dirs = flow.Directions();
            const int W = flow.Width(), D = flow.Depth();
            for (int gz = (std::max)(0, cz - n); gz <= (std::min)(D - 1, cz + n); ++gz)
                for (int gx = (std::max)(0, cx - n); gx <= (std::min)(W - 1, cx + n); ++gx)
                {
                    if (!in.grid->IsWalkable(gx, gz)) continue;
                    const Vector2 d = dirs[(size_t)gz * W + gx];
                    const float len = d.Length();
                    if (len < 1e-3f) continue;
                    const Vector3 c = in.grid->CellToWorld(gx, gz);
                    ArrowPx(dl, toPanel(c.x, c.z), -d.x / len, -d.y / len, cellPx * 0.75f, IM_COL32(255, 255, 255, 255), thick);
                }
            const Vector3 t = in.grid->CellToWorld(flow.TargetX(), flow.TargetZ());
            dl->AddCircle(toPanel(t.x, t.z), cellPx * 0.45f, IM_COL32(255, 255, 255, 255), 16, thick);
        }
    }
    else if (m_Mode == Mode::Crowd)
    {
        for (const Vector2& d : m_CrowdDots)
        {
            dl->AddCircleFilled(toPanel(d.x, d.y), (std::max)(1.5f, thick) + 1.0f, IM_COL32(0, 0, 0, 200), 8);
            dl->AddCircleFilled(toPanel(d.x, d.y), (std::max)(1.5f, thick), IM_COL32(255, 230, 60, 230), 8);
        }
        for (int lz = 0; lz < m_CrowdN; ++lz)
            for (int lx = 0; lx < m_CrowdN; ++lx)
            {
                const size_t k = (size_t)lz * m_CrowdN + lx;
                const int cnt = m_CrowdCount[k];
                if (cnt <= 0) continue;
                const Vector2 v = m_CrowdSum[k] / (float)cnt;
                const float speed = v.Length();
                if (speed < 0.1f) continue;   // 止まっている（点だけ）
                const Vector3 c = in.grid->CellToWorld(m_CrowdX0 + lx, m_CrowdZ0 + lz);
                const float t = std::clamp((cnt - 1) / 5.0f, 0.0f, 1.0f);   // 白（1 体）→ 黄（6 体以上）。下地の青赤と分ける
                ArrowPx(dl, toPanel(c.x, c.z), -v.x / speed, -v.y / speed, cellPx * std::clamp(0.3f + speed * 0.15f, 0.3f, 1.0f),
                    Rgb(1.0f, 1.0f - 0.1f * t, 1.0f - 0.75f * t), thick);
            }
    }

    // プレイヤー：白い三角（向き = 0 度で上）
    {
        const float a = DirectX::XMConvertToRadians(in.playerYawDeg);
        const float fx = -std::sin(a), fy = -std::cos(a);   // 前 (sin, cos) を窓へ
        const ImVec2 c(o.x + size * 0.5f, o.y + size * 0.5f);
        const float r = (std::max)(6.0f, size / 45.0f);
        dl->AddTriangleFilled(ImVec2(c.x + fx * r, c.y + fy * r),
            ImVec2(c.x - fx * r * 0.6f - fy * r * 0.6f, c.y - fy * r * 0.6f + fx * r * 0.6f),
            ImVec2(c.x - fx * r * 0.6f + fy * r * 0.6f, c.y - fy * r * 0.6f - fx * r * 0.6f), IM_COL32(255, 255, 255, 255));
    }
    dl->PopClipRect();
    dl->AddText(ImVec2(o.x + size * 0.5f - 4.0f, o.y + 2.0f), IM_COL32(220, 220, 220, 200), "N");
    dl->AddRect(o, e, IM_COL32(200, 170, 90, 200), 0.0f, 0, 1.5f);

    // 色の目盛り（窓の下）：青 → 白 → 赤 と、その両端の値
    {
        const float barY = e.y + 8.0f, barH = 10.0f;
        const float barX0 = o.x + 70.0f, barX1 = e.x - 70.0f;
        dl->AddRectFilled(ImVec2(o.x - 4.0f, e.y + 4.0f), ImVec2(e.x + 4.0f, barY + barH + 8.0f), IM_COL32(8, 10, 14, 215), 6.0f);
        const int steps = 24;
        for (int i = 0; i < steps; ++i)
        {
            const float a = barX0 + (barX1 - barX0) * i / steps, b = barX0 + (barX1 - barX0) * (i + 1) / steps;
            dl->AddRectFilled(ImVec2(a, barY), ImVec2(b + 0.5f, barY + barH), Heat((i + 0.5f) / steps, heatDim));
        }
        char l[48], r[48];
        if (m_Mode == Mode::Wind) { snprintf(l, sizeof(l), "風 0 m/s"); snprintf(r, sizeof(r), "%.0f m/s", hi); }
        else { snprintf(l, sizeof(l), "低 %.1fm", lo); snprintf(r, sizeof(r), "高 %.1fm", hi); }
        const float th = ImGui::GetTextLineHeight();
        dl->AddText(ImVec2(o.x + 2.0f, barY + barH * 0.5f - th * 0.5f), IM_COL32(220, 220, 220, 255), l);
        dl->AddText(ImVec2(barX1 + 6.0f, barY + barH * 0.5f - th * 0.5f), IM_COL32(220, 220, 220, 255), r);
    }
}
