// ============================================================
// CollisionMath.h
// 衝突判定の純粋数学ライブラリ（ECS 非依存、無状態、自由関数のみ）
// 形状: Sphere / Capsule(垂直) / AABB / Segment / Ray
// 各判定は bool 版 + Contact 版を提供。
//
// 【法線の約定（全関数共通・厳守）】
//   Intersect(A, B, out) の out.normal は「A を B から押し出す方向」
//   （B 表面 → A の向き）。out.depth は正のめり込み深さ。
//   → 押し出しは常に A.center += normal * depth で正しくなる。
//
// 【距離比較】可能な限り平方距離（sqrt 回避）。sqrt は交差確定後のみ。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cmath>
#include <algorithm>

namespace CollisionMath
{
    using DirectX::SimpleMath::Vector3;

    // ========================================================
    // 形状定義
    // ========================================================
    struct Sphere
    {
        Vector3 center;
        float   radius;
    };

    // 垂直カプセル（線分 = center ± height/2 の Y 方向、+ 半径）
    struct Capsule
    {
        Vector3 center;
        float   radius;
        float   height;   // 円柱部分の高さ（両端半球は含まない）
    };

    struct AABB
    {
        Vector3 min;
        Vector3 max;
    };

    // 平面（n は正規化済み。n·p <= d が「内側」）
    struct Plane
    {
        Vector3 n;
        float   d;

        float SignedDist(const Vector3& p) const { return n.Dot(p) - d; }   // 正 = 外側
    };

    // 凸多面体 = 平面の集合（全部の内側の共通部分）。
    // 台形柱・斜坡・楔など、軸に揃わない面を持つ静的地形用。
    // 面数は固定上限（vector を持たせない: WorldCollider が毎フレーム値コピーされる）
    constexpr int kMaxConvexPlanes = 12;
    struct Convex
    {
        Plane planes[kMaxConvexPlanes];
        int   count = 0;

        bool Contains(const Vector3& p) const
        {
            for (int i = 0; i < count; ++i)
                if (planes[i].SignedDist(p) > 0.0f) return false;
            return true;
        }
    };

    // 有限線分
    struct Segment
    {
        Vector3 start;
        Vector3 end;
    };

    // 射線（起点 + 方向 + 最大距離）
    struct Ray
    {
        Vector3 origin;
        Vector3 dir;       // 正規化しておくこと
        float   maxDist;   // これ以上遠くは命中扱いしない
    };

    // ========================================================
    // 結果構造体
    // ========================================================
    // 交差の接触情報（押し出し・コンボ生成に使う）
    struct Contact
    {
        Vector3 normal;   // A を B から離す方向（B 表面 → A）
        float   depth;    // めり込み深さ（正 = 交差中）
        Vector3 point;    // 接触点（おおよそ B 表面上）
    };

    // レイキャストの命中情報
    struct RayHit
    {
        bool    hit = false;
        float   t = 0.0f;      // origin から命中点までの距離
        Vector3 point = {};        // 命中点
        Vector3 normal = {};        // 命中面の法線（射線側を向く）
    };

    // ========================================================
    // 基礎ユーティリティ（各判定が共有する部品）
    // ========================================================

    // 点 → 線分 の最近点（clamp 投影）
    inline Vector3 ClosestPointOnSegment(const Vector3& p,
        const Vector3& a, const Vector3& b)
    {
        Vector3 ab = b - a;
        float abLenSq = ab.LengthSquared();
        if (abLenSq < 1e-8f) return a;
        float t = (p - a).Dot(ab) / abLenSq;
        t = std::clamp(t, 0.0f, 1.0f);
        return a + ab * t;
    }

    // 点 → AABB の最近点（各軸を範囲内に clamp するだけ）
    inline Vector3 ClosestPointOnAABB(const Vector3& p, const AABB& box)
    {
        return Vector3(
            std::clamp(p.x, box.min.x, box.max.x),
            std::clamp(p.y, box.min.y, box.max.y),
            std::clamp(p.z, box.min.z, box.max.z));
    }

