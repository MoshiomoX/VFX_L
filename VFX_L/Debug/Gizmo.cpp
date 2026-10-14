// ============================================================
// Gizmo.cpp
// ============================================================
#include "Debug/Gizmo.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    enum Handle : int
    {
        kNone = -1,
        kAxisX = 0, kAxisY = 1, kAxisZ = 2,
        kPlaneYZ = 3, kPlaneXZ = 4, kPlaneXY = 5,   // 法線が X / Y / Z
        kCenter = 6,
    };

    // ---- フレームごとのカメラ ----
    Matrix  s_ViewProj;
    Matrix  s_InvViewProj;
    Vector3 s_CamPos;
    Vector3 s_CamRight;
    Vector3 s_CamForward;
    float   s_W = 1.0f, s_H = 1.0f;
    // 3D を描いている領域の左上（ImGui の座標系で）。
    // ImGui の multi-viewport が有効だと ImGui の座標はデスクトップの絶対座標になり、
    // マウス位置も draw list もそれで来る。無効なら (0, 0) なので、常に足し引きしておけばよい
    ImVec2  s_Origin = ImVec2(0.0f, 0.0f);

    // ---- 掴んでいる物 ----
    size_t  s_ActiveId = 0;          // 0 = 何も掴んでいない
    int     s_ActiveHandle = kNone;
    bool    s_ActiveTouched = false; // 今フレーム、持ち主が Translate を呼んだか
    Vector3 s_StartPos;              // 掴んだ時の点の位置
    float   s_StartT = 0.0f;         // 軸：掴んだ時の軸上の位置
    Vector3 s_StartHit;              // 面：掴んだ時の面上の位置
    Vector3 s_PlaneNormal;

    bool    s_HoverAny = false;

    const Vector3 kAxes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
    const ImU32   kAxisCol[3] = { IM_COL32(235, 70, 70, 255), IM_COL32(90, 220, 90, 255), IM_COL32(80, 130, 255, 255) };
    const ImU32   kHotCol = IM_COL32(255, 230, 60, 255);

    bool Project(const Vector3& p, ImVec2& out)
    {
        const Vector4 c = Vector4::Transform(Vector4(p.x, p.y, p.z, 1.0f), s_ViewProj);
        if (c.w <= 1e-4f) return false;   // カメラの後ろ
        out.x = s_Origin.x + (c.x / c.w * 0.5f + 0.5f) * s_W;
        out.y = s_Origin.y + (1.0f - (c.y / c.w * 0.5f + 0.5f)) * s_H;
        return true;
    }

    void MouseRay(const ImVec2& m, Vector3& origin, Vector3& dir)
    {
        const float nx = (m.x - s_Origin.x) / s_W * 2.0f - 1.0f;
        const float ny = 1.0f - (m.y - s_Origin.y) / s_H * 2.0f;
        const Vector3 n = Vector3::Transform(Vector3(nx, ny, 0.0f), s_InvViewProj);   // w 除算込み
        const Vector3 f = Vector3::Transform(Vector3(nx, ny, 1.0f), s_InvViewProj);
        origin = n;
        dir = f - n;
        dir.Normalize();
    }

    // 軸（p0 + t * a）の上で、マウスの光線に一番近い t
    bool ClosestOnAxis(const Vector3& p0, const Vector3& a, const Vector3& ro, const Vector3& rd, float& t)
    {
        const Vector3 w = p0 - ro;
        const float b = a.Dot(rd);
        const float denom = 1.0f - b * b;
        if (std::abs(denom) < 1e-5f) return false;   // 軸が視線とほぼ平行
        t = (b * rd.Dot(w) - a.Dot(w)) / denom;
        return true;
    }

    bool HitPlane(const Vector3& p0, const Vector3& n, const Vector3& ro, const Vector3& rd, Vector3& hit)
    {
        const float denom = rd.Dot(n);
        if (std::abs(denom) < 1e-5f) return false;   // 面を真横から見ている
        const float s = (p0 - ro).Dot(n) / denom;
        if (s < 0.0f) return false;
        hit = ro + rd * s;
        return true;
    }

    float DistToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
    {
        const float vx = b.x - a.x, vy = b.y - a.y;
        const float wx = p.x - a.x, wy = p.y - a.y;
        const float len2 = vx * vx + vy * vy;
        float t = (len2 > 1e-6f) ? (wx * vx + wy * vy) / len2 : 0.0f;
        t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
        const float dx = wx - vx * t, dy = wy - vy * t;
        return std::sqrt(dx * dx + dy * dy);
    }

    // 凸四角形の内外（頂点の回り方はどちらでもよい）
    bool InQuad(const ImVec2& p, const ImVec2 q[4])
    {
        int sign = 0;
        for (int i = 0; i < 4; ++i)
        {
            const ImVec2& a = q[i];
            const ImVec2& b = q[(i + 1) % 4];
            const float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            const int s = (c > 0.0f) ? 1 : (c < 0.0f) ? -1 : 0;
            if (s == 0) continue;
            if (sign == 0) sign = s;
            else if (s != sign) return false;
        }
        return true;
    }

    float Snap(float v, float step) { return std::round(v / step) * step; }
}

