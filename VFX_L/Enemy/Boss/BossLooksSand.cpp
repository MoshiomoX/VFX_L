// ============================================================
// BossLooksSand.cpp
// 砂漠 1 面目の Boss「砂の守護者」の飾り（2026-10-24：ファラオの頭巾・付け髭・光る目・襟飾りと宝石・石の肩当て・体の周りを回る浮き岩）。
// 寸法は焼いた雑魚の網の部品の包囲ボックスに対する割合。色と宣言は BossLooksCommon.h。2026-10-26 に BossLooks.cpp から分けた
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    // ---- 頭：ファラオの頭巾（金と瑠璃の縞）+ 額の金の帯と蛇の飾り + 付け髭 + 光る目 ----
    std::shared_ptr<Model> BuildHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.05f * s.x;   // 頭から浮かせる厚み
        // 頭頂の覆い（左右に縞）
        b.StripesX(Vec3(lo.x - e, hi.y - 0.04f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 0.09f * s.y, hi.z + 0.5f * e), 7, kGold, kLapis);
        // 両脇の垂れ（頭の横 + 頭の下で肩の前へ）
        for (int side = -1; side <= 1; side += 2)
        {
            const float x0 = (side > 0) ? hi.x : lo.x - 1.6f * e;
            const float x1 = (side > 0) ? hi.x + 1.6f * e : lo.x;
            b.StripesY(Vec3(x0, lo.y, c.z - 0.25f * s.z), Vec3(x1, hi.y - 0.04f * s.y, hi.z - 0.04f * s.z), 6, kGold, kLapis);
            b.StripesY(Vec3(x0, lo.y - 0.32f * s.y, c.z - 0.05f * s.z), Vec3(x1, lo.y, hi.z + 0.06f * s.z), 3, kLapis, kGold);
        }
        // 後ろの垂れ（長いと横から見て旗のようだった。2026-10-24 の 1 回目は頭の下 0.3 まで）
        b.StripesY(Vec3(lo.x - e, lo.y - 0.12f * s.y, lo.z - 1.6f * e), Vec3(hi.x + e, hi.y - 0.04f * s.y, lo.z), 6, kGold, kLapis);
        // 額の金の帯・蛇の飾り（ウラエウス）
        b.Box(Vec3(lo.x - e, hi.y - 0.20f * s.y, hi.z), Vec3(hi.x + e, hi.y - 0.08f * s.y, hi.z + 0.035f * s.z), kGold);
        b.Box(Vec3(c.x - 0.06f * s.x, hi.y - 0.26f * s.y, hi.z + 0.02f * s.z), Vec3(c.x + 0.06f * s.x, hi.y + 0.06f * s.y, hi.z + 0.12f * s.z), kGold, kGoldDark);
        // 付け髭（瑠璃と金の縞）
        b.StripesY(Vec3(c.x - 0.07f * s.x, lo.y - 0.28f * s.y, hi.z - 0.12f * s.z), Vec3(c.x + 0.07f * s.x, lo.y + 0.06f * s.y, hi.z + 0.04f * s.z), 4, kLapis, kGold);
        // 光る目（皮の目は消してある）
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.21f * s.x;
            b.Box(Vec3(ex - 0.09f * s.x, lo.y + 0.47f * s.y, hi.z - 0.005f), Vec3(ex + 0.09f * s.x, lo.y + 0.56f * s.y, hi.z + 0.03f * s.z), kGlow);
        }
        return b.Build(device);
    }

    // ---- 胴：胸の襟飾り（金と瑠璃の帯）+ 金枠の光る宝石 + 金の腰帯 ----
    std::shared_ptr<Model> BuildTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        b.StripesY(Vec3(lo.x + 0.04f * s.x, hi.y - 0.30f * s.y, hi.z - 0.01f), Vec3(hi.x - 0.04f * s.x, hi.y, hi.z + 0.05f * s.z), 3, kGold, kLapis);
        b.Box(Vec3(lo.x + 0.04f * s.x, hi.y - 0.01f, c.z), Vec3(hi.x - 0.04f * s.x, hi.y + 0.03f * s.y, hi.z + 0.05f * s.z), kGold);
        b.Box(Vec3(c.x - 0.13f * s.x, lo.y + 0.36f * s.y, hi.z - 0.01f), Vec3(c.x + 0.13f * s.x, lo.y + 0.60f * s.y, hi.z + 0.035f * s.z), kGold, kGoldDark);
        b.Box(Vec3(c.x - 0.08f * s.x, lo.y + 0.41f * s.y, hi.z), Vec3(c.x + 0.08f * s.x, lo.y + 0.55f * s.y, hi.z + 0.06f * s.z), kGlow);
        b.Box(Vec3(lo.x - 0.03f * s.x, lo.y + 0.02f * s.y, lo.z - 0.03f * s.z), Vec3(hi.x + 0.03f * s.x, lo.y + 0.15f * s.y, hi.z + 0.03f * s.z), kGold, kGoldDark);
        return b.Build(device);
    }

    // ---- 腕：石の肩当て（不揃いな角）+ 下縁の金の帯。焼いた歩きの姿勢で腕が振れているので、高さと奥行きは肩の付け根に合わせる ----
    std::shared_ptr<Model> BuildArm(ID3D11Device* device, const Part& A, const Vec3& pivot, uint32_t seed)
    {
        Builder b;
        std::mt19937 rng(seed);
        const float w = A.Size().x;   // 腕の振りは x 軸回り = 幅は変わらない
        const float x0 = A.lo.x - 0.30f * w, x1 = A.hi.x + 0.30f * w;
        b.Box(Vec3(x0, pivot.y - 0.60f * w, pivot.z - 0.85f * w), Vec3(x1, pivot.y + 0.62f * w, pivot.z + 0.85f * w),
            kStoneLight, kStoneDark, 0.06f * w, &rng);
        b.Box(Vec3(x0 - 0.05f * w, pivot.y - 0.74f * w, pivot.z - 0.90f * w), Vec3(x1 + 0.05f * w, pivot.y - 0.55f * w, pivot.z + 0.90f * w), kGold, kGoldDark);
        // 肩当ての上の金の鋲
        b.Box(Vec3((x0 + x1) * 0.5f - 0.14f * w, pivot.y + 0.60f * w, pivot.z - 0.14f * w), Vec3((x0 + x1) * 0.5f + 0.14f * w, pivot.y + 0.76f * w, pivot.z + 0.14f * w), kGold, kGoldDark);
        return b.Build(device);
    }

    // ---- 体の周りを回る 6 つの浮き岩（不揃いな 2 つの塊を重ねた欠片 + 細い金の筋）----
    // （2026-10-24 の 1 回目は 4 つの大きな箱 = 1 個 1.4m で体を隠した → 小さく・遠く・数を増やす）
    std::shared_ptr<Model> BuildOrbitRocks(ID3D11Device* device, const Model& model)
    {
        Builder b;
        std::mt19937 rng(20261024u);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const float footY = model.GetBoundsMin().y;
        const float height = model.GetBoundsMax().y - footY;
        const float radius = 1.10f;   // 網の空間（Boss は 3 倍 = 3.3m）
        const float hy[6] = { 0.50f, 0.78f, 0.62f, 0.92f, 0.56f, 0.84f };
        const float size[6] = { 0.12f, 0.09f, 0.13f, 0.08f, 0.10f, 0.09f };
        for (int k = 0; k < 6; ++k)
        {
            const float a = DirectX::XMConvertToRadians(20.0f + 60.0f * (float)k + 12.0f * u(rng));
            const float rr = radius * (1.0f + 0.10f * u(rng));
            const Vec3 c(std::cos(a) * rr, footY + height * hy[k], std::sin(a) * rr);
            const float r = size[k];
            b.Box(c - Vec3(r, r * 0.75f, r * 0.85f), c + Vec3(r, r * 0.75f, r * 0.85f), kStoneLight, kStoneDark, 0.35f * r, &rng);
            const Vec3 c2 = c + Vec3(0.5f * r * u(rng), 0.55f * r, 0.5f * r * u(rng));
            b.Box(c2 - Vec3(0.6f * r, 0.45f * r, 0.6f * r), c2 + Vec3(0.6f * r, 0.45f * r, 0.6f * r), kStoneLight, kStoneDark, 0.3f * r, &rng);
            // 外側の面に細い金の筋
            const Vec3 out(std::cos(a), 0.0f, std::sin(a));
            const Vec3 vc = c + out * (r * 0.88f);
            b.Box(vc - Vec3(0.07f * r, 0.5f * r, 0.07f * r), vc + Vec3(0.07f * r, 0.5f * r, 0.07f * r), kVein);
        }
        return b.Build(device);
    }
}
