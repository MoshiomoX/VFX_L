// ============================================================
// TreeBuilder.cpp
// ============================================================
#include "World/TreeBuilder.h"

using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kPi = 3.14159265f;

    struct Rng
    {
        uint32_t s;
        explicit Rng(uint32_t seed) : s(seed * 747796405u + 2891336453u) { if (s == 0) s = 1; Next(); }
        uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float F() { return (float)(Next() & 0xFFFFFFu) / 16777216.0f; }
        float R(float a, float b) { return a + (b - a) * F(); }
        int I(int a, int b) { return a + (int)(F() * (float)(b - a + 1) * 0.9999f); }   // a..b
        Vector3 Dir()   // 単位球面上
        {
            const float z = R(-1.0f, 1.0f), a = R(0.0f, 2.0f * kPi), r = std::sqrt((std::max)(0.0f, 1.0f - z * z));
            return { r * std::cos(a), z, r * std::sin(a) };
        }
    };

    Vector3 Perp(const Vector3& d)
    {
        Vector3 p = (std::fabs(d.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).Cross(d);
        p.Normalize();
        return p;
    }

    // 方位角（ラジアン）と水平からの仰角（度）から向き
    Vector3 AzEl(float az, float elDeg)
    {
        const float el = elDeg * kPi / 180.0f;
        return { std::cos(az) * std::cos(el), std::sin(el), std::sin(az) * std::cos(el) };
    }

    struct Branch
    {
        std::vector<Vector3> pts;
        std::vector<float> rad;
        std::vector<float> w;   // 各点の branchW
        int level = 0;
        float phase = 0.0f;
        float length = 0.0f;
    };

    struct LeafSeed
    {
        Vector3 pos, dir;
        float w, phase;
    };

    struct Builder
    {
        const TreeBuilder::Params& p;
        Rng rng;
        float H;
        std::vector<Branch> branches;
        std::vector<LeafSeed> leafSeeds;

        explicit Builder(const TreeBuilder::Params& pp) : p(pp), rng(pp.seed), H((std::max)(pp.height, 0.3f)) {}

        // 枝 1 本：start から dir へ len、segs 区間の折れ線。区間ごとに上へ曲がり、少し揺らぐ
        int Grow(Vector3 start, Vector3 dir, float len, int segs, float r0, float r1, float upBend, float wobble,
            int level, float w0, float flex, float phase)
        {
            Branch b;
            b.level = level; b.phase = phase; b.length = len;
            dir.Normalize();
            Vector3 pos = start;
            const float step = len / (float)segs;
            for (int k = 0; k <= segs; ++k)
            {
                const float s = (float)k / (float)segs;
                b.pts.push_back(pos);
                b.rad.push_back(r0 + (r1 - r0) * s);
                b.w.push_back(w0 + flex * std::pow(s, 1.5f));
                if (k == segs) break;
                dir += Vector3(0.0f, upBend, 0.0f) + rng.Dir() * wobble;
                dir.Normalize();
                pos += dir * step;
            }
            branches.push_back(std::move(b));
            return (int)branches.size() - 1;
        }

        // 折れ線上の s（0..1）の点・向き・半径・重み
        void Sample(int bi, float s, Vector3& pos, Vector3& dir, float& rad, float& w) const
        {
            const Branch& b = branches[(size_t)bi];
            const int n = (int)b.pts.size() - 1;
            const float f = std::clamp(s, 0.0f, 1.0f) * (float)n;
            const int i = (std::min)((int)f, n - 1);
            const float t = f - (float)i;
            pos = Vector3::Lerp(b.pts[(size_t)i], b.pts[(size_t)i + 1], t);
            dir = b.pts[(size_t)i + 1] - b.pts[(size_t)i];
            dir.Normalize();
            rad = b.rad[(size_t)i] + (b.rad[(size_t)i + 1] - b.rad[(size_t)i]) * t;
            w = b.w[(size_t)i] + (b.w[(size_t)i + 1] - b.w[(size_t)i]) * t;
        }

        // 親の s0..s1 に count 本の子枝
        void Children(int parent, int count, float s0, float s1, float lenMul0, float lenMul1, int segs,
            float radMul, float upBend, float wobble, float flex, float upMix)
        {
            const float parentLen = branches[(size_t)parent].length;
            const float parentPhase = branches[(size_t)parent].phase;
            const int level = branches[(size_t)parent].level + 1;
            const float twist0 = rng.R(0.0f, 2.0f * kPi);
            for (int c = 0; c < count; ++c)
            {
                const float s = s0 + (s1 - s0) * (((float)c + rng.R(0.2f, 0.8f)) / (float)count);
                Vector3 pos, dir; float rad, w;
                Sample(parent, s, pos, dir, rad, w);
                // 親の周りを黄金角で回して横へ
                const float a = twist0 + (float)c * 2.39996f + rng.R(-0.4f, 0.4f);
                const Vector3 n0 = Perp(dir), n1 = dir.Cross(n0);
                const Vector3 side = n0 * std::cos(a) + n1 * std::sin(a);
                Vector3 cd = dir * 0.5f + side * 0.8f + Vector3(0.0f, upMix, 0.0f);
                const float len = parentLen * rng.R(lenMul0, lenMul1) * (1.15f - 0.45f * s);
                // 位相は親と同じ（違うと付け根で親子の揺れがずれて裂ける）
                Grow(pos, cd, len, segs, rad * radMul, rad * radMul * 0.25f, upBend, wobble,
                    level, w, flex, parentPhase);
            }
        }

        // 枝の s0..1 に葉の付け根を並べる（枝の周りの球の中にばらまく）
        void LeavesOn(int bi, float s0, float perMeter, float clusterR)
        {
            const Branch& b = branches[(size_t)bi];
            const float span = b.length * (1.0f - s0);
            const int n = (std::max)(1, (int)(span * perMeter * p.leafDensity + 0.5f));
            const float phase = b.phase;
            for (int i = 0; i < n; ++i)
            {
                const float s = s0 + (1.0f - s0) * rng.F();
                Vector3 pos, dir; float rad, w;
                Sample(bi, s, pos, dir, rad, w);
                const float r = clusterR * (0.35f + 0.65f * s) * std::cbrt(rng.F());
                leafSeeds.push_back({ pos + rng.Dir() * r, dir, w + 0.08f, phase });
            }
        }

        void Broadleaf(bool bare)
        {
            const float yaw = p.yawDeg * kPi / 180.0f;
            const float r0 = std::clamp(H * 0.034f, 0.06f, 0.4f);
            const float trunkLen = H * (bare ? rng.R(0.32f, 0.42f) : rng.R(0.26f, 0.34f));
            const Vector3 trunkDir(rng.R(-0.1f, 0.1f), 1.0f, rng.R(-0.1f, 0.1f));
            const int trunk = Grow(p.root - Vector3(0.0f, 0.15f, 0.0f), trunkDir, trunkLen + 0.15f, 5,
                r0 * 1.2f, r0 * 0.72f, 0.02f, bare ? 0.09f : 0.05f, 0, 0.0f, 0.04f, rng.R(0.0f, 2.0f * kPi));

            std::vector<int> limbs;
            {
                Vector3 pos, dir; float rad, w;
                Sample(trunk, 1.0f, pos, dir, rad, w);
                limbs.push_back(Grow(pos, dir + Vector3(0, 0.6f, 0), H * rng.R(0.32f, 0.4f), 4, rad, rad * 0.2f,
                    0.02f, 0.08f, 1, w, 0.28f, rng.R(0.0f, 2.0f * kPi)));
            }
            const int nLimb = rng.I(5, 7);
            for (int i = 0; i < nLimb; ++i)
            {
                const float u = (float)i / (float)(std::max)(nLimb - 1, 1);
                const float s = 0.45f + 0.55f * u + rng.R(-0.05f, 0.05f);
                Vector3 pos, dir; float rad, w;
                Sample(trunk, s, pos, dir, rad, w);
                const float az = yaw + (float)i * 2.39996f + rng.R(-0.3f, 0.3f);
                const Vector3 d = AzEl(az, rng.R(18.0f, 36.0f) + 18.0f * u);
                const float len = H * rng.R(0.38f, 0.5f) * (1.1f - 0.35f * u);
                limbs.push_back(Grow(pos, d, len, 4, rad * 0.62f, rad * 0.12f, 0.07f, bare ? 0.16f : 0.1f,
                    1, w, 0.3f, rng.R(0.0f, 2.0f * kPi)));
            }

            std::vector<int> twigs;
            for (int li : limbs)
            {
                const size_t before = branches.size();
                Children(li, bare ? rng.I(5, 7) : rng.I(4, 6), 0.25f, 0.95f, 0.4f, 0.6f, 3, 0.55f, 0.05f,
                    bare ? 0.22f : 0.15f, 0.45f, 0.2f);
                for (size_t k = before; k < branches.size(); ++k) twigs.push_back((int)k);
            }
            if (bare)
            {
                // 枯れ木：小枝の先にさらに細い枝
                for (int ti : twigs)
                    Children(ti, rng.I(2, 3), 0.4f, 0.95f, 0.4f, 0.55f, 2, 0.6f, 0.04f, 0.25f, 0.35f, 0.15f);
                return;
            }
            const float k = std::pow(H / 5.0f, 0.6f);
            for (int ti : twigs) LeavesOn(ti, 0.1f, 120.0f, 0.42f * k);
            for (int li : limbs) LeavesOn(li, 0.45f, 80.0f, 0.5f * k);
        }

        void Shrub()
        {
            const float yaw = p.yawDeg * kPi / 180.0f;
            const int n = rng.I(6, 9);
            std::vector<int> stems;
            for (int i = 0; i < n; ++i)
            {
                const float az = yaw + (float)i * 2.39996f + rng.R(-0.3f, 0.3f);
                const Vector3 base = p.root + Vector3(std::cos(az), 0.0f, std::sin(az)) * (H * rng.R(0.03f, 0.1f)) - Vector3(0, 0.05f, 0);
                const Vector3 d = AzEl(az, rng.R(45.0f, 78.0f));
                const float r = std::clamp(H * 0.025f, 0.012f, 0.05f);
                stems.push_back(Grow(base, d, H * rng.R(0.75f, 1.05f), 4, r, r * 0.3f, 0.03f, 0.12f,
                    1, 0.0f, 0.55f, rng.R(0.0f, 2.0f * kPi)));
            }
            std::vector<int> all = stems;
            for (int si : stems)
            {
                const size_t before = branches.size();
                Children(si, rng.I(2, 3), 0.3f, 0.9f, 0.3f, 0.45f, 2, 0.7f, 0.03f, 0.15f, 0.4f, 0.15f);
                for (size_t k = before; k < branches.size(); ++k) all.push_back((int)k);
            }
            const float cr = 0.16f * std::pow(H, 0.5f);
            for (int bi : all) LeavesOn(bi, 0.2f, 68.0f, cr);
        }
    };
}

