// ============================================================
// EnemyLooks.cpp
// 遺跡の敵の皮と飾り（説明は EnemyLooks.h）。寸法は焼いた雑魚の網の部品の包囲ボックスに対する割合（BossLooks.cpp と同じ流儀）。
// 網の空間：足元 = カプセルの下（y ≒ -0.9）、頭 y 0.29〜0.76、+Z が正面。腕は焼いた歩きの姿勢で前後に振れているので、
// 手に持つ物（杖・剣・鉤）は付け根 → 手の向き（腕に沿う向き）で組む = 待機で腕を下ろすと真っ直ぐ立つ
// ============================================================
#include "Enemy/EnemyLooks.h"
#include "Enemy/LookBuilder.h"
#include "Swarm/SwarmSystem.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace LookBuilder;

namespace
{
    // 色（線形。頂点色がそのまま掛かる。HDR = bloom で光る）
    const Vec4 kSteel = { 0.13f, 0.135f, 0.15f, 1.0f };     // 暗い鋼（0.30 では草の上で白く見えた）
    const Vec4 kSteelDark = { 0.045f, 0.045f, 0.055f, 1.0f };
    const Vec4 kSteelLight = { 0.24f, 0.245f, 0.27f, 1.0f };
    const Vec4 kCurseCloth = { 0.13f, 0.02f, 0.17f, 1.0f };
    const Vec4 kCurseGlow = { 1.7f, 0.3f, 2.8f, 1.0f };    // 2.6 / 4.2 は bloom で体ごと桃色になった
    const Vec4 kVoid = { 0.008f, 0.006f, 0.012f, 1.0f };
    const Vec4 kLeather = { 0.10f, 0.055f, 0.03f, 1.0f };
    const Vec4 kStone = { 0.20f, 0.20f, 0.19f, 1.0f };
    const Vec4 kStoneDark = { 0.085f, 0.085f, 0.08f, 1.0f };
    const Vec4 kStoneLight = { 0.32f, 0.32f, 0.30f, 1.0f };
    const Vec4 kEmber = { 4.5f, 1.1f, 0.25f, 1.0f };
    const Vec4 kRobe = { 0.035f, 0.018f, 0.05f, 1.0f };
    const Vec4 kRobeLight = { 0.09f, 0.045f, 0.12f, 1.0f };
    const Vec4 kTrim = { 0.20f, 0.17f, 0.06f, 1.0f };
    const Vec4 kBone = { 0.62f, 0.58f, 0.45f, 1.0f };
    const Vec4 kBoneDark = { 0.30f, 0.27f, 0.19f, 1.0f };
    const Vec4 kNecroGlow = { 0.4f, 3.8f, 1.1f, 1.0f };
    const Vec4 kWood = { 0.17f, 0.085f, 0.035f, 1.0f };
    const Vec4 kRedEye = { 3.0f, 0.35f, 0.18f, 1.0f };
    const Vec4 kRust = { 0.24f, 0.11f, 0.04f, 1.0f };
    const Vec4 kIron = { 0.13f, 0.12f, 0.11f, 1.0f };
    const Vec4 kIronLight = { 0.26f, 0.24f, 0.22f, 1.0f };
    const Vec4 kChain = { 0.30f, 0.29f, 0.28f, 1.0f };
    const Vec4 kWardenGlow = { 4.2f, 1.5f, 0.25f, 1.0f };

