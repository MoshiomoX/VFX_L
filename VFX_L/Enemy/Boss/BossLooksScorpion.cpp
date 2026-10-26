// ============================================================
// 砂漠 2 面目の Boss「大サソリ」（2026-10-26、ユーザー「砂漠の Boss にサソリを入れたい」「方块の動物 + プログラムの動きで」）:
// 方块人の体は描かず（SetBossLook の hideBody）、部品を骨にして飾りだけで組む。
// 10-26 夜にユーザーの参考画（黒紫の棘だらけの甲羅、関節と腹の隙間から毒の緑が透ける、緑に光る目、鋸歯の鋏、毒液の滴る針）に
// 寄せて作り直し。丸い物は LookBuilder の Tube / Ball（ユーザー「四角と三角だけでなく円柱・球も使っていい」）、棘は Spike：
//   胴 = 甲羅（重なった 5 枚の甲片 + 背と縁の棘 + 甲片の間の緑の筋）+ 腹（暗い、緑の層）+ 頭甲（緑の目・額の棘・顎）。torso に付く
//   脚 = 8 本を 1 本ずつ胴に付け、VS の mode 5（付け根の縦軸で前後に掻く、三脚歩行）。付け根の球・腿の円柱・膝の緑の球・脛・爪先
//   鋏 = 胴に付ける（腕の部品に付けると歩きの振りで上下に暴れた）。肩の球・上腕 / 前腕の円柱・肘の緑・手のひら・鋸歯の 2 本の顎
//   尾 = 6 節の円柱 + 関節の緑の球 + 各節の背の棘 + 緑に光る毒嚢（球）+ 針 + 滴。torso に付け、VS の mode 3 で付け根の周りに揺れ、
//        毒針の溜めで後ろへ反り、突っ込みで前へ振り下ろす
// 寸法は焼いた網の空間（足元 y -0.9）。Boss は VS が 3 倍にする = 幅 3.3m・尾の先 5m 余り
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    std::shared_ptr<Model> BuildScorpionBody(ID3D11Device* device)
    {
        Builder b;
        // 腹（暗い）と、腹と甲羅の間に透ける毒の緑の層
        b.Box(Vec3(-0.46f, -0.82f, -0.66f), Vec3(0.46f, -0.70f, 0.52f), kChitinDark);
        b.Box(Vec3(-0.43f, -0.72f, -0.64f), Vec3(0.43f, -0.62f, 0.50f), kVenomDim);
        // 甲羅の本体
        b.Box(Vec3(-0.52f, -0.68f, -0.66f), Vec3(0.52f, -0.42f, 0.52f), kChitin, kChitinDark);
        // 背の 5 枚の甲片：後ろの縁が上がり、次の甲片の上に重なる。各甲片の下の縁に緑の筋、背に棘 3 本
        for (int i = 0; i < 5; ++i)
        {
            const float z0 = -0.64f + 0.235f * (float)i, z1 = z0 + 0.27f;
            Vec3 v[8] = {
                { -0.50f, -0.50f, z0 }, { 0.50f, -0.50f, z0 }, { 0.50f, -0.52f, z1 }, { -0.50f, -0.52f, z1 },
                { -0.50f, -0.33f, z0 }, { 0.50f, -0.33f, z0 }, { 0.50f, -0.40f, z1 }, { -0.50f, -0.40f, z1 } };
            b.Hex(v, kChitinLight, kChitin);
            b.Box(Vec3(-0.48f, -0.46f, z1 - 0.02f), Vec3(0.48f, -0.40f, z1 + 0.02f), kVenomDim);
            for (int k = -1; k <= 1; ++k)
            {
                const Vec3 base((float)k * 0.30f, -0.33f, z0 + 0.06f);
                b.Spike(base, base + Vec3((float)k * 0.08f, 0.22f, -0.12f), 0.09f, kSpike);
            }
        }
        // 側縁の棘（左右 4 本ずつ、外へ）
        for (int i = 0; i < 4; ++i)
            for (int side = -1; side <= 1; side += 2)
            {
                const float sx = (float)side;
                const Vec3 base(sx * 0.50f, -0.45f, -0.55f + 0.30f * (float)i);
                b.Spike(base, base + Vec3(sx * 0.22f, 0.05f, -0.04f), 0.08f, kSpike);
            }
        // 頭甲（前、少し下がる）+ 額の張り出し + 緑の目 + 額の棘 + 顎 + 口の緑
        b.Box(Vec3(-0.36f, -0.72f, 0.50f), Vec3(0.36f, -0.46f, 0.92f), kChitinLight, kChitin);
        b.Box(Vec3(-0.30f, -0.50f, 0.80f), Vec3(0.30f, -0.42f, 0.94f), kChitin);
        b.Ball(Vec3(-0.16f, -0.52f, 0.92f), 0.06f, kScorpEye, 8, 3);
        b.Ball(Vec3(0.16f, -0.52f, 0.92f), 0.06f, kScorpEye, 8, 3);
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = (float)side;
            b.Spike(Vec3(sx * 0.22f, -0.44f, 0.70f), Vec3(sx * 0.30f, -0.28f, 0.60f), 0.07f, kSpike);
            b.Spike(Vec3(sx * 0.12f, -0.68f, 0.90f), Vec3(sx * 0.16f, -0.84f, 1.04f), 0.07f, kChitinDark);   // 顎
        }
        b.Box(Vec3(-0.10f, -0.70f, 0.88f), Vec3(0.10f, -0.62f, 0.93f), kVenom);
        return b.Build(device);
    }

    // 脚 1 本：付け根 root から外上へ腿、膝（緑の球）から外下へ脛、爪先。膝の上に棘
    std::shared_ptr<Model> BuildScorpionLeg(ID3D11Device* device, const Vec3& root, float sx)
    {
        Builder b;
        const Vec3 knee = root + Vec3(sx * 0.45f, 0.38f, 0.03f);
        const Vec3 foot(sx * 1.25f, -0.90f, root.z + 0.12f);
        b.Ball(root, 0.10f, kChitinDark, 8, 3);
        b.Tube(root, knee, 0.085f, 0.07f, kChitin, 7);
        b.Spike(knee + Vec3(0.0f, 0.04f, 0.0f), knee + Vec3(sx * 0.06f, 0.24f, -0.02f), 0.07f, kSpike);
        b.Ball(knee, 0.085f, kVenomDim, 8, 3);
        b.Tube(knee, foot, 0.065f, 0.025f, kChitin, 7);
        const Vec3 mid = (knee + foot) * 0.5f;
        b.Spike(mid, mid + Vec3(sx * 0.04f, 0.16f, -0.02f), 0.05f, kSpike);
        b.Ball(foot, 0.035f, kClawTip, 6, 3);
        return b.Build(device);
    }

    // 鋏：肩（胴の前の角）から外上へ上腕、肘（緑）から前下へ前腕、太い手のひら、鋸歯の付いた上下の顎（間に毒の緑）
    std::shared_ptr<Model> BuildScorpionClaw(ID3D11Device* device, float sx)
    {
        Builder b;
        const Vec3 P(sx * 0.40f, -0.50f, 0.48f);
        const Vec3 elbow(sx * 0.80f, -0.28f, 0.62f);
        const Vec3 wrist(sx * 0.74f, -0.48f, 1.08f);
        b.Ball(P, 0.12f, kChitinDark, 8, 3);
        b.Tube(P, elbow, 0.13f, 0.11f, kChitin, 8);
        b.Ball(elbow, 0.12f, kVenomDim, 8, 3);
        b.Spike(elbow + Vec3(0.0f, 0.05f, 0.0f), elbow + Vec3(sx * 0.05f, 0.28f, -0.08f), 0.08f, kSpike);
        b.Tube(elbow, wrist, 0.12f, 0.14f, kChitin, 8);
        // 手のひら
        // 手のひら（小さめ。顎の方を大きく = 参考画の巨大な鋸歯の鋏）
        const Vec3 palm = wrist + Vec3(0.0f, 0.0f, 0.18f);
        b.OBox(palm, Vec3(sx, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.17f, 0.14f, 0.20f), kChitinLight, kChitin);
        b.Spike(palm + Vec3(sx * 0.06f, 0.12f, -0.05f), palm + Vec3(sx * 0.12f, 0.36f, -0.15f), 0.08f, kSpike);
        b.Spike(palm + Vec3(sx * 0.16f, 0.0f, 0.0f), palm + Vec3(sx * 0.36f, 0.04f, -0.06f), 0.08f, kSpike);
        // 顎：下（固定）と上（可動）。根元から先へ細る円柱、内側に大きな歯、外側に棘
        const Vec3 jawBase = palm + Vec3(0.0f, 0.0f, 0.20f);
        const Vec3 a0 = jawBase + Vec3(0.0f, -0.10f, 0.0f), t0 = jawBase + Vec3(sx * 0.14f, -0.20f, 0.86f);
        const Vec3 a1 = jawBase + Vec3(0.0f, 0.10f, 0.0f), t1 = jawBase + Vec3(sx * 0.12f, 0.26f, 0.80f);
        b.Tube(a0, t0, 0.14f, 0.03f, kChitin, 6);
        b.Tube(a1, t1, 0.13f, 0.03f, kChitin, 6);
        for (int k = 1; k <= 5; ++k)
        {
            const float t = (float)k / 6.0f;
            const float h = 0.14f * (1.0f - 0.5f * t);   // 根元の歯が大きい
            const Vec3 p0 = a0 + (t0 - a0) * t + Vec3(0.0f, 0.05f, 0.0f);
            const Vec3 p1 = a1 + (t1 - a1) * t - Vec3(0.0f, 0.05f, 0.0f);
            b.Spike(p0, p0 + Vec3(0.0f, h, 0.03f), 0.06f, kClawTip);
            b.Spike(p1, p1 - Vec3(0.0f, h, -0.03f), 0.06f, kClawTip);
            if (k & 1)
            {
                b.Spike(p0 - Vec3(0.0f, 0.10f, 0.0f), p0 - Vec3(-sx * 0.02f, 0.26f, -0.02f), 0.05f, kSpike);
                b.Spike(p1 + Vec3(0.0f, 0.10f, 0.0f), p1 + Vec3(sx * 0.02f, 0.26f, 0.02f), 0.05f, kSpike);
            }
        }
        b.Box(jawBase + Vec3(-0.05f, -0.04f, 0.05f), jawBase + Vec3(0.05f, 0.04f, 0.34f), kVenomDim);
        return b.Build(device);
    }

    // 尾：付け根 hinge から後ろ上へ弧を描いて頭の上へ。節 = 円柱、関節 = 緑の球、各節の背（弧の外側）に棘、先に毒嚢・針・滴
    std::shared_ptr<Model> BuildScorpionTail(ID3D11Device* device, const Vec3& hinge)
    {
        Builder b;
        const Vec3 pts[7] = { hinge, hinge + Vec3(0, 0.32f, -0.32f), hinge + Vec3(0, 0.72f, -0.40f), hinge + Vec3(0, 1.10f, -0.22f),
                              hinge + Vec3(0, 1.38f, 0.18f), hinge + Vec3(0, 1.45f, 0.62f), hinge + Vec3(0, 1.30f, 0.98f) };
        const float w[7] = { 0.30f, 0.27f, 0.24f, 0.22f, 0.20f, 0.18f, 0.16f };
        const Vec3 curl = hinge + Vec3(0.0f, 0.9f, 0.3f);   // 弧の中心（棘はここから外へ）
        for (int i = 0; i < 6; ++i)
        {
            b.Tube(pts[i], pts[i + 1], w[i] * 0.5f, w[i + 1] * 0.5f, (i & 1) ? kChitin : kChitinLight, 8);
            if (i < 5) b.Ball(pts[i + 1], w[i + 1] * 0.55f, kVenomDim, 8, 3);
            const Vec3 mid = (pts[i] + pts[i + 1]) * 0.5f;
            Vec3 out = mid - curl;
            out.Normalize();
            b.Spike(mid, mid + out * 0.30f, 0.10f, kSpike);
            b.Spike(mid + Vec3(0.12f, 0.0f, 0.0f), mid + Vec3(0.22f, 0.0f, 0.0f) + out * 0.16f, 0.06f, kSpike);
            b.Spike(mid - Vec3(0.12f, 0.0f, 0.0f), mid - Vec3(0.22f, 0.0f, 0.0f) + out * 0.16f, 0.06f, kSpike);
        }
        const Vec3 bulb = pts[6];
        b.Ball(bulb, 0.17f, kVenom, 10, 5);                                                            // 毒嚢（光る）
        b.Box(bulb + Vec3(-0.12f, 0.02f, -0.16f), bulb + Vec3(0.12f, 0.17f, 0.04f), kChitin, kChitinDark);   // 毒嚢の上の甲
        b.Tube(bulb + Vec3(0, -0.02f, 0.12f), bulb + Vec3(0, -0.26f, 0.52f), 0.07f, 0.012f, kChitinDark, 6);   // 針
        b.Ball(bulb + Vec3(0, -0.34f, 0.48f), 0.035f, kVenom, 6, 3);                                   // 滴る毒
        return b.Build(device);
    }
}
