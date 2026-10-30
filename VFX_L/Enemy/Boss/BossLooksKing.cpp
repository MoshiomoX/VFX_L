// ============================================================
// 遺跡 3 面目の Boss「骸の王」の飾り（2026-10-29、ユーザー「見た目はダークソウル 3 のウォルニール風に」）。
// 闇の水溜まり（足元の特効、シーン）から上半身だけ這い出た、冠を被った巨大な骸骨。方块人の体は描かず（hideBody）、
// 全部を胴の部品に付けて浮く魔神と同じ VS の g_AnimMode 6（部品のクリップを掛けない）で動かす：
//   ・体：少し前へ傾き（胸は起きている）、肋骨より下で後ろへ反って闇に潜る背骨・肋骨の籠・胸骨・鎖骨・肩甲骨 + 肩と背から垂れるぼろぼろの黒い布
//   ・頭：傾いた体から前を見上げる頭蓋骨（暗い眼窩の奥に紫の火・歯・少し開いた顎）+ 金の冠（尖りと紫の宝石）
//   ・腕：長い骨の腕を前の地面へ伸ばして体を支える（這う姿）。手首に金の腕輪を三重。普段は手を突いたまま動かさず、詠唱で肩から前上へ
//     振り上げ（g_Flap.w）、その間に前腕が肘で上へ折れる（g_Hinge = 肘。10-29 ユーザー「真っ直ぐの腕で振るのは気持ち悪い」）
// 座標は焼いた網の空間（地面 y -0.9、+Z = 正面）。体は直立で書いて Builder の pitch で腰から前へ倒す（KingLean と同じ変換）
// ============================================================
#include "Enemy/Boss/BossLooksCommon.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

namespace BossLooksDetail
{
    namespace
    {
        const Vec3 kLeanPivot(0.0f, -0.85f, 0.0f);
        // 腰から前へ倒す（rad。Builder::pitch の負 = 前へ）。初めは -0.55（約 31 度 + 背骨の曲がりで横から見ると 45 度近く、
        // 這いつくばって見えた）。10-29 ユーザー「体が前に傾きすぎでは」→ ウォルニールのように胸を起こして -0.25（約 14 度）
        constexpr float kLean = -0.25f;
        const Vec3 kNeck(0.0f, 0.36f, 0.0f);
        constexpr float kHeadUp = 0.15f;         // 首で起こす rad（体の傾きと合わせて顔は少し下 = -0.10 で前のプレイヤーを見下ろす）
        // 10-29 ユーザー「背骨が真っ直ぐすぎる、少し曲げて」「頭をもっと大きく」「体も大きく」「前腕を少し太く」:
        constexpr float kHS = 1.7f;              // 頭蓋骨と冠を首を中心に大きく（1 倍では腕に比べて小さく、1.35 でもまだ小さかった）
        constexpr float kBody = 1.22f;           // 体（背骨・肋骨・肩・布・頭）を腰を中心に大きく（腕は肩の位置だけ付いて行く）
        // 体ごと闇へ沈める（焼いた網の空間 m。5.5 倍で約 1.8m）。肋骨より下に出ている背骨が長すぎた（10-29 ユーザー「下半身が長い」）。
        // 腕は手を地面に突いたまま肩だけ下がる
        constexpr float kSink = 0.32f;

        // 背骨の前後のずれ（直立の座標の y → z）。肋骨より下は大きく後ろへ反ってから闇へ潜り、胸の背も少し丸い（猫背）。
        // 真っ直ぐな棒に見えた（10-29）ので曲げた。肋骨の背の付け根・肩甲骨・布もこれに合わせる
        float SpineZ(float y)
        {
            const float t1 = std::clamp((y + 0.16f) / 0.46f, 0.0f, 1.0f);
            const float t2 = std::clamp((y + 0.90f) / 0.74f, 0.0f, 1.0f);
            return -0.05f - 0.06f * std::sin(3.1415927f * t1) - 0.15f * std::sin(3.1415927f * t2);
        }

