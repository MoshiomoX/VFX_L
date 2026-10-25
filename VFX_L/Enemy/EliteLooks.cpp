// ============================================================
// EliteLooks.cpp
// 面の固定エリートの皮と飾り（説明は EliteLooks.h）。寸法は焼いた雑魚の網の部品の包囲ボックスに対する割合
// （EnemyLooks.cpp と同じ流儀。網の空間：足元 y ≒ -0.9、頭 y 0.29〜0.76、+Z が正面。手に持つ物は FrameOf の腕の向きで組む）
// ============================================================
#include "Enemy/EliteLooks.h"
#include "Enemy/LookBuilder.h"
#include "Swarm/SwarmSystem.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace LookBuilder;

namespace
{
    // 色（線形。HDR = bloom で光る）
    const Vec4 kVoid = { 0.008f, 0.006f, 0.012f, 1.0f };
    const Vec4 kIron = { 0.10f, 0.095f, 0.09f, 1.0f };
    const Vec4 kIronLight = { 0.24f, 0.23f, 0.22f, 1.0f };
    const Vec4 kSteel = { 0.42f, 0.43f, 0.46f, 1.0f };
    const Vec4 kBronze = { 0.36f, 0.20f, 0.06f, 1.0f };
    const Vec4 kGold = { 0.62f, 0.40f, 0.08f, 1.0f };
    const Vec4 kBone = { 0.60f, 0.55f, 0.42f, 1.0f };
    const Vec4 kFur = { 0.12f, 0.07f, 0.035f, 1.0f };
    const Vec4 kFurLight = { 0.22f, 0.15f, 0.08f, 1.0f };
    const Vec4 kLeather = { 0.10f, 0.055f, 0.03f, 1.0f };
    const Vec4 kWood = { 0.15f, 0.08f, 0.035f, 1.0f };
    const Vec4 kRedEye = { 3.6f, 0.35f, 0.15f, 1.0f };
    const Vec4 kHood = { 0.035f, 0.08f, 0.035f, 1.0f };
    const Vec4 kHoodLight = { 0.08f, 0.15f, 0.06f, 1.0f };
    const Vec4 kForestGlow = { 0.6f, 3.4f, 0.8f, 1.0f };
    const Vec4 kFeather = { 0.80f, 0.78f, 0.70f, 1.0f };
    const Vec4 kSand = { 0.62f, 0.50f, 0.32f, 1.0f };
    const Vec4 kSandDark = { 0.36f, 0.26f, 0.15f, 1.0f };
    const Vec4 kCloth = { 0.55f, 0.16f, 0.08f, 1.0f };
    const Vec4 kAmberGlow = { 3.4f, 1.8f, 0.3f, 1.0f };
    const Vec4 kJewel = { 0.2f, 1.4f, 2.6f, 1.0f };
    const Vec4 kBurgundy = { 0.20f, 0.025f, 0.03f, 1.0f };
    const Vec4 kBurgundyLight = { 0.38f, 0.07f, 0.04f, 1.0f };
    const Vec4 kFlame = { 5.0f, 1.6f, 0.25f, 1.0f };
    const Vec4 kFlameHot = { 6.0f, 4.0f, 1.2f, 1.0f };
    const Vec4 kShadow = { 0.018f, 0.016f, 0.035f, 1.0f };
    const Vec4 kShadowLight = { 0.06f, 0.05f, 0.11f, 1.0f };
    const Vec4 kViolet = { 2.2f, 0.5f, 4.2f, 1.0f };
    const Vec4 kNavy = { 0.02f, 0.03f, 0.08f, 1.0f };
    const Vec4 kNavyLight = { 0.06f, 0.08f, 0.19f, 1.0f };
    const Vec4 kParchment = { 0.62f, 0.56f, 0.40f, 1.0f };
    const Vec4 kCurseGlow = { 1.8f, 0.6f, 3.4f, 1.0f };
    const Vec4 kBookRed = { 0.24f, 0.03f, 0.04f, 1.0f };
    const Vec4 kBookGreen = { 0.03f, 0.14f, 0.07f, 1.0f };

