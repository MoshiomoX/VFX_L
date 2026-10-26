// ============================================================
// EliteLooksRuins.cpp
// 遺跡の固定エリート「影の剣士」「呪いの司書」の飾り（2026-10-25。司書の周りを回る本も）。色と宣言は EliteLooksCommon.h。
// 2026-10-26 に EliteLooks.cpp から分けた
// ============================================================
#include "Enemy/Elite/EliteLooksCommon.h"

using namespace LookBuilder;
using namespace EliteLooksDetail;

namespace EliteLooksDetail
{
    // ============================================================
    // 影の剣士
    // ============================================================
    std::shared_ptr<Model> ShadeHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.07f * s.x;
        b.Box(Vec3(lo.x - e, lo.y - 0.3f * e, lo.z - e), Vec3(hi.x + e, hi.y + e, hi.z + e), kShadowLight, kShadow);   // 覆面
        b.Box(Vec3(lo.x - 0.3f * e, lo.y + 0.48f * s.y, hi.z + e), Vec3(hi.x + 0.3f * e, lo.y + 0.56f * s.y, hi.z + 1.6f * e), kViolet);   // 一筋の目
        // 頭の後ろで結んだ布の端（2 本、後ろ下へ）
        for (int sg = -1; sg <= 1; sg += 2)
            b.Beam(Vec3(c.x + (float)sg * 0.1f * s.x, lo.y + 0.55f * s.y, lo.z - e),
                Vec3(c.x + (float)sg * 0.25f * s.x, lo.y + 0.10f * s.y, lo.z - 0.65f * s.z), 0.12f * s.x, kShadow, 0.05f * s.x);
        return b.Build(device);
    }
    std::shared_ptr<Model> ShadeTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.07f * s.x;
        for (int sg = -1; sg <= 1; sg += 2)   // 肩当て
        {
            const float x0 = (sg < 0) ? lo.x - 1.6f * e : hi.x - 0.25f * s.x;
            const float x1 = (sg < 0) ? lo.x + 0.25f * s.x : hi.x + 1.6f * e;
            b.Box(Vec3(x0, hi.y - 0.18f * s.y, lo.z - 1.2f * e), Vec3(x1, hi.y + 0.06f * s.y, hi.z + 1.2f * e), kShadowLight, kShadow);
        }
        b.Box(Vec3(lo.x - 0.6f * e, lo.y + 0.04f * s.y, lo.z - 0.6f * e), Vec3(hi.x + 0.6f * e, lo.y + 0.14f * s.y, hi.z + 0.6f * e), kViolet);   // 紫の帯
        // 首から後ろへなびく襟巻き（3 節）
        Vec3 p(c.x, hi.y, lo.z - e);
        for (int k = 0; k < 3; ++k)
        {
            const Vec3 q = p + Vec3(0.05f * s.x * (float)((k & 1) ? -1 : 1), -0.22f * s.y, -0.35f * s.z);
            b.Beam(p, q, 0.30f * s.x, kShadowLight, 0.24f * s.x);
            p = q;
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> ShadeBladeArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const Vec3 grip = f.hand + f.az * (0.12f * f.w);
        b.Beam(grip - f.down * 0.10f, grip + f.down * 0.08f, 0.04f, kShadow);                         // 柄
        b.OBox(grip + f.down * 0.09f, f.ax, f.ay, f.az, Vec3(0.06f, 0.012f, 0.06f), kGold, kGold);   // 鍔
        const Vec3 b0 = grip + f.down * 0.10f, b1 = grip + f.down * 1.0f + f.az * 0.06f;   // 少し反った刃
        b.Beam(b0, b1, 0.030f, kSteel, 0.008f);
        b.Beam(b0 + f.az * 0.018f, b1 + f.az * 0.018f, 0.010f, kViolet, 0.004f);   // 紫に光る刃文
        return b.Build(device);
    }
    std::shared_ptr<Model> ShadeArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.35f, kShadowLight, kShadow);
        return b.Build(device);
    }

    // ============================================================
    // 呪いの司書
    // ============================================================
    std::shared_ptr<Model> LibrarianHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.09f * s.x;
        b.Box(Vec3(lo.x - e, lo.y - 0.4f * e, lo.z - 1.3f * e), Vec3(hi.x + e, hi.y + 1.2f * e, hi.z + 0.8f * e), kNavyLight, kNavy);   // 深い頭巾
        b.Box(Vec3(lo.x - 1.2f * e, lo.y - 0.4f * e, hi.z + 0.4f * e), Vec3(hi.x + 1.2f * e, lo.y + 0.08f * s.y, hi.z + 1.0f * e), kGold);   // 縁取り
        b.Box(Vec3(lo.x + 0.10f * s.x, lo.y + 0.10f * s.y, hi.z + 0.8f * e), Vec3(hi.x - 0.10f * s.x, lo.y + 0.75f * s.y, hi.z + 1.0f * e), kVoid);
        for (int sg = -1; sg <= 1; sg += 2)   // 光る眼鏡（金の縁 + 淡い紫の玉）
        {
            const float ex = c.x + (float)sg * 0.17f * s.x;
            b.Box(Vec3(ex - 0.09f * s.x, lo.y + 0.42f * s.y, hi.z + 1.0f * e), Vec3(ex + 0.09f * s.x, lo.y + 0.56f * s.y, hi.z + 1.2f * e), kGold);
            b.Box(Vec3(ex - 0.06f * s.x, lo.y + 0.44f * s.y, hi.z + 1.2f * e), Vec3(ex + 0.06f * s.x, lo.y + 0.54f * s.y, hi.z + 1.4f * e), kCurseGlow);
        }
        b.Beam(Vec3(c.x, hi.y + e, lo.z), Vec3(c.x, hi.y + 0.12f * s.y, lo.z - 0.4f * s.z), 0.36f * s.x, kNavy, 0.06f * s.x);   // 後ろへ垂れる頭巾の先
        return b.Build(device);
    }
    std::shared_ptr<Model> LibrarianTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.06f * s.x;
        b.Box(Vec3(lo.x - e, hi.y - 0.14f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 0.05f * s.y, hi.z + e), kNavyLight, kNavy);   // 襟
        b.Box(Vec3(c.x - 0.04f * s.x, lo.y, hi.z), Vec3(c.x + 0.04f * s.x, hi.y - 0.10f * s.y, hi.z + 1.2f * e), kGold);   // 前の金の筋
        // 胸から下がる鎖と小さな本
        b.Chain(Vec3(lo.x + 0.1f * s.x, hi.y - 0.10f * s.y, hi.z + 1.2f * e), Vec3(c.x + 0.12f * s.x, lo.y + 0.30f * s.y, hi.z + 1.4f * e), 0.07f, 0.012f, kGold);
        const Vec3 bk(c.x + 0.15f * s.x, lo.y + 0.18f * s.y, hi.z + 1.5f * e);
        b.Box(bk - Vec3(0.10f * s.x, 0.12f * s.y, 0.04f * s.z), bk + Vec3(0.10f * s.x, 0.12f * s.y, 0.04f * s.z), kBookRed, kParchment);
        // 長い衣の裾
        b.Box(Vec3(lo.x - 0.8f * e, lo.y - 0.55f * s.y, lo.z - 0.8f * e), Vec3(hi.x + 0.8f * e, lo.y + 0.05f * s.y, hi.z + 0.8f * e), kNavyLight, kNavy);
        return b.Build(device);
    }
    std::shared_ptr<Model> LibrarianBookArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        // 手の前に開いた本（2 枚の板を少し V に）+ 頁から立つ紫の光
        const Vec3 cen = f.hand + f.az * (0.18f + 0.5f * f.w);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const Vec3 side = Vec3(1.0f, 0.0f, 0.0f) * ((float)sg * 0.11f);
            b.OBox(cen + side + Vec3(0.0f, 0.02f * std::fabs((float)sg), 0.0f), Vec3(1.0f, 0.0f, 0.0f), f.az, f.ay, Vec3(0.11f, 0.07f, 0.012f), kBookGreen, kBookGreen);
            b.OBox(cen + side + Vec3(0.0f, 0.03f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), f.az, f.ay, Vec3(0.10f, 0.065f, 0.01f), kParchment, kParchment);
        }
        b.OBox(cen + Vec3(0.0f, 0.10f, 0.0f), f.ax, f.ay, f.az, Vec3(0.03f, 0.05f, 0.03f), kCurseGlow, kCurseGlow);
        return b.Build(device);
    }
    std::shared_ptr<Model> LibrarianArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.25f, kNavyLight, kNavy);
        return b.Build(device);
    }
    // 体の周りを回る 3 冊の本（kDecoOrbit）
    std::shared_ptr<Model> OrbitBooks(ID3D11Device* device, const Model& model)
    {
        Builder b;
        const float footY = model.GetBoundsMin().y;
        const float height = model.GetBoundsMax().y - footY;
        const Vec4 covers[3] = { kBookRed, kBookGreen, kNavyLight };
        for (int k = 0; k < 3; ++k)
        {
            const float a = 2.0944f * (float)k + 0.4f;
            const Vec3 c(std::cos(a) * 0.85f, footY + height * (0.62f + 0.12f * (float)k), std::sin(a) * 0.85f);
            const Vec3 t(-std::sin(a), 0.0f, std::cos(a));   // 回る向き（本の背が外）
            const Vec3 out(std::cos(a), 0.0f, std::sin(a));
            b.OBox(c, t, Vec3(0.0f, 1.0f, 0.0f), out, Vec3(0.11f, 0.14f, 0.04f), covers[k], covers[k]);
            b.OBox(c + out * 0.005f, t, Vec3(0.0f, 1.0f, 0.0f), out, Vec3(0.10f, 0.13f, 0.035f), kParchment, kParchment);
            b.OBox(c + Vec3(0.0f, 0.17f, 0.0f), t, Vec3(0.0f, 1.0f, 0.0f), out, Vec3(0.02f, 0.02f, 0.02f), kCurseGlow, kCurseGlow);
        }
        return b.Build(device);
    }
}
