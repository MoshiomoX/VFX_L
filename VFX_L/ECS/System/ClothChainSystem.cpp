// ============================================================
// ClothChainSystem.cpp
// ============================================================
#include "ECS/System/ClothChainSystem.h"
#include "ECS/System/SkinnedAnimSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/ClothChainComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/TransformComponent.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/BoneMaps.h"

#include <imgui.h>

using namespace DirectX::SimpleMath;

namespace
{
    // 距離の拘束の種類（ClothChainComponent::Link::kind）
    enum LinkKind { kStretch, kBend, kSide, kShear, kSideBend };

    // a を b に向ける最小の回転（どちらも正規化済み）
    Quaternion FromTo(const Vector3& a, const Vector3& b)
    {
        const float d = a.Dot(b);
        if (d < -0.9999f)
        {
            Vector3 axis = a.Cross(Vector3::UnitX);
            if (axis.LengthSquared() < 1e-6f) axis = a.Cross(Vector3::UnitY);
            axis.Normalize();
            return Quaternion(axis.x, axis.y, axis.z, 0.0f);
        }
        const Vector3 axis = a.Cross(b);
        Quaternion q(axis.x, axis.y, axis.z, 1.0f + d);
        q.Normalize();
        return q;
    }

    // 線分 a-b・半径 r のカプセルから p を押し出す。sagittal = 体の左右（right）の成分は無視して測る
    // （1 本鎖の時。鎖は布の中心線なので、横にずれた脚でも布の幅のどこかには当たる）
    void PushOut(Vector3& p, const Vector3& a, const Vector3& b, float r,
        const Vector3& right, const Vector3& back, bool sagittal)
    {
        auto flat = [&](const Vector3& v) { return sagittal ? v - right * v.Dot(right) : v; };
        const Vector3 pa = flat(p - a);
        const Vector3 ba = flat(b - a);
        const float bb = ba.LengthSquared();
        const float t = (bb > 1e-8f) ? std::clamp(pa.Dot(ba) / bb, 0.0f, 1.0f) : 0.0f;
        const Vector3 d = pa - ba * t;
        const float len = d.Length();
        if (len >= r) return;
        if (len < 1e-5f) { p += back * r; return; }
        p += d * ((r - len) / len);
    }

    // 体のカプセル（世界）。格子の時の押し出しは PushOutFast（成分で書き、外接箱の外は素通り）
    struct Cap
    {
        Vector3 a, b;
        float r;
        float bax, bay, baz, bb;              // b - a、|b - a|²
        float minX, minY, minZ, maxX, maxY, maxZ;
    };

    Cap MakeCap(const Vector3& a, const Vector3& b, float r)
    {
        Cap c;
        c.a = a; c.b = b; c.r = r;
        c.bax = b.x - a.x; c.bay = b.y - a.y; c.baz = b.z - a.z;
        c.bb = c.bax * c.bax + c.bay * c.bay + c.baz * c.baz;
        c.minX = (std::min)(a.x, b.x) - r; c.maxX = (std::max)(a.x, b.x) + r;
        c.minY = (std::min)(a.y, b.y) - r; c.maxY = (std::max)(a.y, b.y) + r;
        c.minZ = (std::min)(a.z, b.z) - r; c.maxZ = (std::max)(a.z, b.z) + r;
        return c;
    }

    void PushOutFast(Vector3& p, const Cap& c, const Vector3& back)
    {
        if (p.x < c.minX || p.x > c.maxX || p.y < c.minY || p.y > c.maxY || p.z < c.minZ || p.z > c.maxZ) return;
        const float pax = p.x - c.a.x, pay = p.y - c.a.y, paz = p.z - c.a.z;
        float t = (c.bb > 1e-8f) ? (pax * c.bax + pay * c.bay + paz * c.baz) / c.bb : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float dx = pax - c.bax * t, dy = pay - c.bay * t, dz = paz - c.baz * t;
        const float len2 = dx * dx + dy * dy + dz * dz;
        if (len2 >= c.r * c.r) return;
        if (len2 < 1e-10f) { p.x += back.x * c.r; p.y += back.y * c.r; p.z += back.z * c.r; return; }
        const float len = std::sqrt(len2);
        const float k = (c.r - len) / len;
        p.x += dx * k; p.y += dy * k; p.z += dz * k;
    }

