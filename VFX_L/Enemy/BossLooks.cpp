// ============================================================
// BossLooks.cpp
// 面の Boss の皮と飾り（説明は BossLooks.h）。飾りの寸法は全部、焼いた雑魚の網の部品の包囲ボックス
// （頭・胴・腕。名前で探し、無ければ位置で推す）に対する割合で決める = 網の大きさが変わっても付いて来る。
// 網の空間：足元 = カプセルの下（y ≒ -0.9）、高さ 1.6、+Z が正面。Boss は VS がこれを体格倍（3 倍）にする
// ============================================================
#include "Enemy/BossLooks.h"
#include "Enemy/LookBuilder.h"
#include "Swarm/SwarmSystem.h"
#include "Graphics/Model/Model.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace LookBuilder;

namespace
{
    // 色（線形。頂点色がそのまま掛かる。皮の砂岩 sRGB 0.88/0.71/0.47 ≒ 線形 0.75/0.46/0.19 に合わせる）
    const Vec4 kGold = { 0.85f, 0.52f, 0.10f, 1.0f };
    const Vec4 kGoldDark = { 0.52f, 0.30f, 0.05f, 1.0f };
    const Vec4 kLapis = { 0.035f, 0.09f, 0.36f, 1.0f };
    const Vec4 kStoneLight = { 0.66f, 0.42f, 0.19f, 1.0f };
    const Vec4 kStoneDark = { 0.33f, 0.20f, 0.08f, 1.0f };
    const Vec4 kGlow = { 7.0f, 3.6f, 0.7f, 1.0f };       // 目・胸の宝石（HDR = bloom で光る）
    const Vec4 kVein = { 2.2f, 1.1f, 0.22f, 1.0f };      // 浮き岩の金の筋（3.2 は白く飛んだ）
    // 草原「岩の巨人」（2026-10-25）
    const Vec4 kMoss = { 0.06f, 0.16f, 0.025f, 1.0f };
    const Vec4 kMossLight = { 0.13f, 0.30f, 0.04f, 1.0f };
    const Vec4 kRock = { 0.21f, 0.22f, 0.21f, 1.0f };
    const Vec4 kRockDark = { 0.09f, 0.10f, 0.10f, 1.0f };
    const Vec4 kHorn = { 0.52f, 0.48f, 0.38f, 1.0f };
    const Vec4 kRune = { 0.5f, 4.6f, 1.3f, 1.0f };       // 目・胸の紋（HDR の緑）
    const Vec4 kFlower[3] = { { 1.0f, 0.75f, 0.06f, 1.0f }, { 0.92f, 0.92f, 0.86f, 1.0f }, { 0.95f, 0.30f, 0.50f, 1.0f } };


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

// ============================================================
// 草原の Boss「岩の巨人」（2026-10-25、ユーザー「巨人の見た目も変えて」。方向は推奨の「苔むした岩の巨人」）:
// 頭 = 苔の帽子（塊と垂れ）+ 草と小さな花 + 石の角 + 石の眉 + 緑に光る目、胴 = 緑に光る紋を刻んだ石の胸板 + 肩の苔 + 蔦の帯、
// 腕 = 苔と草の載った肩の大岩 + 腕の向きに沿った岩の拳。皮は Kenney Blocky O（牙のあるオーク）を灰色の岩と苔に塗り直した物
// ============================================================
namespace
{
    // 上面 y の範囲（lo.xz〜hi.xz）に細い草の葉を n 本・花を flowers 本。unit = 大きさの基準（頭の幅など）
    void Tufts(Builder& b, std::mt19937& rng, const Vec3& lo, const Vec3& hi, float y, float unit, int n, int flowers)
    {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = lo.x + (hi.x - lo.x) * u(rng), z = lo.z + (hi.z - lo.z) * u(rng);
            const float w = 0.022f * unit, hgt = (0.10f + 0.12f * u(rng)) * unit;
            b.Box(Vec3(x - w, y - 0.01f * unit, z - w), Vec3(x + w, y + hgt, z + w), (i & 1) ? kMossLight : kMoss, kMoss, 0.7f * w, &rng);
        }
        for (int i = 0; i < flowers; ++i)
        {
            const float x = lo.x + (hi.x - lo.x) * (0.15f + 0.7f * u(rng)), z = lo.z + (hi.z - lo.z) * (0.15f + 0.7f * u(rng));
            const float sw = 0.012f * unit, sh = 0.14f * unit, fh = 0.035f * unit;
            b.Box(Vec3(x - sw, y - 0.01f * unit, z - sw), Vec3(x + sw, y + sh, z + sw), kMossLight);
            b.Box(Vec3(x - fh, y + sh, z - fh), Vec3(x + fh, y + sh + 1.6f * fh, z + fh), kFlower[i % 3]);
        }
    }