    // 線分 vs 線分 の最短距離（最近点 c1, c2 を返す）
    // 参照: Real-Time Collision Detection の ClosestPtSegmentSegment
    inline float ClosestPtSegmentSegment(
        const Vector3& p1, const Vector3& q1,
        const Vector3& p2, const Vector3& q2,
        Vector3& c1, Vector3& c2)
    {
        Vector3 d1 = q1 - p1;   // 線分1 方向
        Vector3 d2 = q2 - p2;   // 線分2 方向
        Vector3 r = p1 - p2;
        float a = d1.Dot(d1);
        float e = d2.Dot(d2);
        float f = d2.Dot(r);

        float s, t;
        const float EPS = 1e-8f;

        if (a <= EPS && e <= EPS) { c1 = p1; c2 = p2; return (c1 - c2).LengthSquared(); }
        if (a <= EPS) { s = 0.0f; t = std::clamp(f / e, 0.0f, 1.0f); }
        else
        {
            float c = d1.Dot(r);
            if (e <= EPS) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
            else
            {
                float b = d1.Dot(d2);
                float denom = a * e - b * b;
                s = (denom > EPS) ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
                t = (b * s + f) / e;
                if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
                else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
            }
        }
        c1 = p1 + d1 * s;
        c2 = p2 + d2 * t;
        return (c1 - c2).LengthSquared();
    }

    // カプセルの線分端点を取り出す（垂直前提）
    inline void CapsuleSegment(const Capsule& c, Vector3& a, Vector3& b)
    {
        float halfH = c.height * 0.5f;
        a = c.center + Vector3(0, halfH, 0);
        b = c.center + Vector3(0, -halfH, 0);
    }

    // ========================================================
    // Sphere vs Sphere
    // ========================================================
    inline bool IntersectSphereSphere(const Sphere& a, const Sphere& b)
    {
        float r = a.radius + b.radius;
        return (a.center - b.center).LengthSquared() <= r * r;
    }
    inline bool IntersectSphereSphere(const Sphere& a, const Sphere& b, Contact& out)
    {
        Vector3 d = a.center - b.center;
        float distSq = d.LengthSquared();
        float r = a.radius + b.radius;
        if (distSq > r * r) return false;
        float dist = std::sqrt(distSq);
        out.normal = (dist > 1e-6f) ? d / dist : Vector3(0, 1, 0);
        out.depth = r - dist;
        out.point = b.center + out.normal * b.radius;
        return true;
    }

    // ========================================================
    // Sphere vs Capsule（Contact の A = Sphere）
    // ========================================================
    inline bool IntersectSphereCapsule(const Sphere& s, const Capsule& cap)
    {
        Vector3 a, b; CapsuleSegment(cap, a, b);
        Vector3 closest = ClosestPointOnSegment(s.center, a, b);
        float r = s.radius + cap.radius;
        return (s.center - closest).LengthSquared() <= r * r;
    }
    inline bool IntersectSphereCapsule(const Sphere& s, const Capsule& cap, Contact& out)
    {
        Vector3 a, b; CapsuleSegment(cap, a, b);
        Vector3 closest = ClosestPointOnSegment(s.center, a, b);
        Vector3 d = s.center - closest;
        float distSq = d.LengthSquared();
        float r = s.radius + cap.radius;
        if (distSq > r * r) return false;
        float dist = std::sqrt(distSq);
        out.normal = (dist > 1e-6f) ? d / dist : Vector3(0, 1, 0);
        out.depth = r - dist;
        out.point = closest + out.normal * cap.radius;
        return true;
    }