    // 毎フレームの作業用の配列（2026-10-25）。毎回確保し直さないように使い回す（中身は毎回全部書き直す。
    // 呼ぶのはメインスレッドの ClothChainSystem::Update だけ）
    struct Scratch
    {
        std::vector<Matrix> G, restG;
        std::vector<Vector3> target, m;
        std::vector<float> groundY, rowPull;
        std::vector<Cap> caps;
    };
    Scratch& Work()
    {
        static Scratch s;
        return s;
    }

    void BuildLinks(ClothChainComponent& c)
    {
        c.links.clear();
        auto add = [&](int a, int b, int kind) { c.links.push_back({ a, b, -1.0f, kind }); };
        for (int x = 0; x < c.cols; ++x)
        {
            for (int k = 0; k + 1 < c.rows; ++k) add(c.Node(x, k), c.Node(x, k + 1), kStretch);
            for (int k = 0; k + 2 < c.rows; ++k) add(c.Node(x, k), c.Node(x, k + 2), kBend);
        }
        if (c.cols < 2) return;
        // 根の段は全部固定なので横 / 横の 1 つ飛ばしは 1 段目から
        for (int k = 1; k < c.rows; ++k)
        {
            for (int x = 0; x + 1 < c.cols; ++x) add(c.Node(x, k), c.Node(x + 1, k), kSide);
            for (int x = 0; x + 2 < c.cols; ++x) add(c.Node(x, k), c.Node(x + 2, k), kSideBend);
        }
        for (int k = 0; k + 1 < c.rows; ++k)
            for (int x = 0; x + 1 < c.cols; ++x)
            {
                add(c.Node(x, k), c.Node(x + 1, k + 1), kShear);
                add(c.Node(x + 1, k), c.Node(x, k + 1), kShear);
            }
    }

    bool Init(ClothChainComponent& c, const SkinnedModel& model)
    {
        const Skeleton& sk = model.GetSkeleton();
        c.anchor = sk.FindBoneIndex(c.anchorBone);
        const int rows = (int)c.chainBones.size();
        if (c.anchor < 0 || rows < 3) return false;
        for (int k = 0; k + 1 < rows; ++k)
            if (sk.FindBoneIndex(c.chainBones[k]) < 0) return false;

        // 列：読み込み時に分けた列の骨（SkinnedModel::SplitClothChain）が全部あれば格子、無ければ 1 本鎖
        int cols = (std::max)(1, c.columns);
        if ((cols % 2) == 0) --cols;
        auto boneName = [&](int x, int k, int center)
        {
            return (x == center) ? c.chainBones[k] : SkinnedModel::ClothColumnBoneName(c.chainBones[k], x);
        };
        if (cols > 1)
            for (int x = 0; x < cols && cols > 1; ++x)
                for (int k = 0; k + 1 < rows; ++k)
                    if (sk.FindBoneIndex(boneName(x, k, cols / 2)) < 0)
                    {
                        std::cout << "[ClothChain] column bones not found (" << boneName(x, k, cols / 2)
                            << "), using a single chain" << std::endl;
                        cols = 1;
                        break;
                    }
        c.cols = cols;
        c.rows = rows;
        const int center = cols / 2;
        c.bones.assign(cols * rows, -1);
        c.bindG.assign(cols * rows, Matrix::Identity);
        for (int x = 0; x < cols; ++x)
            for (int k = 0; k < rows; ++k)
            {
                const int b = sk.FindBoneIndex(boneName(x, k, center));
                c.bones[c.Node(x, k)] = b;
                if (b >= 0) c.bindG[c.Node(x, k)] = model.GetSkinBindGlobal(b);
            }
        // 終点が無い：真ん中の列は最後の骨を前の骨と同じ向き・長さで延ばし、他の列は真ん中と同じだけ延ばす
        if (c.bones[c.Node(center, rows - 1)] < 0)
        {
            const Vector3 p1 = c.bindG[c.Node(center, rows - 2)].Translation(), p0 = c.bindG[c.Node(center, rows - 3)].Translation();
            Matrix m = c.bindG[c.Node(center, rows - 2)];
            m.Translation(p1 + (p1 - p0));
            c.bindG[c.Node(center, rows - 1)] = m;
        }
        const Vector3 endStep = c.bindG[c.Node(center, rows - 1)].Translation() - c.bindG[c.Node(center, rows - 2)].Translation();
        for (int x = 0; x < cols; ++x)
        {
            if (x == center || c.bones[c.Node(x, rows - 1)] >= 0) continue;
            Matrix m = c.bindG[c.Node(x, rows - 2)];
            m.Translation(m.Translation() + endStep);
            c.bindG[c.Node(x, rows - 1)] = m;
        }

        const Matrix anchorBind = model.GetSkinBindGlobal(c.anchor);
        c.anchorBindInv = anchorBind.Invert();

        for (auto& col : c.colliders)
        {
            col.a = sk.FindBoneIndex(col.boneA);
            col.b = sk.FindBoneIndex(col.boneB);
        }

        // 体の向き（モデル空間）: 上 = 鎖の先 → 根、後ろ = 上背 → 鎖の根（上に直交）
        Vector3 up = c.bindG[c.Node(center, 0)].Translation() - c.bindG[c.Node(center, rows - 1)].Translation();
        Vector3 back = c.bindG[c.Node(center, 0)].Translation() - anchorBind.Translation();
        up.Normalize();
        back -= up * back.Dot(up);
        if (back.LengthSquared() < 1e-8f) return false;
        back.Normalize();
        c.backModel = back;
        c.rightModel = up.Cross(back);
        c.rightModel.Normalize();

        BuildLinks(c);
        c.pos.clear();
        c.prev.clear();
        c.ready = true;
        std::cout << "[ClothChain] " << cols << " x " << rows << " nodes, " << c.links.size() << " links" << std::endl;
        return true;
    }

