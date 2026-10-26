// ============================================================
// 草原 2 面目の Boss「月下の狼人」の飾り（2026-10-26、ユーザー「草原 2 も人形、半人半獣」→ 狼人）：
// 方块人の体は描かず（hideBody）、6 つの部品（頭・胴・腕 × 2・脚 × 2）それぞれに毛皮の部品を付ける = 部品の歩き / 殴りの動きに
// そのまま付いて来る人形の獣。
//   頭 = 狼の頭（長い鼻面・黒い鼻・開いた顎と牙・赤く光る目・立った耳・首の後ろのたてがみ）
//   胴 = 猫背の毛皮の胴（背の盛り上がり・胸の明るい毛・肩の毛房・背筋の毛・尾）
//   腕 = 肩の球・毛皮の上腕 / 前腕（円柱）・手の球・4 本の爪
//   脚 = 腿・脛（後ろへ曲がる）・足の先と 3 本の爪、膝の毛房
// 寸法は方块人の部品の包囲ボックスに対する割合。Boss は VS が 3 倍にする。狂暴の赤はシーンが bodyTint に入れる
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    namespace
    {
        // 毛房：base から dir の向きへ n 本、少しずつ向きを散らす
        void FurTufts(Builder& b, std::mt19937& rng, const Vec3& base, const Vec3& dir, float spread, float len, float w, int n, const Vec4& c)
        {
            std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const Vec3 a = base + Vec3(u(rng), u(rng), u(rng)) * spread;
                const Vec3 d = dir + Vec3(u(rng), u(rng), u(rng)) * 0.35f;
                b.Spike(a, a + d * len, w, c);
            }
        }
    }

    std::shared_ptr<Model> BuildWolfHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        std::mt19937 rng(2611u);
        // 方块人の頭は立方体で大きすぎる（ユーザー「頭太大了」）→ 首の位置はそのまま、幅 0.58 / 高さ 0.66 / 奥行き 0.70 に縮めた箱を基準にする
        const Vec3 s = H.Size() * Vec3(0.58f, 0.66f, 0.70f);
        const Vec3 c0 = H.Center();
        const Vec3 lo(c0.x - 0.5f * s.x, H.lo.y, c0.z - 0.5f * s.z);
        const Vec3 hi = lo + s;
        const Vec3 c = (lo + hi) * 0.5f;
        const float e = 0.06f * s.x;
        // 頭蓋（頭より少し大きい毛皮）+ 額
        b.Box(Vec3(lo.x - e, lo.y + 0.25f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + e, hi.z + 0.1f * s.z), kWolfFur, kWolfFurDark);
        // 鼻面（前へ長く、少し低い）+ 黒い鼻 + 口
        const Vec3 mz0(c.x - 0.22f * s.x, lo.y + 0.22f * s.y, hi.z + 0.05f * s.z), mz1(c.x + 0.22f * s.x, lo.y + 0.56f * s.y, hi.z + 0.62f * s.z);
        b.Box(mz0, mz1, kWolfFurLight, kWolfFur);
        b.Ball(Vec3(c.x, mz1.y - 0.04f * s.y, mz1.z), 0.09f * s.x, kWolfNose, 7, 3);
        // 下顎（少し開く）+ 舌 + 牙（上 4 本・下 2 本）
        b.Box(Vec3(mz0.x + 0.02f * s.x, lo.y + 0.02f * s.y, hi.z), Vec3(mz1.x - 0.02f * s.x, lo.y + 0.17f * s.y, mz1.z - 0.06f * s.z), kWolfFurDark);
        b.Box(Vec3(c.x - 0.08f * s.x, lo.y + 0.15f * s.y, mz1.z - 0.30f * s.z), Vec3(c.x + 0.08f * s.x, lo.y + 0.19f * s.y, mz1.z - 0.10f * s.z), kWolfTongue);
        for (int k = 0; k < 4; ++k)
        {
            const float x = mz0.x + 0.06f * s.x + (mz1.x - mz0.x - 0.12f * s.x) * (float)k / 3.0f;
            const Vec3 t(x, mz0.y + 0.01f * s.y, mz1.z - 0.10f * s.z);
            b.Spike(t, t - Vec3(0.0f, (k == 0 || k == 3) ? 0.16f * s.y : 0.10f * s.y, 0.0f), 0.045f * s.x, kWolfClaw);
        }
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const Vec3 t(c.x + (float)sg * 0.15f * s.x, lo.y + 0.16f * s.y, mz1.z - 0.16f * s.z);
            b.Spike(t, t + Vec3(0.0f, 0.13f * s.y, 0.0f), 0.045f * s.x, kWolfClaw);
            // 目（赤く光る）と眉の毛
            b.Ball(Vec3(c.x + (float)sg * 0.21f * s.x, lo.y + 0.66f * s.y, hi.z + 0.08f * s.z), 0.07f * s.x, kWolfEye, 7, 3);
            b.Box(Vec3(c.x + (float)sg * 0.21f * s.x - 0.10f * s.x, lo.y + 0.74f * s.y, hi.z + 0.02f * s.z),
                  Vec3(c.x + (float)sg * 0.21f * s.x + 0.10f * s.x, lo.y + 0.80f * s.y, hi.z + 0.12f * s.z), kWolfFurDark);
            // 耳（立つ。外側は毛皮、内側は明るい）
            const Vec3 eb(c.x + (float)sg * 0.30f * s.x, hi.y - 0.05f * s.y, c.z - 0.05f * s.z);
            b.Spike(eb, eb + Vec3((float)sg * 0.12f * s.x, 0.50f * s.y, -0.10f * s.z), 0.20f * s.x, kWolfFur);
            b.Spike(eb + Vec3(0.0f, 0.02f * s.y, 0.04f * s.z), eb + Vec3((float)sg * 0.10f * s.x, 0.40f * s.y, -0.06f * s.z), 0.10f * s.x, kWolfBelly);
        }
        // たてがみ（頭の後ろから首へ、後ろ下へ）
        FurTufts(b, rng, Vec3(c.x, hi.y - 0.15f * s.y, lo.z - 0.02f * s.z), Vec3(0.0f, 0.25f, -1.0f), 0.12f * s.x, 0.30f * s.y, 0.07f * s.x, 9, kWolfFurDark);
        FurTufts(b, rng, Vec3(c.x, lo.y + 0.35f * s.y, lo.z), Vec3(0.0f, -0.4f, -1.0f), 0.14f * s.x, 0.28f * s.y, 0.07f * s.x, 7, kWolfFur);
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildWolfTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        std::mt19937 rng(2612u);
        // 方块人の胴は幅広（ユーザー「太胖了」）→ 幅 0.72 / 奥行き 0.78 に絞った箱を基準に、腰へ向かって細くする
        const Vec3 s0 = T.Size();
        const Vec3 s(s0.x * 0.72f, s0.y, s0.z * 0.78f);
        const Vec3 c0 = T.Center();
        const Vec3 lo(c0.x - 0.5f * s.x, T.lo.y, c0.z - 0.5f * s.z);
        const Vec3 hi = lo + s;
        const Vec3 c = (lo + hi) * 0.5f;
        const float e = 0.05f * s.x;
        // 胴の毛皮（肩は広く腰は細い = 上下で幅を変えた六面体）+ 猫背（上の背が後ろ上へ盛り上がる）+ 胸と腹の明るい毛
        {
            const float wx = 0.5f * s.x + e, wz = 0.5f * s.z + e;      // 肩の半幅
            const float hx = 0.36f * s.x, hz = 0.40f * s.z;             // 腰の半幅
            const float y0 = lo.y - 0.03f * s.y, y1 = hi.y + 0.04f * s.y;
            Vec3 p[8] = {
                Vec3(c.x - hx, y0, c.z - hz), Vec3(c.x + hx, y0, c.z - hz), Vec3(c.x + hx, y0, c.z + hz), Vec3(c.x - hx, y0, c.z + hz),
                Vec3(c.x - wx, y1, c.z - wz), Vec3(c.x + wx, y1, c.z - wz), Vec3(c.x + wx, y1, c.z + wz), Vec3(c.x - wx, y1, c.z + wz) };
            b.Hex(p, kWolfFur, kWolfFurDark);
        }
        b.Box(Vec3(lo.x - 0.3f * e, c.y + 0.10f * s.y, lo.z - 0.40f * s.z), Vec3(hi.x + 0.3f * e, hi.y + 0.20f * s.y, lo.z + 0.1f * s.z), kWolfFurDark, kWolfFurDark);
        b.Box(Vec3(lo.x + 0.14f * s.x, lo.y + 0.10f * s.y, hi.z + 0.4f * e), Vec3(hi.x - 0.14f * s.x, hi.y - 0.18f * s.y, hi.z + 1.4f * e), kWolfBelly, kWolfFurLight);
        // 首の毛（胴の上に襟）
        b.Box(Vec3(lo.x - 0.2f * e, hi.y - 0.02f * s.y, lo.z - 0.2f * e), Vec3(hi.x + 0.2f * e, hi.y + 0.14f * s.y, hi.z + 0.2f * e), kWolfFurDark, kWolfFurDark);
        // 肩の毛房（上外へ）と背筋の毛（後ろ上へ）
        for (int sg = -1; sg <= 1; sg += 2)
            FurTufts(b, rng, Vec3((sg < 0) ? lo.x - e : hi.x + e, hi.y - 0.05f * s.y, c.z), Vec3((float)sg * 0.6f, 1.0f, -0.2f), 0.06f * s.x, 0.22f * s.y, 0.07f * s.x, 6, kWolfFur);
        for (int k = 0; k < 6; ++k)
        {
            const float y = hi.y + 0.2f * s.y - (float)k * 0.14f * s.y;
            const Vec3 base(c.x, y, lo.z - 0.45f * s.z + (float)k * 0.05f * s.z);
            b.Spike(base, base + Vec3(0.0f, 0.12f * s.y, -0.18f * s.z), 0.09f * s.x, kWolfFurDark);
        }
        // 尾（腰の後ろから後ろ下へ、先が太い）
        const Vec3 t0(c.x, lo.y + 0.10f * s.y, lo.z - 0.3f * e);
        const Vec3 t1 = t0 + Vec3(0.0f, -0.22f * s.y, -0.45f * s.z);
        const Vec3 t2 = t1 + Vec3(0.06f * s.x, -0.05f * s.y, -0.45f * s.z);
        b.Tube(t0, t1, 0.11f * s.x, 0.12f * s.x, kWolfFur, 7);
        b.Tube(t1, t2, 0.12f * s.x, 0.09f * s.x, kWolfFurDark, 7);
        b.Ball(t2, 0.15f * s.x, kWolfFurLight, 8, 3);
        FurTufts(b, rng, t2, Vec3(0.0f, -0.3f, -1.0f), 0.08f * s.x, 0.18f * s.y, 0.06f * s.x, 5, kWolfFur);
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildWolfArm(ID3D11Device* device, const Part& A, const Vec3& pivot)
    {
        Builder b;
        std::mt19937 rng(2613u + (uint32_t)(pivot.x > 0.0f ? 1 : 0));
        const ArmFrame f = FrameOf(A, pivot);
        const float w = f.w;
        // 肩の球 + 上腕（付け根 → 手へ 55%）+ 肘の球 + 前腕 + 手の球
        const Vec3 elbow = pivot + (f.hand - pivot) * 0.55f;
        b.Ball(pivot, 0.62f * w, kWolfFur, 9, 4);
        b.Tube(pivot, elbow, 0.48f * w, 0.40f * w, kWolfFur, 8);
        b.Ball(elbow, 0.42f * w, kWolfFurDark, 8, 3);
        b.Tube(elbow, f.hand, 0.38f * w, 0.33f * w, kWolfFurLight, 8);
        b.Ball(f.hand, 0.44f * w, kWolfFurDark, 8, 3);
        // 前腕の毛房（肘の外側）と 4 本の爪（手から先へ、少し開く）
        FurTufts(b, rng, elbow, Vec3(0.0f, 0.0f, -1.0f), 0.2f * w, 0.9f * w, 0.18f * w, 5, kWolfFur);
        for (int k = 0; k < 4; ++k)
        {
            const float t = ((float)k - 1.5f) / 1.5f;
            const Vec3 base = f.hand + f.ax * (t * 0.38f * w) - f.down * (0.1f * w);
            b.Spike(base, base + f.down * (0.95f * w) + f.az * (0.15f * w) + f.ax * (t * 0.18f * w), 0.20f * w, kWolfClaw);
        }
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildWolfLeg(ID3D11Device* device, const Part& L, const Vec3& pivot)
    {
        Builder b;
        std::mt19937 rng(2615u + (uint32_t)(pivot.x > 0.0f ? 1 : 0));
        const Vec3 s = L.Size();
        const float w = s.x, h = pivot.y - L.lo.y;
        // 腿（股から前下へ）→ 膝（球）→ 脛（後ろ下へ = 獣の後ろ脚）→ 足首 → 足の先（前へ）と爪
        const Vec3 knee = pivot + Vec3(0.0f, -0.48f * h, 0.18f * h);
        const Vec3 ankle = pivot + Vec3(0.0f, -0.86f * h, -0.08f * h);
        const Vec3 toe = Vec3(pivot.x, L.lo.y + 0.08f * h, pivot.z + 0.30f * h);
        b.Ball(pivot, 0.58f * w, kWolfFur, 8, 3);
        b.Tube(pivot, knee, 0.52f * w, 0.40f * w, kWolfFur, 8);
        b.Ball(knee, 0.42f * w, kWolfFurDark, 8, 3);
        b.Tube(knee, ankle, 0.36f * w, 0.29f * w, kWolfFurLight, 8);
        b.Ball(ankle, 0.32f * w, kWolfFurDark, 8, 3);
        b.Tube(ankle, toe, 0.32f * w, 0.26f * w, kWolfFurDark, 7);
        for (int k = -1; k <= 1; ++k)
        {
            const Vec3 base = toe + Vec3((float)k * 0.22f * w, -0.02f * h, 0.0f);
            b.Spike(base, base + Vec3((float)k * 0.08f * w, -0.04f * h, 0.28f * w), 0.16f * w, kWolfClaw);
        }
        FurTufts(b, rng, knee, Vec3(0.0f, 0.3f, -1.0f), 0.15f * w, 0.7f * w, 0.15f * w, 5, kWolfFur);
        return b.Build(device);
    }
}