    // ========================================================
    // Sphere vs AABB（Contact の A = Sphere）
    // ========================================================
    inline bool IntersectSphereAABB(const Sphere& s, const AABB& box)
    {
        Vector3 closest = ClosestPointOnAABB(s.center, box);
        return (s.center - closest).LengthSquared() <= s.radius * s.radius;
    }
    inline bool IntersectSphereAABB(const Sphere& s, const AABB& box, Contact& out)
    {
        Vector3 closest = ClosestPointOnAABB(s.center, box);
        Vector3 d = s.center - closest;
        float distSq = d.LengthSquared();
        if (distSq > s.radius * s.radius) return false;

        float dist = std::sqrt(distSq);
        if (dist > 1e-6f)
        {
            // 球心が箱の外 → 最近点から球心へ押し出す
            out.normal = d / dist;
            out.depth = s.radius - dist;
        }
        else
        {
            // 球心が箱の内部 → 最も近い面へ押し出す（各面までの距離を比較）
            Vector3 c = (box.min + box.max) * 0.5f;
            Vector3 he = (box.max - box.min) * 0.5f;
            Vector3 local = s.center - c;
            float dx = he.x - std::abs(local.x);
            float dy = he.y - std::abs(local.y);
            float dz = he.z - std::abs(local.z);
            if (dx < dy && dx < dz)      out.normal = Vector3((local.x < 0) ? -1.f : 1.f, 0, 0), out.depth = dx + s.radius;
            else if (dy < dz)            out.normal = Vector3(0, (local.y < 0) ? -1.f : 1.f, 0), out.depth = dy + s.radius;
            else                         out.normal = Vector3(0, 0, (local.z < 0) ? -1.f : 1.f), out.depth = dz + s.radius;
        }
        out.point = closest;
        return true;
    }

    // ========================================================
    // Capsule vs AABB（Contact の A = Capsule）
    //   カプセル線分と AABB の最近点で近似（垂直カプセル + 軸箱で実用十分）
    // ========================================================
    inline bool IntersectCapsuleAABB(const Capsule& cap, const AABB& box)
    {
        Vector3 a, b; CapsuleSegment(cap, a, b);
        // 線分上の複数点で最近を探す簡易版（端点 + 中央）で判定
        Vector3 pts[3] = { a, b, cap.center };
        for (auto& p : pts)
        {
            Vector3 closest = ClosestPointOnAABB(p, box);
            if ((p - closest).LengthSquared() <= cap.radius * cap.radius) return true;
        }
        return false;
    }
    inline bool IntersectCapsuleAABB(const Capsule& cap, const AABB& box, Contact& out)
    {
        Vector3 a, b; CapsuleSegment(cap, a, b);

        // ============================================================
        // サンプル数を相手の大きさで決める。
        //
        // 8点は「高いカプセルが大きな面に当たる」時のための数。
        // 雑魚のカプセルは軸線 1m 程度で、地形の箱は最小でも 2m 角。
        // その組み合わせだと 8点でも 3点でも見つかる最深点は同じで、
        // 間隔を細かくしただけの無駄になる。
        //
        // 判定式: 軸線が相手の最小辺より短ければ 3点で足りる
        // ============================================================
        const Vector3 boxExtent = box.max - box.min;
        const float minExtent = (std::min)(boxExtent.x, (std::min)(boxExtent.y, boxExtent.z));
        const int SAMPLES = (cap.height > minExtent) ? 8 : 3;

        float bestDepth = -1.0f;
        Vector3 bestNormal, bestPoint;

        for (int i = 0; i <= SAMPLES; ++i)
        {
            float t = (float)i / SAMPLES;
            Vector3 p = a + (b - a) * t;
            Vector3 closest = ClosestPointOnAABB(p, box);
            Vector3 d = p - closest;
            float distSq = d.LengthSquared();

            if (distSq <= cap.radius * cap.radius)
            {
                float dist = std::sqrt(distSq);
                float depth = cap.radius - dist;
                if (depth > bestDepth)
                {
                    bestDepth = depth;
                    bestNormal = (dist > 1e-6f) ? d / dist : Vector3(0, 1, 0);
                    bestPoint = closest;
                }
            }
        }

        if (bestDepth < 0.0f) return false;

        out.normal = bestNormal;
        out.depth = bestDepth;
        out.point = bestPoint;
        return true;
    }

