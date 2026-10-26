// ============================================================
// 遺跡 1 面目の Boss「墓守の騎士」の飾り（2026-10-26）：大きな呪いの鎧。体は方块人（皮 = 呪いの鎧の鋼 RuinsArmor.png）に、
//   頭 = 角（曲がった骨）の付いた兜（丸い天辺・覗き穴・紫に光る目・紫の房）
//   胴 = 胸当てと紫に光る十字の紋・背当て・革の帯・腰の板・胸に斜めに掛けた鎖
//   右腕 = 丸い肩当て（棘付き）・籠手・大剣（柄・鍔・刃 + 紫の光筋）
//   左腕 = 丸い肩当て・籠手・前腕に巻いた鎖と手から垂れる鎖（双鎖の技の鎖はシーンの別物）
// 寸法は方块人の部品の包囲ボックスに対する割合（EnemyLooks.cpp の鎧と同じ流儀）。Boss は VS が 3 倍にする。
// 丸い物は LookBuilder の Ball / Tube、角と棘は Spike。色と宣言は BossLooksCommon.h
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    std::shared_ptr<Model> BuildKnightHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.09f * s.x;
        // 兜（頭より一回り大きい）+ 下の縁 + 丸い天辺
        b.Box(Vec3(lo.x - e, lo.y - 0.6f * e, lo.z - e), Vec3(hi.x + e, hi.y + 0.5f * e, hi.z + e), kKSteel, kKSteelDark);
        b.Box(Vec3(lo.x - 1.5f * e, lo.y - 0.9f * e, lo.z - 1.5f * e), Vec3(hi.x + 1.5f * e, lo.y + 0.14f * s.y, hi.z + 1.5f * e), kKSteelDark);
        b.Ball(Vec3(c.x, hi.y + 0.2f * e, c.z), 0.46f * s.x, kKSteel, 10, 4);
        // 正面の縦の筋と覗き穴（黒）+ 紫に光る目
        b.Box(Vec3(c.x - 0.05f * s.x, lo.y, hi.z + e), Vec3(c.x + 0.05f * s.x, hi.y + e, hi.z + 1.6f * e), kKSteelLight);
        const float slitY0 = lo.y + 0.45f * s.y, slitY1 = lo.y + 0.60f * s.y;
        b.Box(Vec3(lo.x - 0.2f * e, slitY0, hi.z + e), Vec3(hi.x + 0.2f * e, slitY1, hi.z + 1.7f * e), kKVoid);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.22f * s.x;
            b.Box(Vec3(ex - 0.08f * s.x, slitY0 + 0.02f * s.y, hi.z + 1.7f * e), Vec3(ex + 0.08f * s.x, slitY1 - 0.02f * s.y, hi.z + 1.9f * e), kKGlow);
            // 角：兜の横から外上へ曲がる骨（円柱 → 棘）
            const float sx = (float)sg;
            const Vec3 base(c.x + sx * 0.50f * s.x, hi.y - 0.10f * s.y, c.z);
            const Vec3 mid = base + Vec3(sx * 0.32f * s.x, 0.28f * s.y, -0.06f * s.z);
            const Vec3 tip = mid + Vec3(sx * 0.14f * s.x, 0.46f * s.y, -0.16f * s.z);
            b.Tube(base, mid, 0.13f * s.x, 0.10f * s.x, kKBone, 7);
            b.Ball(mid, 0.10f * s.x, kKBone, 7, 3);
            b.Spike(mid, tip, 0.19f * s.x, kKBone);
        }
        // 頭頂の紫の房（前から後ろへ垂れる）
        b.Box(Vec3(c.x - 0.07f * s.x, hi.y + 0.4f * e, lo.z - 0.5f * e), Vec3(c.x + 0.07f * s.x, hi.y + 0.55f * s.y, hi.z - 0.25f * s.z), kKCloth);
        b.Box(Vec3(c.x - 0.06f * s.x, hi.y - 0.25f * s.y, lo.z - 3.0f * e), Vec3(c.x + 0.06f * s.x, hi.y + 0.42f * s.y, lo.z - e), kKCloth);
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildKnightTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        const float e = 0.08f * s.z;
        // 胸当て + 真ん中の稜線 + 紫に光る十字の紋
        b.Box(Vec3(lo.x - e, c.y - 0.2f * s.y, hi.z), Vec3(hi.x + e, hi.y + 0.04f * s.y, hi.z + 2.0f * e), kKSteelLight, kKSteel);
        b.Box(Vec3(c.x - 0.06f * s.x, c.y - 0.2f * s.y, hi.z + 2.0f * e), Vec3(c.x + 0.06f * s.x, hi.y + 0.04f * s.y, hi.z + 2.8f * e), kKSteel);
        b.Box(Vec3(c.x - 0.04f * s.x, c.y - 0.12f * s.y, hi.z + 2.8f * e), Vec3(c.x + 0.04f * s.x, c.y + 0.30f * s.y, hi.z + 3.1f * e), kKGlow);
        b.Box(Vec3(c.x - 0.18f * s.x, c.y + 0.10f * s.y, hi.z + 2.8f * e), Vec3(c.x + 0.18f * s.x, c.y + 0.18f * s.y, hi.z + 3.1f * e), kKGlow);
        // 背当て
        b.Box(Vec3(lo.x - e, c.y - 0.2f * s.y, lo.z - 1.6f * e), Vec3(hi.x + e, hi.y, lo.z), kKSteel, kKSteelDark);
        // 革の帯と留め金
        b.Box(Vec3(lo.x - 1.2f * e, lo.y - 0.04f * s.y, lo.z - 1.2f * e), Vec3(hi.x + 1.2f * e, lo.y + 0.14f * s.y, hi.z + 1.2f * e), kKLeather);
        b.Box(Vec3(c.x - 0.08f * s.x, lo.y - 0.02f * s.y, hi.z + 1.2f * e), Vec3(c.x + 0.08f * s.x, lo.y + 0.12f * s.y, hi.z + 2.1f * e), kKSteelLight);
        // 腰の板（前・後ろ・横）
        b.Box(Vec3(lo.x + 0.05f * s.x, lo.y - 0.40f * s.y, hi.z + 0.2f * e), Vec3(hi.x - 0.05f * s.x, lo.y, hi.z + 1.0f * e), kKSteel, kKSteelDark);
        b.Box(Vec3(lo.x + 0.05f * s.x, lo.y - 0.36f * s.y, lo.z - 1.0f * e), Vec3(hi.x - 0.05f * s.x, lo.y, lo.z - 0.2f * e), kKSteel, kKSteelDark);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float x = (sg < 0) ? lo.x - 1.4f * e : hi.x + 0.4f * e;
            b.Box(Vec3(x, lo.y - 0.34f * s.y, c.z - 0.35f * s.z), Vec3(x + e, lo.y, c.z + 0.35f * s.z), kKSteel, kKSteelDark);
        }
        // 胸に斜めに掛けた鎖
        b.Chain(Vec3(lo.x - 0.5f * e, hi.y - 0.02f * s.y, hi.z + 2.4f * e), Vec3(hi.x + 0.5f * e, lo.y + 0.10f * s.y, hi.z + 2.4f * e), 0.10f, 0.028f, kKChain);
        return b.Build(device);
    }

    namespace
    {
        // 丸い肩当て（棘付き）と籠手
        void KnightShoulder(Builder& b, const ArmFrame& f, const Vec3& pivot)
        {
            const float w = f.w;
            const float out = (pivot.x >= 0.0f) ? 1.0f : -1.0f;
            const Vec3 sc = pivot + Vec3(out * 0.55f * w, 0.35f * w, 0.0f);
            b.Ball(sc, 1.05f * w, kKSteel, 10, 4);
            b.Ball(sc + Vec3(0.0f, 0.15f * w, 0.0f), 0.85f * w, kKSteelLight, 10, 3);
            b.Spike(sc + Vec3(out * 0.5f * w, 0.8f * w, 0.0f), sc + Vec3(out * 1.0f * w, 1.7f * w, -0.2f * w), 0.30f * w, kKBone);
            b.Spike(sc + Vec3(out * 0.9f * w, 0.3f * w, 0.5f * w), sc + Vec3(out * 1.7f * w, 0.7f * w, 0.9f * w), 0.22f * w, kKBone);
            b.OBox(f.hand - f.down * (0.15f * w), f.ax, f.ay, f.az, Vec3(0.62f * w, 0.45f * w, 0.62f * w), kKSteelLight, kKSteel);
        }
    }

    // 右腕：肩当て + 籠手 + 大剣（柄は手の中、刃は手から先へ）
    std::shared_ptr<Model> BuildKnightSwordArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const float w = f.w;
        KnightShoulder(b, f, pivot);
        const Vec3 grip = f.hand + f.az * (0.15f * w);
        b.Tube(grip - f.down * 0.32f, grip + f.down * 0.22f, 0.055f, 0.055f, kKLeather, 7);
        b.Ball(grip - f.down * 0.34f, 0.075f, kKSteelDark, 7, 3);                                     // 柄頭
        b.OBox(grip + f.down * 0.26f, f.ax, f.ay, f.az, Vec3(0.045f, 0.05f, 0.34f), kKSteelDark, kKSteelDark);   // 鍔
        b.OBox(grip + f.down * 0.95f, f.ax, f.ay, f.az, Vec3(0.028f, 0.66f, 0.15f), kKSteelLight, kKSteel);   // 刃
        b.OBox(grip + f.down * 0.95f, f.ax, f.ay, f.az, Vec3(0.034f, 0.52f, 0.035f), kKGlow, kKGlow);          // 刃の紫の光筋
        b.Spike(grip + f.down * 1.60f, grip + f.down * 1.95f, 0.26f, kKSteelLight);                      // 切っ先
        return b.Build(device);
    }

    // 左腕：肩当て + 籠手 + 前腕に巻いた鎖 + 手から垂れる鎖と鉤
    std::shared_ptr<Model> BuildKnightChainArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        const ArmFrame f = FrameOf(A, pivot);
        const float w = f.w;
        KnightShoulder(b, f, pivot);
        b.Chain(f.hand - f.down * 0.55f + f.az * (0.40f * w), f.hand - f.down * 0.10f - f.az * (0.40f * w), 0.09f, 0.024f, kKChain);
        b.Chain(f.hand - f.down * 0.40f - f.az * (0.40f * w), f.hand + f.down * 0.05f + f.az * (0.40f * w), 0.09f, 0.024f, kKChain);
        const Vec3 hang = f.hand + f.down * 0.75f + f.az * (0.25f * w);
        b.Chain(f.hand + f.down * 0.05f, hang, 0.10f, 0.028f, kKChain);
        b.Spike(hang, hang + f.down * 0.14f + f.az * 0.10f, 0.08f, kKSteelLight);
        return b.Build(device);
    }
}