    // 今のポーズで「上背に付いたままの形」（モデル空間の global）を出す
    bool RestShape(const ClothChainComponent& c, const SkinnedAnimComponent& a, std::vector<Matrix>& G, std::vector<Matrix>& restG)
    {
        if (!SkinnedAnimSystem::BuildPose(a, G, false)) return false;
        if (c.anchor >= (int)G.size()) return false;
        const Matrix attach = c.anchorBindInv * G[c.anchor];   // 上背: スキニングした時 → 今（モデル空間）
        restG.resize(c.bindG.size());
        for (size_t i = 0; i < c.bindG.size(); ++i) restG[i] = c.bindG[i] * attach;
        return true;
    }

    // 揺らさない時（無効）：上背に付いたままの形を書く（元の重みは背骨に半分付いていたが、今は鎖だけなので）
    void WriteRest(const ClothChainComponent& c, SkinnedAnimComponent& a)
    {
        Scratch& w = Work();
        std::vector<Matrix>& G = w.G;
        std::vector<Matrix>& restG = w.restG;
        if (!RestShape(c, a, G, restG)) return;
        for (size_t i = 0; i < restG.size(); ++i)
            if (c.bones[i] >= 0) a.boneOverrides.push_back({ c.bones[i], restG[i] });
    }

    // ノード（モデル空間 m）→ 骨の global。各骨 = 目標の形の向きを、縦（自分 → 下の段）が合うよう最小の回転で回し、
    // 格子の時は横（左隣 → 右隣）も合うよう縦の軸周りに捻る
    void WriteBones(const ClothChainComponent& c, SkinnedAnimComponent& a, const std::vector<Matrix>& restG, const std::vector<Vector3>& m)
    {
        for (int x = 0; x < c.cols; ++x)
        {
            const int xl = (std::max)(x - 1, 0), xr = (std::min)(x + 1, c.cols - 1);
            Quaternion lastRot = Quaternion::Identity;
            Vector3 lastScale(1.0f, 1.0f, 1.0f);
            for (int k = 0; k + 1 < c.rows; ++k)
            {
                const int i = c.Node(x, k), j = c.Node(x, k + 1);
                Vector3 s, t;
                Quaternion r;
                Matrix rg = restG[i];
                rg.Decompose(s, r, t);
                Vector3 rd = restG[j].Translation() - restG[i].Translation();
                Vector3 sd = m[j] - m[i];
                if (rd.LengthSquared() > 1e-10f && sd.LengthSquared() > 1e-10f)
                {
                    rd.Normalize();
                    sd.Normalize();
                    const Quaternion swing = FromTo(rd, sd);
                    r = r * swing;
                    if (c.cols > 1)
                    {
                        Vector3 sr = (restG[c.Node(xr, k)].Translation() + restG[c.Node(xr, k + 1)].Translation())
                                   - (restG[c.Node(xl, k)].Translation() + restG[c.Node(xl, k + 1)].Translation());
                        Vector3 ss = (m[c.Node(xr, k)] + m[c.Node(xr, k + 1)]) - (m[c.Node(xl, k)] + m[c.Node(xl, k + 1)]);
                        sr = Vector3::Transform(sr, swing);
                        sr -= sd * sr.Dot(sd);
                        ss -= sd * ss.Dot(sd);
                        if (sr.LengthSquared() > 1e-10f && ss.LengthSquared() > 1e-10f)
                        {
                            sr.Normalize();
                            ss.Normalize();
                            const float ang = std::atan2(sr.Cross(ss).Dot(sd), sr.Dot(ss));
                            r = r * Quaternion::CreateFromAxisAngle(sd, ang);
                        }
                    }
                    r.Normalize();
                }
                lastRot = r;
                lastScale = s;
                if (c.bones[i] >= 0)
                    a.boneOverrides.push_back({ c.bones[i],
                        Matrix::CreateScale(s) * Matrix::CreateFromQuaternion(r) * Matrix::CreateTranslation(m[i]) });
            }
            const int e = c.Node(x, c.rows - 1);
            if (c.bones[e] >= 0)
                a.boneOverrides.push_back({ c.bones[e],
                    Matrix::CreateScale(lastScale) * Matrix::CreateFromQuaternion(lastRot) * Matrix::CreateTranslation(m[e]) });
        }
    }