    // ========================================================
    // 点 → 凸多面体
    //   外側: 違反している平面へ順に投影する（凸なら数回で収束。
    //         辺・頂点領域では 2〜3 枚を交互に押されて落ち着く）
    //   内側: 一番浅い平面（|符号付き距離| 最小）の上へ。
    //   戻り値は「p が内側だったか」
    // ========================================================
    inline bool ClosestPointOnConvex(const Vector3& p, const Convex& hull, Vector3& outClosest,
        Vector3& outNormal, float& outInsideDepth)
    {
        if (hull.count == 0) { outClosest = p; outNormal = Vector3(0, 1, 0); outInsideDepth = 0; return false; }

        // ---- 内側判定 + 一番浅い面 ----
        int   shallow = -1;
        float shallowDist = -1e30f;   // 符号付き距離の最大（負の中で 0 に一番近い）
        bool  inside = true;
        for (int i = 0; i < hull.count; ++i)
        {
            const float sd = hull.planes[i].SignedDist(p);
            if (sd > 0.0f) inside = false;
            if (sd > shallowDist) { shallowDist = sd; shallow = i; }
        }
        if (inside)
        {
            const Plane& pl = hull.planes[shallow];
            outNormal = pl.n;
            outInsideDepth = -shallowDist;          // 面までの距離（正）
            outClosest = p - pl.n * shallowDist;    // 面の上
            return true;
        }

        // ---- 外側: 逐次投影 ----
        Vector3 q = p;
        for (int iter = 0; iter < 6; ++iter)
        {
            int   worst = -1;
            float worstDist = 1e-5f;
            for (int i = 0; i < hull.count; ++i)
            {
                const float sd = hull.planes[i].SignedDist(q);
                if (sd > worstDist) { worstDist = sd; worst = i; }
            }
            if (worst < 0) break;
            q -= hull.planes[worst].n * worstDist;
        }
        outClosest = q;
        Vector3 d = p - q;
        const float len = d.Length();
        outNormal = (len > 1e-6f) ? d / len : hull.planes[shallow].n;
        outInsideDepth = 0.0f;
        return false;
    }

    // ========================================================
    // Sphere vs Convex（Contact の A = Sphere）
    // ========================================================
    inline bool IntersectSphereConvex(const Sphere& s, const Convex& hull, Contact& out)
    {
        Vector3 closest, n; float insideDepth;
        const bool inside = ClosestPointOnConvex(s.center, hull, closest, n, insideDepth);
        if (inside)
        {
            out.normal = n;
            out.depth = insideDepth + s.radius;
            out.point = closest;
            return true;
        }
        const float distSq = (s.center - closest).LengthSquared();
        if (distSq > s.radius * s.radius) return false;
        out.normal = n;
        out.depth = s.radius - std::sqrt(distSq);
        out.point = closest;
        return true;
    }
    inline bool IntersectSphereConvex(const Sphere& s, const Convex& hull)
    {
        Contact c; return IntersectSphereConvex(s, hull, c);
    }

    // ========================================================
    // Capsule vs Convex（Contact の A = Capsule）
    //   AABB 版と同じく軸線を採様して最深点を採る
    // ========================================================
    inline bool IntersectCapsuleConvex(const Capsule& cap, const Convex& hull, Contact& out)
    {
        Vector3 a, b; CapsuleSegment(cap, a, b);
        const int SAMPLES = 8;

        float bestDepth = -1.0f;
        Contact best;
        for (int i = 0; i <= SAMPLES; ++i)
        {
            const float t = (float)i / SAMPLES;
            Contact c;
            if (IntersectSphereConvex({ a + (b - a) * t, cap.radius }, hull, c) && c.depth > bestDepth)
            {
                bestDepth = c.depth;
                best = c;
            }
        }
        if (bestDepth < 0.0f) return false;
        out = best;
        return true;
    }
    inline bool IntersectCapsuleConvex(const Capsule& cap, const Convex& hull)
    {
        Contact c; return IntersectCapsuleConvex(cap, hull, c);
    }

    // ========================================================
    // Ray vs Convex（Cyrus-Beck: 各平面で射線区間を切り詰める）
    //   起点が内側なら AABB 版と同じく命中無し
    // ========================================================
    inline RayHit RaycastConvex(const Ray& ray, const Convex& hull)
    {
        RayHit r;
        float tmin = 0.0f, tmax = ray.maxDist;
        int enter = -1;
        for (int i = 0; i < hull.count; ++i)
        {
            const Plane& pl = hull.planes[i];
            const float denom = pl.n.Dot(ray.dir);
            const float num = pl.d - pl.n.Dot(ray.origin);   // 正 = 起点は内側
            if (std::abs(denom) < 1e-8f)
            {
                if (num < 0.0f) return r;    // 平行で外側
                continue;
            }
            const float t = num / denom;
            if (denom < 0.0f) { if (t > tmin) { tmin = t; enter = i; } }   // 入る
            else              { if (t < tmax) tmax = t; }                  // 出る
            if (tmin > tmax) return r;
        }
        if (enter < 0) return r;
        r.hit = true;
        r.t = tmin;
        r.point = ray.origin + ray.dir * tmin;
        r.normal = hull.planes[enter].n;
        return r;
    }

