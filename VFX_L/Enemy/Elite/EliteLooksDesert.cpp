// ============================================================
// EliteLooksDesert.cpp
// 砂漠の固定エリート「砂塵の騎兵」「炎砂の術師」の飾り（2026-10-25）。色と宣言は EliteLooksCommon.h。2026-10-26 に EliteLooks.cpp から分けた
// ============================================================
#include "Enemy/Elite/EliteLooksCommon.h"

using namespace LookBuilder;
using namespace EliteLooksDetail;

namespace EliteLooksDetail
{
    // ============================================================
    // 砂塵の騎兵
    // ============================================================
    std::shared_ptr<Model> RiderHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.08f * s.x;
        // 頭布（段を重ねる）
        b.Box(Vec3(lo.x - e, lo.y + 0.55f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 0.6f * e, hi.z + e), kSand, kSandDark);
        b.Box(Vec3(lo.x - 1.4f * e, lo.y + 0.62f * s.y, lo.z - 1.4f * e), Vec3(hi.x + 1.4f * e, lo.y + 0.80f * s.y, hi.z + 1.4f * e), kSandDark);
        b.Box(Vec3(lo.x - 0.6f * e, hi.y + 0.6f * e, lo.z - 0.6f * e), Vec3(hi.x + 0.6f * e, hi.y + 0.16f * s.y, hi.z + 0.6f * e), kSand);
        b.Box(Vec3(c.x - 0.07f * s.x, lo.y + 0.66f * s.y, hi.z + 1.4f * e), Vec3(c.x + 0.07f * s.x, lo.y + 0.78f * s.y, hi.z + 1.8f * e), kJewel);   // 額の宝石
        // 覆面（顔の下半分）と琥珀の目
        b.Box(Vec3(lo.x - 0.5f * e, lo.y - 0.3f * e, lo.z - 0.5f * e), Vec3(hi.x + 0.5f * e, lo.y + 0.44f * s.y, hi.z + 0.8f * e), kCloth, kCloth);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.2f * s.x;
            b.Box(Vec3(ex - 0.06f * s.x, lo.y + 0.48f * s.y, hi.z), Vec3(ex + 0.06f * s.x, lo.y + 0.54f * s.y, hi.z + 0.4f * e), kAmberGlow);
        }
        // 後ろへ垂れる布
        b.Beam(Vec3(c.x, lo.y + 0.75f * s.y, lo.z - e), Vec3(c.x + 0.05f * s.x, lo.y - 0.25f * s.y, lo.z - 0.35f * s.z), 0.40f * s.x, kSand, 0.30f * s.x);
        return b.Build(device);
    }
    std::shared_ptr<Model> RiderTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.06f * s.x;
        b.Box(Vec3(lo.x + 0.08f * s.x, lo.y + 0.30f * s.y, hi.z), Vec3(hi.x - 0.08f * s.x, hi.y - 0.08f * s.y, hi.z + 1.6f * e), kBronze, kBronze);   // 青銅の胸当て
        b.StripesY(Vec3(lo.x + 0.12f * s.x, lo.y + 0.34f * s.y, hi.z + 1.6f * e), Vec3(hi.x - 0.12f * s.x, hi.y - 0.12f * s.y, hi.z + 2.0f * e), 4, kGold, kBronze);
        b.Box(Vec3(lo.x - 1.2f * e, hi.y - 0.16f * s.y, lo.z - 1.2f * e), Vec3(hi.x + 1.2f * e, hi.y + 0.04f * s.y, hi.z + 1.2f * e), kSand, kSandDark);   // 肩の布
        b.Box(Vec3(lo.x - 0.6f * e, lo.y, lo.z - 0.6f * e), Vec3(hi.x + 0.6f * e, lo.y + 0.14f * s.y, hi.z + 0.6f * e), kCloth);   // 赤い腰布
        (void)c;
        return b.Build(device);
    }
    std::shared_ptr<Model> RiderLanceArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.3f, kBronze, kBronze);
        const Vec3 grip = f.hand + f.az * (0.12f * f.w);
        const Vec3 butt = grip - f.down * 0.5f;
        const Vec3 tip = grip + f.down * 1.15f;
        b.Beam(butt, tip, 0.045f, kWood);
        b.Beam(tip, tip + f.down * 0.28f, 0.07f, kSteel, 0.004f);   // 穂先
        // 穂先の下の赤い小旗
        const Vec3 fl = tip - f.down * 0.05f;
        const Vec3 q[4] = { fl, fl - f.down * 0.16f, fl - f.down * 0.16f + f.az * 0.22f, fl - f.down * 0.02f + f.az * 0.26f };
        b.Plate(q, f.ax, 0.01f, kCloth);
        return b.Build(device);
    }
    std::shared_ptr<Model> RiderShieldArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const float out = (pivot.x >= 0.0f) ? 1.0f : -1.0f;
        const Vec3 cen = f.hand - f.down * (0.7f * f.w) + Vec3(out * 0.7f * f.w, 0.0f, 0.0f);
        const Vec3 ox(1.0f, 0.0f, 0.0f);
        b.OBox(cen, ox, f.ay, f.az, Vec3(0.04f, 0.22f, 0.22f), kBronze, kBronze);
        b.OBox(cen, ox, f.ay, f.az, Vec3(0.05f, 0.16f, 0.16f), kSandDark, kSandDark);
        b.OBox(cen + ox * (out * 0.03f), ox, f.ay, f.az, Vec3(0.04f, 0.06f, 0.06f), kGold, kGold);
        return b.Build(device);
    }

    // ============================================================
    // 炎砂の術師
    // ============================================================
    std::shared_ptr<Model> FlameHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.08f * s.x;
        b.Box(Vec3(lo.x - 1.8f * e, hi.y - 0.10f * s.y, lo.z - 1.8f * e), Vec3(hi.x + 1.8f * e, hi.y + 0.02f * s.y, hi.z + 1.8f * e), kBurgundy);   // つば
        b.Box(Vec3(lo.x - e, lo.y + 0.30f * s.y, lo.z - e), Vec3(hi.x + e, hi.y, hi.z + e), kBurgundyLight, kBurgundy);
        b.Box(Vec3(lo.x - 1.05f * e, hi.y - 0.18f * s.y, lo.z - 1.05f * e), Vec3(hi.x + 1.05f * e, hi.y - 0.10f * s.y, hi.z + 1.05f * e), kGold);   // 金の帯
        // とんがりを 4 段、少しずつ後ろへ曲げる
        float w = 0.42f * s.x;
        Vec3 base(c.x, hi.y + 0.02f * s.y, c.z);
        for (int k = 0; k < 4; ++k)
        {
            const Vec3 top = base + Vec3(0.0f, 0.16f * s.y, -0.05f * s.z * (float)k);
            b.Box(Vec3(top.x - w, base.y, top.z - w), Vec3(top.x + w, top.y, top.z + w), kBurgundyLight, kBurgundy);
            base = top;
            w *= 0.68f;
        }
        b.Box(Vec3(lo.x + 0.10f * s.x, lo.y + 0.20f * s.y, hi.z + e), Vec3(hi.x - 0.10f * s.x, lo.y + 0.62f * s.y, hi.z + 1.3f * e), kVoid);   // 顔の影
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.17f * s.x;
            b.Box(Vec3(ex - 0.05f * s.x, lo.y + 0.44f * s.y, hi.z + 1.3f * e), Vec3(ex + 0.05f * s.x, lo.y + 0.51f * s.y, hi.z + 1.5f * e), kFlame);
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> FlameTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.06f * s.x;
        b.Box(Vec3(lo.x - e, hi.y - 0.12f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 0.05f * s.y, hi.z + e), kGold, kBurgundy);   // 金の襟
        b.Box(Vec3(lo.x - 0.6f * e, lo.y + 0.06f * s.y, lo.z - 0.6f * e), Vec3(hi.x + 0.6f * e, lo.y + 0.20f * s.y, hi.z + 0.6f * e), kGold);   // 飾り帯
        b.Box(Vec3(c.x - 0.09f * s.x, lo.y + 0.50f * s.y, hi.z + 0.5f * e), Vec3(c.x + 0.09f * s.x, lo.y + 0.68f * s.y, hi.z + 1.8f * e), kFlame);   // 熾火の宝石
        b.Box(Vec3(c.x - 0.13f * s.x, lo.y + 0.46f * s.y, hi.z), Vec3(c.x + 0.13f * s.x, lo.y + 0.72f * s.y, hi.z + 1.0f * e), kGold);
        // 前に垂れる布（裾）
        b.Box(Vec3(c.x - 0.18f * s.x, lo.y - 0.45f * s.y, hi.z), Vec3(c.x + 0.18f * s.x, lo.y + 0.10f * s.y, hi.z + 0.8f * e), kBurgundyLight, kBurgundy);
        return b.Build(device);
    }
    std::shared_ptr<Model> FlameStaffArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const Vec3 grip = f.hand + f.az * (0.12f * f.w);
        const Vec3 foot = grip + f.down * 0.55f;
        const Vec3 head = grip - f.down * 0.85f;
        b.Beam(foot, head, 0.045f, kWood);
        // 先の金の爪 3 本と、爪に抱えた炎（段を重ねた HDR の箱）
        for (int k = 0; k < 3; ++k)
        {
            const float a = 2.0944f * (float)k;
            const Vec3 sideV = f.ax * std::cos(a) + f.az * std::sin(a);
            b.Beam(head, head - f.down * 0.14f + sideV * 0.07f, 0.025f, kGold, 0.008f);
        }
        b.OBox(head - f.down * 0.08f, f.ax, f.ay, f.az, Vec3(0.05f, 0.05f, 0.05f), kFlameHot, kFlameHot);
        b.OBox(head - f.down * 0.15f, f.ax, f.ay, f.az, Vec3(0.035f, 0.05f, 0.035f), kFlame, kFlame);
        b.OBox(head - f.down * 0.21f, f.ax, f.ay, f.az, Vec3(0.02f, 0.035f, 0.02f), kFlame, kFlame);
        return b.Build(device);
    }
    std::shared_ptr<Model> FlameArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.12f, kGold, kGold);
        return b.Build(device);
    }

}
