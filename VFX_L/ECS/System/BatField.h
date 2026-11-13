// ============================================================
// BatField.h
// 吸血コウモリの見た目。WeaponSystem::GetBats を毎フレーム読み、コードで作った低ポリの蝙蝠
// （体・頭・耳・羽ばたく 2 枚の膜の翼・光る目）を VFXMeshRenderer へ積む。
// 飛んでいる向きへ体を向け、後ろに赤い光の残像。噛んで戻る時は血の赤が強くなる
// ============================================================
#pragma once
#include "ECS/System/WeaponSystem.h"
#include <SimpleMath.h>
#include <memory>
#include <vector>

class Model;
class VFXMeshRenderer;

class BatField
{
public:
    void Submit(VFXMeshRenderer& renderer, const std::vector<WeaponSystem::Bat>& bats);
    // 1 匹に 1 つ弱い赤の点光源（Update の中で呼ぶ。点光源の表は Render の頭で GPU へ）
    void SubmitLights(const std::vector<WeaponSystem::Bat>& bats) const;
    bool  lights = true;
    DirectX::SimpleMath::Vector3 lightColor = { 1.0f, 0.10f, 0.14f };
    float lightRadius = 2.2f;
    float lightIntensity = 0.45f;
    float lightReturnMul = 1.8f;   // 血を運んで戻る時
    void DrawImGui();

    bool  enabled = true;
    float scale = 0.9f;         // 全体の大きさ（翼の端から端まで約 0.85m × これ）
    float flapFreq = 22.0f;        // 羽ばたき（rad/秒）
    float flapAmp = 0.95f;         // 羽ばたきの振れ（rad）
    DirectX::SimpleMath::Vector3 bodyColor = { 0.10f, 0.035f, 0.08f };
    DirectX::SimpleMath::Vector3 wingColor = { 0.20f, 0.04f, 0.13f };
    DirectX::SimpleMath::Vector3 eyeColor = { 4.0f, 0.35f, 0.30f };     // HDR
    // 血の赤の細い尾（WeaponSystem::Bat::trail の位置をつなぐ。半透明）
    DirectX::SimpleMath::Vector3 trailColor = { 0.75f, 0.02f, 0.06f };
    float trailWidth = 0.07f;      // 頭の太さ m（尻尾は 1/4）
    float trailAlpha = 0.9f;
    float trailIntensity = 1.3f;
    float returnGlow = 1.8f;       // 戻る時（血を運ぶ）の血の玉の光

private:
    void EnsureModels();
    bool m_Tried = false;
    std::shared_ptr<Model> m_Sphere;   // 体・頭・目・光（半径 0.5）
    std::shared_ptr<Model> m_Wing;     // 右の翼（付け根 = 原点、+X へ広がる、前 = +Z）
    std::shared_ptr<Model> m_Ear;      // 耳（三角）
};
