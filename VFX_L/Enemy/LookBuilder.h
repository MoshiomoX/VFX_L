// ============================================================
// LookBuilder.h
// 敵の飾りを積む道具（2026-10-25、Enemy/BossLooks.cpp から分けた。Enemy/EnemyLooks.cpp と共用）。
// 飾りは焼いた雑魚の網と同じ空間（足元 = カプセルの下 y ≒ -0.9、高さ 1.6、+Z が正面）で HexahedronBatch に箱を積み、
// 頂点色で塗る（HDR の色は bloom で光る）。寸法は部品の包囲ボックス（Model::SubMesh の boundsMin / Max）に対する割合で決める
// ============================================================
#pragma once
#include "Graphics/Model/Model.h"
#include "Graphics/PrimitiveBuilder.h"
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <utility>

struct ID3D11Device;

namespace LookBuilder
{
    using Vec3 = DirectX::SimpleMath::Vector3;
    using Vec4 = DirectX::SimpleMath::Vector4;

    struct Part
    {
        int index = -1;
        Vec3 lo, hi;
        Vec3 Size() const { return hi - lo; }
        Vec3 Center() const { return (lo + hi) * 0.5f; }
        bool Valid() const { return index >= 0; }
    };

    struct Builder
    {
        PrimitiveBuilder::HexahedronBatch batch;
        int boxes = 0;
        // 全体の拡大（2026-10-26、雄鹿「体をもう少し大きく」）：積む前に pivot の周りに scale 倍する。1 = そのまま
        float scale = 1.0f;
        Vec3 pivot = { 0.0f, 0.0f, 0.0f };
        // 全体の仰角（2026-10-27、狼人の頭）：pivot を通る横軸の周りに pitch rad 回す（正 = 前が上がる）。0 = そのまま
        float pitch = 0.0f;
        // 最後の平行移動（2026-10-27、狼人の足を地面 y -0.9 へ下ろす）
        Vec3 move = { 0.0f, 0.0f, 0.0f };
        Vec3 Xf(const Vec3& p) const
        {
            Vec3 q = pivot + (p - pivot) * scale;
            if (pitch != 0.0f)
            {
                const float s = std::sin(pitch), c = std::cos(pitch);
                const float y = q.y - pivot.y, z = q.z - pivot.z;
                q.y = pivot.y + y * c + z * s;
                q.z = pivot.z - y * s + z * c;
            }
            return q + move;
        }
        void Append8(Vec3 v[8], const Vec4& top, const Vec4& side)
        {
            if (scale != 1.0f || pitch != 0.0f || move != Vec3(0.0f, 0.0f, 0.0f)) for (int i = 0; i < 8; ++i) v[i] = Xf(v[i]);
            batch.Append(v, top, side);
            ++boxes;
        }
        // jitter > 0 = 角を少しずつずらす（石の削り出しの不揃い）
        void Box(const Vec3& lo, const Vec3& hi, const Vec4& top, const Vec4& side, float jitter = 0.0f, std::mt19937* rng = nullptr)
        {
            Vec3 v[8] = {
                { lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { lo.x, lo.y, hi.z },
                { lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z } };
            if (jitter > 0.0f && rng)
            {
                std::uniform_real_distribution<float> u(-jitter, jitter);
                for (Vec3& p : v) p += Vec3(u(*rng), u(*rng), u(*rng));
            }
            Append8(v, top, side);
        }
        void Box(const Vec3& lo, const Vec3& hi, const Vec4& c) { Box(lo, hi, c, c); }
        // 向きのある箱（中心 c、軸 ax / ay / az（単位）に沿った半分の大きさ h）。腕の向きに沿わせる拳など
        void OBox(const Vec3& c, const Vec3& ax, const Vec3& ay, const Vec3& az, const Vec3& h,
            const Vec4& top, const Vec4& side, float jitter = 0.0f, std::mt19937* rng = nullptr)
        {
            auto corner = [&](float sx, float sy, float sz) { return c + ax * (sx * h.x) + ay * (sy * h.y) + az * (sz * h.z); };
            Vec3 v[8] = {
                corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1), corner(-1, -1, 1),
                corner(-1, 1, -1), corner(1, 1, -1), corner(1, 1, 1), corner(-1, 1, 1) };
            if (jitter > 0.0f && rng)
            {
                std::uniform_real_distribution<float> u(-jitter, jitter);
                for (Vec3& p : v) p += Vec3(u(*rng), u(*rng), u(*rng));
            }
            Append8(v, top, side);
        }
        // n 本の縞（a / b 交互）。y = 上下に積む、x = 左右に並べる
        void StripesY(const Vec3& lo, const Vec3& hi, int n, const Vec4& a, const Vec4& b)
        {
            for (int i = 0; i < n; ++i)
            {
                const float y0 = lo.y + (hi.y - lo.y) * (float)i / (float)n;
                const float y1 = lo.y + (hi.y - lo.y) * (float)(i + 1) / (float)n;
                Box(Vec3(lo.x, y0, lo.z), Vec3(hi.x, y1, hi.z), (i & 1) ? b : a);
            }
        }
        void StripesX(const Vec3& lo, const Vec3& hi, int n, const Vec4& a, const Vec4& b)
        {
            for (int i = 0; i < n; ++i)
            {
                const float x0 = lo.x + (hi.x - lo.x) * (float)i / (float)n;
                const float x1 = lo.x + (hi.x - lo.x) * (float)(i + 1) / (float)n;
                Box(Vec3(x0, lo.y, lo.z), Vec3(x1, hi.y, hi.z), (i & 1) ? b : a);
            }
        }
        // 8 隅（Box と同じ並び：下面 4 点を一周 → 上面 4 点）。鏡像（左の翼など）なら並びを裏返して面を外向きに保つ（2026-10-25）
        void Hex(Vec3 v[8], const Vec4& top, const Vec4& side)
        {
            const float vol = (v[1] - v[0]).Cross(v[3] - v[0]).Dot(v[4] - v[0]);
            if (vol > 0.0f) { std::swap(v[1], v[3]); std::swap(v[5], v[7]); }
            Append8(v, top, side);
        }
        // a → b の角柱（断面 w × w、先の太さ w2。負 = 同じ）。骨・角・杖・鎖の輪の辺
        void Beam(const Vec3& a, const Vec3& b, float w, const Vec4& c, float w2 = -1.0f)
        {
            Vec3 d = b - a;
            const float len = d.Length();
            if (len < 1e-5f) return;
            d /= len;
            const Vec3 up = (std::fabs(d.y) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
            Vec3 s1 = d.Cross(up);
            s1.Normalize();
            Vec3 s2 = s1.Cross(d);
            s2.Normalize();
            const float h0 = 0.5f * w, h1 = 0.5f * ((w2 < 0.0f) ? w : w2);
            Vec3 v[8] = {
                a - s1 * h0 - s2 * h0, a + s1 * h0 - s2 * h0, a + s1 * h0 + s2 * h0, a - s1 * h0 + s2 * h0,
                b - s1 * h1 - s2 * h1, b + s1 * h1 - s2 * h1, b + s1 * h1 + s2 * h1, b - s1 * h1 + s2 * h1 };
            Hex(v, c, c);
        }
        // 4 点（一周の順、ほぼ同じ平面）の薄い板。厚み t を n の向きに。翼の膜
        void Plate(const Vec3 q[4], const Vec3& n, float t, const Vec4& c)
        {
            const Vec3 o = n * (0.5f * t);
            Vec3 v[8] = { q[0] - o, q[1] - o, q[2] - o, q[3] - o, q[0] + o, q[1] + o, q[2] + o, q[3] + o };
            Hex(v, c, c);
        }
        // 鎖：a → b に長さ linkLen の輪を並べる（1 つ置きに 90 度回す）。輪 = 細い角柱 4 本の長方形
        void Chain(const Vec3& a, const Vec3& b, float linkLen, float thick, const Vec4& c)
        {
            Vec3 d = b - a;
            const float len = d.Length();
            if (len < 1e-4f) return;
            d /= len;
            const Vec3 up = (std::fabs(d.y) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
            Vec3 p1 = d.Cross(up);
            p1.Normalize();
            Vec3 p2 = p1.Cross(d);
            p2.Normalize();
            const float step = linkLen * 0.78f;   // 隣の輪と少し重なる
            const int n = (std::max)(1, (int)(len / step));
            const float halfW = linkLen * 0.32f;
            for (int i = 0; i < n; ++i)
            {
                const Vec3 m = a + d * ((float)i * step + 0.5f * linkLen);
                const Vec3 p = (i & 1) ? p2 : p1;
                const Vec3 e0 = m - d * (0.5f * linkLen), e1 = m + d * (0.5f * linkLen);
                Beam(e0 - p * halfW, e1 - p * halfW, thick, c);
                Beam(e0 + p * halfW, e1 + p * halfW, thick, c);
                Beam(e0 - p * halfW, e0 + p * halfW, thick, c);
                Beam(e1 - p * halfW, e1 + p * halfW, thick, c);
            }
        }
        // ---- 丸い物（2026-10-26、ユーザー「四角と三角だけでなく円柱・球も使っていい」）。六面体を扇形に並べて作る ----
        // a → b の円柱（断面は segs 角形。半径 r0 → r1。中は少し細い同心の輪 = 外から見て詰まって見える）。脚・尾の節・腕
        void Tube(const Vec3& a, const Vec3& b, float r0, float r1, const Vec4& c, int segs = 8)
        {
            Vec3 d = b - a;
            const float len = d.Length();
            if (len < 1e-5f) return;
            d /= len;
            const Vec3 up = (std::fabs(d.y) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
            Vec3 s1 = d.Cross(up);
            s1.Normalize();
            Vec3 s2 = s1.Cross(d);
            s2.Normalize();
            segs = (std::max)(segs, 3);
            const float inner = 0.3f;   // 内側の輪の半径の割合（面は外側だけ見える）
            for (int i = 0; i < segs; ++i)
            {
                const float t0 = 6.2831853f * (float)i / (float)segs, t1 = 6.2831853f * (float)(i + 1) / (float)segs;
                const Vec3 o0 = s1 * std::cos(t0) + s2 * std::sin(t0), o1 = s1 * std::cos(t1) + s2 * std::sin(t1);
                Vec3 v[8] = {
                    a + o0 * (r0 * inner), a + o0 * r0, a + o1 * r0, a + o1 * (r0 * inner),
                    b + o0 * (r1 * inner), b + o0 * r1, b + o1 * r1, b + o1 * (r1 * inner) };
                Hex(v, c, c);
            }
        }
        // 中心 c・半径 r の球（経線 segs × 緯線 rings の殻）。関節・目・毒嚢
        void Ball(const Vec3& c, float r, const Vec4& col, int segs = 8, int rings = 4)
        {
            segs = (std::max)(segs, 3);
            rings = (std::max)(rings, 2);
            const float inner = 0.55f;
            auto dir = [](float phi, float th) { return Vec3(std::cos(phi) * std::cos(th), std::sin(phi), std::cos(phi) * std::sin(th)); };
            for (int j = 0; j < rings; ++j)
            {
                const float p0 = -1.5707963f + 3.1415927f * (float)j / (float)rings;
                const float p1 = -1.5707963f + 3.1415927f * (float)(j + 1) / (float)rings;
                for (int i = 0; i < segs; ++i)
                {
                    const float t0 = 6.2831853f * (float)i / (float)segs, t1 = 6.2831853f * (float)(i + 1) / (float)segs;
                    const Vec3 d00 = dir(p0, t0), d01 = dir(p0, t1), d10 = dir(p1, t0), d11 = dir(p1, t1);
                    Vec3 v[8] = {
                        c + d00 * (r * inner), c + d00 * r, c + d01 * r, c + d01 * (r * inner),
                        c + d10 * (r * inner), c + d10 * r, c + d11 * r, c + d11 * (r * inner) };
                    Hex(v, col, col);
                }
            }
        }
        // 先の尖った棘：根元 a（太さ w）から先 b へ（先は点）。甲羅の縁・尾の節の背
        void Spike(const Vec3& a, const Vec3& b, float w, const Vec4& c) { Beam(a, b, w, c, 0.004f); }
        std::shared_ptr<Model> Build(ID3D11Device* device) const { return boxes ? batch.Build(device) : nullptr; }
    };

    // 腕の向き：付け根 → 手（包囲ボックスの付け根から遠い方の下の角）。ay = 手 → 付け根、az ≒ 前、ax = 横。
    // 焼いた歩きの姿勢では腕が前後に振れているので、手に持つ物（杖・剣・鉤）はこの向きで組む（2026-10-25 に EnemyLooks.cpp から移した）
    struct ArmFrame
    {
        Vec3 hand, down, ax, ay, az;
        float w = 0.2f;
    };
    inline ArmFrame FrameOf(const Part& A, const Vec3& pivot)
    {
        ArmFrame f;
        f.w = A.Size().x;
        const float zEnd = (pivot.z - A.lo.z > A.hi.z - pivot.z) ? A.lo.z + 0.5f * f.w : A.hi.z - 0.5f * f.w;
        f.hand = Vec3(A.Center().x, A.lo.y + 0.5f * f.w, zEnd);
        f.down = f.hand - pivot;
        if (f.down.LengthSquared() < 1e-6f) f.down = Vec3(0.0f, -1.0f, 0.0f);
        f.down.Normalize();
        f.ay = -f.down;
        f.az = Vec3(1.0f, 0.0f, 0.0f).Cross(f.ay);
        f.az.Normalize();
        f.ax = f.ay.Cross(f.az);
        f.ax.Normalize();
        return f;
    }

    inline bool NameHas(std::string s, const char* key)
    {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s.find(key) != std::string::npos;
    }

    // 頭・胴・左右の腕を探す。名前（Kenney は head / torso / arm-left …）が無ければ、頭 = 一番上、胴 = 頭以外で一番大きい物
    inline void FindParts(const Model& model, Part& head, Part& torso, Part& armA, Part& armB)
    {
        const auto& subs = model.GetSubMeshes();
        auto take = [&](Part& p, int i) { p.index = i; p.lo = subs[i].boundsMin; p.hi = subs[i].boundsMax; };
        for (int i = 0; i < (int)subs.size(); ++i)
        {
            const std::string& n = subs[i].nodeName;
            if (!head.Valid() && NameHas(n, "head")) take(head, i);
            else if (!torso.Valid() && (NameHas(n, "torso") || NameHas(n, "body") || NameHas(n, "chest"))) take(torso, i);
            else if (NameHas(n, "arm"))
            {
                if (!armA.Valid()) take(armA, i);
                else if (!armB.Valid()) take(armB, i);
            }
        }
        if (!head.Valid())
        {
            float best = -1.0e9f;
            for (int i = 0; i < (int)subs.size(); ++i)
            {
                const float y = (subs[i].boundsMin.y + subs[i].boundsMax.y) * 0.5f;
                if (y > best) { best = y; take(head, i); }
            }
        }
        if (!torso.Valid())
        {
            float best = -1.0f;
            for (int i = 0; i < (int)subs.size(); ++i)
            {
                if (i == head.index) continue;
                const Vec3 s = subs[i].boundsMax - subs[i].boundsMin;
                const float vol = s.x * s.y * s.z;
                if (vol > best) { best = vol; take(torso, i); }
            }
        }
    }
}