    // ============================================================
    // 呪いの鎧
    // ============================================================
    std::shared_ptr<Model> ArmorHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.07f * s.x;
        // 兜（頭より一回り大きい）+ 下の縁
        b.Box(Vec3(lo.x - e, lo.y - 0.6f * e, lo.z - e), Vec3(hi.x + e, hi.y + e, hi.z + e), kSteel, kSteelDark);
        b.Box(Vec3(lo.x - 1.4f * e, lo.y - 0.8f * e, lo.z - 1.4f * e), Vec3(hi.x + 1.4f * e, lo.y + 0.12f * s.y, hi.z + 1.4f * e), kSteelDark);
        // 正面の縦の筋と覗き穴（黒）+ 紫に光る目
        b.Box(Vec3(c.x - 0.05f * s.x, lo.y, hi.z + e), Vec3(c.x + 0.05f * s.x, hi.y + e, hi.z + 1.5f * e), kSteelLight);
        const float slitY0 = lo.y + 0.47f * s.y, slitY1 = lo.y + 0.60f * s.y;
        b.Box(Vec3(lo.x - 0.2f * e, slitY0, hi.z + e), Vec3(hi.x + 0.2f * e, slitY1, hi.z + 1.6f * e), kVoid);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.22f * s.x;
            b.Box(Vec3(ex - 0.07f * s.x, slitY0 + 0.02f * s.y, hi.z + 1.6f * e), Vec3(ex + 0.07f * s.x, slitY1 - 0.02f * s.y, hi.z + 1.8f * e), kCurseGlow);
        }
        // 頭頂の紫の房（前から後ろへ）
        b.Box(Vec3(c.x - 0.07f * s.x, hi.y + e, lo.z - 0.5f * e), Vec3(c.x + 0.07f * s.x, hi.y + 0.42f * s.y, hi.z - 0.2f * s.z), kCurseCloth);
        b.Box(Vec3(c.x - 0.06f * s.x, hi.y - 0.1f * s.y, lo.z - 2.5f * e), Vec3(c.x + 0.06f * s.x, hi.y + 0.32f * s.y, lo.z - e), kCurseCloth);
        return b.Build(device);
    }
    std::shared_ptr<Model> ArmorTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.08f * s.z;
        // 胸当て + 真ん中の稜線 + 紫の紋
        b.Box(Vec3(lo.x - e, c.y - 0.15f * s.y, hi.z), Vec3(hi.x + e, hi.y + 0.03f * s.y, hi.z + 1.8f * e), kSteelLight, kSteel);
        b.Box(Vec3(c.x - 0.06f * s.x, c.y - 0.15f * s.y, hi.z + 1.8f * e), Vec3(c.x + 0.06f * s.x, hi.y + 0.03f * s.y, hi.z + 2.6f * e), kSteel);
        b.Box(Vec3(c.x - 0.10f * s.x, c.y + 0.05f * s.y, hi.z + 2.6f * e), Vec3(c.x + 0.10f * s.x, c.y + 0.25f * s.y, hi.z + 2.9f * e), kCurseGlow);
        // 背当て
        b.Box(Vec3(lo.x - e, c.y - 0.15f * s.y, lo.z - 1.5f * e), Vec3(hi.x + e, hi.y, lo.z), kSteel, kSteelDark);
        // 革の帯と留め金
        b.Box(Vec3(lo.x - 1.2f * e, lo.y - 0.04f * s.y, lo.z - 1.2f * e), Vec3(hi.x + 1.2f * e, lo.y + 0.14f * s.y, hi.z + 1.2f * e), kLeather);
        b.Box(Vec3(c.x - 0.08f * s.x, lo.y - 0.02f * s.y, hi.z + 1.2f * e), Vec3(c.x + 0.08f * s.x, lo.y + 0.12f * s.y, hi.z + 2.0f * e), kSteelLight);
        // 腰の板（前・後ろ・横。脚はこの裏で動く）
        b.Box(Vec3(lo.x + 0.05f * s.x, lo.y - 0.36f * s.y, hi.z + 0.2f * e), Vec3(hi.x - 0.05f * s.x, lo.y, hi.z + 1.0f * e), kSteel, kSteelDark);
        b.Box(Vec3(lo.x + 0.05f * s.x, lo.y - 0.32f * s.y, lo.z - 1.0f * e), Vec3(hi.x - 0.05f * s.x, lo.y, lo.z - 0.2f * e), kSteel, kSteelDark);
        return b.Build(device);
    }
    std::shared_ptr<Model> ArmorArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const float w = f.w;
        const float out = (pivot.x >= 0.0f) ? 1.0f : -1.0f;
        // 肩当て（2 枚重ね）+ 鋲
        b.Box(Vec3((std::min)(pivot.x, pivot.x + out * 1.35f * w), pivot.y - 0.35f * w, pivot.z - 0.85f * w),
              Vec3((std::max)(pivot.x, pivot.x + out * 1.35f * w), pivot.y + 0.55f * w, pivot.z + 0.85f * w), kSteelLight, kSteel);
        b.Box(Vec3((std::min)(pivot.x, pivot.x + out * 1.20f * w), pivot.y - 0.75f * w, pivot.z - 0.75f * w),
              Vec3((std::max)(pivot.x, pivot.x + out * 1.20f * w), pivot.y - 0.25f * w, pivot.z + 0.75f * w), kSteel, kSteelDark);
        b.Box(Vec3(pivot.x + out * 1.35f * w - 0.06f * w, pivot.y + 0.05f * w, pivot.z - 0.12f * w),
              Vec3(pivot.x + out * 1.35f * w + 0.06f * w, pivot.y + 0.30f * w, pivot.z + 0.12f * w), kSteelDark);
        // 籠手（手の周り、腕に沿った箱）
        b.OBox(f.hand - f.down * (0.15f * w), f.ax, f.ay, f.az, Vec3(0.62f * w, 0.45f * w, 0.62f * w), kSteelLight, kSteel);
        return b.Build(device);
    }

    // ============================================================
    // 石像ガーゴイル
    // ============================================================
    std::shared_ptr<Model> GargoyleHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float x = (float)sg;
            // 角（2 節、後ろへ反る）
            const Vec3 h0(c.x + x * 0.30f * s.x, hi.y - 0.08f * s.y, c.z + 0.04f * s.z);
            const Vec3 h1(c.x + x * 0.45f * s.x, hi.y + 0.34f * s.y, c.z - 0.18f * s.z);
            const Vec3 h2(c.x + x * 0.40f * s.x, hi.y + 0.60f * s.y, c.z - 0.55f * s.z);
            b.Beam(h0, h1, 0.16f * s.x, kStoneLight, 0.11f * s.x);
            b.Beam(h1, h2, 0.11f * s.x, kStoneLight, 0.03f * s.x);
            // 尖った耳
            b.Beam(Vec3(c.x + x * 0.49f * s.x, lo.y + 0.66f * s.y, c.z), Vec3(c.x + x * 0.72f * s.x, lo.y + 1.0f * s.y, c.z - 0.12f * s.z),
                   0.12f * s.x, kStone, 0.03f * s.x);
            // 牙（鼻面の下から下へ）
            b.Beam(Vec3(c.x + x * 0.16f * s.x, lo.y + 0.10f * s.y, hi.z + 0.17f * s.z), Vec3(c.x + x * 0.16f * s.x, lo.y - 0.07f * s.y, hi.z + 0.19f * s.z),
                   0.06f * s.x, kBone, 0.015f * s.x);
            // 赤く光る目（眉の下）
            const float ex = c.x + x * 0.23f * s.x;
            b.Box(Vec3(ex - 0.08f * s.x, lo.y + 0.47f * s.y, hi.z - 0.005f), Vec3(ex + 0.08f * s.x, lo.y + 0.58f * s.y, hi.z + 0.035f * s.z), kEmber);
        }
        // 鼻面（顔の下半分を覆う）+ 鼻の穴
        b.Box(Vec3(c.x - 0.29f * s.x, lo.y + 0.06f * s.y, hi.z - 0.02f * s.z), Vec3(c.x + 0.29f * s.x, lo.y + 0.43f * s.y, hi.z + 0.22f * s.z), kStone, kStoneDark);
        b.Box(Vec3(c.x - 0.14f * s.x, lo.y + 0.30f * s.y, hi.z + 0.22f * s.z), Vec3(c.x + 0.14f * s.x, lo.y + 0.38f * s.y, hi.z + 0.24f * s.z), kStoneDark);
        // 張り出した眉
        b.Box(Vec3(lo.x - 0.02f * s.x, lo.y + 0.58f * s.y, hi.z - 0.06f * s.z), Vec3(hi.x + 0.02f * s.x, lo.y + 0.70f * s.y, hi.z + 0.10f * s.z), kStoneDark, kStoneDark);
        return b.Build(device);
    }
    // 背中の翼（右 = +x。左は鏡像）。膜 3 枚 + 骨 5 本、付け根 = 戻り値の hinge（VS が前向きの軸で回す）
    std::shared_ptr<Model> GargoyleWing(ID3D11Device* device, const Part& T, float side, Vec3& hinge)
    {
        Builder b;
        const float zb = T.lo.z - 0.04f;   // 背中のすぐ後ろ
        auto P = [&](float x, float y) { return Vec3(side * x, y, zb); };
        const Vec3 p0 = P(0.14f, 0.24f), p1 = P(0.55f, 0.62f), p2 = P(1.00f, 0.50f);
        const Vec3 f1 = P(0.95f, -0.05f), f2 = P(0.68f, -0.22f), f3 = P(0.40f, -0.12f), r = P(0.16f, 0.02f), rm = P(0.13f, 0.13f);
        const Vec3 n(0.0f, 0.0f, 1.0f);
        const Vec3 q1[4] = { p0, p1, p2, f1 };
        const Vec3 q2[4] = { p0, f1, f2, f3 };
        const Vec3 q3[4] = { p0, f3, r, rm };
        b.Plate(q1, n, 0.025f, kStoneDark);
        b.Plate(q2, n, 0.025f, kStoneDark);
        b.Plate(q3, n, 0.025f, kStoneDark);
        const Vec3 back(0.0f, 0.0f, -0.035f);   // 骨は膜の外（後ろ）側に浮かせる
        b.Beam(p0 + back, p1 + back, 0.07f, kStoneLight, 0.05f);
        b.Beam(p1 + back, p2 + back, 0.05f, kStoneLight, 0.025f);
        b.Beam(p1 + back, f1 + back, 0.035f, kStone, 0.02f);
        b.Beam(p1 + back, f2 + back, 0.035f, kStone, 0.02f);
        b.Beam(p1 + back, f3 + back, 0.035f, kStone, 0.02f);
        b.Beam(p1 + back, p1 + back + Vec3(side * 0.04f, 0.14f, 0.0f), 0.05f, kStoneLight, 0.01f);   // 翼の肘の爪
        hinge = p0;
        return b.Build(device);
    }
    std::shared_ptr<Model> GargoyleTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        // 尾（3 節 + 先の鏃）
        const Vec3 t0(c.x, lo.y + 0.06f * s.y, lo.z), t1(c.x, lo.y - 0.34f * s.y, lo.z - 0.70f * s.z);
        const Vec3 t2(c.x, lo.y - 0.48f * s.y, lo.z - 1.50f * s.z), t3(c.x, lo.y - 0.36f * s.y, lo.z - 2.10f * s.z);
        b.Beam(t0, t1, 0.10f, kStone, 0.08f);
        b.Beam(t1, t2, 0.08f, kStone, 0.06f);
        b.Beam(t2, t3, 0.06f, kStone, 0.04f);
        b.Beam(t3 + Vec3(0.0f, -0.06f, 0.04f), t3 + Vec3(0.0f, 0.10f, -0.14f), 0.12f, kStoneDark, 0.01f);
        // 背骨の棘
        for (int i = 0; i < 3; ++i)
        {
            const float y = lo.y + (0.30f + 0.22f * (float)i) * s.y;
            b.Beam(Vec3(c.x, y, lo.z), Vec3(c.x, y + 0.05f, lo.z - 0.12f), 0.06f, kStoneLight, 0.01f);
        }
        // 胸の荒い石の板
        b.Box(Vec3(lo.x + 0.08f * s.x, c.y - 0.05f * s.y, hi.z), Vec3(hi.x - 0.08f * s.x, hi.y - 0.05f * s.y, hi.z + 0.04f), kStoneLight, kStone);
        return b.Build(device);
    }
    std::shared_ptr<Model> GargoyleArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        // 爪 3 本（手の先から前下へ）
        for (int k = -1; k <= 1; ++k)
        {
            const Vec3 base = f.hand + f.ax * ((float)k * 0.3f * f.w) + f.down * (0.35f * f.w) + f.az * (0.3f * f.w);
            b.Beam(base, base + f.down * (0.45f * f.w) + f.az * (0.35f * f.w), 0.16f * f.w, kBone, 0.03f * f.w);
        }
        return b.Build(device);
    }

    // ============================================================
    // 死霊術師
    // ============================================================
    std::shared_ptr<Model> NecroHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.09f * s.x;
        // 頭巾：頭頂・後ろ・両脇（前は開いていて顔は闇）
        b.Box(Vec3(lo.x - e, hi.y - 0.05f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 1.2f * e, hi.z + 1.6f * e), kRobe, kRobe);
        b.Box(Vec3(lo.x - e, lo.y - 0.25f * s.y, lo.z - 1.6f * e), Vec3(hi.x + e, hi.y, lo.z), kRobe);
        b.Box(Vec3(lo.x - e, lo.y - 0.25f * s.y, lo.z - e), Vec3(lo.x, hi.y, hi.z + 1.6f * e), kRobe);
        b.Box(Vec3(hi.x, lo.y - 0.25f * s.y, lo.z - e), Vec3(hi.x + e, hi.y, hi.z + 1.6f * e), kRobe);
        // 頭巾の先（後ろへ垂れる尖り）
        b.Beam(Vec3(c.x, hi.y + 0.5f * e, c.z - 0.2f * s.z), Vec3(c.x, hi.y + 0.30f * s.y, lo.z - 0.25f * s.z), 0.22f * s.x, kRobe, 0.04f * s.x);
        // 縁取り（顔の周りの細い帯）
        b.Box(Vec3(lo.x - e, hi.y - 0.10f * s.y, hi.z + 1.3f * e), Vec3(hi.x + e, hi.y - 0.02f * s.y, hi.z + 1.8f * e), kTrim);
        // 顔の闇 + 緑に光る目
        b.Box(Vec3(lo.x + 0.02f * s.x, lo.y + 0.02f * s.y, hi.z + 0.2f * e), Vec3(hi.x - 0.02f * s.x, hi.y - 0.06f * s.y, hi.z + 0.6f * e), kVoid);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.19f * s.x;
            b.Box(Vec3(ex - 0.06f * s.x, lo.y + 0.48f * s.y, hi.z + 0.6f * e), Vec3(ex + 0.06f * s.x, lo.y + 0.56f * s.y, hi.z + 0.8f * e), kNecroGlow);
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> NecroTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.10f * s.z;
        // 肩掛け
        b.Box(Vec3(lo.x - 1.3f * e, hi.y - 0.22f * s.y, lo.z - 1.3f * e), Vec3(hi.x + 1.3f * e, hi.y + 0.04f * s.y, hi.z + 1.3f * e), kRobeLight, kRobe);
        // 長い衣の裾（腰から足元まで。脚はこの中で動く）+ 前の縦の帯
        b.Box(Vec3(lo.x - 1.6f * e, lo.y - 1.15f * s.y, lo.z - 1.8f * e), Vec3(hi.x + 1.6f * e, lo.y + 0.18f * s.y, hi.z + 1.8f * e), kRobe, kRobe);
        b.Box(Vec3(c.x - 0.09f * s.x, lo.y - 1.12f * s.y, hi.z + 1.8f * e), Vec3(c.x + 0.09f * s.x, hi.y - 0.22f * s.y, hi.z + 2.2f * e), kTrim);
        // 骨の首飾り（小さな頭蓋）
        b.Box(Vec3(c.x - 0.10f * s.x, c.y + 0.02f * s.y, hi.z + 2.2f * e), Vec3(c.x + 0.10f * s.x, c.y + 0.22f * s.y, hi.z + 3.2f * e), kBone, kBoneDark);
        b.Box(Vec3(c.x - 0.07f * s.x, c.y + 0.12f * s.y, hi.z + 3.2f * e), Vec3(c.x - 0.01f * s.x, c.y + 0.18f * s.y, hi.z + 3.4f * e), kVoid);
        b.Box(Vec3(c.x + 0.01f * s.x, c.y + 0.12f * s.y, hi.z + 3.2f * e), Vec3(c.x + 0.07f * s.x, c.y + 0.18f * s.y, hi.z + 3.4f * e), kVoid);
        // 骨の紐の帯
        b.Box(Vec3(lo.x - 1.7f * e, lo.y + 0.10f * s.y, lo.z - 1.9f * e), Vec3(hi.x + 1.7f * e, lo.y + 0.20f * s.y, hi.z + 1.9f * e), kBoneDark);
        return b.Build(device);
    }
    std::shared_ptr<Model> NecroStaffArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        // 袖口
        b.OBox(f.hand - f.down * (0.35f * f.w), f.ax, f.ay, f.az, Vec3(0.62f * f.w, 0.22f * f.w, 0.62f * f.w), kRobeLight, kRobe);
        // 杖：腕に沿って手の前を通り、足元から頭の上まで
        const Vec3 off = f.az * (0.75f * f.w);
        const Vec3 bottom = f.hand + f.down * 0.55f + off, top = f.hand - f.down * 1.15f + off;
        b.Beam(bottom, top, 0.055f, kWood);
        // 先の爪 2 本 + 緑の宝石
        b.Beam(top, top - f.down * 0.16f + f.ax * 0.08f, 0.03f, kBoneDark, 0.01f);
        b.Beam(top, top - f.down * 0.16f - f.ax * 0.08f, 0.03f, kBoneDark, 0.01f);
        b.Beam(top - f.down * 0.02f, top - f.down * 0.16f, 0.11f, kNecroGlow, 0.05f);
        return b.Build(device);
    }
    std::shared_ptr<Model> NecroArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        b.OBox(f.hand - f.down * (0.35f * f.w), f.ax, f.ay, f.az, Vec3(0.62f * f.w, 0.22f * f.w, 0.62f * f.w), kRobeLight, kRobe);
        return b.Build(device);
    }

    // ============================================================
    // 骸骨
    // ============================================================
    std::shared_ptr<Model> SkeletonHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.035f * s.x;
        // 頭蓋（皮の顔を隠す）
        b.Box(Vec3(lo.x - e, lo.y + 0.12f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + e, hi.z + e), kBone, kBone);
        // 眼窩 + 奥の赤い点
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.21f * s.x;
            b.Box(Vec3(ex - 0.11f * s.x, lo.y + 0.46f * s.y, hi.z + e), Vec3(ex + 0.11f * s.x, lo.y + 0.66f * s.y, hi.z + 1.4f * e), kVoid);
            b.Box(Vec3(ex - 0.035f * s.x, lo.y + 0.53f * s.y, hi.z + 1.4f * e), Vec3(ex + 0.035f * s.x, lo.y + 0.59f * s.y, hi.z + 1.6f * e), kRedEye);
        }
        // 鼻の穴
        b.Box(Vec3(c.x - 0.05f * s.x, lo.y + 0.36f * s.y, hi.z + e), Vec3(c.x + 0.05f * s.x, lo.y + 0.44f * s.y, hi.z + 1.4f * e), kVoid);
        // 顎（少し細い）と歯
        b.Box(Vec3(lo.x + 0.12f * s.x, lo.y - 0.02f * s.y, lo.z + 0.12f * s.z), Vec3(hi.x - 0.12f * s.x, lo.y + 0.14f * s.y, hi.z + 0.5f * e), kBone, kBoneDark);
        b.Box(Vec3(c.x - 0.25f * s.x, lo.y + 0.14f * s.y, hi.z), Vec3(c.x + 0.25f * s.x, lo.y + 0.28f * s.y, hi.z + 1.2f * e), kVoid);
        for (int k = 0; k < 6; ++k)
        {
            const float x0 = c.x - 0.24f * s.x + (float)k * 0.082f * s.x;
            b.Box(Vec3(x0, lo.y + 0.15f * s.y, hi.z + 0.8f * e), Vec3(x0 + 0.06f * s.x, lo.y + 0.27f * s.y, hi.z + 1.5f * e), kBone);
        }
        return b.Build(device);
    }
    std::shared_ptr<Model> SkeletonTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float z0 = hi.z + 0.004f, z1 = hi.z + 0.03f;
        // 肋骨の間の闇 + 肋骨 4 本 + 胸骨
        b.Box(Vec3(lo.x + 0.06f * s.x, c.y - 0.18f * s.y, hi.z), Vec3(hi.x - 0.06f * s.x, hi.y - 0.06f * s.y, z0), kVoid);
        for (int k = 0; k < 4; ++k)
        {
            const float y = c.y - 0.12f * s.y + (float)k * 0.14f * s.y;
            b.Box(Vec3(lo.x + 0.04f * s.x, y, z0), Vec3(hi.x - 0.04f * s.x, y + 0.06f * s.y, z1), kBone);
        }
        b.Box(Vec3(c.x - 0.05f * s.x, c.y - 0.18f * s.y, z0), Vec3(c.x + 0.05f * s.x, hi.y - 0.06f * s.y, z1), kBone);
        // 背骨の後ろ + 骨盤の帯
        b.Box(Vec3(c.x - 0.06f * s.x, lo.y, lo.z - 0.02f), Vec3(c.x + 0.06f * s.x, hi.y, lo.z), kBoneDark);
        b.Box(Vec3(lo.x - 0.01f, lo.y - 0.02f * s.y, lo.z - 0.01f), Vec3(hi.x + 0.01f, lo.y + 0.10f * s.y, hi.z + 0.01f), kBoneDark);
        return b.Build(device);
    }
    std::shared_ptr<Model> SkeletonSwordArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const Vec3 grip = f.hand + f.down * (0.15f * f.w) + f.az * (0.2f * f.w);
        b.Beam(grip - f.down * 0.06f, grip + f.down * 0.08f, 0.04f, kLeather);                    // 柄
        b.Beam(grip + f.down * 0.08f + f.ax * 0.11f, grip + f.down * 0.08f - f.ax * 0.11f, 0.035f, kRust);   // 鍔
        b.Beam(grip + f.down * 0.09f, grip + f.down * 0.74f, 0.055f, kRust, 0.012f);               // 錆びた刃
        return b.Build(device);
    }
    std::shared_ptr<Model> SkeletonShieldArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const float out = (pivot.x >= 0.0f) ? 1.0f : -1.0f;
        const Vec3 cen = f.hand - f.down * (0.6f * f.w) + Vec3(out * 0.7f * f.w, 0.0f, 0.0f);
        b.OBox(cen, Vec3(1.0f, 0.0f, 0.0f), f.ay, f.az, Vec3(0.06f, 0.24f, 0.22f), kWood, kWood);
        b.OBox(cen + Vec3(out * 0.04f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), f.ay, f.az, Vec3(0.04f, 0.07f, 0.07f), kRust, kRust);
        return b.Build(device);
    }

    // ============================================================
    // 鎖の番人
    // ============================================================
    std::shared_ptr<Model> WardenHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.07f * s.x;
        // 鉄の桶兜 + 鋲の帯 + 頭頂の棘
        b.Box(Vec3(lo.x - e, lo.y - 0.4f * e, lo.z - e), Vec3(hi.x + e, hi.y + 0.6f * e, hi.z + e), kIronLight, kIron);
        b.Box(Vec3(lo.x - 1.4f * e, hi.y - 0.12f * s.y, lo.z - 1.4f * e), Vec3(hi.x + 1.4f * e, hi.y - 0.02f * s.y, hi.z + 1.4f * e), kRust);
        b.Beam(Vec3(c.x, hi.y + 0.6f * e, c.z), Vec3(c.x, hi.y + 0.38f * s.y, c.z), 0.10f, kIron, 0.01f);
        // 覗き穴 2 本（橙に光る）+ 顔の下の格子
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.21f * s.x;
            b.Box(Vec3(ex - 0.13f * s.x, lo.y + 0.50f * s.y, hi.z + e), Vec3(ex + 0.13f * s.x, lo.y + 0.59f * s.y, hi.z + 1.4f * e), kVoid);
            b.Box(Vec3(ex - 0.07f * s.x, lo.y + 0.52f * s.y, hi.z + 1.4f * e), Vec3(ex + 0.07f * s.x, lo.y + 0.57f * s.y, hi.z + 1.6f * e), kWardenGlow);
        }
        for (int k = -1; k <= 1; ++k)
            b.Box(Vec3(c.x + (float)k * 0.16f * s.x - 0.025f * s.x, lo.y + 0.08f * s.y, hi.z + e), Vec3(c.x + (float)k * 0.16f * s.x + 0.025f * s.x, lo.y + 0.40f * s.y, hi.z + 1.7f * e), kIron);
        return b.Build(device);
    }
    std::shared_ptr<Model> WardenTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.06f;
        // 棘の首輪
        b.Box(Vec3(lo.x + 0.1f * s.x, hi.y - 0.04f * s.y, lo.z + 0.1f * s.z), Vec3(hi.x - 0.1f * s.x, hi.y + 0.10f * s.y, hi.z - 0.05f * s.z), kIron);
        for (int k = 0; k < 4; ++k)
        {
            const float a = 0.785f + 1.5708f * (float)k;
            const Vec3 base(c.x + std::sin(a) * 0.36f * s.x, hi.y + 0.03f * s.y, c.z + std::cos(a) * 0.36f * s.z);
            b.Beam(base, base + Vec3(std::sin(a) * 0.10f, 0.03f, std::cos(a) * 0.10f), 0.05f, kIronLight, 0.01f);
        }
        // 胸に斜めに掛けた鎖（左肩 → 右腰）
        b.Chain(Vec3(lo.x + 0.02f, hi.y - 0.02f, hi.z + 0.035f), Vec3(hi.x - 0.02f, lo.y + 0.02f, hi.z + 0.035f), 0.11f, 0.022f, kChain);
        // 革の前掛け + 鉄の留め具
        b.Box(Vec3(c.x - 0.30f * s.x, lo.y - 0.45f * s.y, hi.z), Vec3(c.x + 0.30f * s.x, lo.y + 0.10f * s.y, hi.z + e * 0.6f), kLeather);
        b.Box(Vec3(lo.x - 0.01f, lo.y - 0.02f * s.y, lo.z - 0.01f), Vec3(hi.x + 0.01f, lo.y + 0.12f * s.y, hi.z + 0.01f), kLeather);
        b.Box(Vec3(c.x - 0.07f * s.x, lo.y, hi.z + 0.01f), Vec3(c.x + 0.07f * s.x, lo.y + 0.11f * s.y, hi.z + 0.03f), kRust);
        return b.Build(device);
    }
    std::shared_ptr<Model> WardenHookArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const Vec3 grip = f.hand + f.down * (0.15f * f.w) + f.az * (0.15f * f.w);
        // 柄 → 鉤（先が上へ曲がる）
        const Vec3 e0 = grip + f.down * 0.42f;
        const Vec3 e1 = e0 + f.down * 0.10f + f.az * 0.13f;
        const Vec3 e2 = e1 - f.down * 0.07f + f.az * 0.10f;
        const Vec3 e3 = e2 - f.down * 0.14f + f.az * 0.01f;
        b.Beam(grip - f.down * 0.05f, e0, 0.05f, kIron);
        b.Beam(e0, e1, 0.055f, kIronLight);
        b.Beam(e1, e2, 0.05f, kIronLight);
        b.Beam(e2, e3, 0.045f, kIronLight, 0.01f);
        // 手から垂れる鎖
        b.Chain(grip, grip + Vec3(0.0f, -0.32f, 0.0f) - f.az * 0.06f, 0.09f, 0.018f, kChain);
        return b.Build(device);
    }
    std::shared_ptr<Model> WardenShackleArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        b.OBox(f.hand - f.down * (0.30f * f.w), f.ax, f.ay, f.az, Vec3(0.66f * f.w, 0.24f * f.w, 0.66f * f.w), kIronLight, kIron);
        b.Chain(f.hand - f.down * (0.30f * f.w) + f.az * (0.66f * f.w), f.hand + Vec3(0.0f, -0.38f, 0.0f) + f.az * 0.08f, 0.09f, 0.018f, kChain);
        return b.Build(device);
    }
}

