// ============================================================
// BladeField.h
// 回る剣（魔剣の輪・回旋斬）の見た目。WeaponSystem::GetBladeSets を毎フレーム読み、
// 剣のモデル（AreaProfile::bladeModel、先端 +Y）を VFXMeshRenderer へ積む：
//   半透明のガラスのような本体 + 加算の光の殻 + 回った跡の残像。判定は WeaponSystem（GPU の範囲）
// ============================================================
#pragma once
#include "ECS/System/WeaponSystem.h"
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Model;
class VFXMeshRenderer;

class BladeField
{
public:
    void Submit(VFXMeshRenderer& renderer, const std::vector<WeaponSystem::BladeSet>& sets, float lift);
    void DrawImGui();

    bool  enabled = true;
    float tiltDeg = 28.0f;         // 剣の平らな面をカメラへ少し向ける（剣の向きの軸で回す）
    float bodyAlpha = 0.85f;
    float bodyIntensity = 1.15f;
    float whiten = 0.25f;          // 本体の色を道具の色から白へ寄せる割合（幻影らしく淡く）
    float shellScale = 1.12f;      // 光の殻の大きさ（本体に対して）
    float shellAlpha = 0.42f;
    float shellIntensity = 1.6f;
    int   trailCount = 6;          // 残像の数
    float trailStepDeg = 9.0f;     // 残像の間隔（回った角度。速く回る回旋斬でも途切れない）
    float trailAlpha = 0.30f;
    float bob = 0.08f;             // 上下のゆれ（m）
    // ---- 刀光：剣が通った弧に残る光の帯（剣の側が明るく、後ろへ消える）----
    bool  slashLight = true;
    float slashTime = 0.2f;        // 帯の長さ = 回る速さ × この秒（度に直して 40〜160 度）
    float slashAlpha = 0.7f;
    float slashIntensity = 2.4f;
    float slashWhiten = 0.12f;     // 帯の色を白へ寄せる割合

private:
    // 刀光の帯（内側の半径の比 inner、長さ spanDeg 度。剣の向き = +Z、後ろ = -X 側へ回る）。形ごとに 1 回作って使い回す
    std::unordered_map<int, std::shared_ptr<Model>> m_Arcs;
    Model* Arc(float inner, float spanDeg);

    struct ModelInfo
    {
        std::shared_ptr<Model> model;
        DirectX::SimpleMath::Vector3 hilt;     // 柄の端（モデル空間）
        DirectX::SimpleMath::Vector3 middle;   // 剣の真ん中
        float length = 1.0f;                   // 柄から先端まで（モデル空間）
        bool tried = false;
    };
    std::unordered_map<std::string, ModelInfo> m_Models;
    const ModelInfo* Find(const std::string& path);
};
