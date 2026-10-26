// ============================================================
// BossLooksCommon.h
// BossLooks*.cpp の内側だけで使う物：色（線形。頂点色がそのまま掛かる）・共通の小物（Tufts）・Boss 毎の組み立て関数の宣言。
// 2026-10-26 に BossLooks.cpp（700 行）を Boss 毎（BossLooksSand / Scorpion / Stag.cpp、岩の巨人と Apply は BossLooks.cpp）に
// 分けた時に作った。外から呼ぶのは BossLooks.h の Apply だけ
// ============================================================
#pragma once
#include "Enemy/LookBuilder.h"
#include "Graphics/Model/Model.h"
#include <memory>
#include <random>

struct ID3D11Device;

namespace BossLooksDetail
{
    using namespace LookBuilder;   // Vec3 / Vec4 / Part / Builder

    // 色（線形。頂点色がそのまま掛かる。皮の砂岩 sRGB 0.88/0.71/0.47 ≒ 線形 0.75/0.46/0.19 に合わせる）
    inline const Vec4 kGold = { 0.85f, 0.52f, 0.10f, 1.0f };
    inline const Vec4 kGoldDark = { 0.52f, 0.30f, 0.05f, 1.0f };
    inline const Vec4 kLapis = { 0.035f, 0.09f, 0.36f, 1.0f };
    inline const Vec4 kStoneLight = { 0.66f, 0.42f, 0.19f, 1.0f };
    inline const Vec4 kStoneDark = { 0.33f, 0.20f, 0.08f, 1.0f };
    inline const Vec4 kGlow = { 7.0f, 3.6f, 0.7f, 1.0f };       // 目・胸の宝石（HDR = bloom で光る）
    inline const Vec4 kVein = { 2.2f, 1.1f, 0.22f, 1.0f };      // 浮き岩の金の筋（3.2 は白く飛んだ）
    // 草原「岩の巨人」（2026-10-25）
    inline const Vec4 kMoss = { 0.06f, 0.16f, 0.025f, 1.0f };
    inline const Vec4 kMossLight = { 0.13f, 0.30f, 0.04f, 1.0f };
    inline const Vec4 kRock = { 0.21f, 0.22f, 0.21f, 1.0f };
    inline const Vec4 kRockDark = { 0.09f, 0.10f, 0.10f, 1.0f };
    inline const Vec4 kHorn = { 0.52f, 0.48f, 0.38f, 1.0f };
    inline const Vec4 kRune = { 0.5f, 4.6f, 1.3f, 1.0f };       // 目・胸の紋（HDR の緑）
    inline const Vec4 kFlower[3] = { { 1.0f, 0.75f, 0.06f, 1.0f }, { 0.92f, 0.92f, 0.86f, 1.0f }, { 0.95f, 0.30f, 0.50f, 1.0f } };
    // 砂漠 2 面目「大サソリ」（10-26 ユーザーの参考画：黒紫の棘だらけの甲羅、甲片と腹の隙間から毒の緑が透ける、緑に光る目、
    // 鋸歯の鋏、毒液の滴る針。それまでは赤褐色だった）
    inline const Vec4 kChitin = { 0.065f, 0.035f, 0.095f, 1.0f };     // 黒紫（砂漠の日差しで一段明るく見えるので暗めに）
    inline const Vec4 kChitinLight = { 0.13f, 0.08f, 0.18f, 1.0f };   // 甲片の明るい面
    inline const Vec4 kChitinDark = { 0.03f, 0.02f, 0.045f, 1.0f };
    inline const Vec4 kSpike = { 0.20f, 0.16f, 0.24f, 1.0f };         // 棘（灰紫）
    inline const Vec4 kClawTip = { 0.30f, 0.27f, 0.33f, 1.0f };       // 鋏の歯・脚の先
    inline const Vec4 kScorpEye = { 1.4f, 6.0f, 0.9f, 1.0f };         // 緑に光る目
    inline const Vec4 kVenom = { 0.9f, 3.6f, 0.3f, 1.0f };            // 毒の緑（HDR = bloom）
    inline const Vec4 kVenomDim = { 0.25f, 0.9f, 0.08f, 1.0f };       // 関節・隙間から透ける緑（弱め）
    // 遺跡 1 面目「墓守の騎士」（2026-10-26）：呪いの鎧と同じ暗い鋼・紫の呪い（EnemyLooks.cpp の kSteel 系と同じ値）+ 骨の角
    inline const Vec4 kKSteel = { 0.13f, 0.135f, 0.15f, 1.0f };
    inline const Vec4 kKSteelDark = { 0.045f, 0.045f, 0.055f, 1.0f };
    inline const Vec4 kKSteelLight = { 0.24f, 0.245f, 0.27f, 1.0f };
    inline const Vec4 kKCloth = { 0.13f, 0.02f, 0.17f, 1.0f };
    inline const Vec4 kKGlow = { 1.7f, 0.3f, 2.8f, 1.0f };
    inline const Vec4 kKVoid = { 0.008f, 0.006f, 0.012f, 1.0f };
    inline const Vec4 kKLeather = { 0.10f, 0.055f, 0.03f, 1.0f };
    inline const Vec4 kKChain = { 0.30f, 0.29f, 0.28f, 1.0f };
    inline const Vec4 kKBone = { 0.55f, 0.50f, 0.40f, 1.0f };
    // 草原 2 面目「月下の狼人」（2026-10-26）：灰の毛皮、明るい腹、黒い鼻、赤く光る目、骨色の爪
    inline const Vec4 kWolfFur = { 0.20f, 0.19f, 0.21f, 1.0f };
    inline const Vec4 kWolfFurLight = { 0.34f, 0.33f, 0.34f, 1.0f };
    inline const Vec4 kWolfFurDark = { 0.09f, 0.09f, 0.10f, 1.0f };
    inline const Vec4 kWolfBelly = { 0.46f, 0.44f, 0.41f, 1.0f };
    inline const Vec4 kWolfEye = { 3.5f, 0.4f, 0.2f, 1.0f };
    inline const Vec4 kWolfNose = { 0.02f, 0.02f, 0.02f, 1.0f };
    inline const Vec4 kWolfClaw = { 0.60f, 0.56f, 0.46f, 1.0f };
    inline const Vec4 kWolfTongue = { 0.55f, 0.10f, 0.14f, 1.0f };
    // 草原 3 面目「雷鳴の雄鹿」（2026-10-26）：雷の鹿なので体も青（ユーザー「青いなら体も青に」）：藍の毛並み、
    // 明るい青の腹、黒い蹄、氷のような淡青の角、青白く光る目と角の先
    // 10-26 ユーザーの参考画（青灰の鹿 + 金の扇のような大角 + 角と枝に白い花 + 首胸の金の飾り）に寄せた：
    inline const Vec4 kFur = { 0.18f, 0.26f, 0.52f, 1.0f };        // 青灰
    inline const Vec4 kFurLight = { 0.32f, 0.42f, 0.70f, 1.0f };
    inline const Vec4 kFurDark = { 0.08f, 0.12f, 0.30f, 1.0f };
    inline const Vec4 kBelly = { 0.55f, 0.65f, 0.85f, 1.0f };
    inline const Vec4 kHoof = { 0.03f, 0.03f, 0.08f, 1.0f };
    inline const Vec4 kAntler = { 0.85f, 0.52f, 0.10f, 1.0f };     // 金（kGold と同じ）
    inline const Vec4 kAntlerDark = { 0.52f, 0.30f, 0.05f, 1.0f };
    inline const Vec4 kAntlerGlow = { 2.6f, 1.8f, 0.5f, 1.0f };    // 角の縁（少し光る金）
    inline const Vec4 kBark = { 0.26f, 0.17f, 0.08f, 1.0f };       // 花の枝
    inline const Vec4 kLeaf = { 0.92f, 0.90f, 0.84f, 1.0f };       // 白い花
    inline const Vec4 kLeafLight = { 1.00f, 0.96f, 0.90f, 1.0f };
    inline const Vec4 kBlossomHeart = { 0.95f, 0.72f, 0.30f, 1.0f };
    inline const Vec4 kVine = { 0.16f, 0.30f, 0.08f, 1.0f };
    inline const Vec4 kStagEye = { 0.6f, 3.0f, 6.0f, 1.0f };
    inline const Vec4 kSpark = { 1.6f, 3.6f, 6.0f, 1.0f };

