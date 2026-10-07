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

#include <imgui.h>

using namespace DirectX::SimpleMath;

namespace
{
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

    // 線分 a-b・半径 r のカプセルから p を押し出す。体の左右（right）の成分は無視して測る
    // （鎖は布の中心線なので、横にずれた脚でも布の幅のどこかには当たる）
    void PushOutSagittal(Vector3& p, const Vector3& a, const Vector3& b, float r,
        const Vector3& right, const Vector3& back)
    {
        auto flat = [&](const Vector3& v) { return v - right * v.Dot(right); };
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

    bool Init(ClothChainComponent& c, const SkinnedModel& model)
    {
        const Skeleton& sk = model.GetSkeleton();
        c.anchor = sk.FindBoneIndex(c.anchorBone);
        c.bones.clear();
        c.bindG.clear();
        for (const auto& name : c.chainBones) c.bones.push_back(sk.FindBoneIndex(name));
        const size_t n = c.bones.size();
        if (c.anchor < 0 || n < 3) return false;
        for (size_t i = 0; i + 1 < n; ++i)
            if (c.bones[i] < 0) return false;

        for (int b : c.bones) c.bindG.push_back(b >= 0 ? model.GetSkinBindGlobal(b) : Matrix::Identity);
        if (c.bones.back() < 0)
        {
            // 終点が無い: 最後の骨を前の骨と同じ向き・長さで延ばす
            const Vector3 p1 = c.bindG[n - 2].Translation(), p0 = c.bindG[n - 3].Translation();
            Matrix m = c.bindG[n - 2];
            m.Translation(p1 + (p1 - p0));
            c.bindG.back() = m;
        }
        const Matrix anchorBind = model.GetSkinBindGlobal(c.anchor);
        c.anchorBindInv = anchorBind.Invert();

        for (auto& col : c.colliders)
        {
            col.a = sk.FindBoneIndex(col.boneA);
            col.b = sk.FindBoneIndex(col.boneB);
        }

        // 体の向き（モデル空間）: 上 = 鎖の先 → 根、後ろ = 上背 → 鎖の根（上に直交）
        Vector3 up = c.bindG.front().Translation() - c.bindG.back().Translation();
        Vector3 back = c.bindG.front().Translation() - anchorBind.Translation();
        up.Normalize();
        back -= up * back.Dot(up);
        if (back.LengthSquared() < 1e-8f) return false;
        back.Normalize();
        c.backModel = back;
        c.rightModel = up.Cross(back);
        c.rightModel.Normalize();

        c.restLen.clear();
        c.restLen2.clear();
        c.pos.clear();
        c.prev.clear();
        c.ready = true;
        return true;
    }

    void Step(ClothChainComponent& c, SkinnedAnimComponent& a, const TransformComponent& tf, float dt,
        const std::function<float(float, float)>& groundHeight)
    {
        std::vector<Matrix> G;
        if (!SkinnedAnimSystem::BuildPose(a, G, false)) return;
        if (c.anchor >= (int)G.size()) return;

        const Matrix W = SkinnedAnimSystem::WorldMatrix(tf, a);
        const Matrix attach = c.anchorBindInv * G[c.anchor];   // 上背: スキニングした時 → 今（モデル空間）
        const size_t n = c.bindG.size();

        // 目標の形 = 鎖を上背に付けたまま動かした物
        std::vector<Matrix> restG(n);
        std::vector<Vector3> target(n);
        for (size_t k = 0; k < n; ++k)
        {
            restG[k] = c.bindG[k] * attach;
            target[k] = Vector3::Transform(restG[k].Translation(), W);
        }
        if (c.restLen.size() != n - 1)
        {
            c.restLen.resize(n - 1);
            c.restLen2.resize(n - 2);
            for (size_t k = 0; k + 1 < n; ++k) c.restLen[k] = (target[k + 1] - target[k]).Length();
            for (size_t k = 0; k + 2 < n; ++k) c.restLen2[k] = (target[k + 2] - target[k]).Length();
        }

        const Matrix bodyW = attach * W;
        Vector3 right = Vector3::TransformNormal(c.rightModel, bodyW);
        Vector3 back = Vector3::TransformNormal(c.backModel, bodyW);
        right.Normalize();
        back.Normalize();

        // 初回 / 瞬間移動（自動テストの配置換え等）は目標の形から始める
        const Vector3 root = target[0];
        if (c.pos.size() != n || (root - c.lastRoot).Length() > 2.0f)
        {
            c.pos = target;
            c.prev = target;
            c.lastRoot = root;
            c.prevH = c.substep;
        }

        struct Cap { Vector3 a, b; float r; };
        std::vector<Cap> caps;
        for (const auto& col : c.colliders)
        {
            if (col.a < 0 || col.b < 0 || col.a >= (int)G.size() || col.b >= (int)G.size()) continue;
            caps.push_back({ Vector3::Transform(G[col.a].Translation(), W),
                             Vector3::Transform(G[col.b].Translation(), W), col.radius + c.margin });
        }
        const float feetY = W.Translation().y;   // モデルの原点 = 足元

        Vector3 windDir = c.windDir;
        windDir.y = 0.0f;
        if (windDir.LengthSquared() > 1e-8f) windDir.Normalize();

        const int steps = std::clamp((int)std::ceil(dt / (std::max)(c.substep, 1e-4f)), 1, 8);
        const float h = dt / (float)steps;
        const Vector3 rootFrom = c.lastRoot;
        for (int s = 0; s < steps; ++s)
        {
            c.time += h;
            const float gust = 0.5f * std::sin(c.time * 1.7f) + 0.5f * std::sin(c.time * 0.63f + 1.3f);
            const Vector3 wind = windDir * (c.windSpeed + c.windGust * gust);
            const float ratio = h / (std::max)(c.prevH, 1e-5f);
            const float keep = std::pow(1.0f - std::clamp(c.damping, 0.0f, 0.99f), h * 120.0f);

            // 積分（根は固定）
            for (size_t k = 1; k < n; ++k)
            {
                const Vector3 vel = (c.pos[k] - c.prev[k]) / (std::max)(c.prevH, 1e-5f);
                const Vector3 acc = Vector3(0.0f, -c.gravity, 0.0f) + (wind - vel) * c.drag;
                const Vector3 next = c.pos[k] + (c.pos[k] - c.prev[k]) * (ratio * keep) + acc * (h * h);
                c.prev[k] = c.pos[k];
                c.pos[k] = next;
            }
            c.prev[0] = c.pos[0];
            c.pos[0] = Vector3::Lerp(rootFrom, root, (float)(s + 1) / (float)steps);

            // 元の形へ少し戻す（根の側ほど強い）
            for (size_t k = 1; k < n; ++k)
            {
                const float t = (float)k / (float)(n - 1);
                const float st = std::clamp(c.stiffRoot + (c.stiffTip - c.stiffRoot) * t, 0.0f, 1.0f);
                const float f = 1.0f - std::pow(1.0f - st, h * 120.0f);
                c.pos[k] += (target[k] - c.pos[k]) * f;
            }

            for (int it = 0; it < c.iterations; ++it)
            {
                // 隣の距離（根は動かさない）
                for (size_t k = 0; k + 1 < n; ++k)
                {
                    const Vector3 d = c.pos[k + 1] - c.pos[k];
                    const float len = d.Length();
                    if (len < 1e-6f) continue;
                    const float diff = (len - c.restLen[k]) / len;
                    if (k == 0) c.pos[1] -= d * diff;
                    else { c.pos[k] += d * (0.5f * diff); c.pos[k + 1] -= d * (0.5f * diff); }
                }
                // 1 つ飛ばし（縮んだ = 折れた時だけ広げる）
                for (size_t k = 0; k + 2 < n; ++k)
                {
                    const Vector3 d = c.pos[k + 2] - c.pos[k];
                    const float len = d.Length();
                    if (len < 1e-6f || len >= c.restLen2[k]) continue;
                    const float diff = (len - c.restLen2[k]) / len * c.bend;
                    if (k == 0) c.pos[2] -= d * diff;
                    else { c.pos[k] += d * (0.5f * diff); c.pos[k + 2] -= d * (0.5f * diff); }
                }
                // 体と地面
                for (size_t k = 1; k < n; ++k)
                {
                    for (const auto& cap : caps) PushOutSagittal(c.pos[k], cap.a, cap.b, cap.r, right, back);
                    const float gy = (groundHeight ? groundHeight(c.pos[k].x, c.pos[k].z) : feetY) + c.groundOffset;
                    if (c.pos[k].y < gy) c.pos[k].y = gy;
                }
            }
            c.prevH = h;
        }
        c.lastRoot = root;

        // ---- 骨へ書き戻す（モデル空間）----
        const Matrix invW = W.Invert();
        std::vector<Vector3> m(n);
        for (size_t k = 0; k < n; ++k) m[k] = Vector3::Transform(c.pos[k], invW);

        Quaternion lastRot = Quaternion::Identity;
        Vector3 lastScale(1.0f, 1.0f, 1.0f);
        for (size_t j = 0; j + 1 < n; ++j)
        {
            Vector3 rd = restG[j + 1].Translation() - restG[j].Translation();
            Vector3 sd = m[j + 1] - m[j];
            Vector3 s, t;
            Quaternion r;
            Matrix rg = restG[j];
            rg.Decompose(s, r, t);
            if (rd.LengthSquared() > 1e-10f && sd.LengthSquared() > 1e-10f)
            {
                rd.Normalize();
                sd.Normalize();
                r = r * FromTo(rd, sd);
                r.Normalize();
            }
            lastRot = r;
            lastScale = s;
            a.boneOverrides.push_back({ c.bones[j],
                Matrix::CreateScale(s) * Matrix::CreateFromQuaternion(r) * Matrix::CreateTranslation(m[j]) });
        }
        if (c.bones.back() >= 0)
            a.boneOverrides.push_back({ c.bones.back(),
                Matrix::CreateScale(lastScale) * Matrix::CreateFromQuaternion(lastRot) * Matrix::CreateTranslation(m[n - 1]) });
    }
}

void ClothChainSystem::Update(Registry& reg, float dt)
{
    if (dt <= 0.0f) return;
    reg.CreateView<TransformComponent, SkinnedAnimComponent, ClothChainComponent>()
        .EachFrom<ClothChainComponent>([&](Entity, TransformComponent& tf, SkinnedAnimComponent& a, ClothChainComponent& c)
            {
                a.boneOverrides.clear();
                if (!c.enabled || !a.model || c.failed) return;
                if (!c.ready && !Init(c, *a.model))
                {
                    c.failed = true;
                    std::cout << "[ClothChain] bones not found (anchor " << c.anchorBone << "), disabled" << std::endl;
                    return;
                }
                Step(c, a, tf, dt, groundHeight);
            });
}

ClothChainComponent ClothChainSystem::CCCape()
{
    ClothChainComponent c;
    c.anchorBone = "CC_Base_Spine02";
    c.chainBones = { "Cloak1", "Cloak2", "Cloak3", "Cloak4", "Cloak5", "Cloak6", "Cloak7", "Cloak8", "Cloak8_end" };
    // 半径は Blender で測った T ポーズの位置から（マントは背骨の線の 0.17〜0.21m 後ろに垂れている）
    c.colliders = {
        { "CC_Base_Hip",     "CC_Base_NeckTwist01", 0.13f },   // 胴
        { "CC_Base_L_Thigh", "CC_Base_L_Calf",      0.085f },  // 腿
        { "CC_Base_R_Thigh", "CC_Base_R_Calf",      0.085f },
        { "CC_Base_L_Calf",  "CC_Base_L_Foot",      0.065f },  // 脛
        { "CC_Base_R_Calf",  "CC_Base_R_Foot",      0.065f },
    };
    return c;
}

void ClothChainSystem::DrawImGui(ClothChainComponent& c)
{
    ImGui::Checkbox("Enabled##cape", &c.enabled);
    if (c.failed) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "bones not found");
    ImGui::SliderFloat("Gravity##cape", &c.gravity, 0.0f, 30.0f);
    ImGui::SliderFloat("Air Drag (1/s)", &c.drag, 0.0f, 5.0f);
    ImGui::SliderFloat("Damping##cape", &c.damping, 0.0f, 0.2f);
    ImGui::SliderFloat("Shape Stiffness Root", &c.stiffRoot, 0.0f, 1.0f);
    ImGui::SliderFloat("Shape Stiffness Tip", &c.stiffTip, 0.0f, 1.0f);
    ImGui::SliderFloat("Bend Stiffness", &c.bend, 0.0f, 1.0f);
    ImGui::SliderFloat("Wind Speed", &c.windSpeed, 0.0f, 8.0f);
    ImGui::SliderFloat("Wind Gust", &c.windGust, 0.0f, 5.0f);
    ImGui::DragFloat3("Wind Dir", &c.windDir.x, 0.01f, -1.0f, 1.0f);
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
    if (c.pos.size() >= 2)
    {
        const Vector3 d = c.pos.back() - c.pos.front();
        ImGui::Text("tip from root: %.2f m, drop %.2f m", d.Length(), -d.y);
    }
}
