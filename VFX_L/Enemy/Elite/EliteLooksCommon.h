// ============================================================
// EliteLooksCommon.h
// EliteLooks*.cpp の内側だけで使う物：色（線形。HDR = bloom で光る）・籠手（Bracer）・型毎の組み立て関数の宣言。
// 2026-10-26 に EliteLooks.cpp（550 行）を面毎（EliteLooksGrass / Desert / Ruins.cpp、Apply は EliteLooks.cpp）に分けた時に作った。
// 外から呼ぶのは EliteLooks.h の Apply だけ
// ============================================================
#pragma once
#include "Enemy/LookBuilder.h"
#include "Graphics/Model/Model.h"
#include <memory>

struct ID3D11Device;

namespace EliteLooksDetail
{
    using namespace LookBuilder;   // Vec3 / Vec4 / Part / Builder / ArmFrame

    // 色（線形。HDR = bloom で光る）
    inline const Vec4 kVoid = { 0.008f, 0.006f, 0.012f, 1.0f };
    inline const Vec4 kIron = { 0.10f, 0.095f, 0.09f, 1.0f };
    inline const Vec4 kIronLight = { 0.24f, 0.23f, 0.22f, 1.0f };
    inline const Vec4 kSteel = { 0.42f, 0.43f, 0.46f, 1.0f };
    inline const Vec4 kBronze = { 0.36f, 0.20f, 0.06f, 1.0f };
    inline const Vec4 kGold = { 0.62f, 0.40f, 0.08f, 1.0f };
    inline const Vec4 kBone = { 0.60f, 0.55f, 0.42f, 1.0f };
    inline const Vec4 kFur = { 0.12f, 0.07f, 0.035f, 1.0f };
    inline const Vec4 kFurLight = { 0.22f, 0.15f, 0.08f, 1.0f };
    inline const Vec4 kLeather = { 0.10f, 0.055f, 0.03f, 1.0f };
    inline const Vec4 kWood = { 0.15f, 0.08f, 0.035f, 1.0f };
    inline const Vec4 kRedEye = { 3.6f, 0.35f, 0.15f, 1.0f };
    inline const Vec4 kHood = { 0.035f, 0.08f, 0.035f, 1.0f };
    inline const Vec4 kHoodLight = { 0.08f, 0.15f, 0.06f, 1.0f };
    inline const Vec4 kForestGlow = { 0.6f, 3.4f, 0.8f, 1.0f };
    inline const Vec4 kFeather = { 0.80f, 0.78f, 0.70f, 1.0f };
    inline const Vec4 kSand = { 0.62f, 0.50f, 0.32f, 1.0f };
    inline const Vec4 kSandDark = { 0.36f, 0.26f, 0.15f, 1.0f };
    inline const Vec4 kCloth = { 0.55f, 0.16f, 0.08f, 1.0f };
    inline const Vec4 kAmberGlow = { 3.4f, 1.8f, 0.3f, 1.0f };
    inline const Vec4 kJewel = { 0.2f, 1.4f, 2.6f, 1.0f };
    inline const Vec4 kBurgundy = { 0.20f, 0.025f, 0.03f, 1.0f };
    inline const Vec4 kBurgundyLight = { 0.38f, 0.07f, 0.04f, 1.0f };
    inline const Vec4 kFlame = { 5.0f, 1.6f, 0.25f, 1.0f };
    inline const Vec4 kFlameHot = { 6.0f, 4.0f, 1.2f, 1.0f };
    inline const Vec4 kShadow = { 0.018f, 0.016f, 0.035f, 1.0f };
    inline const Vec4 kShadowLight = { 0.06f, 0.05f, 0.11f, 1.0f };
    inline const Vec4 kViolet = { 2.2f, 0.5f, 4.2f, 1.0f };
    inline const Vec4 kNavy = { 0.02f, 0.03f, 0.08f, 1.0f };
    inline const Vec4 kNavyLight = { 0.06f, 0.08f, 0.19f, 1.0f };
    inline const Vec4 kParchment = { 0.62f, 0.56f, 0.40f, 1.0f };
    inline const Vec4 kCurseGlow = { 1.8f, 0.6f, 3.4f, 1.0f };
    inline const Vec4 kBookRed = { 0.24f, 0.03f, 0.04f, 1.0f };
    inline const Vec4 kBookGreen = { 0.03f, 0.14f, 0.07f, 1.0f };

    // 腕を一回り囲む箱（籠手）。手首から肘へ len 割
    inline void Bracer(Builder& b, const ArmFrame& f, float len, const Vec4& top, const Vec4& side)
    {
        const Vec3 c = f.hand - f.down * (len * 0.5f * f.w * 2.0f);
        b.OBox(c, f.ax, f.ay, f.az, Vec3(0.62f * f.w, len * f.w, 0.62f * f.w), top, side);
    }

    // ---- 草原：大斧の戦鬼 / 森の弓手（EliteLooksGrass.cpp）----
    std::shared_ptr<Model> AxeHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> AxeTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> AxeWeaponArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> AxeOffArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> ArcherHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> ArcherTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> ArcherBowArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> ArcherArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    // ---- 砂漠：砂塵の騎兵 / 炎砂の術師（EliteLooksDesert.cpp）----
    std::shared_ptr<Model> RiderHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> RiderTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> RiderLanceArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> RiderShieldArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> FlameHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> FlameTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> FlameStaffArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> FlameArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    // ---- 遺跡：影の剣士 / 呪いの司書（EliteLooksRuins.cpp）----
    std::shared_ptr<Model> ShadeHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> ShadeTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> ShadeBladeArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> ShadeArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> LibrarianHead(ID3D11Device* device, const Part& H);
    std::shared_ptr<Model> LibrarianTorso(ID3D11Device* device, const Part& T);
    std::shared_ptr<Model> LibrarianBookArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> LibrarianArm(ID3D11Device* device, const Part& A, const Vec3& pivot);
    std::shared_ptr<Model> OrbitBooks(ID3D11Device* device, const Model& model);
}