    void Step(ClothChainComponent& c, SkinnedAnimComponent& a, const TransformComponent& tf, float dt,
        const std::function<float(float, float)>& groundHeight, const WindState& worldWind, float windTime,
        const LocalWind* localWind)
    {
        Scratch& w = Work();
        std::vector<Matrix>& G = w.G;
        std::vector<Matrix>& restG = w.restG;
        const auto poseT0 = std::chrono::steady_clock::now();
        if (!RestShape(c, a, G, restG)) return;

        c.poseMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - poseT0).count();
        const Matrix W = SkinnedAnimSystem::WorldMatrix(tf, a);
        const int N = c.cols * c.rows;
        const int center = c.CenterCol();

        // 目標の形 = 鎖を上背に付けたまま動かした物
        std::vector<Vector3>& target = w.target;
        target.resize(N);
        for (int i = 0; i < N; ++i) target[i] = Vector3::Transform(restG[i].Translation(), W);
        if (!c.links.empty() && c.links[0].rest < 0.0f)
            for (auto& l : c.links) l.rest = (target[l.b] - target[l.a]).Length();

        const Matrix attach = c.anchorBindInv * G[c.anchor];
        const Matrix bodyW = attach * W;
        Vector3 right = Vector3::TransformNormal(c.rightModel, bodyW);
        Vector3 back = Vector3::TransformNormal(c.backModel, bodyW);
        right.Normalize();
        back.Normalize();

        // 初回 / 瞬間移動（自動テストの配置換え等）は目標の形から始める
        const Vector3 root = target[c.Node(center, 0)];
        if ((int)c.pos.size() != N || (root - c.lastRoot).Length() > 2.0f)
        {
            c.pos = target;
            c.prev = target;
            c.lastRoot = root;
            c.prevH = c.substep;
        }

        std::vector<Cap>& caps = w.caps;
        caps.clear();
        for (const auto& col : c.colliders)
        {
            if (col.a < 0 || col.b < 0 || col.a >= (int)G.size() || col.b >= (int)G.size()) continue;
            caps.push_back(MakeCap(Vector3::Transform(G[col.a].Translation(), W),
                                   Vector3::Transform(G[col.b].Translation(), W), col.radius + c.margin));
        }
        const float feetY = W.Translation().y;   // モデルの原点 = 足元
        const bool sagittal = (c.cols < 2);