void EnemyLooks::ApplyRuins(ID3D11Device* device, SwarmSystem& swarm)
{
    const std::shared_ptr<Model> modelRef = swarm.GetEnemyModel();
    const Model* model = modelRef.get();
    if (!model || !device) return;

    Part head, torso, armA, armB;
    FindParts(*model, head, torso, armA, armB);
    // 右手 = +x の腕（Kenney の arm-right は x > 0）。持ち物は右手、左手は枷・盾
    Part armR = armA, armL = armB;
    if (armR.Valid() && armL.Valid() && armR.Center().x < armL.Center().x) std::swap(armR, armL);
    const Vec3 pivR = armR.Valid() ? swarm.GetPartPivot((uint32_t)armR.index) : Vec3::Zero;
    const Vec3 pivL = armL.Valid() ? swarm.GetPartPivot((uint32_t)armL.index) : Vec3::Zero;
    std::cout << "[EnemyLooks] ruins: head " << head.index << " torso " << torso.index << " arms R " << armR.index << " L " << armL.index << std::endl;

    using Deco = SwarmSystem::BossDecoration;
    auto add = [](std::vector<Deco>& list, std::shared_ptr<Model> m, const Part& p)
        {
            if (!m || !p.Valid()) return;
            Deco d;
            d.model = std::move(m);
            d.part = (uint32_t)p.index;
            list.push_back(std::move(d));
        };
    auto& rm = ResourceManager::Get();

    // 呪いの鎧（蘇った鎧も同じリスト。VS が紫に脈打たせる）
    {
        std::vector<Deco> d;
        add(d, ArmorHead(device, head), head);
        add(d, ArmorTorso(device, torso), torso);
        add(d, ArmorArm(device, armR, pivR), armR);
        add(d, ArmorArm(device, armL, pivL), armL);
        swarm.SetListLook(Swarm::kDrawListArmor, rm.LoadTexture(Res::Tex::RuinsArmorAlbedo), std::move(d));
    }
    // 石像ガーゴイル（翼は g_AnimMode 2 で羽ばたく）
    {
        std::vector<Deco> d;
        add(d, GargoyleHead(device, head), head);
        add(d, GargoyleTorso(device, torso), torso);
        add(d, GargoyleArm(device, armR, pivR), armR);
        add(d, GargoyleArm(device, armL, pivL), armL);
        if (torso.Valid())
            for (int sg = -1; sg <= 1; sg += 2)
            {
                Vec3 hinge;
                Deco w;
                w.model = GargoyleWing(device, torso, (float)sg, hinge);
                w.part = (uint32_t)torso.index;
                w.flap = Vec4(hinge.x, hinge.y, hinge.z, (float)sg);
                if (w.model) d.push_back(std::move(w));
            }
        swarm.SetListLook(Swarm::kDrawListGargoyle, rm.LoadTexture(Res::Tex::RuinsGargoyleAlbedo), std::move(d));
    }
    // 死霊術師（杖は右手）
    {
        std::vector<Deco> d;
        add(d, NecroHead(device, head), head);
        add(d, NecroTorso(device, torso), torso);
        add(d, NecroStaffArm(device, armR, pivR), armR);
        add(d, NecroArm(device, armL, pivL), armL);
        swarm.SetListLook(Swarm::kDrawListNecro, rm.LoadTexture(Res::Tex::RuinsNecroAlbedo), std::move(d));
    }
    // 骸骨（右手に剣、左手に盾）
    {
        std::vector<Deco> d;
        add(d, SkeletonHead(device, head), head);
        add(d, SkeletonTorso(device, torso), torso);
        add(d, SkeletonSwordArm(device, armR, pivR), armR);
        add(d, SkeletonShieldArm(device, armL, pivL), armL);
        swarm.SetListLook(Swarm::kDrawListSkeleton, rm.LoadTexture(Res::Tex::RuinsSkeletonAlbedo), std::move(d));
    }
    // 鎖の番人（右手に鉤、左手に枷）
    {
        std::vector<Deco> d;
        add(d, WardenHead(device, head), head);
        add(d, WardenTorso(device, torso), torso);
        add(d, WardenHookArm(device, armR, pivR), armR);
        add(d, WardenShackleArm(device, armL, pivL), armL);
        swarm.SetListLook(Swarm::kDrawListWarden, rm.LoadTexture(Res::Tex::RuinsWardenAlbedo), std::move(d));
    }
}