    // 腕を一回り囲む箱（籠手）。手首から肘へ len 割
    void Bracer(Builder& b, const ArmFrame& f, float len, const Vec4& top, const Vec4& side)
    {
        const Vec3 c = f.hand - f.down * (len * 0.5f * f.w * 2.0f);
        b.OBox(c, f.ax, f.ay, f.az, Vec3(0.62f * f.w, len * f.w, 0.62f * f.w), top, side);
    }

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

void EliteLooks::Apply(int stage, ID3D11Device* device, SwarmSystem& swarm)
{
    const std::shared_ptr<Model> modelRef = swarm.GetEnemyModel();
    const Model* model = modelRef.get();
    if (!model || !device) return;

    Part head, torso, armA, armB;
    FindParts(*model, head, torso, armA, armB);
    Part armR = armA, armL = armB;   // 右手 = +x の腕（持ち物）、左手 = 盾・弓・本
    if (armR.Valid() && armL.Valid() && armR.Center().x < armL.Center().x) std::swap(armR, armL);
    const Vec3 pivR = armR.Valid() ? swarm.GetPartPivot((uint32_t)armR.index) : Vec3::Zero;
    const Vec3 pivL = armL.Valid() ? swarm.GetPartPivot((uint32_t)armL.index) : Vec3::Zero;

    using Deco = SwarmSystem::BossDecoration;
    auto add = [](std::vector<Deco>& list, std::shared_ptr<Model> m, uint32_t part, float orbit = 0.0f)
        {
            if (!m) return;
            Deco d;
            d.model = std::move(m);
            d.part = part;
            d.orbitSpeed = orbit;
            list.push_back(std::move(d));
        };
    const uint32_t H = (uint32_t)head.index, T = (uint32_t)torso.index, R = (uint32_t)armR.index, L = (uint32_t)armL.index;
    const bool ok = head.Valid() && torso.Valid() && armR.Valid() && armL.Valid();
    auto& rm = ResourceManager::Get();
    std::vector<Deco> a, b;
    const wchar_t* texA = Res::Tex::EliteAxeAlbedo;
    const wchar_t* texB = Res::Tex::EliteArcherAlbedo;
    if (ok && stage == 2)
    {
        add(a, RiderHead(device, head), H);
        add(a, RiderTorso(device, torso), T);
        add(a, RiderLanceArm(device, armR, pivR), R);
        add(a, RiderShieldArm(device, armL, pivL), L);
        add(b, FlameHead(device, head), H);
        add(b, FlameTorso(device, torso), T);
        add(b, FlameStaffArm(device, armR, pivR), R);
        add(b, FlameArm(device, armL, pivL), L);
        texA = Res::Tex::EliteRiderAlbedo;
        texB = Res::Tex::EliteFlameAlbedo;
    }
    else if (ok && stage == 3)
    {
        add(a, ShadeHead(device, head), H);
        add(a, ShadeTorso(device, torso), T);
        add(a, ShadeBladeArm(device, armR, pivR), R);
        add(a, ShadeArm(device, armL, pivL), L);
        add(b, LibrarianHead(device, head), H);
        add(b, LibrarianTorso(device, torso), T);
        add(b, LibrarianArm(device, armR, pivR), R);
        add(b, LibrarianBookArm(device, armL, pivL), L);
        add(b, OrbitBooks(device, *model), SwarmSystem::kDecoOrbit, 0.8f);
        texA = Res::Tex::EliteShadeAlbedo;
        texB = Res::Tex::EliteLibrarianAlbedo;
    }
    else if (ok)
    {
        add(a, AxeHead(device, head), H);
        add(a, AxeTorso(device, torso), T);
        add(a, AxeWeaponArm(device, armR, pivR), R);
        add(a, AxeOffArm(device, armL, pivL), L);
        add(b, ArcherHead(device, head), H);
        add(b, ArcherTorso(device, torso), T);
        add(b, ArcherArm(device, armR, pivR), R);
        add(b, ArcherBowArm(device, armL, pivL), L);
    }
    swarm.SetListLook(Swarm::kDrawListEliteA, rm.LoadTexture(texA), std::move(a));
    swarm.SetListLook(Swarm::kDrawListEliteB, rm.LoadTexture(texB), std::move(b));
    std::cout << "[EliteLooks] stage " << stage << " decorations set" << std::endl;
}