        // 拘束の種類 → 強さ / 縮んだ時だけか
        float stiff[5] = { 1.0f, c.bend, c.sideStretch, c.shear, c.sideBend };
        for (float& s : stiff) s = std::clamp(s, 0.0f, 1.0f);
        const bool compressOnly[5] = { false, true, false, false, true };

        const int steps = std::clamp((int)std::ceil(dt / (std::max)(c.substep, 1e-4f)), 1, 8);
        const float h = dt / (float)steps;
        const Vector3 rootMove = root - c.lastRoot;
        std::vector<float>& groundY = w.groundY;
        groundY.assign(N, 0.0f);
        std::vector<float>& rowPull = w.rowPull;   // 段ごとの「元の形へ戻す」割合（1 刻みあたり）
        rowPull.assign(c.rows, 0.0f);
        for (int k = 1; k < c.rows; ++k)
        {
            const float t = (float)k / (float)(c.rows - 1);
            const float st = std::clamp(c.stiffRoot + (c.stiffTip - c.stiffRoot) * t, 0.0f, 1.0f);
            rowPull[k] = 1.0f - std::pow(1.0f - st, h * 120.0f);
        }
        for (int s = 0; s < steps; ++s)
        {
            c.time += h;
            // 世界の風（草と同じ波を根の位置で引く）+ 局所の風源（竜巻・渦・砂旋・突風の渦と吹き上げ。2026-10-22）
            Vector3 wind = worldWind.Velocity(windTime, root.x, root.z) * c.windMul;
            if (localWind) wind += localWind->Air(root.x, root.z) * c.localWindMul;
            const float ratio = h / (std::max)(c.prevH, 1e-5f);
            const float keep = std::pow(1.0f - std::clamp(c.damping, 0.0f, 0.99f), h * 120.0f);

            // 積分（根の段は固定）
            for (int x = 0; x < c.cols; ++x)
                for (int k = 1; k < c.rows; ++k)
                {
                    const int i = c.Node(x, k);
                    const Vector3 vel = (c.pos[i] - c.prev[i]) / (std::max)(c.prevH, 1e-5f);
                    const Vector3 rel = wind - vel;
                    Vector3 acc = Vector3(0.0f, -c.gravity, 0.0f) + rel * c.drag;
                    if (!sagittal && c.flutter > 0.0f)
                    {
                        // はためき：根 → 先へ流れる波で面の向き（後ろ）と横に揺さぶる。強さは空気との相対の速さに比例
                        const float ph = c.flutterFreq * c.time - 1.1f * (float)k + 0.7f * (float)x;
                        acc += (back * std::sin(ph) + right * (0.5f * std::sin(1.3f * ph + 1.7f * (float)x)))
                            * (c.flutter * rel.Length() * c.drag);
                    }
                    const Vector3 next = c.pos[i] + (c.pos[i] - c.prev[i]) * (ratio * keep) + acc * (h * h);
                    c.prev[i] = c.pos[i];
                    c.pos[i] = next;
                }
            const float back01 = 1.0f - (float)(s + 1) / (float)steps;   // 根はこのフレームの移動を刻みで追う
            for (int x = 0; x < c.cols; ++x)
            {
                const int i = c.Node(x, 0);
                c.prev[i] = c.pos[i];
                c.pos[i] = target[i] - rootMove * back01;
            }

            // 元の形へ少し戻す（根の側ほど強い）
            for (int x = 0; x < c.cols; ++x)
                for (int k = 1; k < c.rows; ++k)
                {
                    const int i = c.Node(x, k);
                    c.pos[i] += (target[i] - c.pos[i]) * rowPull[k];
                }

            // 地面の高さはこの刻みの始めに 1 度だけ引く（反復の中ではほとんど動かない）
            for (int i = 0; i < N; ++i)
                groundY[i] = (groundHeight ? groundHeight(c.pos[i].x, c.pos[i].z) : feetY) + c.groundOffset;

            // 反復の中は Debug でも重くならないよう SimpleMath を通さずに書く（45 ノード x 195 本 x 11 カプセル。
            // 2026-10-23：SimpleMath のままだと Debug で 1 フレーム 1.4ms 掛かった）
            Vector3* P = c.pos.data();
            const ClothChainComponent::Link* L = c.links.data();
            const int nLinks = (int)c.links.size();
            const int rows = c.rows;
            for (int it = 0; it < c.iterations; ++it)
            {
                // 距離（根の段は動かさない）
                for (int li = 0; li < nLinks; ++li)
                {
                    const ClothChainComponent::Link& l = L[li];
                    const float st = stiff[l.kind];
                    if (st <= 0.0f) continue;
                    Vector3& pa = P[l.a];
                    Vector3& pb = P[l.b];
                    const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
                    const float len2 = dx * dx + dy * dy + dz * dz;
                    if (len2 < 1e-12f) continue;
                    const float len = std::sqrt(len2);
                    if (compressOnly[l.kind] && len >= l.rest) continue;
                    const bool fa = (l.a % rows) == 0, fb = (l.b % rows) == 0;
                    if (fa && fb) continue;
                    float k = (len - l.rest) / len * st;
                    if (fa) { pb.x -= dx * k; pb.y -= dy * k; pb.z -= dz * k; }
                    else if (fb) { pa.x += dx * k; pa.y += dy * k; pa.z += dz * k; }
                    else
                    {
                        k *= 0.5f;
                        pa.x += dx * k; pa.y += dy * k; pa.z += dz * k;
                        pb.x -= dx * k; pb.y -= dy * k; pb.z -= dz * k;
                    }
                }
                // 体と地面
                for (int i = 0; i < N; ++i)
                {
                    if ((i % rows) == 0) continue;
                    Vector3& p = P[i];
                    for (const auto& cap : caps)
                    {
                        if (sagittal) PushOut(p, cap.a, cap.b, cap.r, right, back, true);
                        else PushOutFast(p, cap, back);
                    }
                    if (p.y < groundY[i]) p.y = groundY[i];
                }
            }
            c.prevH = h;
        }
        c.lastRoot = root;

