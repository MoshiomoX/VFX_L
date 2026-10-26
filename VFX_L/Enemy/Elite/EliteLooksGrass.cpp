// ============================================================
// EliteLooksGrass.cpp
// 草原の固定エリート「大斧の戦鬼」「森の弓手」の飾り（2026-10-25）。色と宣言は EliteLooksCommon.h。2026-10-26 に EliteLooks.cpp から分けた
// ============================================================
#include "Enemy/Elite/EliteLooksCommon.h"

using namespace LookBuilder;
using namespace EliteLooksDetail;

namespace EliteLooksDetail
{
    // ============================================================
    // 大斧の戦鬼
    // ============================================================
    std::shared_ptr<Model> AxeHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.08f * s.x;
        b.Box(Vec3(lo.x - e, lo.y + 0.30f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + e, hi.z + e), kIronLight, kIron);   // 鉄兜（顔の上半分）
        b.Box(Vec3(lo.x - 1.5f * e, lo.y + 0.28f * s.y, lo.z - 1.5f * e), Vec3(hi.x + 1.5f * e, lo.y + 0.40f * s.y, hi.z + 1.5f * e), kBronze);   // 青銅の縁
        b.Box(Vec3(c.x - 0.05f * s.x, lo.y + 0.15f * s.y, hi.z + e), Vec3(c.x + 0.05f * s.x, hi.y, hi.z + 1.6f * e), kBronze);   // 鼻当て
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.22f * s.x;
            b.Box(Vec3(ex - 0.10f * s.x, lo.y + 0.46f * s.y, hi.z + e), Vec3(ex + 0.10f * s.x, lo.y + 0.56f * s.y, hi.z + 1.3f * e), kVoid);
            b.Box(Vec3(ex - 0.05f * s.x, lo.y + 0.48f * s.y, hi.z + 1.3f * e), Vec3(ex + 0.05f * s.x, lo.y + 0.54f * s.y, hi.z + 1.5f * e), kRedEye);
            // 角：横へ出て上へ反り、先は細く
            const Vec3 r0(c.x + (float)sg * (0.5f * s.x + e), lo.y + 0.75f * s.y, c.z);
            const Vec3 r1 = r0 + Vec3((float)sg * 0.28f * s.x, 0.18f * s.y, 0.05f * s.z);
            const Vec3 r2 = r1 + Vec3((float)sg * 0.12f * s.x, 0.38f * s.y, 0.10f * s.z);
            b.Beam(r0, r1, 0.16f * s.x, kBone, 0.12f * s.x);
            b.Beam(r1, r2, 0.12f * s.x, kBone, 0.02f * s.x);
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> AxeTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.06f * s.x;
        std::mt19937 rng(25101u);
        // 毛皮の肩掛け（不揃いの塊を肩に並べる）
        for (int k = 0; k < 6; ++k)
        {
            const float x = lo.x - e + (s.x + 2.0f * e) * ((float)k + 0.5f) / 6.0f;
            b.Box(Vec3(x - 0.12f * s.x, hi.y - 0.18f * s.y, lo.z - 1.5f * e), Vec3(x + 0.12f * s.x, hi.y + 0.10f * s.y, hi.z + 1.5f * e),
                (k & 1) ? kFurLight : kFur, kFur, 0.03f * s.x, &rng);
        }
        // 胸の締め紐（斜め 2 本）と髑髏の留め金
        b.Beam(Vec3(lo.x, hi.y - 0.12f * s.y, hi.z + e), Vec3(hi.x, lo.y + 0.25f * s.y, hi.z + e), 0.10f * s.x, kLeather);
        b.Beam(Vec3(hi.x, hi.y - 0.12f * s.y, hi.z + e), Vec3(lo.x, lo.y + 0.25f * s.y, hi.z + e), 0.10f * s.x, kLeather);
        b.Box(Vec3(lo.x - 0.5f * e, lo.y, lo.z - 0.5f * e), Vec3(hi.x + 0.5f * e, lo.y + 0.14f * s.y, hi.z + 0.5f * e), kLeather);   // 腰帯
        b.Box(Vec3(c.x - 0.10f * s.x, lo.y + 0.01f * s.y, hi.z + 0.5f * e), Vec3(c.x + 0.10f * s.x, lo.y + 0.17f * s.y, hi.z + 1.6f * e), kBone);
        return b.Build(device);
    }
    std::shared_ptr<Model> AxeWeaponArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.35f, kIronLight, kIron);
        // 柄は腕に沿って下へ長く、先に両刃（前後へ張り出す）
        const Vec3 grip = f.hand + f.az * (0.15f * f.w);
        const Vec3 top = grip - f.down * 0.18f;
        const Vec3 end = grip + f.down * 0.95f;
        b.Beam(top, end, 0.05f, kWood);
        b.Beam(end, end + f.down * 0.06f, 0.07f, kIronLight);
        const Vec3 headC = grip + f.down * 0.82f;
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const Vec3 bc = headC + f.az * ((float)sg * 0.16f);
            b.OBox(bc, f.ax, f.ay, f.az, Vec3(0.022f, 0.15f, 0.12f), kSteel, kIronLight);
            b.OBox(bc + f.az * ((float)sg * 0.12f), f.ax, f.ay, f.az, Vec3(0.018f, 0.19f, 0.02f), kSteel, kSteel);   // 刃先（少し長い）
        }
        b.OBox(headC, f.ax, f.ay, f.az, Vec3(0.05f, 0.08f, 0.06f), kIron, kIron);
        return b.Build(device);
    }
    std::shared_ptr<Model> AxeOffArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.4f, kIronLight, kIron);
        const float out = (pivot.x >= 0.0f) ? 1.0f : -1.0f;
        for (int k = 0; k < 3; ++k)
        {
            const Vec3 base = f.hand - f.down * (0.15f + 0.12f * (float)k) * f.w * 3.0f + Vec3(out * 0.62f * f.w, 0.0f, 0.0f);
            b.Beam(base, base + Vec3(out * 0.10f, 0.03f, 0.0f), 0.05f, kIronLight, 0.005f);
        }
        return b.Build(device);
    }

    // ============================================================
    // 森の弓手
    // ============================================================
    std::shared_ptr<Model> ArcherHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.08f * s.x;
        b.Box(Vec3(lo.x - e, lo.y - 0.4f * e, lo.z - 1.2f * e), Vec3(hi.x + e, hi.y + e, hi.z + 0.6f * e), kHoodLight, kHood);   // 頭巾
        b.Beam(Vec3(c.x, hi.y, lo.z + 0.2f * s.z), Vec3(c.x, hi.y + 0.25f * s.y, lo.z - 0.35f * s.z), 0.40f * s.x, kHood, 0.04f * s.x);   // 後ろへ尖る先
        b.Box(Vec3(lo.x + 0.12f * s.x, lo.y + 0.15f * s.y, hi.z + 0.6f * e), Vec3(hi.x - 0.12f * s.x, lo.y + 0.75f * s.y, hi.z + 0.9f * e), kVoid);   // 顔の影
        b.Box(Vec3(lo.x + 0.08f * s.x, lo.y - 0.2f * e, hi.z + 0.6f * e), Vec3(hi.x - 0.08f * s.x, lo.y + 0.32f * s.y, hi.z + 1.4f * e), kLeather);   // 口元の布
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.18f * s.x;
            b.Box(Vec3(ex - 0.05f * s.x, lo.y + 0.48f * s.y, hi.z + 0.9f * e), Vec3(ex + 0.05f * s.x, lo.y + 0.54f * s.y, hi.z + 1.1f * e), kForestGlow);
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> ArcherTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.06f * s.x;
        b.Beam(Vec3(lo.x, hi.y - 0.05f * s.y, hi.z + e), Vec3(hi.x, lo.y + 0.10f * s.y, hi.z + e), 0.08f * s.x, kLeather);   // 胸の革紐
        b.Box(Vec3(lo.x - e, hi.y - 0.10f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 0.06f * s.y, hi.z + e), kHood);   // 頭巾の裾（肩）
        // 背中の矢筒（右肩から斜め）と矢羽
        const Vec3 q0(c.x + 0.18f * s.x, lo.y + 0.10f * s.y, lo.z - 0.12f * s.z);
        const Vec3 q1(c.x - 0.10f * s.x, hi.y + 0.12f * s.y, lo.z - 0.12f * s.z);
        b.Beam(q0, q1, 0.28f * s.x, kLeather, 0.30f * s.x);
        for (int k = 0; k < 4; ++k)
        {
            const Vec3 a = q1 + Vec3(-0.08f * s.x + 0.05f * s.x * (float)k, 0.0f, 0.03f * s.z * (float)(k & 1));
            b.Beam(a, a + Vec3(-0.06f * s.x, 0.30f * s.y, 0.0f), 0.025f * s.x, kWood);
            b.Box(a + Vec3(-0.07f * s.x, 0.22f * s.y, -0.025f), a + Vec3(-0.03f * s.x, 0.32f * s.y, 0.025f), kFeather);
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> ArcherBowArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.3f, kLeather, kLeather);
        // 弓：手を中心に腕の向きへ長さ 0.95、前へ反る弧（6 本の角柱）+ 弦
        const Vec3 grip = f.hand + f.az * (0.10f * f.w);
        const float L = 0.95f;
        Vec3 prev;
        for (int k = 0; k <= 6; ++k)
        {
            const float t = (float)k / 6.0f;
            const float u = t * 2.0f - 1.0f;
            const Vec3 p = grip + f.down * (u * 0.5f * L) + f.az * (0.16f * (1.0f - u * u));
            if (k > 0) b.Beam(prev, p, (k == 3 || k == 4) ? 0.05f : 0.035f, kWood);
            prev = p;
        }
        b.Beam(grip - f.down * (0.5f * L), grip + f.down * (0.5f * L), 0.008f, kFeather);   // 弦
        return b.Build(device);
    }
    std::shared_ptr<Model> ArcherArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        Bracer(b, f, 0.35f, kLeather, kLeather);
        return b.Build(device);
    }

}
