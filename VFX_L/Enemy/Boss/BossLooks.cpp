// ============================================================
// BossLooks.cpp
// 面の Boss の皮と飾り（説明は BossLooks.h）。飾りの寸法は全部、焼いた雑魚の網の部品の包囲ボックス
// （頭・胴・腕。名前で探し、無ければ位置で推す）に対する割合で決める = 網の大きさが変わっても付いて来る。
// 網の空間：足元 = カプセルの下（y ≒ -0.9）、高さ 1.6、+Z が正面。Boss は VS がこれを体格倍（3 倍）にする
// 2026-10-26 に 700 行を分けた：砂の守護者 = BossLooksSand.cpp、大サソリ = BossLooksScorpion.cpp、雄鹿 = BossLooksStag.cpp、
// 色と共通の小物と宣言 = BossLooksCommon.h。ここに残るのは岩の巨人と Apply（Boss 毎に飾りを挿げ替える所）
// ============================================================
#include "Enemy/Boss/BossLooks.h"
#include "Enemy/Boss/BossLooksCommon.h"
#include "Swarm/SwarmSystem.h"
#include "Graphics/Model/Model.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace LookBuilder;
using namespace BossLooksDetail;

// ============================================================
// 草原の Boss「岩の巨人」（2026-10-25、ユーザー「巨人の見た目も変えて」。方向は推奨の「苔むした岩の巨人」）:
// 頭 = 苔の帽子（塊と垂れ）+ 草と小さな花 + 石の角 + 石の眉 + 緑に光る目、胴 = 緑に光る紋を刻んだ石の胸板 + 肩の苔 + 蔦の帯、
// 腕 = 苔と草の載った肩の大岩 + 腕の向きに沿った岩の拳。皮は Kenney Blocky O（牙のあるオーク）を灰色の岩と苔に塗り直した物
// ============================================================
namespace BossLooksDetail
{
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
    if (type == BossType::Stag)
    {
        const uint32_t torsoPart = torso.Valid() ? (uint32_t)torso.index : 0u;
        add(BuildStagBody(device), torsoPart);
        // 4 本の脚：胴に付け、各自の付け根を軸に歩きの時計で振る（mode 4）。左前 + 右後が位相 0、右前 + 左後が π
        for (int i = 0; i < 4; ++i)
        {
            const bool front = i < 2;
            const float sx = (i & 1) ? 1.0f : -1.0f;
            const Vec3 hinge(sx * 0.21f, 0.32f, front ? 0.40f : -0.52f);
            SwarmSystem::BossDecoration d;
            d.model = BuildStagLeg(device, hinge, front);
            d.part = torsoPart;
            d.leg = true;
            const bool pairA = (front && sx < 0.0f) || (!front && sx > 0.0f);
            const Vec3 h = StagXf(hinge);
            d.flap = { h.x, h.y, h.z, pairA ? 0.0f : 3.14159f };
            if (d.model) decos.push_back(std::move(d));
        }
        {
            SwarmSystem::BossDecoration d;
            const Vec3 hinge(0.0f, 0.42f, 0.50f);   // 首の付け根（胸の上）
            d.model = BuildStagHead(device, hinge);
            d.part = torsoPart;
            d.tail = true;
            const Vec3 h = StagXf(hinge);
            d.flap = { h.x, h.y, h.z, 0.9f };   // 突進 / 殴りの時に頭を前へ下げる角度（rad）
            if (d.model) decos.push_back(std::move(d));
        }
        swarm.SetBossLook(nullptr, std::move(decos), true);   // 方块人の体は描かない
        return;
    }
    if (type == BossType::Scorpion)
    {
        const uint32_t torsoPart = torso.Valid() ? (uint32_t)torso.index : 0u;
        add(BuildScorpionBody(device), torsoPart);
        // 8 本の脚：1 本ずつ胴に付け、各自の付け根の縦軸で前後に掻く（VS の mode 5）。三脚歩行 = 左の 1・3 本目 + 右の 2・4 本目が
        // 同じ位相、残りが逆。以前は方块人の leg 部品 2 つに付けていて、股の横軸の振りで横へ伸びた脚が上下に暴れた
        const float zs[4] = { 0.40f, 0.12f, -0.16f, -0.44f };
        for (int i = 0; i < 4; ++i)
            for (int side = -1; side <= 1; side += 2)
            {
                const float sx = (float)side;
                const Vec3 root(sx * 0.48f, -0.62f, zs[i]);
                SwarmSystem::BossDecoration d;
                d.model = BuildScorpionLeg(device, root, sx);
                d.part = torsoPart;
                d.insectLeg = true;
                const bool phaseA = (((i & 1) == 0) == (side < 0));
                d.flap = { root.x, root.y, root.z, phaseA ? 0.0f : 3.14159f };
                if (d.model) decos.push_back(std::move(d));
            }
        // 鋏：胴に付ける（腕の部品に付けると歩き / 殴りの振りで上下に暴れた）
        add(BuildScorpionClaw(device, -1.0f), torsoPart);
        add(BuildScorpionClaw(device, 1.0f), torsoPart);
        {
            SwarmSystem::BossDecoration d;
            const Vec3 hinge(0.0f, -0.45f, -0.70f);
            d.model = BuildScorpionTail(device, hinge);
            d.part = torsoPart;
            d.tail = true;
            d.flap = { hinge.x, hinge.y, hinge.z, 1.1f };   // w = 刺す時に前へ振る角度（rad）
            if (d.model) decos.push_back(std::move(d));
        }
        swarm.SetBossLook(nullptr, std::move(decos), true);   // 方块人の体は描かない
        return;
    }
    if (type == BossType::Werewolf)
    {
        // 月下の狼人（草原 2 面目、2026-10-26）：体は隠し、頭・胴・腕 × 2・脚 × 2 それぞれに毛皮の部品（部品の動きに付いて来る）
        int legA = -1, legB = -1;
        {
            const auto& subs = model->GetSubMeshes();
            for (int i = 0; i < (int)subs.size(); ++i)
                if (NameHas(subs[i].nodeName, "leg")) { if (legA < 0) legA = i; else if (legB < 0) legB = i; }
        }
        if (head.Valid()) add(BuildWolfHead(device, head), (uint32_t)head.index);
        if (torso.Valid()) add(BuildWolfTorso(device, torso), (uint32_t)torso.index);
        if (armA.Valid()) add(BuildWolfArm(device, armA, swarm.GetPartPivot((uint32_t)armA.index)), (uint32_t)armA.index);
        if (armB.Valid()) add(BuildWolfArm(device, armB, swarm.GetPartPivot((uint32_t)armB.index)), (uint32_t)armB.index);
        const auto& subs = model->GetSubMeshes();
        for (int li : { legA, legB })
        {
            if (li < 0) continue;
            Part L;
            L.index = li;
            L.lo = subs[(size_t)li].boundsMin;
            L.hi = subs[(size_t)li].boundsMax;
            add(BuildWolfLeg(device, L, swarm.GetPartPivot((uint32_t)li)), (uint32_t)li);
        }
        swarm.SetBossLook(nullptr, std::move(decos), true);   // 方块人の体は描かない
        return;
    }
    if (type == BossType::CryptKnight)
    {
        // 墓守の騎士（遺跡 1 面目、2026-10-26）：体は方块人 + 呪いの鎧の鋼の皮、右腕 = 大剣、左腕 = 鎖（右 = +x の腕）
        Part armR = armA, armL = armB;
        if (armR.Valid() && armL.Valid() && armR.Center().x < armL.Center().x) std::swap(armR, armL);
        if (head.Valid()) add(BuildKnightHead(device, head), (uint32_t)head.index);
        if (torso.Valid()) add(BuildKnightTorso(device, torso), (uint32_t)torso.index);
        if (armR.Valid()) add(BuildKnightSwordArm(device, armR, swarm.GetPartPivot((uint32_t)armR.index)), (uint32_t)armR.index);
        if (armL.Valid()) add(BuildKnightChainArm(device, armL, swarm.GetPartPivot((uint32_t)armL.index)), (uint32_t)armL.index);
        swarm.SetBossLook(ResourceManager::Get().LoadTexture(Res::Tex::RuinsArmorAlbedo), std::move(decos));
        return;
    }
    // 岩の巨人（草原。遺跡 2・3 面目の Boss を作るまではそれらもこれ）
    if (head.Valid()) add(BuildStoneHead(device, head), (uint32_t)head.index);
    if (torso.Valid()) add(BuildStoneTorso(device, torso), (uint32_t)torso.index);
    if (armA.Valid()) add(BuildStoneArm(device, armA, swarm.GetPartPivot((uint32_t)armA.index), 41u), (uint32_t)armA.index);
    if (armB.Valid()) add(BuildStoneArm(device, armB, swarm.GetPartPivot((uint32_t)armB.index), 43u), (uint32_t)armB.index);
    swarm.SetBossLook(ResourceManager::Get().LoadTexture(Res::Tex::BossStoneGiantAlbedo), std::move(decos));
}