        // ---- 骨へ書き戻す（モデル空間）----
        const Matrix invW = W.Invert();
        std::vector<Vector3>& m = w.m;
        m.resize(N);
        for (int i = 0; i < N; ++i) m[i] = Vector3::Transform(c.pos[i], invW);
        WriteBones(c, a, restG, m);
    }
}

void ClothChainSystem::Update(Registry& reg, float dt)
{
    if (dt <= 0.0f) return;
    reg.CreateView<TransformComponent, SkinnedAnimComponent, ClothChainComponent>()
        .EachFrom<ClothChainComponent>([&](Entity, TransformComponent& tf, SkinnedAnimComponent& a, ClothChainComponent& c)
            {
                a.boneOverrides.clear();
                if (!a.model || c.failed) return;
                if (!c.ready && !Init(c, *a.model))
                {
                    c.failed = true;
                    std::cout << "[ClothChain] bones not found (anchor " << c.anchorBone << "), disabled" << std::endl;
                    return;
                }
                if (!c.enabled) { WriteRest(c, a); return; }
                const auto t0 = std::chrono::steady_clock::now();
                Step(c, a, tf, dt, groundHeight, wind, windTime, localWind);
                c.stepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
            });
}

ClothChainComponent ClothChainSystem::CCCape()
{
    ClothChainComponent c;
    c.anchorBone = kCCCapeAnchor;
    c.chainBones.assign(std::begin(kCCCapeChain), std::end(kCCCapeChain));
    c.columns = kCCCapeColumns;   // 列の骨は読み込み時に足してある（ResourceManager → SkinnedModel::SplitClothChain）
    // 半径は Blender で測った T ポーズの位置から（マントは背骨の線の 0.17〜0.21m 後ろに垂れている、幅 ±0.26m）
    c.colliders = {
        { "CC_Base_Hip",        "CC_Base_NeckTwist01", 0.13f },   // 胴
        { "CC_Base_L_Thigh",    "CC_Base_L_Upperarm",  0.10f },   // 胴の脇（端の列が体の横へ回り込んだ時）
        { "CC_Base_R_Thigh",    "CC_Base_R_Upperarm",  0.10f },
        { "CC_Base_L_Upperarm", "CC_Base_L_Forearm",   0.055f },  // 腕（走って後ろへ振った時に端を押す）
        { "CC_Base_R_Upperarm", "CC_Base_R_Forearm",   0.055f },
        { "CC_Base_L_Forearm",  "CC_Base_L_Hand",      0.045f },
        { "CC_Base_R_Forearm",  "CC_Base_R_Hand",      0.045f },
        { "CC_Base_L_Thigh",    "CC_Base_L_Calf",      0.085f },  // 腿
        { "CC_Base_R_Thigh",    "CC_Base_R_Calf",      0.085f },
        { "CC_Base_L_Calf",     "CC_Base_L_Foot",      0.065f },  // 脛
        { "CC_Base_R_Calf",     "CC_Base_R_Foot",      0.065f },
    };
    return c;
}

