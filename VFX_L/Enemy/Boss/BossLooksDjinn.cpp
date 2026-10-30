// ============================================================
// 砂漠 3 面目の Boss「砂嵐の魔神」の飾り（2026-10-29、ユーザー「新しい Boss」→ 方案表の砂嵐の魔神）。
// 上半身だけの魔神が砂旋の上に浮く。方块人の体は描かず（hideBody）、全部を胴の部品に付けて VS の g_AnimMode 6
// （部品の歩き・殴りのクリップを掛けない、ゆっくり息をする）で動かす：
//   ・体：藍の肌の逞しい胴（平たい胸板・肩の丸み）+ 金の首飾りと紅玉 + 紅の腰布と金の帯 + 腰から下は砂に変わる
//   ・頭：白い頭巾（金の帯・紅玉・金の羽根飾り）、尖った耳と金の耳飾り、金に光る目、黒い顎鬚
//   ・腕：肩を軸に振り上げる（詠唱 = 両腕を前上へ、砂の拳 = 叩き付け）。金の腕輪・籠手、掌に燃える火の玉
//   ・下半身：体の軸の周りを回る砂の螺旋（kDecoOrbit）+ 腰の周りを逆に回る砂の塊と金の粒。足元には砂旋の特効（シーン）
// 座標は焼いた網の空間（地面 y -0.9、+Z = 正面）を直接書く。Boss は VS が 3 倍にし、一段目はさらに hover m 浮かせる
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    namespace
    {
        // 白い頭巾の巻き（斜めの帯）
        void TurbanWrap(Builder& b, float y, float r, float tilt, const Vec4& c)
        {
            const int n = 10;
            for (int i = 0; i < n; ++i)
            {
                const float a0 = 6.2831853f * (float)i / (float)n, a1 = 6.2831853f * (float)(i + 1) / (float)n;
                const Vec3 p0(std::cos(a0) * r, y + std::sin(a0) * tilt, 0.03f + std::sin(a0) * r);
                const Vec3 p1(std::cos(a1) * r, y + std::sin(a1) * tilt, 0.03f + std::sin(a1) * r);
                b.Beam(p0, p1, 0.045f, c);
            }
        }
    }

    // 胴・頭・首飾り・腰布（腕と下半身の砂は別）
    std::shared_ptr<Model> BuildDjinnBody(ID3D11Device* device)
    {
        Builder b;
        std::mt19937 rng(2910u);
        // ---- 胴：細い腰から広い胸と肩へ（逞しい楔形）----
        {
            const float y0 = -0.20f, y1 = 0.28f;
            Vec3 p[8] = {
                Vec3(-0.15f, y0, -0.10f), Vec3(0.15f, y0, -0.10f), Vec3(0.15f, y0, 0.10f), Vec3(-0.15f, y0, 0.10f),
                Vec3(-0.30f, y1, -0.15f), Vec3(0.30f, y1, -0.15f), Vec3(0.30f, y1, 0.16f), Vec3(-0.30f, y1, 0.16f) };
            b.Hex(p, kDjinnSkinLight, kDjinnSkin);
        }
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float sx = (float)sg;
            b.Box(Vec3((std::min)(sx * 0.015f, sx * 0.25f), 0.09f, 0.135f), Vec3((std::max)(sx * 0.015f, sx * 0.25f), 0.25f, 0.172f), kDjinnSkinLight, kDjinnSkinLight);   // 胸板（平ら）
            b.Box(Vec3((std::min)(sx * 0.03f, sx * 0.23f), 0.075f, 0.13f), Vec3((std::max)(sx * 0.03f, sx * 0.23f), 0.095f, 0.165f), kDjinnSkinDark, kDjinnSkinDark);   // 胸板の下の影
            b.Ball(Vec3(sx * 0.30f, 0.24f, 0.0f), 0.125f, kDjinnSkin, 10, 5);         // 肩
            b.Ball(Vec3(sx * 0.16f, 0.24f, -0.08f), 0.11f, kDjinnSkin, 8, 4);         // 背の盛り上がり
            // 腹の筋（2 段）
            for (int r = 0; r < 2; ++r)
                b.Box(Vec3(sx * 0.075f - 0.045f, -0.05f + 0.09f * (float)r, 0.105f), Vec3(sx * 0.075f + 0.045f, 0.02f + 0.09f * (float)r, 0.135f), kDjinnSkinLight, kDjinnSkin);
        }
        // ---- 首と金の首飾り（玉の輪 + 胸の紅玉）----
        b.Tube(Vec3(0.0f, 0.26f, 0.0f), Vec3(0.0f, 0.37f, 0.02f), 0.085f, 0.075f, kDjinnSkin, 10);
        for (int i = 0; i < 12; ++i)
        {
            const float a = 6.2831853f * (float)i / 12.0f;
            b.Ball(Vec3(std::cos(a) * 0.15f, 0.29f - 0.03f * (std::max)(0.0f, std::sin(a)), 0.01f + std::sin(a) * 0.13f), 0.028f, kGold, 6, 3);
        }
        b.Ball(Vec3(0.0f, 0.22f, 0.17f), 0.045f, kDjinnRuby, 8, 4);
        b.Box(Vec3(-0.04f, 0.22f, 0.14f), Vec3(0.04f, 0.29f, 0.16f), kGold, kGoldDark);
        // ---- 紅の腰布と金の帯・留め具、垂れる布の端 ----
        {
            Vec3 p[8] = {
                Vec3(-0.19f, -0.30f, -0.14f), Vec3(0.19f, -0.30f, -0.14f), Vec3(0.19f, -0.30f, 0.14f), Vec3(-0.19f, -0.30f, 0.14f),
                Vec3(-0.18f, -0.15f, -0.13f), Vec3(0.18f, -0.15f, -0.13f), Vec3(0.18f, -0.15f, 0.13f), Vec3(-0.18f, -0.15f, 0.13f) };
            b.Hex(p, kDjinnSash, kDjinnSashDark);
        }
        b.Box(Vec3(-0.19f, -0.19f, -0.145f), Vec3(0.19f, -0.15f, 0.145f), kGold, kGoldDark);
        b.Ball(Vec3(0.0f, -0.17f, 0.15f), 0.04f, kDjinnRuby, 8, 4);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float sx = (float)sg;
            b.Beam(Vec3(sx * 0.13f, -0.28f, 0.135f), Vec3(sx * 0.16f, -0.50f, 0.17f), 0.08f, kDjinnSash, 0.05f);
        }
        // ---- 腰から下：肌が砂に変わる（細くなる砂の塊。下は回る螺旋の飾りに続く）----
        {
            Vec3 p[8] = {
                Vec3(-0.11f, -0.46f, -0.09f), Vec3(0.11f, -0.46f, -0.09f), Vec3(0.11f, -0.46f, 0.09f), Vec3(-0.11f, -0.46f, 0.09f),
                Vec3(-0.17f, -0.30f, -0.12f), Vec3(0.17f, -0.30f, -0.12f), Vec3(0.17f, -0.30f, 0.12f), Vec3(-0.17f, -0.30f, 0.12f) };
            b.Hex(p, kDjinnSand, kDjinnSandDark);
        }

        // ---- 頭 ----
        {
            Vec3 p[8] = {
                Vec3(-0.095f, 0.36f, -0.05f), Vec3(0.095f, 0.36f, -0.05f), Vec3(0.085f, 0.36f, 0.15f), Vec3(-0.085f, 0.36f, 0.15f),
                Vec3(-0.12f, 0.60f, -0.08f), Vec3(0.12f, 0.60f, -0.08f), Vec3(0.12f, 0.60f, 0.14f), Vec3(-0.12f, 0.60f, 0.14f) };
            b.Hex(p, kDjinnSkinLight, kDjinnSkin);
        }
        b.Box(Vec3(-0.105f, 0.52f, 0.13f), Vec3(0.105f, 0.55f, 0.165f), kDjinnSkinDark, kDjinnSkinDark);       // 眉
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float sx = (float)sg;
            b.Box(Vec3(sx * 0.05f - 0.03f, 0.48f, 0.14f), Vec3(sx * 0.05f + 0.03f, 0.51f, 0.165f), kDjinnEye);   // 金に光る目
            b.Spike(Vec3(sx * 0.115f, 0.50f, 0.02f), Vec3(sx * 0.22f, 0.59f, -0.05f), 0.045f, kDjinnSkin);      // 尖った耳
            b.Ball(Vec3(sx * 0.125f, 0.43f, 0.02f), 0.026f, kGold, 6, 3);                                        // 耳飾り
        }
        b.Box(Vec3(-0.045f, 0.405f, 0.145f), Vec3(0.045f, 0.42f, 0.165f), kDjinnBeard, kDjinnBeard);             // 口
        b.Spike(Vec3(0.0f, 0.39f, 0.12f), Vec3(0.0f, 0.24f, 0.17f), 0.06f, kDjinnBeard);                          // 顎鬚
        // 白い頭巾：巻いた帯 2 段 + 丸い頂 + 金の帯 + 紅玉 + 金の羽根飾り
        b.Tube(Vec3(0.0f, 0.565f, 0.03f), Vec3(0.0f, 0.66f, 0.03f), 0.15f, 0.145f, kDjinnTurban, 12);
        TurbanWrap(b, 0.60f, 0.15f, 0.03f, kDjinnTurbanDark);
        TurbanWrap(b, 0.64f, 0.14f, -0.03f, kDjinnTurban);
        b.Ball(Vec3(0.0f, 0.68f, 0.03f), 0.125f, kDjinnTurban, 10, 5);
        b.Box(Vec3(-0.05f, 0.585f, 0.165f), Vec3(0.05f, 0.655f, 0.19f), kGold, kGoldDark);
        b.Ball(Vec3(0.0f, 0.62f, 0.195f), 0.035f, kDjinnRuby, 8, 4);
        b.Spike(Vec3(0.0f, 0.65f, 0.17f), Vec3(0.03f, 0.88f, 0.09f), 0.05f, kDjinnPlume);
        b.Spike(Vec3(0.01f, 0.65f, 0.16f), Vec3(-0.04f, 0.83f, 0.05f), 0.035f, kGold);
        (void)rng;
        return b.Build(device);
    }

    // 腕 1 本（sx = -1 左 / +1 右）。肩 = DjinnShoulder(sx)（VS がここを軸に振り上げる）
    Vec3 DjinnShoulder(float sx) { return Vec3(sx * 0.32f, 0.24f, 0.0f); }

    std::shared_ptr<Model> BuildDjinnArm(ID3D11Device* device, float sx)
    {
        Builder b;
        const Vec3 sh = DjinnShoulder(sx);
        const Vec3 elbow(sx * 0.41f, 0.0f, 0.04f);
        const Vec3 wrist(sx * 0.43f, -0.22f, 0.11f);
        const Vec3 hand(sx * 0.435f, -0.275f, 0.125f);
        b.Tube(sh, elbow, 0.088f, 0.07f, kDjinnSkin, 10);
        b.Ball(elbow, 0.072f, kDjinnSkin, 8, 4);
        b.Tube(elbow, wrist, 0.07f, 0.06f, kDjinnSkin, 10);
        // 金の腕輪（二の腕）と籠手（前腕の下半分）
        b.Tube(sh + (elbow - sh) * 0.45f, sh + (elbow - sh) * 0.62f, 0.094f, 0.09f, kGold, 10);
        b.Tube(elbow + (wrist - elbow) * 0.45f, wrist, 0.077f, 0.072f, kGold, 10);
        b.Tube(elbow + (wrist - elbow) * 0.52f, elbow + (wrist - elbow) * 0.6f, 0.08f, 0.08f, kGoldDark, 10);
        // 手（握った拳）と掌の火の玉
        b.Ball(hand, 0.068f, kDjinnSkin, 8, 4);
        b.Box(hand + Vec3(-0.05f, -0.06f, -0.02f), hand + Vec3(0.05f, -0.01f, 0.07f), kDjinnSkinLight, kDjinnSkin);
        b.Ball(hand + Vec3(-sx * 0.02f, -0.02f, 0.07f), 0.045f, kDjinnFire, 8, 4);
        return b.Build(device);
    }

    // 下半身の砂の螺旋（体の軸の周りを回る：kDecoOrbit）。腰の下 -0.42 から地面の上 -0.88 へ細くなる二重の螺旋
    std::shared_ptr<Model> BuildDjinnTail(ID3D11Device* device)
    {
        Builder b;
        std::mt19937 rng(2911u);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const int n = 30;
        for (int strand = 0; strand < 2; ++strand)
            for (int i = 0; i < n; ++i)
            {
                const float t = (float)i / (float)(n - 1);
                const float y = -0.42f - 0.46f * t;
                const float r = 0.15f * (1.0f - t) + 0.03f;
                const float a = t * 3.5f * 6.2831853f + 3.1415927f * (float)strand;
                const Vec3 c(std::cos(a) * r, y, std::sin(a) * r);
                const float s = 0.05f * (1.0f - 0.55f * t);
                const bool spark = (i % 7) == 3;
                const Vec4 col = spark ? kDjinnSandGlow : ((i & 1) ? kDjinnSand : kDjinnSandDark);
                b.Box(c - Vec3(s, s * 0.8f, s), c + Vec3(s, s * 0.8f, s), col, col, 0.3f * s, &rng);
            }
        // 芯：細くなる砂の柱（螺旋の隙間を埋める）
        b.Tube(Vec3(0.0f, -0.42f, 0.0f), Vec3(0.0f, -0.86f, 0.0f), 0.10f, 0.025f, kDjinnSandDark, 10);
        (void)u;
        return b.Build(device);
    }

    // 腰の周りを（螺旋と逆へ）回る砂の塊と金の粒
    std::shared_ptr<Model> BuildDjinnRing(ID3D11Device* device)
    {
        Builder b;
        std::mt19937 rng(2912u);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int i = 0; i < 12; ++i)
        {
            const float a = 6.2831853f * ((float)i + 0.3f * u(rng)) / 12.0f;
            const float r = 0.30f + 0.06f * u(rng);
            const float y = -0.32f + 0.12f * u(rng);
            const Vec3 c(std::cos(a) * r, y, std::sin(a) * r);
            const float s = (i % 3 == 0) ? 0.022f : 0.035f + 0.02f * u(rng);
            const Vec4 col = (i % 3 == 0) ? kDjinnSandGlow : kDjinnSand;
            b.Box(c - Vec3(s, s, s), c + Vec3(s, s, s), col, (i % 3 == 0) ? col : kDjinnSandDark, 0.3f * s, &rng);
        }
        return b.Build(device);
    }
}