        // x 軸の周りに p を pivot で ang 回す（Builder::Xf の pitch と同じ向き：正 = 前が上がる）
        Vec3 RotX(const Vec3& p, const Vec3& pivot, float ang)
        {
            const float s = std::sin(ang), c = std::cos(ang);
            const float y = p.y - pivot.y, z = p.z - pivot.z;
            return Vec3(p.x, pivot.y + y * c + z * s, pivot.z - y * s + z * c);
        }
        Builder LeanBuilder()
        {
            Builder b;
            b.pivot = kLeanPivot;
            b.scale = kBody;
            b.pitch = kLean;
            b.move = Vec3(0.0f, -kSink, 0.0f);
            return b;
        }
        // 頭の部品：首で起こしてから（体と同じく）前へ倒れる。軸に沿わない箱は 8 隅を回して Hex で
        // 頭を首の上へ持ち上げる量（頭を首の根元を中心に 1.7 倍にしたので、下顎が鎖骨・胸骨の裏へ沈んで下の歯が見えなかった。10-29）
        constexpr float kHeadLift = 0.12f;
        Vec3 H(const Vec3& p) { return RotX(kNeck + (p - kNeck) * kHS + Vec3(0.0f, kHeadLift, 0.0f), kNeck, kHeadUp); }
        void HBox(Builder& b, const Vec3& lo, const Vec3& hi, const Vec4& top, const Vec4& side)
        {
            Vec3 v[8] = {
                H(Vec3(lo.x, lo.y, lo.z)), H(Vec3(hi.x, lo.y, lo.z)), H(Vec3(hi.x, lo.y, hi.z)), H(Vec3(lo.x, lo.y, hi.z)),
                H(Vec3(lo.x, hi.y, lo.z)), H(Vec3(hi.x, hi.y, lo.z)), H(Vec3(hi.x, hi.y, hi.z)), H(Vec3(lo.x, hi.y, hi.z)) };
            b.Hex(v, top, side);
        }
    }

    Vec3 KingLean(const Vec3& p) { return RotX(kLeanPivot + (p - kLeanPivot) * kBody, kLeanPivot, kLean) - Vec3(0.0f, kSink, 0.0f); }
    Vec3 KingShoulder(float sx) { return KingLean(Vec3(sx * 0.48f, 0.30f, 0.0f)); }   // 樽形の籠に合わせて肩を外へ（10-29、0.33 → 0.48）
    // 肘 / 手首。体を起こして肩が上・後ろへ動いた分、腕は外へ開いて前の地面へ突く（肘が外を向く腕立ての形）
    Vec3 KingElbow(float sx) { return Vec3(sx * 0.95f, -0.32f, 0.80f); }   // 腕を上げる時に前腕が折れる所（VS の g_Hinge）
    Vec3 KingWrist(float sx) { return Vec3(sx * 1.02f, -0.78f, 1.18f); }