void ClothChainSystem::DrawImGui(ClothChainComponent& c)
{
    ImGui::Checkbox("Enabled##cape", &c.enabled);
    if (c.failed) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "bones not found");
    else if (c.ready) ImGui::Text("grid %d columns x %d rows, %d links, step %.3f ms (pose %.3f)", c.cols, c.rows, (int)c.links.size(), c.stepMs, c.poseMs);
    ImGui::SliderFloat("Gravity##cape", &c.gravity, 0.0f, 30.0f);
    ImGui::SliderFloat("Air Drag (1/s)", &c.drag, 0.0f, 5.0f);
    ImGui::SliderFloat("Damping##cape", &c.damping, 0.0f, 0.2f);
    ImGui::SliderFloat("Shape Stiffness Root", &c.stiffRoot, 0.0f, 1.0f);
    ImGui::SliderFloat("Shape Stiffness Tip", &c.stiffTip, 0.0f, 1.0f);
    ImGui::SliderFloat("Bend Stiffness", &c.bend, 0.0f, 1.0f);
    ImGui::SliderFloat("Side Stretch (grid)", &c.sideStretch, 0.0f, 1.0f);
    ImGui::SliderFloat("Shear (grid)", &c.shear, 0.0f, 1.0f);
    ImGui::SliderFloat("Side Bend (grid, low = folds)", &c.sideBend, 0.0f, 1.0f);
    ImGui::SliderFloat("Flutter (grid)", &c.flutter, 0.0f, 1.5f);
    ImGui::SliderFloat("Flutter Freq (rad/s)", &c.flutterFreq, 1.0f, 30.0f);
    ImGui::SliderFloat("World Wind x", &c.windMul, 0.0f, 3.0f);   // 向き・速さは Weather パネルの World Wind
    ImGui::SliderFloat("Local Wind x (tornado / vortex / gust)", &c.localWindMul, 0.0f, 2.0f);
    ImGui::SliderInt("Iterations##cape", &c.iterations, 1, 12);
    ImGui::SliderFloat("Margin (cloth thickness)", &c.margin, 0.0f, 0.1f);
    ImGui::SliderFloat("Ground Offset##cape", &c.groundOffset, 0.0f, 0.2f);
    for (auto& col : c.colliders)
    {
        ImGui::PushID(&col);
        ImGui::SliderFloat((col.boneA + " -> " + col.boneB).c_str(), &col.radius, 0.0f, 0.3f);
        ImGui::PopID();
    }
    if (ImGui::Button("Reset Cape")) { c.pos.clear(); c.prev.clear(); }
    if (c.ready && (int)c.pos.size() == c.cols * c.rows)
    {
        const Vector3 d = c.CenterTip() - c.CenterRoot();
        ImGui::Text("tip from root: %.2f m, drop %.2f m", d.Length(), -d.y);
        if (c.cols > 1)
        {
            const float w = (c.pos[c.Node(c.cols - 1, c.rows - 1)] - c.pos[c.Node(0, c.rows - 1)]).Length();
            ImGui::Text("hem width: %.2f m", w);
        }
    }
}
