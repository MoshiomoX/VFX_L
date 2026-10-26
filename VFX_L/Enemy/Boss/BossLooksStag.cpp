// ============================================================
// 草原 3 面目の Boss「雷鳴の雄鹿」（2026-10-26、ユーザー「鹿を先に」= 方块の動物を部品の骨で動かす試作）:
// 方块人の体は描かず、胴（torso）・首 + 頭 + 角（torso に付けて VS の mode 3 = 首の付け根で縦に揺れ、殴り = 突進の時は
// 頭を下げて角を前へ）・前脚 × 2（arm の部品 = 歩きで前後に振れる）・後脚 × 2（leg の部品）・尾。方块人の歩きは
// 左腕と右脚が同時に前へ出る = 四足の対角の歩き（斜対歩）にそのまま使える。足元 y -0.9、肩の高さ 0.45、角の先 2.0。
// Boss は VS が 3 倍にする = 肩 4m・角の先 8.7m の大きな鹿
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    // 雄鹿の全体の大きさ（10-26 ユーザー「体をもう少し大きく」）：足元 y -0.9 を支点に 1.15 倍。飾りの座標はそのまま書き、
    // Builder が積む時に拡大する。VS に渡す軸（脚の付け根・首の付け根）も同じ StagXf で拡大する
    constexpr float kStagScale = 1.15f;
    const Vec3 kStagPivot = { 0.0f, -0.9f, 0.0f };
    Vec3 StagXf(const Vec3& p) { return kStagPivot + (p - kStagPivot) * kStagScale; }
    Builder StagBuilder() { Builder b; b.scale = kStagScale; b.pivot = kStagPivot; return b; }

    // 苔（10-26 ユーザー「その上に苔の感じを」）：面の上の不揃いな苔の塊 + 縁から垂れる苔の房。
    // patch = 上面 y にある lo〜hi の範囲へ n 枚、厚さ 0.025〜0.05
    void MossPatches(Builder& b, std::mt19937& rng, const Vec3& lo, const Vec3& hi, float y, int n)
    {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = lo.x + (hi.x - lo.x) * u(rng), z = lo.z + (hi.z - lo.z) * u(rng);
            const float w = 0.05f + 0.07f * u(rng), d = 0.05f + 0.07f * u(rng), h = 0.025f + 0.025f * u(rng);
            b.Box(Vec3(x - w, y - 0.01f, z - d), Vec3(x + w, y + h, z + d), (i & 1) ? kMossLight : kMoss, kMoss, 0.015f, &rng);
        }
    }
    // 垂れる苔の房：a の周りから下へ len（太さ w）。先は細く
    void MossStrand(Builder& b, std::mt19937& rng, const Vec3& a, float len, float w)
    {
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const Vec3 tip = a + Vec3(0.03f * u(rng), -len, 0.03f * u(rng));
        b.Beam(a, tip, w, kMoss, w * 0.45f);
        b.Box(a - Vec3(w * 0.8f, 0.0f, w * 0.8f), a + Vec3(w * 0.8f, w * 0.6f, w * 0.8f), kMossLight, kMoss);
    }

    // 枝：root から dir へ len、先で 2 本に分かれる（depth 段）。末端に葉の塊
    void Twig(Builder& b, const Vec3& root, const Vec3& dir, float len, float w, int depth, std::mt19937& rng)
    {
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const Vec3 tip = root + dir * len;
        b.Beam(root, tip, w, kBark, w * 0.55f);
        if (depth <= 0)
        {
            // 葉：小さい塊を 2〜3 個（向きを少しずつ変える）
            const int leaves = 2 + ((rng() & 1u) ? 1 : 0);
            for (int k = 0; k < leaves; ++k)
            {
                const Vec3 c = tip + Vec3(u(rng) * 0.03f, 0.02f + k * 0.025f, u(rng) * 0.03f);
                b.Box(c - Vec3(0.035f, 0.012f, 0.035f), c + Vec3(0.035f, 0.016f, 0.035f), kLeafLight, kLeaf, 0.008f, &rng);   // 花びら
                b.Box(c - Vec3(0.012f, 0.0f, 0.012f), c + Vec3(0.012f, 0.026f, 0.012f), kBlossomHeart);                        // 花芯
            }
            return;
        }
        // 2〜3 本に分かれる（短く、よく曲がる = 古木の枝ぶり）
        const int kids = 2 + ((rng() % 3u) == 0u ? 1 : 0);
        for (int k = 0; k < kids; ++k)
        {
            Vec3 d = dir + Vec3(u(rng) * 0.9f, 0.25f + u(rng) * 0.35f, u(rng) * 0.9f);
            d.Normalize();
            Twig(b, root + dir * (len * (0.45f + 0.5f * (float)k / (float)kids)), d, len * 0.7f, w * 0.62f, depth - 1, rng);
        }
    }
    // 蔓：a → b を n 段のジグザグで這わせる（途中に小さい葉）
    void Vine(Builder& b, const Vec3& a, const Vec3& c, int n, const Vec3& side, std::mt19937& rng)
    {
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        Vec3 prev = a;
        for (int i = 1; i <= n; ++i)
        {
            const float t = (float)i / (float)n;
            Vec3 p = a + (c - a) * t + side * (0.04f * std::sin(t * 9.0f)) + Vec3(0.0f, 0.012f * u(rng), 0.0f);
            b.Beam(prev, p, 0.03f, kVine, 0.03f);
            if ((i % 2) == 0) b.Box(p - Vec3(0.025f, 0.006f, 0.025f), p + Vec3(0.025f, 0.012f, 0.025f), kLeaf);
            prev = p;
        }
    }
    // 小さな茸：軸 + 傘
    void Mushroom(Builder& b, const Vec3& base, float h, const Vec4& cap)
    {
        b.Box(base - Vec3(0.012f, 0.0f, 0.012f), base + Vec3(0.012f, h, 0.012f), Vec4(0.80f, 0.75f, 0.62f, 1.0f));
        b.Box(base + Vec3(-0.035f, h, -0.035f), base + Vec3(0.035f, h + 0.025f, 0.035f), cap, cap);
    }
    // 古い森の主の生え物（胴の上面 y 0.45、肩 z 0.3〜0.4、尻 z -0.55）。
    // 10-26 ユーザー「枝が長い、もっと複雑に」→ 枝は短く（0.18〜0.26）3 段に分かれ本数を倍に、葉は小さく多く、
    // 苔は大小 10 枚で脇腹まで、肩と尻に樹皮の板、背に茸、蔓は 3 本ジグザグ
    void StagGrowth(Builder& b, std::mt19937& rng)
    {
        // 金の飾り板：肩当て（左右）、背の中央の筋、尻の板。縁は少し光る金、面は暗い金に青の嵌め込み
        auto goldPlate = [&](const Vec3& lo, const Vec3& hi)
            {
                b.Box(lo, hi, kAntlerGlow, kAntlerDark);
                const Vec3 c = (lo + hi) * 0.5f, s = (hi - lo) * 0.5f;
                b.Box(Vec3(c.x - s.x * 0.55f, hi.y - 0.002f, c.z - s.z * 0.55f), Vec3(c.x + s.x * 0.55f, hi.y + 0.012f, c.z + s.z * 0.55f), kFurDark);
            };
        goldPlate(Vec3(-0.32f, 0.30f, 0.20f), Vec3(-0.20f, 0.48f, 0.46f));
        goldPlate(Vec3(0.20f, 0.30f, 0.20f), Vec3(0.32f, 0.48f, 0.46f));
        goldPlate(Vec3(-0.31f, 0.24f, -0.64f), Vec3(-0.20f, 0.46f, -0.42f));
        goldPlate(Vec3(0.20f, 0.24f, -0.64f), Vec3(0.31f, 0.46f, -0.42f));
        b.Box(Vec3(-0.05f, 0.45f, -0.40f), Vec3(0.05f, 0.49f, 0.30f), kAntlerGlow, kAntlerDark);   // 背筋の金線
        for (int i = 0; i < 4; ++i)   // 背筋の金の留め具
            b.Box(Vec3(-0.09f, 0.45f, -0.34f + 0.2f * i), Vec3(0.09f, 0.505f, -0.30f + 0.2f * i), kAntler, kAntlerDark);
        // 胸当て（前）：金の縁 + 青の面 + 中央の宝石
        b.Box(Vec3(-0.26f, 0.14f, 0.60f), Vec3(0.26f, 0.50f, 0.65f), kAntler, kAntlerDark);
        b.Box(Vec3(-0.20f, 0.20f, 0.65f), Vec3(0.20f, 0.44f, 0.665f), kFurDark);
        b.Box(Vec3(-0.05f, 0.27f, 0.665f), Vec3(0.05f, 0.37f, 0.70f), kStagEye);
        // 白い花の枝：肩の飾り板と尻の板から短く（3 段）
        const Vec3 roots[6][2] = {
            { { -0.26f, 0.48f, 0.33f }, { -0.5f, 0.8f, 0.3f } }, { { 0.26f, 0.48f, 0.33f }, { 0.5f, 0.8f, 0.3f } },
            { { -0.25f, 0.46f, -0.53f }, { -0.5f, 0.7f, -0.5f } }, { { 0.25f, 0.46f, -0.53f }, { 0.5f, 0.7f, -0.5f } },
            { { -0.10f, 0.47f, 0.05f }, { -0.3f, 0.9f, 0.2f } }, { { 0.12f, 0.47f, -0.15f }, { 0.35f, 0.9f, -0.2f } } };
        for (int i = 0; i < 6; ++i)
        {
            Vec3 d = roots[i][1];
            d.Normalize();
            Twig(b, roots[i][0], d, 0.16f + 0.06f * (float)(i % 3), 0.035f + 0.01f * (float)(i % 2), 2, rng);
        }
        // 苔（10-26）：背の金線の両脇、肩と尻の板の周り、脇腹の上の縁に不揃いな塊。縁からは房が垂れ、背には細い草
        MossPatches(b, rng, Vec3(-0.30f, 0.0f, -0.40f), Vec3(-0.08f, 0.0f, 0.30f), 0.48f, 7);
        MossPatches(b, rng, Vec3(0.08f, 0.0f, -0.40f), Vec3(0.30f, 0.0f, 0.30f), 0.48f, 7);
        MossPatches(b, rng, Vec3(-0.28f, 0.0f, 0.46f), Vec3(0.28f, 0.0f, 0.60f), 0.55f, 4);     // 胸の上
        MossPatches(b, rng, Vec3(-0.26f, 0.0f, -0.70f), Vec3(0.26f, 0.0f, -0.52f), 0.45f, 4);   // 尻の上
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = (float)side;
            // 脇腹の上の縁に乗る苔（横へはみ出す）と、そこから垂れる房
            for (int i = 0; i < 5; ++i)
            {
                const float z = -0.50f + 0.22f * (float)i;
                b.Box(Vec3(sx * 0.27f - 0.05f, 0.40f, z - 0.07f), Vec3(sx * 0.27f + 0.05f + sx * 0.04f, 0.50f, z + 0.07f), kMossLight, kMoss, 0.015f, &rng);
                if (i != 2) MossStrand(b, rng, Vec3(sx * 0.33f, 0.40f, z), 0.10f + 0.06f * (float)(i % 3), 0.035f);
            }
            MossStrand(b, rng, Vec3(sx * 0.22f, 0.05f, -0.68f), 0.14f, 0.04f);   // 尻の下
            MossStrand(b, rng, Vec3(sx * 0.26f, 0.10f, 0.62f), 0.12f, 0.035f);   // 胸の下
        }
        Tufts(b, rng, Vec3(-0.26f, 0.0f, -0.42f), Vec3(0.26f, 0.0f, 0.34f), 0.49f, 0.55f, 12, 0);
    }

    std::shared_ptr<Model> BuildStagBody(ID3D11Device* device)
    {
        Builder b = StagBuilder();
        b.Box(Vec3(-0.30f, -0.02f, -0.56f), Vec3(0.30f, 0.47f, 0.46f), kFur, kFurDark);     // 胴（10-26 少し太く）
        b.Box(Vec3(-0.26f, -0.10f, -0.46f), Vec3(0.26f, 0.02f, 0.39f), kBelly);              // 腹（明るい）
        b.Box(Vec3(-0.32f, 0.06f, 0.40f), Vec3(0.32f, 0.53f, 0.63f), kFurLight, kFur);       // 胸（前へ丸く）
        b.Box(Vec3(-0.29f, 0.03f, -0.69f), Vec3(0.29f, 0.43f, -0.52f), kFur, kFurDark);      // 尻
        b.Box(Vec3(-0.05f, 0.30f, -0.80f), Vec3(0.05f, 0.42f, -0.66f), kFurLight);           // 尾
        std::mt19937 rng(77u);
        StagGrowth(b, rng);   // 苔・枝・蔓（古い森の主）
        return b.Build(device);
    }
    // 首 + 頭 + 角（torso に付け、mode 3 で首の付け根 hinge の周りに揺れる / 突進で前へ下げる）
    std::shared_ptr<Model> BuildStagHead(ID3D11Device* device, const Vec3& hinge)
    {
        Builder b = StagBuilder();
        const Vec3 neckTop = hinge + Vec3(0.0f, 0.55f, 0.40f);
        b.Beam(hinge, neckTop, 0.26f, kFur, 0.20f);                                              // 首
        b.Box(neckTop + Vec3(-0.12f, -0.02f, -0.10f), neckTop + Vec3(0.12f, 0.22f, 0.22f), kFur, kFurDark);   // 頭
        b.Box(neckTop + Vec3(-0.08f, -0.02f, 0.22f), neckTop + Vec3(0.08f, 0.12f, 0.46f), kFurLight, kFur);   // 鼻面
        b.Box(neckTop + Vec3(-0.03f, 0.04f, 0.44f), neckTop + Vec3(0.03f, 0.10f, 0.49f), kHoof);              // 鼻
        for (int side = -1; side <= 1; side += 2)
        {
            const float sx = (float)side;
            b.Box(neckTop + Vec3(sx * 0.12f - 0.015f, 0.08f, 0.17f), neckTop + Vec3(sx * 0.12f + 0.015f, 0.14f, 0.22f), kStagEye);   // 目
            b.Beam(neckTop + Vec3(sx * 0.10f, 0.20f, -0.02f), neckTop + Vec3(sx * 0.22f, 0.36f, -0.10f), 0.08f, kFurLight, 0.03f);     // 耳
            // 角（10-26 参考画）：金の大きな扇。主幹が外上へ、その外側に 2 枚の金の葉型の板、先は光る金。枝が 3 本
            const Vec3 base = neckTop + Vec3(sx * 0.09f, 0.22f, 0.02f);
            const Vec3 mid1 = base + Vec3(sx * 0.18f, 0.32f, -0.08f);
            const Vec3 mid2 = mid1 + Vec3(sx * 0.16f, 0.36f, -0.06f);
            const Vec3 top = mid2 + Vec3(sx * 0.02f, 0.34f, 0.02f);
            b.Beam(base, mid1, 0.11f, kAntler, 0.09f);
            b.Beam(mid1, mid2, 0.09f, kAntler, 0.08f);
            b.Beam(mid2, top, 0.08f, kAntlerGlow, 0.04f);
            // 葉型の板：主幹の外側に 2 枚（幅広の面が横を向く）、縁を光る金で囲む
            {
                const Vec3 out(sx, 0.0f, 0.0f);
                const Vec3 q1[4] = { mid1, mid1 + out * 0.28f + Vec3(0, 0.10f, -0.10f), mid2 + out * 0.22f + Vec3(0, 0.14f, -0.06f), mid2 };
                b.Plate(q1, Vec3(0, 0, 1), 0.035f, kAntlerDark);
                const Vec3 q2[4] = { mid1 + out * 0.28f + Vec3(0, 0.10f, -0.10f), mid1 + out * 0.42f + Vec3(0, 0.30f, -0.12f), mid2 + out * 0.30f + Vec3(0, 0.32f, -0.08f), mid2 + out * 0.22f + Vec3(0, 0.14f, -0.06f) };
                b.Plate(q2, Vec3(0, 0, 1), 0.03f, kAntler);
                b.Beam(q2[1], q2[2], 0.05f, kAntlerGlow, 0.05f);   // 縁
                b.Beam(q1[1], q2[1], 0.045f, kAntlerGlow, 0.045f);
                const Vec3 q3[4] = { mid2, mid2 + out * 0.22f + Vec3(0, 0.14f, -0.06f), top + out * 0.16f + Vec3(0, 0.08f, 0.0f), top };
                b.Plate(q3, Vec3(0, 0, 1), 0.03f, kAntlerDark);
                b.Beam(q3[2], top + Vec3(0, 0.06f, 0), 0.045f, kAntlerGlow, 0.03f);
            }
            const Vec3 tines[3][2] = {
                { mid1, mid1 + Vec3(sx * 0.02f, 0.20f, 0.30f) },
                { mid2, mid2 + Vec3(sx * 0.10f, 0.22f, 0.26f) },
                { (mid2 + top) * 0.5f, (mid2 + top) * 0.5f + Vec3(sx * -0.10f, 0.26f, 0.18f) },
            };
            for (const auto& t : tines)
            {
                b.Beam(t[0], t[1], 0.06f, kAntler, 0.025f);
                b.Box(t[1] - Vec3(0.03f, 0.03f, 0.03f), t[1] + Vec3(0.03f, 0.03f, 0.03f), kAntlerGlow);
            }
            b.Box(top - Vec3(0.03f, 0.02f, 0.03f), top + Vec3(0.03f, 0.04f, 0.03f), kAntlerGlow);
            // 角から生えた白い花の枝（参考画：角の間と外側に花）
            std::mt19937 rng(91u + (side > 0 ? 1u : 0u));
            Twig(b, (mid1 + mid2) * 0.5f, Vec3(sx * 0.3f, 0.7f, 0.55f), 0.15f, 0.035f, 2, rng);
            Twig(b, (base + mid1) * 0.5f, Vec3(sx * -0.4f, 0.6f, 0.5f), 0.13f, 0.03f, 2, rng);
            Twig(b, mid2 + (top - mid2) * 0.3f, Vec3(sx * 0.5f, 0.5f, 0.4f), 0.12f, 0.03f, 1, rng);
            Twig(b, mid1 + Vec3(sx * 0.30f, 0.2f, -0.1f), Vec3(sx * 0.6f, 0.7f, 0.0f), 0.12f, 0.03f, 1, rng);
        }
        // 苔（10-26）：首の上の筋、角の間の頭の上、角の付け根から垂れる房
        {
            std::mt19937 rng(131u);
            MossPatches(b, rng, Vec3(-0.08f, 0.0f, 0.16f), Vec3(0.08f, 0.0f, 0.44f), hinge.y + 0.30f, 3);
            b.Box(hinge + Vec3(-0.09f, 0.22f, 0.14f), hinge + Vec3(0.09f, 0.27f, 0.26f), kMossLight, kMoss, 0.012f, &rng);
            b.Box(hinge + Vec3(-0.08f, 0.42f, 0.28f), hinge + Vec3(0.08f, 0.47f, 0.38f), kMossLight, kMoss, 0.012f, &rng);
            b.Box(neckTop + Vec3(-0.10f, 0.21f, -0.10f), neckTop + Vec3(0.10f, 0.26f, 0.06f), kMossLight, kMoss, 0.012f, &rng);   // 角の間
            for (int side = -1; side <= 1; side += 2)
            {
                const float sx = (float)side;
                const Vec3 base = neckTop + Vec3(sx * 0.09f, 0.22f, 0.02f);
                b.Box(base + Vec3(-0.07f, -0.01f, -0.07f), base + Vec3(0.07f, 0.05f, 0.07f), kMossLight, kMoss, 0.012f, &rng);
                MossStrand(b, rng, base + Vec3(sx * 0.16f, 0.26f, -0.10f), 0.12f, 0.03f);
                MossStrand(b, rng, base + Vec3(sx * 0.30f, 0.56f, -0.14f), 0.10f, 0.025f);
                MossStrand(b, rng, neckTop + Vec3(sx * 0.12f, 0.10f, -0.10f), 0.08f, 0.03f);   // 耳の下
            }
        }
        // 首の金の首輪（2 段）と、首筋の金の縁取り
        {
            const Vec3 n0 = hinge + Vec3(0.0f, 0.14f, 0.10f);
            b.Box(n0 - Vec3(0.17f, 0.04f, 0.14f), n0 + Vec3(0.17f, 0.05f, 0.14f), kAntler, kAntlerDark);
            const Vec3 n1 = hinge + Vec3(0.0f, 0.38f, 0.26f);
            b.Box(n1 - Vec3(0.14f, 0.03f, 0.12f), n1 + Vec3(0.14f, 0.04f, 0.12f), kAntlerGlow, kAntlerDark);
            b.Beam(hinge + Vec3(0.0f, 0.12f, 0.24f), neckTop + Vec3(0.0f, 0.0f, 0.12f), 0.05f, kAntler, 0.04f);   // 首の前の金線
        }
        return b.Build(device);
    }
    // 脚 1 本（上から 腿・脛・蹄）。hinge = 脚の付け根（胴の下の角。VS の mode 4 がここを軸に前後へ振る）。
    // 前脚は膝が後ろへ曲がり、後脚は飛節が後ろへ出る。蹄は足元 y -0.9
    std::shared_ptr<Model> BuildStagLeg(ID3D11Device* device, const Vec3& hinge, bool front)
    {
        Builder b = StagBuilder();
        const Vec3 knee = hinge + Vec3(0.0f, -0.52f, front ? -0.05f : -0.16f);
        const Vec3 ankle = hinge + Vec3(0.0f, -0.98f, front ? 0.03f : -0.02f);
        const Vec3 hoof(hinge.x, -0.90f, hinge.z + 0.05f);
        b.Beam(hinge + Vec3(0.0f, 0.10f, 0.0f), knee, 0.22f, kFur, 0.13f);
        b.Beam(knee, ankle, 0.12f, kFurDark, 0.09f);
        b.Beam(ankle, hoof, 0.10f, kHoof, 0.12f);
        return b.Build(device);
    }
}