    // 背骨・肋骨・胸骨・鎖骨・肩甲骨・ぼろ布・頭蓋骨・冠（腕は別）
    std::shared_ptr<Model> BuildKingBody(ID3D11Device* device)
    {
        Builder b = LeanBuilder();
        std::mt19937 rng(2913u);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        // ---- 背骨（下は闇に沈む）と棘突起 ----
        for (int i = 0; i < 18; ++i)
        {
            const float y = -0.90f + 0.068f * (float)i;
            const Vec3 c(0.0f, y, SpineZ(y));
            b.Ball(c, 0.062f, (i & 1) ? kKingBone : kKingBoneDark, 6, 3);
            b.Spike(c, c + Vec3(0.0f, -0.01f, -0.095f), 0.04f, kKingBone);
        }
        // ---- 肋骨：8 対。上の胸が一番広く、下へ向かって細くなる（肩・鎖骨と合わせて上広がりの逆三角）。
        //      背骨から前へ回りながら下がり（下の肋骨ほど急）、前の端は胸骨に沿って並ぶ（下の 2 対は届かずに浮く）----
        // 10-29 ユーザー「体が痩せすぎ、膨らませて。ウォルニールは頭より体の方が大きい」で 6 対・半幅 0.19〜0.26 → 8 対・半幅 0.46 まで広げた。
        // その後「太りすぎ。人の骨格を考えて」: 輪を水平に積んだ樽（提灯）になっていたので、前へ下がる傾き・骨を細く（0.034 → 0.028）。
        // 一度「上が細く下が広い」卵形にしたら「上窄下宽じゃないだろ」→ 上の胸（上から 2〜3 本目）が一番広く、下へ細る形に
        constexpr int kRibs = 8;
        Vec3 sternumTop(0.0f, 0.30f, 0.26f), sternumLow = sternumTop;
        for (int k = 0; k < kRibs; ++k)
        {
            const float u = 1.0f - (float)k / (float)(kRibs - 1);          // 0 = 一番上 / 1 = 一番下
            const float y0 = -0.24f + 0.072f * (float)k;                    // 背骨の付け根の高さ
            const float topRound = (std::max)(0.0f, 0.2f - u) / 0.2f;      // 一番上の 1 本だけ少し内へ（鎖骨の下で丸める）
            const float taper = std::pow(u, 1.5f);                          // 下ほど細る
            const float a = 0.46f - 0.16f * taper - 0.10f * topRound;       // 横の張り出し（半幅）：上 0.36 → 上から 2 本目 0.45 → 下 0.30
            const float zf = 0.36f - 0.12f * taper - 0.08f * topRound;      // 前の深さ
            const float drop = 0.07f + 0.12f * u;                           // 前の端が背の付け根より下がる量
            const bool floating = (k < 2);
            const float thEnd = floating ? 3.1415927f * 0.78f : 3.1415927f;
            const float zb = SpineZ(y0);
            auto pt = [&](float sx, float t)
                {
                    const float th = thEnd * t;
                    return Vec3(sx * a * std::sin(th), y0 - drop * t, zb + (zf - zb) * 0.5f * (1.0f - std::cos(th)));
                };
            for (int sg = -1; sg <= 1; sg += 2)
            {
                const int seg = 9;
                for (int j = 0; j < seg; ++j)
                    b.Beam(pt((float)sg, (float)j / seg), pt((float)sg, (float)(j + 1) / seg), 0.028f, (k & 1) ? kKingBone : kKingBoneLight);
            }
            if (k == 2) sternumLow = pt(1.0f, 1.0f);   // 胸骨に届く一番下の肋骨の前の端
        }
        // 籠の中の闇（骨の隙間から見える暗い塊。籠が空っぽの細い枝に見えないように）。肋骨より内側に収める（はみ出すと胸骨が隠れて黒い玉に見えた）
        b.Ball(Vec3(0.0f, -0.16f, 0.0f), 0.19f, kKingCore, 10, 5);
        b.Ball(Vec3(0.0f, 0.0f, 0.02f), 0.25f, kKingCore, 10, 5);
        b.Ball(Vec3(0.0f, 0.16f, 0.02f), 0.26f, kKingCore, 10, 5);
        // 胸骨（鎖骨の付け根から、胸骨に届く一番下の肋骨の前まで）・鎖骨・肩の関節（肩は籠に合わせて外へ）
        b.Beam(sternumTop, sternumLow, 0.06f, kKingBoneLight, 0.04f);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float sx = (float)sg;
            b.Beam(Vec3(sx * 0.04f, 0.30f, 0.25f), Vec3(sx * 0.46f, 0.33f, 0.02f), 0.036f, kKingBoneLight);
            b.Ball(Vec3(sx * 0.48f, 0.30f, 0.0f), 0.10f, kKingBone, 8, 4);   // 肩の関節（上腕の骨頭が入る所）
            // 肩甲骨（背中の板）
            const float bz = SpineZ(0.17f) + 0.05f;   // 丸めた背の分だけ後ろへ
            const Vec3 q[4] = { Vec3(sx * 0.10f, 0.28f, -0.14f + bz), Vec3(sx * 0.42f, 0.30f, -0.08f + bz), Vec3(sx * 0.36f, 0.04f, -0.12f + bz), Vec3(sx * 0.13f, 0.08f, -0.15f + bz) };
            b.Plate(q, Vec3(0.0f, 0.0f, -1.0f), 0.025f, kKingBoneDark);
        }
        // ---- ぼろぼろの黒い布（肩と背から垂れる。先はぎざぎざ）----
        for (int i = 0; i < 11; ++i)
        {
            const float t = (float)i / 10.0f;
            const float x = -0.45f + 0.90f * t;
            const float len = 0.45f + 0.35f * u(rng);
            const Vec3 a(x, 0.31f - 0.05f * std::fabs(x) / 0.45f, -0.13f + SpineZ(0.25f) + 0.05f - 0.02f * u(rng));
            const Vec3 e = a + Vec3(x * 0.25f, -len, -0.05f - 0.05f * u(rng));
            b.Beam(a, e, 0.09f, (i & 1) ? kKingRag : kKingRagLight, 0.02f);
        }
        for (int sg = -1; sg <= 1; sg += 2)
            b.Beam(Vec3((float)sg * 0.30f, 0.33f, 0.05f), Vec3((float)sg * 0.52f, 0.12f, 0.10f), 0.085f, kKingRag, 0.02f);   // 肩に掛かる布

