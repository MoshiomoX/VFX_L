// ============================================================
// 遺跡 2 面目の Boss「石の守護獣」（2026-10-29、ユーザー「残りの Boss も作って」= 方案表の有翼の石竜）:
// 風化した灰色の石の竜（遺跡の門に据えられたガーゴイルが目覚めた物）。方块人の体は描かず、全部を胴（torso）に付ける：
//   ・胴：石の板を組んだ太い胴・胸・腰、背筋の棘、肩の翼の付け根、脇腹と胸のひびの奥に光る琥珀の紋、背の苔
//   ・首 + 頭（mode 3 = 首の付け根で縦に揺れ、息・急降下で頭を前下へ）：太い首、角張った頭、後ろへ反る 2 本の角、光る目、牙
//   ・脚 × 4（mode 4 = 雄鹿と同じ斜対歩）：太い石の脚、3 本の爪
//   ・尾（mode 3 = 付け根で縦に揺れ、技で持ち上げる）：節の尾、背の棘、先に岩の槌（尾の薙ぎ払いで振る）
//   ・翼 × 2（mode 2 = 付け根の前後の軸で回す。CPU が BomberCB::bossWing で羽ばたき・振り上げ・畳み）：腕の骨 + 4 本の指 + 石の膜
// 座標は焼いた網の空間（足元 y -0.9、+Z = 正面）。Boss は VS が 3.4 倍（BossAttacks::bodyScale）= 肩 3.4m・翼を広げて 12m
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    // 胴・首・尾・翼は「設計の座標」で書き、Builder の move で kDrop 下げて置く（1 版目は胴が高く脚が細長い竹馬のようだった。
    // 胴を下げて脚を短く太く = 重い石の竜）。VS へ渡す付け根（首・尾・翼・脚）は下げた後の座標
    constexpr float kDrop = 0.28f;
    const Vec3 kNeckD = { 0.0f, 0.40f, 0.52f };         // 設計の座標の首の付け根
    const Vec3 kTailRootD = { 0.0f, 0.26f, -0.62f };
    const Vec3 kBeastNeck = { kNeckD.x, kNeckD.y - kDrop, kNeckD.z };
    const Vec3 kBeastTailRoot = { kTailRootD.x, kTailRootD.y - kDrop, kTailRootD.z };
    Vec3 BeastLegRoot(int i)
    {
        const bool front = i < 2;
        const float sx = (i & 1) ? 1.0f : -1.0f;
        return Vec3(sx * 0.31f, 0.10f - kDrop, front ? 0.34f : -0.48f);
    }
    Vec3 BeastWingRootD(float sx) { return Vec3(sx * 0.30f, 0.46f, 0.26f); }
    Vec3 BeastWingRoot(float sx) { return BeastWingRootD(sx) - Vec3(0.0f, kDrop, 0.0f); }

    namespace
    {
        Builder DropBuilder()
        {
            Builder b;
            b.move = Vec3(0.0f, -kDrop, 0.0f);
            return b;
        }
        // 苔の塊：上面 y にある lo〜hi の範囲へ n 枚
        void Lichen(Builder& b, std::mt19937& rng, const Vec3& lo, const Vec3& hi, float y, int n)
        {
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const float x = lo.x + (hi.x - lo.x) * u(rng), z = lo.z + (hi.z - lo.z) * u(rng);
                const float w = 0.04f + 0.06f * u(rng), d = 0.04f + 0.06f * u(rng), h = 0.02f + 0.02f * u(rng);
                b.Box(Vec3(x - w, y - 0.01f, z - d), Vec3(x + w, y + h, z + d), (i & 1) ? kBLichenLight : kBLichen, kBLichen, 0.012f, &rng);
            }
        }
        // ひびの奥の紋：a → b の折れ線（n 段、横へ少しずらす）
        void RuneCrack(Builder& b, std::mt19937& rng, const Vec3& a, const Vec3& c, int n, const Vec3& side)
        {
            std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            Vec3 prev = a;
            for (int i = 1; i <= n; ++i)
            {
                const float t = (float)i / (float)n;
                const Vec3 p = a + (c - a) * t + side * (0.035f * u(rng));
                b.Beam(prev, p, 0.022f, kBRune);
                prev = p;
            }
        }
    }

    std::shared_ptr<Model> BuildBeastBody(ID3D11Device* device)
    {
        Builder b = DropBuilder();
        std::mt19937 rng(4401u);
        // 胴・胸・腰（石の板を組んだ形。角を少し揺らして風化させる。2 版目で太く・厚く）
        b.Box(Vec3(-0.38f, -0.14f, -0.50f), Vec3(0.38f, 0.43f, 0.36f), kBStone, kBStoneDark, 0.02f, &rng);
        b.Box(Vec3(-0.41f, -0.08f, 0.26f), Vec3(0.41f, 0.52f, 0.60f), kBStoneLight, kBStone, 0.02f, &rng);       // 胸（前へ張る）
        b.Box(Vec3(-0.36f, -0.10f, -0.70f), Vec3(0.36f, 0.39f, -0.46f), kBStone, kBStoneDark, 0.02f, &rng);      // 腰
        for (int i = 0; i < 4; ++i)   // 腹の板（明るい石の節）
        {
            const float z0 = -0.46f + 0.22f * (float)i;
            b.Box(Vec3(-0.30f, -0.23f, z0), Vec3(0.30f, -0.12f, z0 + 0.19f), kBStoneLight, kBStone);
        }
        // 肩（翼の付け根の石の塊）
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = (float)side;
            b.Box(Vec3(sx * 0.24f - 0.10f, 0.32f, 0.12f), Vec3(sx * 0.24f + 0.10f, 0.54f, 0.42f), kBStoneLight, kBStone, 0.015f, &rng);
            // 脇腹のひびの紋（2 本ずつ）
            RuneCrack(b, rng, Vec3(sx * 0.385f, 0.30f, -0.30f), Vec3(sx * 0.385f, -0.02f, 0.04f), 5, Vec3(0.0f, 0.0f, 1.0f));
            RuneCrack(b, rng, Vec3(sx * 0.385f, 0.24f, 0.10f), Vec3(sx * 0.385f, -0.04f, 0.24f), 3, Vec3(0.0f, 0.0f, 1.0f));
            RuneCrack(b, rng, Vec3(sx * 0.365f, 0.30f, -0.68f), Vec3(sx * 0.365f, 0.02f, -0.52f), 3, Vec3(0.0f, 0.0f, 1.0f));
        }
        // 胸の紋（菱形の枠 + 光る芯）
        {
            const Vec3 c(0.0f, 0.24f, 0.605f);
            b.Beam(c + Vec3(0.0f, 0.13f, 0.0f), c + Vec3(0.11f, 0.0f, 0.0f), 0.03f, kBRune);
            b.Beam(c + Vec3(0.11f, 0.0f, 0.0f), c + Vec3(0.0f, -0.13f, 0.0f), 0.03f, kBRune);
            b.Beam(c + Vec3(0.0f, -0.13f, 0.0f), c + Vec3(-0.11f, 0.0f, 0.0f), 0.03f, kBRune);
            b.Beam(c + Vec3(-0.11f, 0.0f, 0.0f), c + Vec3(0.0f, 0.13f, 0.0f), 0.03f, kBRune);
            b.Box(c - Vec3(0.035f, 0.05f, 0.0f), c + Vec3(0.035f, 0.05f, 0.03f), kBEye);
        }
        // 背筋の棘（大小交互、後ろへ傾く）
        for (int i = 0; i < 7; ++i)
        {
            const float z = 0.42f - 0.17f * (float)i;
            const float h = (i & 1) ? 0.12f : 0.20f;
            b.Spike(Vec3(0.0f, 0.42f, z), Vec3(0.0f, 0.42f + h, z - 0.08f), (i & 1) ? 0.07f : 0.10f, (i & 1) ? kBStoneLight : kBStone);
        }
        // 背の苔
        Lichen(b, rng, Vec3(-0.34f, 0.0f, -0.62f), Vec3(-0.06f, 0.0f, 0.30f), 0.43f, 8);
        Lichen(b, rng, Vec3(0.06f, 0.0f, -0.62f), Vec3(0.34f, 0.0f, 0.30f), 0.43f, 8);
        Lichen(b, rng, Vec3(-0.34f, 0.0f, 0.30f), Vec3(0.34f, 0.0f, 0.56f), 0.52f, 4);
        return b.Build(device);
    }

    // 首 + 頭（付け根 kBeastNeck。mode 3 で付け根の横軸の周りに縦に揺れ、技で前下へ下げる）
    std::shared_ptr<Model> BuildBeastHead(ID3D11Device* device)
    {
        Builder b = DropBuilder();
        std::mt19937 rng(4402u);
        const Vec3 n0 = kNeckD;
        const Vec3 n1 = n0 + Vec3(0.0f, 0.22f, 0.24f);
        const Vec3 n2 = n0 + Vec3(0.0f, 0.38f, 0.46f);   // 頭の付け根
        b.Beam(n0, n1, 0.30f, kBStone, 0.26f);
        b.Beam(n1, n2, 0.26f, kBStone, 0.22f);
        for (int i = 0; i < 3; ++i)   // 首の後ろの棘
        {
            const Vec3 p = n0 + (n2 - n0) * (0.15f + 0.3f * (float)i) + Vec3(0.0f, 0.12f, -0.04f);
            b.Spike(p, p + Vec3(0.0f, 0.12f, -0.10f), 0.07f, kBStoneLight);
        }
        // 頭蓋（角張った石）・眉・鼻面・下顎
        b.Box(n2 + Vec3(-0.15f, -0.08f, -0.08f), n2 + Vec3(0.15f, 0.12f, 0.20f), kBStoneLight, kBStone, 0.015f, &rng);
        b.Box(n2 + Vec3(-0.16f, 0.06f, 0.08f), n2 + Vec3(0.16f, 0.13f, 0.22f), kBStone, kBStoneDark);                 // 眉
        b.Box(n2 + Vec3(-0.10f, -0.07f, 0.20f), n2 + Vec3(0.10f, 0.06f, 0.46f), kBStoneLight, kBStone, 0.01f, &rng);    // 鼻面
        b.Box(n2 + Vec3(-0.09f, -0.18f, 0.08f), n2 + Vec3(0.09f, -0.11f, 0.42f), kBStone, kBStoneDark);                // 下顎（少し開く）
        b.Box(n2 + Vec3(-0.07f, -0.11f, 0.12f), n2 + Vec3(0.07f, -0.07f, 0.40f), kKVoid);                              // 口の中の闇
        b.Box(n2 + Vec3(-0.04f, 0.0f, 0.455f), n2 + Vec3(0.04f, 0.04f, 0.47f), kBRune);                                // 鼻の奥の光
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = (float)side;
            // 目（光る琥珀）
            b.Box(n2 + Vec3(sx * 0.15f - 0.02f, 0.03f, 0.13f), n2 + Vec3(sx * 0.15f + 0.02f, 0.08f, 0.20f), kBEye);
            // 牙（上顎から下へ、下顎から上へ）
            for (int k = 0; k < 3; ++k)
            {
                const float z = 0.22f + 0.08f * (float)k;
                b.Spike(n2 + Vec3(sx * 0.08f, -0.07f, z), n2 + Vec3(sx * 0.08f, -0.13f, z + 0.01f), 0.03f, kBClaw);
                b.Spike(n2 + Vec3(sx * 0.075f, -0.11f, z + 0.04f), n2 + Vec3(sx * 0.075f, -0.06f, z + 0.05f), 0.025f, kBClaw);
            }
            // 後ろへ反る角（2 段 + 先）と、その下の小さな角
            const Vec3 h0 = n2 + Vec3(sx * 0.11f, 0.10f, 0.0f);
            const Vec3 h1 = h0 + Vec3(sx * 0.10f, 0.16f, -0.16f);
            const Vec3 h2 = h1 + Vec3(sx * 0.04f, 0.06f, -0.22f);
            b.Beam(h0, h1, 0.09f, kBStone, 0.07f);
            b.Beam(h1, h2, 0.07f, kBStoneLight, 0.05f);
            b.Spike(h2, h2 + Vec3(0.0f, -0.02f, -0.14f), 0.05f, kBStoneLight);
            b.Spike(n2 + Vec3(sx * 0.14f, 0.0f, -0.02f), n2 + Vec3(sx * 0.26f, -0.02f, -0.16f), 0.05f, kBStone);
            // 頬の襞（後ろへ張る板）
            const Vec3 q[4] = { n2 + Vec3(sx * 0.15f, -0.06f, 0.10f), n2 + Vec3(sx * 0.15f, 0.04f, -0.04f),
                                n2 + Vec3(sx * 0.24f, -0.02f, -0.14f), n2 + Vec3(sx * 0.21f, -0.10f, -0.02f) };
            b.Plate(q, Vec3(1.0f, 0.0f, 0.0f), 0.02f, kBStoneDark);
        }
        Lichen(b, rng, Vec3(-0.12f, 0.0f, -0.06f), Vec3(0.12f, 0.0f, 0.14f), n2.y + 0.12f, 3);
        return b.Build(device);
    }

    // 脚 1 本（i = 0 左前 / 1 右前 / 2 左後 / 3 右後）。上から 肩 / 腿・膝・脛・足と 3 本の爪。足元 y -0.9
    std::shared_ptr<Model> BuildBeastLeg(ID3D11Device* device, int i)
    {
        Builder b;
        const bool front = i < 2;
        const Vec3 hinge = BeastLegRoot(i);
        // 2 版目：胴を下げた分だけ短く、太く（腿 0.24 → 0.32、脛 0.16 → 0.22）。付け根に肩 / 腿の大きな石の玉
        const Vec3 knee = hinge + Vec3(0.0f, -0.30f, front ? 0.07f : -0.11f);
        const Vec3 ankle(hinge.x, -0.74f, hinge.z + (front ? -0.01f : 0.04f));
        b.Ball(hinge + Vec3(0.0f, 0.08f, 0.0f), 0.22f, kBStone, 8, 4);
        b.Beam(hinge + Vec3(0.0f, 0.08f, 0.0f), knee, 0.32f, kBStone, 0.24f);
        b.Ball(knee, 0.14f, kBStoneLight, 8, 4);
        b.Beam(knee, ankle, 0.22f, kBStoneDark, 0.18f);
        b.Box(Vec3(ankle.x - 0.14f, -0.90f, ankle.z - 0.12f), Vec3(ankle.x + 0.14f, -0.72f, ankle.z + 0.16f), kBStone, kBStoneDark);
        for (int k = -1; k <= 1; ++k)
        {
            const Vec3 base(ankle.x + 0.09f * (float)k, -0.84f, ankle.z + 0.15f);
            b.Spike(base, base + Vec3(0.03f * (float)k, -0.05f, 0.14f), 0.065f, kBClaw);
        }
        return b.Build(device);
    }

    // 尾（付け根 kBeastTailRoot。後ろへ伸びて少し下がり、先が持ち上がる節）+ 先の岩の槌
    std::shared_ptr<Model> BuildBeastTail(ID3D11Device* device)
    {
        Builder b = DropBuilder();
        const Vec3 pts[6] = { kTailRootD, Vec3(0.0f, 0.18f, -0.92f), Vec3(0.0f, 0.10f, -1.20f), Vec3(0.0f, 0.10f, -1.46f),
                              Vec3(0.0f, 0.17f, -1.68f), Vec3(0.0f, 0.26f, -1.84f) };
        const float r[6] = { 0.15f, 0.13f, 0.11f, 0.09f, 0.075f, 0.065f };
        for (int i = 0; i < 5; ++i)
        {
            b.Tube(pts[i], pts[i + 1], r[i], r[i + 1], (i & 1) ? kBStone : kBStoneLight, 8);
            b.Ball(pts[i + 1], r[i + 1] * 1.08f, kBStoneDark, 8, 4);
            const Vec3 m = (pts[i] + pts[i + 1]) * 0.5f;
            b.Spike(m + Vec3(0.0f, r[i] * 0.8f, 0.0f), m + Vec3(0.0f, r[i] * 0.8f + 0.11f, -0.07f), 0.06f, kBStoneLight);
        }
        // 岩の槌：丸い岩 + 周りの棘 + 光る紋の帯
        const Vec3 club = pts[5] + Vec3(0.0f, 0.04f, -0.10f);
        b.Ball(club, 0.17f, kBStone, 10, 5);
        b.Tube(club + Vec3(0.0f, 0.0f, 0.04f), club + Vec3(0.0f, 0.0f, -0.04f), 0.175f, 0.175f, kBRune, 12);
        const Vec3 dirs[6] = { { 1, 0.2f, 0 }, { -1, 0.2f, 0 }, { 0, 1, 0 }, { 0, -0.6f, 0.4f }, { 0.6f, 0.5f, -0.6f }, { -0.6f, 0.5f, -0.6f } };
        for (const Vec3& d0 : dirs)
        {
            Vec3 d = d0;
            d.Normalize();
            b.Spike(club + d * 0.14f, club + d * 0.30f, 0.08f, kBStoneLight);
        }
        return b.Build(device);
    }

    // 翼 1 枚（sx = -1 左 / +1 右）。付け根 BeastWingRoot から外・後ろへ：腕の骨（付け根 → 肘 → 手首）+ 手首から 4 本の指の骨、
    // 骨の間に石の膜。焼いた形は横へ広げた形（VS が先を上へ回して畳む / 羽ばたく）
    std::shared_ptr<Model> BuildBeastWing(ID3D11Device* device, float sx)
    {
        Builder b = DropBuilder();
        const Vec3 s0 = BeastWingRootD(sx);
        const Vec3 el(sx * 0.85f, 0.62f, 0.08f);
        const Vec3 wr(sx * 1.30f, 0.74f, -0.08f);
        const Vec3 tips[4] = { Vec3(sx * 1.78f, 0.82f, -0.34f), Vec3(sx * 1.72f, 0.66f, -0.76f), Vec3(sx * 1.30f, 0.52f, -1.02f), Vec3(sx * 0.84f, 0.46f, -0.86f) };
        const Vec3 bodyEdge(sx * 0.34f, 0.44f, -0.34f);
        // 膜（骨の間の三角を 4 点の薄板で。1 点目を重ねて三角に）
        const Vec3 up(0.0f, 1.0f, 0.0f);
        {
            const Vec3 q[4] = { s0, el, tips[3], bodyEdge };
            b.Plate(q, up, 0.02f, kBMembrane);
        }
        {
            const Vec3 q[4] = { el, wr, tips[3], tips[3] };
            b.Plate(q, up, 0.02f, kBMembrane);
        }
        for (int k = 0; k < 3; ++k)
        {
            const Vec3 q[4] = { wr, tips[k], tips[k + 1], tips[k + 1] };
            b.Plate(q, up, 0.02f, (k & 1) ? kBStoneDark : kBMembrane);
        }
        // 骨：腕（太い）、指（細く先は尖る）、縁の骨
        b.Beam(s0, el, 0.11f, kBStone, 0.09f);
        b.Ball(el, 0.07f, kBStoneLight, 8, 4);
        b.Beam(el, wr, 0.09f, kBStone, 0.07f);
        b.Ball(wr, 0.065f, kBStoneLight, 8, 4);
        for (int k = 0; k < 4; ++k)
            b.Beam(wr, tips[k], 0.05f, kBStoneLight, 0.02f);
        // 手首の爪（前上へ）と腕の紋
        b.Spike(wr + Vec3(0.0f, 0.04f, 0.02f), wr + Vec3(sx * 0.06f, 0.16f, 0.16f), 0.05f, kBClaw);
        b.Beam(s0 + (el - s0) * 0.2f + Vec3(0.0f, 0.05f, 0.0f), s0 + (el - s0) * 0.8f + Vec3(0.0f, 0.05f, 0.0f), 0.02f, kBRune);
        return b.Build(device);
    }
}