// ============================================================
// フレームの頭
// ============================================================
void Gizmo::BeginFrame(const Matrix& view, const Matrix& proj)
{
    s_ViewProj = view * proj;
    s_InvViewProj = s_ViewProj.Invert();

    const Matrix camWorld = view.Invert();
    s_CamPos = camWorld.Translation();
    s_CamRight = Vector3(camWorld._11, camWorld._12, camWorld._13);
    s_CamForward = Vector3(camWorld._31, camWorld._32, camWorld._33);
    s_CamRight.Normalize();
    s_CamForward.Normalize();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    s_Origin = vp->Pos;
    s_W = (vp->Size.x > 1.0f) ? vp->Size.x : 1.0f;
    s_H = (vp->Size.y > 1.0f) ? vp->Size.y : 1.0f;

    // 掴んだまま持ち主が居なくなった（シーン切替など）→ 手を離す
    if (s_ActiveId != 0 && !s_ActiveTouched)
    {
        s_ActiveId = 0;
        s_ActiveHandle = kNone;
    }
    s_ActiveTouched = false;
    s_HoverAny = false;
}

bool Gizmo::IsUsing() { return s_ActiveId != 0; }
bool Gizmo::IsHovering() { return s_HoverAny; }

void Gizmo::GetMouseRay(Vector3& origin, Vector3& dir)
{
    MouseRay(ImGui::GetIO().MousePos, origin, dir);
}