        // ---- 首と頭蓋骨（首で起こして前を見上げる）----
        // 首の骨（肩の高さから持ち上げた頭の付け根まで）
        for (int i = 0; i < 4; ++i)
        {
            const float t = (float)i / 3.0f;
            const Vec3 c = Vec3(0.0f, 0.31f, SpineZ(0.31f)) * (1.0f - t) + H(Vec3(0.0f, 0.34f, -0.03f)) * t;
            b.Ball(c, 0.05f, (i & 1) ? kKingBoneDark : kKingBone, 6, 3);
        }
        b.Ball(H(Vec3(0.0f, 0.37f, -0.01f)), 0.04f, kKingBoneDark, 6, 3);
        b.Ball(H(Vec3(0.0f, 0.50f, 0.02f)), 0.135f * kHS, kKingBoneLight, 10, 5);      // 頭蓋
        // 顔と口（10-29 ユーザー「下の歯しか無いみたいで原始人の顔」: 前は顔の箱の下端に歯が 1 列だけ並び、下顎が顔より前へ
        // 突き出て受け口に見えた → 上顎 + 上の歯 / 暗い口の中 / 一回り奥の下顎 + 下の歯 に分け、上の歯が下の歯の前に被さる普通の噛み合わせに）
        HBox(b, Vec3(-0.10f, 0.425f, 0.05f), Vec3(0.10f, 0.53f, 0.15f), kKingBoneLight, kKingBone);   // 顔の骨（額〜頬）
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float sx = (float)sg;
            b.Ball(H(Vec3(sx * 0.05f, 0.485f, 0.145f)), 0.038f * kHS, kKVoid, 8, 4);       // 眼窩
            b.Ball(H(Vec3(sx * 0.05f, 0.485f, 0.168f)), 0.016f * kHS, kKingEye, 6, 3);     // 奥の紫の火
            b.Ball(H(Vec3(sx * 0.095f, 0.455f, 0.115f)), 0.032f * kHS, kKingBone, 6, 3);   // 頬骨
        }
        // 鼻の穴（上が広く下が細い 2 段 = 逆三角）
        HBox(b, Vec3(-0.024f, 0.447f, 0.146f), Vec3(0.024f, 0.465f, 0.156f), kKVoid, kKVoid);
        HBox(b, Vec3(-0.012f, 0.433f, 0.146f), Vec3(0.012f, 0.449f, 0.156f), kKVoid, kKVoid);
        // 上顎（鼻の下の骨。顔の面より少し奥）と上の歯 6 本（下へ垂れる）
        HBox(b, Vec3(-0.085f, 0.402f, 0.06f), Vec3(0.085f, 0.428f, 0.142f), kKingBoneLight, kKingBone);
        for (int i = 0; i < 6; ++i)
        {
            const float x = -0.0575f + 0.023f * (float)i;
            HBox(b, Vec3(x - 0.0085f, 0.372f, 0.122f), Vec3(x + 0.0085f, 0.404f, 0.138f), kKingTooth, kKingTooth);
        }
        // 口の中の闇（上下の歯の隙間から見える）
        HBox(b, Vec3(-0.072f, 0.335f, 0.04f), Vec3(0.072f, 0.40f, 0.116f), kKVoid, kKVoid);
        // 下顎：顎先（上顎より奥 = 突き出さない）+ 左右の枝（耳の下へ上がる）。少し開いている
        {
            Vec3 v[8] = {
                H(Vec3(-0.078f, 0.288f, 0.03f)), H(Vec3(0.078f, 0.288f, 0.03f)), H(Vec3(0.055f, 0.298f, 0.128f)), H(Vec3(-0.055f, 0.298f, 0.128f)),
                H(Vec3(-0.082f, 0.332f, 0.03f)), H(Vec3(0.082f, 0.332f, 0.03f)), H(Vec3(0.06f, 0.336f, 0.126f)), H(Vec3(-0.06f, 0.336f, 0.126f)) };
            b.Hex(v, kKingBone, kKingBoneDark);
            for (int sg = -1; sg <= 1; sg += 2)
            {
                const float x0 = (float)sg * 0.083f;
                HBox(b, Vec3(x0 - 0.013f, 0.29f, -0.03f), Vec3(x0 + 0.013f, 0.43f, 0.05f), kKingBone, kKingBoneDark);
            }
            for (int i = 0; i < 6; ++i)
            {
                const float x = -0.05f + 0.02f * (float)i;
                HBox(b, Vec3(x - 0.0075f, 0.333f, 0.106f), Vec3(x + 0.0075f, 0.357f, 0.121f), kKingTooth, kKingTooth);
            }
        }
        // ---- 金の冠：輪 + 7 本の尖り（正面が一番高い）+ 紫の宝石 ----
        {
            const Vec3 c0 = H(Vec3(0.0f, 0.585f, 0.02f)), c1 = H(Vec3(0.0f, 0.635f, 0.02f));
            b.Tube(c0, c1, 0.125f * kHS, 0.13f * kHS, kGold, 14);
            for (int i = 0; i < 7; ++i)
            {
                const float ang = 3.1415927f * 0.5f + 6.2831853f * (float)i / 7.0f;   // i = 0 が正面
                const float h = (i == 0) ? 0.20f : (i == 1 || i == 6) ? 0.14f : 0.10f;
                const Vec3 base(std::cos(ang) * 0.125f, 0.63f, 0.02f + std::sin(ang) * 0.125f);
                const Vec3 tip = base + Vec3(std::cos(ang) * 0.03f, h, std::sin(ang) * 0.03f);
                b.Spike(H(base), H(tip), 0.045f * kHS, kGold);
                b.Ball(H(base + Vec3(std::cos(ang) * 0.008f, 0.012f, std::sin(ang) * 0.008f)), 0.018f * kHS, kKingGem, 6, 3);
            }
        }
        return b.Build(device);
    }

    // 腕 1 本（sx = -1 左 / +1 右）。肩 = KingShoulder(sx)（倒れた体の肩）。前の地面へ長く伸ばして手を突く（這う姿）
    std::shared_ptr<Model> BuildKingArm(ID3D11Device* device, float sx)
    {
        Builder b;
        std::mt19937 rng(sx > 0.0f ? 2914u : 2915u);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const Vec3 sh = KingShoulder(sx);
        const Vec3 elbow = KingElbow(sx);
        const Vec3 wrist = KingWrist(sx);
        // 人の腕の骨の形：長い骨は幹が細く、両端（関節）だけが膨らむ。上腕は 1 本、前腕は 2 本が隙間を空けて並ぶ。
        // 10-29 ユーザー「腕をもっと太く、主に関節を」→ 幹まで太くしたら「太りすぎ、人の骨格を考えて」: 上腕が半径 0.082 の
        // 一様な管（= 前腕の骨 1 本の 2 倍以上の太さ、ソーセージ）だった → 幹は細く（真ん中が一番細い）、関節の骨端で太さを出す
        Vec3 ua = elbow - sh;
        ua.Normalize();
        Vec3 uside = ua.Cross(Vec3(0.0f, 1.0f, 0.0f));
        uside.Normalize();
        // 上腕骨：幹（両端から真ん中へ細る）+ 肩側の骨頭 + 肘の左右の顆 + 肘頭
        const Vec3 umid = sh + (elbow - sh) * 0.5f;
        b.Tube(sh, umid, 0.046f, 0.034f, kKingBone, 8);
        b.Tube(umid, elbow, 0.034f, 0.044f, kKingBone, 8);
        b.Ball(sh + ua * 0.07f, 0.085f, kKingBoneLight, 8, 4);
        b.Ball(elbow + uside * 0.05f, 0.062f, kKingBoneLight, 8, 4);
        b.Ball(elbow - uside * 0.05f, 0.058f, kKingBone, 8, 4);
        b.Ball(elbow - ua * 0.02f, 0.066f, kKingBoneLight, 8, 4);
        // 前腕：橈骨と尺骨（細い 2 本、間に隙間）。手首側の骨端は膨らむ
        Vec3 d = wrist - elbow;
        d.Normalize();
        Vec3 side = d.Cross(Vec3(0.0f, 1.0f, 0.0f));
        side.Normalize();
        b.Beam(elbow + side * 0.045f, wrist + side * 0.05f, 0.05f, kKingBone, 0.044f);
        b.Beam(elbow - side * 0.045f, wrist - side * 0.036f, 0.046f, kKingBoneDark, 0.04f);
        b.Ball(wrist - d * 0.05f + side * 0.05f, 0.046f, kKingBoneLight, 8, 4);
        b.Ball(wrist - d * 0.05f - side * 0.036f, 0.04f, kKingBone, 8, 4);
        // 金の腕輪（手首に三重、二の腕に 1 つ）
        for (int i = 0; i < 3; ++i)
        {
            const float t = 0.58f + 0.11f * (float)i;
            const Vec3 a = elbow + (wrist - elbow) * t, e = elbow + (wrist - elbow) * (t + 0.07f);
            b.Tube(a, e, 0.09f, 0.088f, (i == 1) ? kGoldDark : kGold, 12);
        }
        b.Tube(sh + (elbow - sh) * 0.45f, sh + (elbow - sh) * 0.55f, 0.054f, 0.052f, kGold, 12);
        // 手：手根 + 4 本の指（中手骨 + 2 節。前の地面へ広げて突く）+ 親指
        b.Ball(wrist, 0.07f, kKingBoneLight, 8, 4);
        for (int f = 0; f < 4; ++f)
        {
            const float spread = (-1.5f + (float)f) * 0.32f;   // 外側へ広げる角（rad）
            const Vec3 dir(std::sin(spread) * sx * -1.0f + 0.05f * u(rng), 0.0f, std::cos(spread));
            // 腕を太くしたら手が小さく見えたので指も長く太く（0.10 / 0.13 / 0.12 → 0.14 / 0.17 / 0.16）
            const Vec3 knuckle = wrist + Vec3(dir.x * 0.14f, -0.06f, dir.z * 0.14f);
            const Vec3 mid = knuckle + Vec3(dir.x * 0.17f, -0.05f, dir.z * 0.17f);
            const Vec3 tip = mid + Vec3(dir.x * 0.16f, -0.03f, dir.z * 0.16f);
            b.Beam(wrist, knuckle, 0.032f, kKingBone);
            b.Ball(knuckle, 0.028f, kKingBoneLight, 6, 3);
            b.Beam(knuckle, mid, 0.028f, kKingBone);
            b.Ball(mid, 0.023f, kKingBoneLight, 6, 3);
            b.Spike(mid, tip, 0.028f, kKingBoneDark);
        }
        b.Beam(wrist, wrist + Vec3(-sx * 0.16f, -0.06f, 0.08f), 0.03f, kKingBone);
        return b.Build(device);
    }
}