// 投げる鎖の一区切り（+Z へ kChainSegLen、原点が根元）。ECS のモデル（卡通・アウトラインが付く）なので大きめの輪
std::shared_ptr<Model> EnemyLooks::BuildChainSegment(ID3D11Device* device)
{
    Builder b;
    b.Chain(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, kChainSegLen), 0.26f, 0.06f, kChain);   // 0.16 / 0.035 では遠くから細い線にしか見えなかった
    return b.Build(device);
}

// 鉤（原点 = 鎖の付け根、+Z が先）：輪 + 柄 + 前へ伸びて上へ返る 2 本の爪
std::shared_ptr<Model> EnemyLooks::BuildChainHook(ID3D11Device* device)
{
    Builder b;
    b.Beam(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.30f), 0.07f, kIronLight);
    b.Box(Vec3(-0.06f, -0.06f, -0.04f), Vec3(0.06f, 0.06f, 0.04f), kIron);
    for (int sg = -1; sg <= 1; sg += 2)
    {
        const float x = (float)sg * 0.11f;
        b.Beam(Vec3(0.0f, 0.0f, 0.28f), Vec3(x, -0.04f, 0.42f), 0.06f, kIronLight);
        b.Beam(Vec3(x, -0.04f, 0.42f), Vec3(x * 1.2f, 0.08f, 0.50f), 0.05f, kIronLight);
        b.Beam(Vec3(x * 1.2f, 0.08f, 0.50f), Vec3(x * 0.8f, 0.20f, 0.42f), 0.045f, kIronLight, 0.01f);
    }
    b.Beam(Vec3(0.0f, 0.0f, 0.30f), Vec3(0.0f, 0.0f, 0.52f), 0.06f, kIronLight, 0.01f);   // 真ん中の槍先
    return b.Build(device);
}