    std::shared_ptr<Model> BuildStoneHead(ID3D11Device* device, const Part& H)
    {
        Builder b;
        std::mt19937 rng(31u);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const Vec3 lo = H.lo, hi = H.hi, s = H.Size(), c = H.Center();
        const float e = 0.05f * s.x;
        // 苔の帽子（板 + 上の塊 + 縁から垂れる苔）
        b.Box(Vec3(lo.x - e, hi.y - 0.03f * s.y, lo.z - e), Vec3(hi.x + e, hi.y + 0.06f * s.y, hi.z + e), kMossLight, kMoss, 0.02f * s.x, &rng);
        for (int i = 0; i < 5; ++i)
        {
            const float r = (0.07f + 0.05f * u(rng)) * s.x;
            const float x = lo.x + r + (s.x - 2.0f * r) * u(rng), z = lo.z + r + (s.z - 2.0f * r) * u(rng);
            b.Box(Vec3(x - r, hi.y + 0.04f * s.y, z - r), Vec3(x + r, hi.y + 0.04f * s.y + 1.1f * r, z + r), kMossLight, kMoss, 0.25f * r, &rng);
        }
        for (int i = 0; i < 8; ++i)
        {
            const int edge = i % 4;
            const float t = u(rng);
            const float dw = (0.05f + 0.05f * u(rng)) * s.x, dh = (0.10f + 0.18f * u(rng)) * s.y;
            float x = lo.x + s.x * t, z = lo.z + s.z * t;
            if (edge == 0) z = hi.z + e * 0.6f; else if (edge == 1) z = lo.z - e * 0.6f; else if (edge == 2) x = hi.x + e * 0.6f; else x = lo.x - e * 0.6f;
            if (edge == 0 && std::fabs(x - c.x) < 0.3f * s.x) x = c.x + ((x < c.x) ? -0.32f : 0.32f) * s.x;   // 顔の前は空ける
            b.Box(Vec3(x - dw, hi.y - dh, z - 0.03f * s.z), Vec3(x + dw, hi.y + 0.02f * s.y, z + 0.03f * s.z), kMoss, kMoss, 0.2f * dw, &rng);
        }
        Tufts(b, rng, Vec3(lo.x, 0.0f, lo.z), Vec3(hi.x, 0.0f, hi.z), hi.y + 0.06f * s.y, s.x, 14, 3);
        // 石の角（上の横から外へ・上へ 3 つの塊）
        for (int side = -1; side <= 1; side += 2)
        {
            const Vec3 p((side > 0) ? hi.x : lo.x, hi.y - 0.20f * s.y, c.z - 0.05f * s.z);
            const float r = 0.10f * s.x;
            for (int k = 0; k < 3; ++k)
            {
                const Vec3 cc = p + Vec3((float)side * (0.05f + 0.10f * (float)k) * s.x, (0.04f + 0.12f * (float)k) * s.y, 0.0f);
                const float hh = r * (1.0f - 0.28f * (float)k);
                b.Box(cc - Vec3(hh, hh, hh), cc + Vec3(hh, hh, hh), kHorn, kHorn * 0.7f, 0.15f * hh, &rng);
            }
        }
        // 石の眉・緑に光る目（皮の目は暗い岩に塗ってある）
        b.Box(Vec3(lo.x - 0.02f * s.x, lo.y + 0.54f * s.y, hi.z - 0.01f), Vec3(hi.x + 0.02f * s.x, lo.y + 0.65f * s.y, hi.z + 0.07f * s.z), kRock, kRockDark, 0.012f * s.x, &rng);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float ex = c.x + (float)sg * 0.21f * s.x;
            b.Box(Vec3(ex - 0.08f * s.x, lo.y + 0.44f * s.y, hi.z - 0.005f), Vec3(ex + 0.08f * s.x, lo.y + 0.53f * s.y, hi.z + 0.03f * s.z), kRune);
        }
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildStoneTorso(ID3D11Device* device, const Part& T)
    {
        Builder b;
        std::mt19937 rng(37u);
        const Vec3 lo = T.lo, hi = T.hi, s = T.Size(), c = T.Center();
        // 石の胸板と緑に光る紋（縦・横の線と端の点 = 古い紋章）
        b.Box(Vec3(lo.x + 0.07f * s.x, lo.y + 0.28f * s.y, hi.z - 0.01f), Vec3(hi.x - 0.07f * s.x, hi.y - 0.06f * s.y, hi.z + 0.06f * s.z), kRock, kRockDark, 0.015f * s.x, &rng);
        const float z0 = hi.z + 0.06f * s.z, z1 = z0 + 0.02f * s.z;
        const float cy = lo.y + 0.62f * s.y;
        b.Box(Vec3(c.x - 0.025f * s.x, lo.y + 0.36f * s.y, z0), Vec3(c.x + 0.025f * s.x, hi.y - 0.14f * s.y, z1), kRune);
        b.Box(Vec3(c.x - 0.20f * s.x, cy - 0.025f * s.y, z0), Vec3(c.x + 0.20f * s.x, cy + 0.025f * s.y, z1), kRune);
        for (int k = 0; k < 4; ++k)
        {
            const float dx = (k == 0) ? -0.20f : (k == 1) ? 0.20f : 0.0f;
            const float dy = (k == 2) ? lo.y + 0.36f * s.y : (k == 3) ? hi.y - 0.14f * s.y : cy;
            const Vec3 p(c.x + dx * s.x, dy, z0);
            const float h = 0.045f * s.x;
            b.Box(p - Vec3(h, h, 0.0f), p + Vec3(h, h, 0.025f * s.z), kRune);
        }
        // 肩の苔・蔦の帯
        b.Box(Vec3(lo.x - 0.02f * s.x, hi.y - 0.02f * s.y, lo.z - 0.02f * s.z), Vec3(hi.x + 0.02f * s.x, hi.y + 0.05f * s.y, hi.z + 0.02f * s.z), kMossLight, kMoss, 0.01f * s.x, &rng);
        b.Box(Vec3(lo.x - 0.03f * s.x, lo.y + 0.02f * s.y, lo.z - 0.03f * s.z), Vec3(hi.x + 0.03f * s.x, lo.y + 0.12f * s.y, hi.z + 0.03f * s.z), kMoss, kMoss, 0.01f * s.x, &rng);
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildStoneArm(ID3D11Device* device, const Part& A, const Vec3& pivot, uint32_t seed)
    {
        Builder b;
        std::mt19937 rng(seed);
        const float w = A.Size().x;   // 腕の太さ（振りは x 軸回りなので幅はそのまま）
        // 肩の大岩 + 苔の帽子 + 草
        const float x0 = A.lo.x - 0.35f * w, x1 = A.hi.x + 0.35f * w;
        b.Box(Vec3(x0, pivot.y - 0.50f * w, pivot.z - 0.90f * w), Vec3(x1, pivot.y + 0.75f * w, pivot.z + 0.90f * w), kRock, kRockDark, 0.12f * w, &rng);
        b.Box(Vec3(x0 + 0.05f * w, pivot.y + 0.68f * w, pivot.z - 0.80f * w), Vec3(x1 - 0.05f * w, pivot.y + 0.86f * w, pivot.z + 0.80f * w), kMossLight, kMoss, 0.06f * w, &rng);
        Tufts(b, rng, Vec3(x0 + 0.1f * w, 0.0f, pivot.z - 0.7f * w), Vec3(x1 - 0.1f * w, 0.0f, pivot.z + 0.7f * w), pivot.y + 0.86f * w, 2.4f * w, 6, 1);
        // 岩の拳：焼いた歩きの姿勢で腕は前後に振れている。付け根から遠い側の端（包囲ボックスの下の角）を手とみなし、
        // 付け根 → 手の向きに沿った箱にする
        const float zEnd = (pivot.z - A.lo.z > A.hi.z - pivot.z) ? A.lo.z + 0.5f * w : A.hi.z - 0.5f * w;
        const Vec3 hand(A.Center().x, A.lo.y + 0.5f * w, zEnd);
        Vec3 down = hand - pivot;
        if (down.LengthSquared() < 1e-6f) down = Vec3(0.0f, -1.0f, 0.0f);
        down.Normalize();
        const Vec3 ay = -down;
        Vec3 az = Vec3(1.0f, 0.0f, 0.0f).Cross(ay);
        az.Normalize();
        Vec3 ax = ay.Cross(az);
        ax.Normalize();
        b.OBox(hand + down * (0.10f * w), ax, ay, az, Vec3(0.85f * w, 0.70f * w, 0.85f * w), kRock, kRockDark, 0.10f * w, &rng);
        return b.Build(device);
    }
}

void BossLooks::Apply(BossType type, ID3D11Device* device, SwarmSystem& swarm)
{
    const std::shared_ptr<Model> modelRef = swarm.GetEnemyModel();
    const Model* model = modelRef.get();
    if (!model || !device)
    {
        swarm.SetBossLook(nullptr, {});
        return;
    }

    Part head, torso, armA, armB;
    FindParts(*model, head, torso, armA, armB);
    std::cout << "[BossLooks] " << BossTypeName(type) << ": head " << head.index << " torso " << torso.index
        << " arms " << armA.index << "," << armB.index << std::endl;

    std::vector<SwarmSystem::BossDecoration> decos;
    auto add = [&](std::shared_ptr<Model> m, uint32_t part, float orbit = 0.0f)
        {
            if (!m) return;
            SwarmSystem::BossDecoration d;
            d.model = std::move(m);
            d.part = part;
            d.orbitSpeed = orbit;
            decos.push_back(std::move(d));
        };
    if (type == BossType::SandGuardian)
    {
        if (head.Valid()) add(BuildHead(device, head), (uint32_t)head.index);
        if (torso.Valid()) add(BuildTorso(device, torso), (uint32_t)torso.index);
        if (armA.Valid()) add(BuildArm(device, armA, swarm.GetPartPivot((uint32_t)armA.index), 11u), (uint32_t)armA.index);
        if (armB.Valid()) add(BuildArm(device, armB, swarm.GetPartPivot((uint32_t)armB.index), 23u), (uint32_t)armB.index);
        add(BuildOrbitRocks(device, *model), SwarmSystem::kDecoOrbit, 0.55f);
        swarm.SetBossLook(ResourceManager::Get().LoadTexture(Res::Tex::BossSandGuardianAlbedo), std::move(decos));
        return;
    }
    // 岩の巨人（草原。遺跡の Boss を作るまでは遺跡もこれ）
    if (head.Valid()) add(BuildStoneHead(device, head), (uint32_t)head.index);
    if (torso.Valid()) add(BuildStoneTorso(device, torso), (uint32_t)torso.index);
    if (armA.Valid()) add(BuildStoneArm(device, armA, swarm.GetPartPivot((uint32_t)armA.index), 41u), (uint32_t)armA.index);
    if (armB.Valid()) add(BuildStoneArm(device, armB, swarm.GetPartPivot((uint32_t)armB.index), 43u), (uint32_t)armB.index);
    swarm.SetBossLook(ResourceManager::Get().LoadTexture(Res::Tex::BossStoneGiantAlbedo), std::move(decos));
}