TreeBuilder::Result TreeBuilder::Build(const Params& p, std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
    std::vector<Leaf>& leaves)
{
    Builder b(p);
    if (p.kind == Kind::Shrub) b.Shrub();
    else b.Broadleaf(p.kind == Kind::Bare);

    Result res;
    const float H = b.H;
    const float mainPow = (p.kind == Kind::Shrub) ? 1.2f : 1.6f;
    auto mainW = [&](const Vector3& pos)
        {
            return std::pow(std::clamp((pos.y - p.root.y) / H, 0.0f, 1.2f), mainPow);
        };

    // ---- 幹・枝の筒 ----
    const size_t v0 = verts.size();
    const uint32_t vertTree = p.treeIndex | ((p.kind == Kind::Bare) ? kBareBit : 0u);
    for (const Branch& br : b.branches)
    {
        int sides = (br.level == 0) ? 8 : (br.level == 1 ? 6 : (br.level == 2 ? 4 : 3));
        if (p.kind == Kind::Shrub) sides = (br.level <= 1) ? 4 : 3;
        const int n = (int)br.pts.size();
        const uint32_t base = (uint32_t)verts.size();

        Vector3 nPrev;
        float along = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            Vector3 t = (k + 1 < n) ? br.pts[(size_t)k + 1] - br.pts[(size_t)k] : br.pts[(size_t)k] - br.pts[(size_t)k - 1];
            if (k > 0 && k + 1 < n) t = br.pts[(size_t)k + 1] - br.pts[(size_t)k - 1];
            t.Normalize();
            // 平行移動した基準の向き（筒がねじれない）
            Vector3 nRef = (k == 0) ? Perp(t) : nPrev - t * nPrev.Dot(t);
            nRef.Normalize();
            nPrev = nRef;
            const Vector3 bRef = t.Cross(nRef);
            if (k > 0) along += (br.pts[(size_t)k] - br.pts[(size_t)k - 1]).Length();

            float rad = br.rad[(size_t)k];
            if (br.level == 0 && k == 0) rad *= 1.3f;   // 根元を少し広げる
            if (k == n - 1) rad *= 0.5f;
            for (int j = 0; j < sides; ++j)
            {
                const float a = 2.0f * kPi * (float)j / (float)sides;
                const Vector3 nrm = nRef * std::cos(a) + bRef * std::sin(a);
                Vertex v{};
                v.pos = br.pts[(size_t)k] + nrm * rad;
                v.tree = vertTree;
                v.normal = nrm;
                v.mainW = mainW(v.pos);
                v.branchW = br.w[(size_t)k];
                v.phase = br.phase;
                v.barkV = along;
                v.tip = (br.level == 0) ? 0.0f : (float)k / (float)(n - 1);
                verts.push_back(v);
            }
        }
        // 先端のふた（1 点に集める）
        {
            const Vector3 last = br.pts.back();
            Vector3 t = br.pts.back() - br.pts[br.pts.size() - 2];
            t.Normalize();
            Vertex v{};
            v.pos = last + t * br.rad.back() * 0.5f;
            v.tree = vertTree;
            v.normal = t;
            v.mainW = mainW(v.pos);
            v.branchW = br.w.back();
            v.phase = br.phase;
            v.barkV = along;
            v.tip = (br.level == 0) ? 0.0f : 1.0f;
            verts.push_back(v);
        }
        for (int k = 0; k + 1 < n; ++k)
            for (int j = 0; j < sides; ++j)
            {
                const uint32_t a = base + (uint32_t)(k * sides + j);
                const uint32_t c = base + (uint32_t)(k * sides + (j + 1) % sides);
                const uint32_t a2 = a + (uint32_t)sides, c2 = c + (uint32_t)sides;
                indices.insert(indices.end(), { a, a2, c, c, a2, c2 });
            }
        const uint32_t cap = base + (uint32_t)(n * sides);
        const uint32_t lastRing = base + (uint32_t)((n - 1) * sides);
        for (int j = 0; j < sides; ++j)
            indices.insert(indices.end(), { lastRing + (uint32_t)j, cap, lastRing + (uint32_t)((j + 1) % sides) });
    }
    res.vertexCount = (int)(verts.size() - v0);

    // ---- 樹冠の範囲（葉が無い木は枝の範囲）----
    Vector3 lo(1e9f), hi(-1e9f);
    if (!b.leafSeeds.empty())
        for (const LeafSeed& s : b.leafSeeds) { lo = Vector3::Min(lo, s.pos); hi = Vector3::Max(hi, s.pos); }
    else
        for (const Branch& br : b.branches)
            if (br.level > 0)
                for (const Vector3& q : br.pts) { lo = Vector3::Min(lo, q); hi = Vector3::Max(hi, q); }
    if (lo.x > hi.x) { lo = p.root; hi = p.root + Vector3(0, H, 0); }
    const Vector3 center = (lo + hi) * 0.5f;
    const float rh = (std::max)((std::max)(hi.x - lo.x, hi.z - lo.z) * 0.5f, 0.2f);
    const float rv = (std::max)((hi.y - lo.y) * 0.5f, 0.2f);
    res.crownCenter = center;
    res.crownRadius = rh;

    // ---- 葉 ----
    const float leafLen = p.leafSize * ((p.kind == Kind::Shrub) ? 0.2f : 0.28f * std::clamp(std::pow(H / 5.0f, 0.35f), 0.7f, 1.4f));
    const size_t l0 = leaves.size();
    const uint32_t tint = (uint32_t)(b.rng.F() * 255.0f) << 24;   // 木ごとの葉の色のばらつき（上位 8 bit）
    for (const LeafSeed& s : b.leafSeeds)
    {
        Vector3 rel = s.pos - center;
        Vector3 e(rel.x / rh, rel.y / rv, rel.z / rh);   // 樹冠を楕円体と見た座標
        const float depth = e.Length();
        Vector3 cn = (depth > 1e-4f) ? e / depth : Vector3(0, 1, 0);
        cn.y += 0.15f;   // 下側が真っ暗にならないよう少し上へ
        cn.Normalize();
        Vector3 out = rel; out.y *= 0.4f;
        if (out.LengthSquared() > 1e-6f) out.Normalize();
        Vector3 tip = s.dir * 0.35f + b.rng.Dir() * 0.8f + out * 0.6f - Vector3(0, 0.2f, 0);
        if (tip.LengthSquared() < 1e-6f) tip = Vector3(0, 1, 0);
        tip.Normalize();

        Leaf lf{};
        lf.pos = s.pos;
        lf.tree = p.treeIndex;
        lf.tip = tip;
        lf.size = leafLen * b.rng.R(0.8f, 1.2f);
        lf.crownN = cn;
        lf.shade = std::clamp(depth, 0.0f, 1.0f);
        lf.mainW = mainW(s.pos);
        lf.branchW = s.w;
        lf.phase = s.phase;
        lf.seed = (b.rng.Next() & 0xFFFFFFu) | tint;
        leaves.push_back(lf);
    }
    res.leafCount = (int)(leaves.size() - l0);
    return res;
}
