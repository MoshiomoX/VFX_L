// ============================================================
// EliteLooks.cpp
// 面の固定エリートの皮と飾り（説明は EliteLooks.h）。寸法は焼いた雑魚の網の部品の包囲ボックスに対する割合
// （EnemyLooks.cpp と同じ流儀。網の空間：足元 y ≒ -0.9、頭 y 0.29〜0.76、+Z が正面。手に持つ物は FrameOf の腕の向きで組む）
// 2026-10-26 に 550 行を分けた：草原 = EliteLooksGrass.cpp、砂漠 = EliteLooksDesert.cpp、遺跡 = EliteLooksRuins.cpp、
// 色と籠手と宣言 = EliteLooksCommon.h。ここに残るのは Apply（面毎に A / B の飾りと皮を入れる所）
// ============================================================
#include "Enemy/Elite/EliteLooks.h"
#include "Enemy/Elite/EliteLooksCommon.h"
#include "Swarm/SwarmSystem.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace LookBuilder;
using namespace EliteLooksDetail;

void EliteLooks::Apply(int stage, ID3D11Device* device, SwarmSystem& swarm)
{
    const std::shared_ptr<Model> modelRef = swarm.GetEnemyModel();
    const Model* model = modelRef.get();
    if (!model || !device) return;

    Part head, torso, armA, armB;
    FindParts(*model, head, torso, armA, armB);
    Part armR = armA, armL = armB;   // 右手 = +x の腕（持ち物）、左手 = 盾・弓・本
    if (armR.Valid() && armL.Valid() && armR.Center().x < armL.Center().x) std::swap(armR, armL);
    const Vec3 pivR = armR.Valid() ? swarm.GetPartPivot((uint32_t)armR.index) : Vec3::Zero;
    const Vec3 pivL = armL.Valid() ? swarm.GetPartPivot((uint32_t)armL.index) : Vec3::Zero;

    using Deco = SwarmSystem::BossDecoration;
    auto add = [](std::vector<Deco>& list, std::shared_ptr<Model> m, uint32_t part, float orbit = 0.0f)
        {
            if (!m) return;
            Deco d;
            d.model = std::move(m);
            d.part = part;
            d.orbitSpeed = orbit;
            list.push_back(std::move(d));
        };
    const uint32_t H = (uint32_t)head.index, T = (uint32_t)torso.index, R = (uint32_t)armR.index, L = (uint32_t)armL.index;
    const bool ok = head.Valid() && torso.Valid() && armR.Valid() && armL.Valid();
    auto& rm = ResourceManager::Get();
    std::vector<Deco> a, b;
    const wchar_t* texA = Res::Tex::EliteAxeAlbedo;
    const wchar_t* texB = Res::Tex::EliteArcherAlbedo;
    if (ok && stage == 2)
    {
        add(a, RiderHead(device, head), H);
        add(a, RiderTorso(device, torso), T);
        add(a, RiderLanceArm(device, armR, pivR), R);
        add(a, RiderShieldArm(device, armL, pivL), L);
        add(b, FlameHead(device, head), H);
        add(b, FlameTorso(device, torso), T);
        add(b, FlameStaffArm(device, armR, pivR), R);
        add(b, FlameArm(device, armL, pivL), L);
        texA = Res::Tex::EliteRiderAlbedo;
        texB = Res::Tex::EliteFlameAlbedo;
    }
    else if (ok && stage == 3)
    {
        add(a, ShadeHead(device, head), H);
        add(a, ShadeTorso(device, torso), T);
        add(a, ShadeBladeArm(device, armR, pivR), R);
        add(a, ShadeArm(device, armL, pivL), L);
        add(b, LibrarianHead(device, head), H);
        add(b, LibrarianTorso(device, torso), T);
        add(b, LibrarianArm(device, armR, pivR), R);
        add(b, LibrarianBookArm(device, armL, pivL), L);
        add(b, OrbitBooks(device, *model), SwarmSystem::kDecoOrbit, 0.8f);
        texA = Res::Tex::EliteShadeAlbedo;
        texB = Res::Tex::EliteLibrarianAlbedo;
    }
    else if (ok)
    {
        add(a, AxeHead(device, head), H);
        add(a, AxeTorso(device, torso), T);
        add(a, AxeWeaponArm(device, armR, pivR), R);
        add(a, AxeOffArm(device, armL, pivL), L);
        add(b, ArcherHead(device, head), H);
        add(b, ArcherTorso(device, torso), T);
        add(b, ArcherArm(device, armR, pivR), R);
        add(b, ArcherBowArm(device, armL, pivL), L);
    }
    swarm.SetListLook(Swarm::kDrawListEliteA, rm.LoadTexture(texA), std::move(a));
    swarm.SetListLook(Swarm::kDrawListEliteB, rm.LoadTexture(texB), std::move(b));
    std::cout << "[EliteLooks] stage " << stage << " decorations set" << std::endl;
}
