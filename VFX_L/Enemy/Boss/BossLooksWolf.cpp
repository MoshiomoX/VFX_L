// ============================================================
// 草原 2 面目の Boss「月下の狼人」の飾り（2026-10-26、ユーザー「草原 2 も人形、半人半獣」→ 狼人）。
// 2026-10-27 ユーザーの参考画（MC の狼人 mod）に合わせて作り直した（「脚がおかしい」「腰を曲げた感じが無い」「鎌は要らない」）：
//   ・方块人の体は描かず（hideBody）、全部を胴の部品に付ける。部品ごとに付けていた時は、腿が股の振り子で前後に暴れていた
//   ・上半身（胴・頭・腕）は腰（kHips）を通る横軸の周りに前へ倒す = 猫背（VS の g_Hunch、歩くと更に深く）
//   ・脚は股の付け根で歩きの時計に振り（mode 4）、前へ振る間は膝が折れる（g_Hinge）。腕も肩で振る（脚と逆の位相）、
//     猫背の分は肩で戻して真下へ垂らし、殴りで前へ振る
//   ・形：肩と僧帽の大きな盛り上がり、その前に低く埋まった頭、首と頭の白い襟毛、膝まで届く長い腕、手首と足首の革の帯、
//     明るい灰の前腕と脚、白い 3 本の足の爪
// 座標は焼いた網の空間（足 ≒ y -0.65、+Z = 正面）を直接書く（方块人の部品の大きさには依らない）。Boss は VS が 3 倍にする
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

        // 四角い帯（Tube を短く、少し太く）= 革の巻き帯
        void Wrap(Builder& b, const Vec3& a, const Vec3& d, float r, float len)
        {
            Vec3 n = d;
            n.Normalize();
            b.Tube(a - n * (0.5f * len), a + n * (0.5f * len), r, r, kWolfWrap, 8);
        }
    }

    // 下の座標は足元 -0.67 で書いてある。焼いた網の地面は y -0.9（方块人の脚の包囲ボックス -0.65 は歩きの姿勢で脚が開いた
    // 高さ = 地面ではない。それで 10-27 の最初の版は Boss の 3 倍で 0.8m 浮いていた）→ 全部を kDown 下げる
    namespace
    {
        constexpr float kDown = 0.23f;
        const Vec3 kRawHips(0.0f, -0.22f, 0.0f);
        const Vec3 kRawNeck(0.0f, 0.22f, 0.12f);
        Vec3 RawHipRoot(float sx) { return Vec3(sx * 0.12f, -0.22f, 0.0f); }
        Vec3 RawKnee(float sx) { return Vec3(sx * 0.16f, -0.43f, 0.07f); }
        Vec3 RawShoulder(float sx) { return Vec3(sx * 0.30f, 0.17f, 0.0f); }
        Vec3 Down(const Vec3& p) { return p - Vec3(0.0f, kDown, 0.0f); }
        Builder WolfBuilder() { Builder b; b.move = Vec3(0.0f, -kDown, 0.0f); return b; }
    }

    // 腰・股・膝・肩（BossLooks.cpp が flap / hinge に入れる。下げた後の位置）
    const Vec3 kWolfHips = Down(kRawHips);
    const Vec3 kWolfNeck = Down(kRawNeck);
    Vec3 WolfHipRoot(float sx) { return Down(RawHipRoot(sx)); }
    Vec3 WolfKnee(float sx) { return Down(RawKnee(sx)); }
    Vec3 WolfShoulder(float sx) { return Down(RawShoulder(sx)); }

    std::shared_ptr<Model> BuildWolfTorso(ID3D11Device* device)
    {
        Builder b = WolfBuilder();
        std::mt19937 rng(2612u);
        // 胴：腰は細く、胸と肩は広い楔形
        {
            const float y0 = -0.25f, y1 = 0.20f;
            const float bx = 0.12f, bz0 = -0.09f, bz1 = 0.09f;      // 腰
            const float tx = 0.28f, tz0 = -0.16f, tz1 = 0.17f;      // 胸
            Vec3 p[8] = {
                Vec3(-bx, y0, bz0), Vec3(bx, y0, bz0), Vec3(bx, y0, bz1), Vec3(-bx, y0, bz1),
                Vec3(-tx, y1, tz0), Vec3(tx, y1, tz0), Vec3(tx, y1, tz1), Vec3(-tx, y1, tz1) };
            b.Hex(p, kWolfFur, kWolfFurDark);
        }
        // 僧帽と肩の盛り上がり（頭の後ろで一番高い）
        {
            Vec3 p[8] = {
                Vec3(-0.31f, 0.13f, -0.17f), Vec3(0.31f, 0.13f, -0.17f), Vec3(0.31f, 0.13f, 0.12f), Vec3(-0.31f, 0.13f, 0.12f),
                Vec3(-0.19f, 0.35f, -0.15f), Vec3(0.19f, 0.35f, -0.15f), Vec3(0.19f, 0.35f, 0.02f), Vec3(-0.19f, 0.35f, 0.02f) };
            b.Hex(p, kWolfFur, kWolfFurDark);
        }
        for (int sg = -1; sg <= 1; sg += 2)
            b.Ball(Vec3((float)sg * 0.29f, 0.18f, -0.01f), 0.11f, kWolfFur, 9, 4);   // 三角筋
        // 胸の前に垂れる尖った毛（参考画の「V」）：胸の下から股の前まで
        {
            const float z0 = 0.13f, z1 = 0.18f;
            Vec3 p[8] = {
                Vec3(-0.02f, -0.38f, z0 - 0.04f), Vec3(0.02f, -0.38f, z0 - 0.04f), Vec3(0.02f, -0.38f, z1 - 0.04f), Vec3(-0.02f, -0.38f, z1 - 0.04f),
                Vec3(-0.24f, 0.14f, z0), Vec3(0.24f, 0.14f, z0), Vec3(0.24f, 0.14f, z1), Vec3(-0.24f, 0.14f, z1) };
            b.Hex(p, kWolfFurDark, kWolfFurDark);
        }
        // 腰の革帯と、腰回りの暗い毛（腿の付け根を隠す）
        {
            Vec3 p[8] = {
                Vec3(-0.15f, -0.25f, -0.11f), Vec3(0.15f, -0.25f, -0.11f), Vec3(0.15f, -0.25f, 0.11f), Vec3(-0.15f, -0.25f, 0.11f),
                Vec3(-0.16f, -0.19f, -0.12f), Vec3(0.16f, -0.19f, -0.12f), Vec3(0.16f, -0.19f, 0.12f), Vec3(-0.16f, -0.19f, 0.12f) };
            b.Hex(p, kWolfWrap, kWolfWrap);
        }
        b.Box(Vec3(-0.17f, -0.33f, -0.12f), Vec3(0.17f, -0.25f, 0.10f), kWolfFurDark, kWolfFurDark);
        // 首と襟の白い毛（首の周りから前上へ）、肩の上の毛、背筋の毛
        FurTufts(b, rng, Vec3(0.0f, 0.24f, 0.10f), Vec3(0.0f, 0.55f, 1.0f), 0.13f, 0.11f, 0.05f, 14, kWolfRuff);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            FurTufts(b, rng, Vec3((float)sg * 0.17f, 0.27f, 0.04f), Vec3((float)sg * 0.6f, 0.6f, 0.6f), 0.05f, 0.10f, 0.05f, 6, kWolfRuff);
            FurTufts(b, rng, Vec3((float)sg * 0.30f, 0.27f, -0.02f), Vec3((float)sg * 0.7f, 1.0f, -0.2f), 0.05f, 0.09f, 0.05f, 5, kWolfFur);
        }
        // 背筋の毛：背の面（僧帽の盛り上がり → 胴の後ろの斜面）に沿って、脊の線の上に並べる（10-27 ユーザー「背中の毛が揃っていない」）
        auto backZ = [](float y)
            {
                if (y >= 0.13f) return -0.17f + (y - 0.13f) / 0.22f * 0.02f;     // 盛り上がりの後ろの面
                return -0.09f - (y + 0.25f) / 0.45f * 0.07f;                       // 胴の後ろの面（腰 -0.09 → 胸 -0.16）
            };
        for (int k = 0; k < 7; ++k)
        {
            const float y = 0.33f - (float)k * 0.075f;
            const float len = 0.075f - (float)k * 0.006f;
            const Vec3 base(0.0f, y, backZ(y) + 0.012f);
            b.Spike(base, base + Vec3(0.0f, 0.45f * len, -len), 0.055f - (float)k * 0.004f, kWolfFurDark);
        }
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildWolfHead(ID3D11Device* device, float pitch)
    {
        Builder b = WolfBuilder();
        std::mt19937 rng(2611u);
        // 猫背で前へ倒れる分を、首の付け根を軸に上へ起こしておく（顔は前を向く）
        b.pivot = kRawNeck;
        b.pitch = pitch;
        // 首（胴の中から頭の後ろへ）
        b.Tube(kRawNeck + Vec3(0.0f, -0.04f, -0.04f), Vec3(0.0f, 0.33f, 0.18f), 0.085f, 0.075f, kWolfFur, 8);
        // 頭蓋
        const Vec3 lo(-0.10f, 0.27f, 0.12f), hi(0.10f, 0.44f, 0.30f);
        b.Box(lo, hi, kWolfFur, kWolfFurDark);
        // 鼻面（明るい灰、前へ長い）+ 黒い鼻 + 下顎（少し開く）+ 舌 + 牙
        b.Box(Vec3(-0.055f, 0.28f, 0.29f), Vec3(0.055f, 0.36f, 0.44f), kWolfFurLight, kWolfFur);
        b.Ball(Vec3(0.0f, 0.352f, 0.44f), 0.028f, kWolfNose, 7, 3);
        b.Box(Vec3(-0.045f, 0.245f, 0.27f), Vec3(0.045f, 0.275f, 0.41f), kWolfFurDark);
        b.Box(Vec3(-0.025f, 0.272f, 0.32f), Vec3(0.025f, 0.282f, 0.39f), kWolfTongue);
        for (int sg = -1; sg <= 1; sg += 2)
        {
            const float x = (float)sg * 0.04f;
            b.Spike(Vec3(x, 0.285f, 0.415f), Vec3(x, 0.255f, 0.415f), 0.016f, kWolfClaw);
            b.Spike(Vec3(x, 0.275f, 0.395f), Vec3(x, 0.300f, 0.395f), 0.014f, kWolfClaw);
            // 目（赤く光る）と眉
            b.Ball(Vec3((float)sg * 0.055f, 0.385f, 0.300f), 0.022f, kWolfEye, 7, 3);
            b.Box(Vec3((float)sg * 0.055f - 0.04f, 0.405f, 0.285f), Vec3((float)sg * 0.055f + 0.04f, 0.425f, 0.315f), kWolfFurDark);
            // 耳（高く立つ。外は毛皮、内は明るい）
            const Vec3 eb((float)sg * 0.065f, 0.42f, 0.18f);
            b.Spike(eb, eb + Vec3((float)sg * 0.035f, 0.19f, -0.03f), 0.07f, kWolfFur);
            b.Spike(eb + Vec3(0.0f, 0.01f, 0.015f), eb + Vec3((float)sg * 0.03f, 0.15f, -0.01f), 0.035f, kWolfFurLight);
            // 頬の白い毛（横後ろへ）
            FurTufts(b, rng, Vec3((float)sg * 0.10f, 0.31f, 0.22f), Vec3((float)sg * 1.0f, -0.1f, -0.4f), 0.03f, 0.08f, 0.035f, 5, kWolfRuff);
        }
        // 頭の上と後ろの白いたてがみ
        FurTufts(b, rng, Vec3(0.0f, 0.44f, 0.18f), Vec3(0.0f, 0.6f, -1.0f), 0.06f, 0.10f, 0.045f, 10, kWolfRuff);
        FurTufts(b, rng, Vec3(0.0f, 0.36f, 0.12f), Vec3(0.0f, -0.2f, -1.0f), 0.08f, 0.10f, 0.05f, 8, kWolfRuff);
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildWolfArm(ID3D11Device* device, float sx)
    {
        Builder b = WolfBuilder();
        std::mt19937 rng(2613u + (uint32_t)(sx > 0.0f ? 1 : 0));
        // 肩 → 肘（外下へ）→ 手首（前下へ = 肘が少し曲がる）→ 手。手は膝の高さまで届く
        const Vec3 S = RawShoulder(sx);
        // （10-27 第 2 版：肘を少し後ろ、手は膝の横。前に出しすぎると猫背で手が体の遥か前に浮いた）
        const Vec3 E(sx * 0.37f, -0.08f, -0.04f);
        const Vec3 W(sx * 0.37f, -0.31f, 0.01f);
        const Vec3 H(sx * 0.365f, -0.39f, 0.035f);
        b.Ball(S, 0.10f, kWolfFur, 9, 4);
        b.Tube(S, E, 0.085f, 0.07f, kWolfFur, 8);
        b.Ball(E, 0.07f, kWolfFur, 8, 3);
        FurTufts(b, rng, E, Vec3(sx * 0.4f, -0.2f, -1.0f), 0.03f, 0.08f, 0.04f, 5, kWolfFur);
        b.Tube(E, W, 0.07f, 0.058f, kWolfFurLight, 8);
        // 手首の革帯 2 本
        Wrap(b, W + (E - W) * 0.10f, E - W, 0.068f, 0.035f);
        Wrap(b, W + (E - W) * 0.32f, E - W, 0.072f, 0.035f);
        // 手（暗い）+ 4 本の指と白い爪（下前へ曲がる）
        b.Tube(W, H, 0.056f, 0.066f, kWolfFurDark, 8);   // 手首 → 手の甲（前腕と手を繋ぐ）
        b.Ball(H, 0.072f, kWolfFurDark, 8, 3);
        for (int k = 0; k < 4; ++k)
        {
            const float t = ((float)k - 1.5f) / 1.5f;
            const Vec3 base = H + Vec3(sx * 0.01f, -0.02f, 0.02f) + Vec3(0.0f, 0.0f, 0.0f) + Vec3(t * 0.045f, 0.0f, 0.0f);
            const Vec3 mid = base + Vec3(t * 0.01f, -0.06f, 0.035f);
            b.Tube(base, mid, 0.018f, 0.015f, kWolfFurDark, 6);
            b.Spike(mid, mid + Vec3(t * 0.006f, -0.055f, 0.012f), 0.022f, kWolfClaw);
        }
        // 親指（内側）
        {
            const Vec3 base = H + Vec3(-sx * 0.05f, 0.0f, 0.02f);
            const Vec3 mid = base + Vec3(-sx * 0.02f, -0.04f, 0.04f);
            b.Tube(base, mid, 0.018f, 0.015f, kWolfFurDark, 6);
            b.Spike(mid, mid + Vec3(0.0f, -0.04f, 0.02f), 0.02f, kWolfClaw);
        }
        return b.Build(device);
    }

    std::shared_ptr<Model> BuildWolfLeg(ID3D11Device* device, float sx)
    {
        Builder b = WolfBuilder();
        std::mt19937 rng(2615u + (uint32_t)(sx > 0.0f ? 1 : 0));
        // 股 → 膝（外前へ、少し屈む）→ 足首（後ろへ）→ 足の先（前へ）。開いた立ち方
        const Vec3 hip = RawHipRoot(sx);
        const Vec3 knee = RawKnee(sx);
        const Vec3 ankle(sx * 0.18f, -0.585f, -0.01f);
        const Vec3 toe(sx * 0.19f, -0.625f, 0.10f);
        b.Ball(hip, 0.085f, kWolfFur, 8, 3);
        b.Tube(hip, knee, 0.085f, 0.065f, kWolfFurLight, 8);
        FurTufts(b, rng, hip + Vec3(sx * 0.06f, -0.02f, 0.0f), Vec3(sx * 0.5f, -1.0f, -0.2f), 0.03f, 0.08f, 0.04f, 4, kWolfFur);
        b.Ball(knee, 0.062f, kWolfFurLight, 8, 3);
        b.Tube(knee, ankle, 0.056f, 0.046f, kWolfFurLight, 8);
        // 足首の革帯
        Wrap(b, ankle + (knee - ankle) * 0.18f, knee - ankle, 0.058f, 0.04f);
        // 足（暗い）+ 前に 3 本の大きな白い爪 + 後ろの蹴爪
        b.Ball(ankle, 0.05f, kWolfFurDark, 8, 3);
        b.Tube(ankle, toe, 0.05f, 0.045f, kWolfFurDark, 7);
        b.Ball(toe, 0.048f, kWolfFurDark, 8, 3);
        for (int k = -1; k <= 1; ++k)
        {
            const Vec3 base = toe + Vec3((float)k * 0.035f, -0.005f, 0.02f);
            b.Spike(base, base + Vec3((float)k * 0.015f, -0.02f, 0.075f), 0.03f, kWolfClaw);
        }
        b.Spike(ankle + Vec3(0.0f, -0.02f, -0.03f), ankle + Vec3(0.0f, -0.035f, -0.08f), 0.025f, kWolfClaw);
        return b.Build(device);
    }
}