    // 上面 y の範囲（lo.xz〜hi.xz）に細い草の葉を n 本・花を flowers 本。unit = 大きさの基準（頭の幅など）
    inline void Tufts(Builder& b, std::mt19937& rng, const Vec3& lo, const Vec3& hi, float y, float unit, int n, int flowers)
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

    // ---- 砂の守護者（BossLooksSand.cpp）----
    std::shared_ptr<Model> BuildHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> BuildTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> BuildArm(ID3D11Device* device, const Part& A, const Vec3& pivot, uint32_t seed);
    std::shared_ptr<Model> BuildOrbitRocks(ID3D11Device* device, const Model& model);
    // ---- 大サソリ（BossLooksScorpion.cpp）----
    std::shared_ptr<Model> BuildScorpionBody(ID3D11Device* device);
    std::shared_ptr<Model> BuildScorpionLeg(ID3D11Device* device, const Vec3& root, float sx);   // 1 本（付け根 root、sx = 左右）
    std::shared_ptr<Model> BuildScorpionClaw(ID3D11Device* device, float sx);
    std::shared_ptr<Model> BuildScorpionTail(ID3D11Device* device, const Vec3& hinge);
    // ---- 雷鳴の雄鹿（BossLooksStag.cpp）。StagXf = 脚 / 首の付け根を雄鹿の拡大に合わせる ----
    Vec3 StagXf(const Vec3& p);
    std::shared_ptr<Model> BuildStagBody(ID3D11Device* device);
    std::shared_ptr<Model> BuildStagHead(ID3D11Device* device, const Vec3& hinge);
    std::shared_ptr<Model> BuildStagLeg(ID3D11Device* device, const Vec3& hinge, bool front);
    // ---- 墓守の騎士（BossLooksKnight.cpp）----
    std::shared_ptr<Model> BuildKnightHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> BuildKnightTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> BuildKnightSwordArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> BuildKnightChainArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    // ---- 月下の狼人（BossLooksWolf.cpp。体は隠し、部品毎に毛皮の部品を付ける）----
    std::shared_ptr<Model> BuildWolfHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> BuildWolfTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> BuildWolfArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> BuildWolfLeg(ID3D11Device* device, const Part& L, const Vec3& pivot);
    // ---- 岩の巨人（BossLooks.cpp）----
    std::shared_ptr<Model> BuildStoneHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> BuildStoneTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> BuildStoneArm(ID3D11Device* device, const Part& A, const Vec3& pivot, uint32_t seed);
}