    // ========================================================
    // 凸多面体の組み立て
    // ========================================================
    // 3 点から平面（法線は (b-a)×(c-a)。outward になる順で渡すこと）
    inline Plane PlaneFromPoints(const Vector3& a, const Vector3& b, const Vector3& c)
    {
        Vector3 n = (b - a).Cross(c - a);
        n.Normalize();
        return { n, n.Dot(a) };
    }

    // 8 頂点の六面体（箱を歪めた物: 台形柱・楔・斜坡）。
    // 頂点順: 下面 0-3（-x-z, +x-z, +x+z, -x+z）、上面 4-7 が同じ順で対応。
    // 法線の向きは重心が内側に来るように揃えるので、面内の頂点順は気にしなくてよい
    inline Convex ConvexFromHexahedron(const Vector3 v[8])
    {
        Vector3 centroid;
        for (int i = 0; i < 8; ++i) centroid += v[i];
        centroid /= 8.0f;

        static const int faces[6][3] = {
            { 0, 1, 3 },   // 下
            { 4, 5, 7 },   // 上
            { 0, 1, 4 },   // 手前（-z 側）
            { 3, 2, 7 },   // 奥（+z 側）
            { 0, 3, 4 },   // 左（-x 側）
            { 1, 2, 5 },   // 右（+x 側）
        };

        Convex h;
        h.count = 6;
        for (int f = 0; f < 6; ++f)
        {
            Plane pl = PlaneFromPoints(v[faces[f][0]], v[faces[f][1]], v[faces[f][2]]);
            if (pl.SignedDist(centroid) > 0.0f) { pl.n = -pl.n; pl.d = -pl.d; }
            h.planes[f] = pl;
        }
        return h;
    }

    // ========================================================
    // Capsule vs Capsule（Contact の A = cap1）
    // ========================================================
    inline bool IntersectCapsuleCapsule(const Capsule& cap1, const Capsule& cap2)
    {
        Vector3 a1, b1, a2, b2;
        CapsuleSegment(cap1, a1, b1);
        CapsuleSegment(cap2, a2, b2);
        Vector3 c1, c2;
        float distSq = ClosestPtSegmentSegment(a1, b1, a2, b2, c1, c2);
        float r = cap1.radius + cap2.radius;
        return distSq <= r * r;
    }
    inline bool IntersectCapsuleCapsule(const Capsule& cap1, const Capsule& cap2, Contact& out)
    {
        Vector3 a1, b1, a2, b2;
        CapsuleSegment(cap1, a1, b1);
        CapsuleSegment(cap2, a2, b2);
        Vector3 c1, c2;
        float distSq = ClosestPtSegmentSegment(a1, b1, a2, b2, c1, c2);
        float r = cap1.radius + cap2.radius;
        if (distSq > r * r) return false;
        float dist = std::sqrt(distSq);
        Vector3 d = c1 - c2;
        out.normal = (dist > 1e-6f) ? d / dist : Vector3(0, 1, 0);
        out.depth = r - dist;
        out.point = c2 + out.normal * cap2.radius;
        return true;
    }

    // ========================================================
    // Segment vs Sphere / Capsule（bool のみ。命中判定用）
    //   Segment は「太さ0の線分」として扱う
    // ========================================================
    inline bool IntersectSegmentSphere(const Segment& seg, const Sphere& s)
    {
        Vector3 closest = ClosestPointOnSegment(s.center, seg.start, seg.end);
        return (s.center - closest).LengthSquared() <= s.radius * s.radius;
    }
    inline bool IntersectSegmentCapsule(const Segment& seg, const Capsule& cap)
    {
        Vector3 a, b; CapsuleSegment(cap, a, b);
        Vector3 c1, c2;
        float distSq = ClosestPtSegmentSegment(seg.start, seg.end, a, b, c1, c2);
        return distSq <= cap.radius * cap.radius;
    }