// ============================================================
// 移動ギズモ 1 個
// ============================================================
bool Gizmo::Translate(const char* id, Vector3& pos, const Options& opt)
{
    const ImGuiIO& io = ImGui::GetIO();
    const size_t hash = std::hash<std::string>{}(id ? id : "") | 1u;   // 0 は「無し」に使う
    const bool active = (s_ActiveId == hash);
    if (active) s_ActiveTouched = true;

    ImVec2 centre;
    if (!Project(pos, centre))
        return false;   // カメラの後ろ。掴んでいる最中でも動かさない

    // ---- 画面上で一定の大きさにする：世界 1 単位が何 px かを測る ----
    ImVec2 unitPx;
    float pxPerUnit = 0.0f;
    if (Project(pos + s_CamRight, unitPx))
        pxPerUnit = std::sqrt((unitPx.x - centre.x) * (unitPx.x - centre.x)
            + (unitPx.y - centre.y) * (unitPx.y - centre.y));
    if (pxPerUnit < 1e-3f) return false;
    const float L = opt.sizePixels / pxPerUnit;   // 軸の長さ（世界単位）

    // ---- 取っ手の画面座標 ----
    ImVec2 tip[3];
    bool   tipOk[3];
    for (int a = 0; a < 3; ++a)
        tipOk[a] = Project(pos + kAxes[a] * L, tip[a]);

    // 面の四角。法線 n の面は残り 2 軸 (u, v) で張る
    ImVec2 quad[3][4];
    bool   quadOk[3] = { false, false, false };
    if (opt.showPlanes)
    {
        const float q0 = 0.28f * L, q1 = 0.52f * L;
        for (int n = 0; n < 3; ++n)
        {
            const Vector3& u = kAxes[(n + 1) % 3];
            const Vector3& v = kAxes[(n + 2) % 3];
            const Vector3 c[4] = {
                pos + u * q0 + v * q0, pos + u * q1 + v * q0,
                pos + u * q1 + v * q1, pos + u * q0 + v * q1 };
            quadOk[n] = true;
            for (int k = 0; k < 4; ++k)
                quadOk[n] = Project(c[k], quad[n][k]) && quadOk[n];
        }
    }

    // ---- どの取っ手の上か ----
    int hover = kNone;
    const bool canGrab = (s_ActiveId == 0) && !io.WantCaptureMouse && !io.KeyAlt;
    if (canGrab)
    {
        const ImVec2 m = io.MousePos;
        const float dx = m.x - centre.x, dy = m.y - centre.y;

        if (opt.showCenter && dx * dx + dy * dy <= 10.0f * 10.0f)
            hover = kCenter;

        if (hover == kNone && opt.showPlanes)
            for (int n = 0; n < 3; ++n)
                if (quadOk[n] && InQuad(m, quad[n])) { hover = kPlaneYZ + n; break; }

        if (hover == kNone)
        {
            float best = 8.0f;   // px
            for (int a = 0; a < 3; ++a)
            {
                if (!tipOk[a]) continue;
                const float d = DistToSegment(m, centre, tip[a]);
                if (d < best) { best = d; hover = kAxisX + a; }
            }
        }

        if (hover != kNone) s_HoverAny = true;
    }

    // ---- 掴む ----
    Vector3 ro, rd;
    MouseRay(io.MousePos, ro, rd);

    if (hover != kNone && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        bool ok = false;
        if (hover <= kAxisZ)
        {
            ok = ClosestOnAxis(pos, kAxes[hover], ro, rd, s_StartT);
        }
        else
        {
            s_PlaneNormal = (hover == kCenter) ? -s_CamForward : kAxes[hover - kPlaneYZ];
            ok = HitPlane(pos, s_PlaneNormal, ro, rd, s_StartHit);
        }
        if (ok)
        {
            s_ActiveId = hash;
            s_ActiveHandle = hover;
            s_ActiveTouched = true;
            s_StartPos = pos;
        }
    }

    // ---- 動かす / 離す ----
    bool changed = false;
    if (s_ActiveId == hash)
    {
        if (!io.MouseDown[0])
        {
            s_ActiveId = 0;
            s_ActiveHandle = kNone;
        }
        else
        {
            const bool snap = (opt.snap > 0.0f) && !io.KeyCtrl;
            Vector3 np = pos;

            if (s_ActiveHandle <= kAxisZ)
            {
                // 掴んだ時の軸（s_StartPos を通る）の上で測る。動いた分だけ足す
                const Vector3& a = kAxes[s_ActiveHandle];
                float t;
                if (ClosestOnAxis(s_StartPos, a, ro, rd, t))
                {
                    float delta = t - s_StartT;
                    if (snap) delta = Snap(delta, opt.snap);
                    np = s_StartPos + a * delta;
                }
            }
            else
            {
                Vector3 hit;
                if (HitPlane(s_StartPos, s_PlaneNormal, ro, rd, hit))
                {
                    np = s_StartPos + (hit - s_StartHit);
                    if (snap)
                    {
                        // 面の法線方向の成分は動いていないので丸めない
                        const int n = (s_ActiveHandle == kCenter) ? -1 : s_ActiveHandle - kPlaneYZ;
                        if (n != 0) np.x = Snap(np.x, opt.snap);
                        if (n != 1) np.y = Snap(np.y, opt.snap);
                        if (n != 2) np.z = Snap(np.z, opt.snap);
                    }
                }
            }

            if (np != pos) { pos = np; changed = true; }
        }
    }

    // ---- 描く（3D の上、ImGui の窓の下）----
    ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    const int hot = (s_ActiveId == hash) ? s_ActiveHandle : hover;

    if (opt.showPlanes)
        for (int n = 0; n < 3; ++n)
        {
            if (!quadOk[n]) continue;
            const bool isHot = (hot == kPlaneYZ + n);
            const ImU32 base = kAxisCol[n];
            const ImU32 fill = isHot ? IM_COL32(255, 230, 60, 150) : ((base & 0x00FFFFFF) | (70u << 24));
            dl->AddQuadFilled(quad[n][0], quad[n][1], quad[n][2], quad[n][3], fill);
            dl->AddQuad(quad[n][0], quad[n][1], quad[n][2], quad[n][3], isHot ? kHotCol : base, 1.5f);
        }

    for (int a = 0; a < 3; ++a)
    {
        if (!tipOk[a]) continue;
        const bool isHot = (hot == kAxisX + a);
        const ImU32 col = isHot ? kHotCol : kAxisCol[a];
        dl->AddLine(centre, tip[a], col, isHot ? 4.5f : 3.0f);
        dl->AddCircleFilled(tip[a], isHot ? 7.0f : 5.5f, col);
    }

    if (opt.showCenter)
    {
        const bool isHot = (hot == kCenter);
        dl->AddCircleFilled(centre, isHot ? 8.0f : 6.0f, isHot ? kHotCol : IM_COL32(235, 235, 235, 255));
        dl->AddCircle(centre, isHot ? 8.0f : 6.0f, IM_COL32(20, 20, 20, 255), 0, 1.5f);
    }

    if (opt.label)
        dl->AddText(ImVec2(centre.x + 12.0f, centre.y + 10.0f), IM_COL32(255, 255, 255, 230), opt.label);

    return changed;
}