    // ========================================================
    // Raycast: Ray vs 各形状
    //   返り値 RayHit の t は origin からの距離。
    //   maxDist を超える命中は hit=false。
    //   normal は射線に対向する面法線。
    // ========================================================

    // --- Ray vs Sphere ---
    inline RayHit RaycastSphere(const Ray& ray, const Sphere& s)
    {
        RayHit r;
        Vector3 m = ray.origin - s.center;
        float b = m.Dot(ray.dir);
        float c = m.LengthSquared() - s.radius * s.radius;

        // 起点が球外 かつ 射線が球から離れる方向 → 命中なし
        if (c > 0.0f && b > 0.0f) return r;

        float disc = b * b - c;
        if (disc < 0.0f) return r;             // 射線が球に届かない

        float t = -b - std::sqrt(disc);        // 手前側の交点
        if (t < 0.0f) t = 0.0f;                // 起点が球内 → t=0
        if (t > ray.maxDist) return r;         // 遠すぎ

        r.hit = true;
        r.t = t;
        r.point = ray.origin + ray.dir * t;
        r.normal = r.point - s.center;
        r.normal.Normalize();
        return r;
    }

    // --- Ray vs AABB（slab method）---
    inline RayHit RaycastAABB(const Ray& ray, const AABB& box)
    {
        RayHit r;
        float tmin = 0.0f;
        float tmax = ray.maxDist;
        int hitAxis = -1;
        float hitSign = 1.0f;

        const float* o = &ray.origin.x;
        const float* d = &ray.dir.x;
        const float* mn = &box.min.x;
        const float* mx = &box.max.x;

        for (int i = 0; i < 3; ++i)
        {
            if (std::abs(d[i]) < 1e-8f)
            {
                // 射線がこの軸に平行 → スラブ外なら命中なし
                if (o[i] < mn[i] || o[i] > mx[i]) return r;
            }
            else
            {
                float inv = 1.0f / d[i];
                float t1 = (mn[i] - o[i]) * inv;
                float t2 = (mx[i] - o[i]) * inv;
                float sign = -1.0f;
                if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
                if (t1 > tmin) { tmin = t1; hitAxis = i; hitSign = sign; }
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return r;      // スラブが交差しない
            }
        }

        if (hitAxis < 0) return r;             // 起点が箱の内部など
        r.hit = true;
        r.t = tmin;
        r.point = ray.origin + ray.dir * tmin;
        r.normal = Vector3(0, 0, 0);
        (&r.normal.x)[hitAxis] = hitSign;      // 命中面の法線（軸方向）
        return r;
    }

    // --- Ray vs Capsule（垂直カプセル）---
    //   カプセル線分と射線の最近距離が半径以内なら命中。
    //   命中 t は近似（最近点位置）で実用十分。
 // --- Ray vs Capsule（垂直カプセル）修正版 ---
    //   最近点ではなく「表面への入射点」を命中点にする。
    inline RayHit RaycastCapsule(const Ray& ray, const Capsule& cap)
    {
        RayHit r;
        Vector3 a, b; CapsuleSegment(cap, a, b);

        Vector3 rayEnd = ray.origin + ray.dir * ray.maxDist;
        Vector3 c1, c2;   // c1=射線側最近点, c2=カプセル軸側最近点
        float distSq = ClosestPtSegmentSegment(ray.origin, rayEnd, a, b, c1, c2);

        float rr = cap.radius * cap.radius;
        if (distSq > rr) return r;   // そもそも当たっていない

        // 最近点までの距離
        float tClosest = (c1 - ray.origin).Dot(ray.dir);   // 射線に沿った距離
        // 表面までの手前分だけ戻す（勾股: sqrt(半径^2 - 最近距離^2)）
        float back = std::sqrt(max(0.0f, rr - distSq));
        float t = tClosest - back;

        if (t < 0.0f) t = 0.0f;               // 起点がカプセル内 → t=0
        if (t > ray.maxDist) return r;

        r.hit = true;
        r.t = t;
        r.point = ray.origin + ray.dir * t;   // 表面上の入射点

        // 法線: 入射点からカプセル軸の最近点へ向かう逆方向（軸 → 表面）
        Vector3 axisPt = ClosestPointOnSegment(r.point, a, b);
        r.normal = r.point - axisPt;
        r.normal.Normalize();
        return r;
    }
}