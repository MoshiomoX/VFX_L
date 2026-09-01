#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Particle/GPUParticleSystem.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXEditor.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/SkinnedModelGPU.h"

#include "Graphics/Model/Model.h"
#include "Graphics/Renderer/Skybox.h"

class TestScene : public SceneBase
{
public:
    void Init() override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void DrawDebugUI();                     // PBR調整パネル
    void SetupPBRMaterials();               // PBRマテリアル設定
    void LoadSkinnedModel();                // スキニングモデル読み込み
    void RenderSkinnedModel(Renderer& renderer);  // スキニングモデル描画

    void InspectModel(const std::string& filepath);

private:
    CameraBase m_Camera;

    // Particle & VFX
    GPUParticleSystem     m_GPUParticleSystem;
    VFXEffect             m_Effect;
    VFXContext            m_Context;
    VFXEditor             m_Editor;
    std::shared_ptr<Texture> m_ParticleTexture;

    // Skybox
    Skybox m_Skybox;

    // PBR Model (Shadowkin)
    std::shared_ptr<Model> m_Model;
    Transform              m_ModelTransform;

    // Skinned Model
    std::shared_ptr<SkinnedModel> m_SkinnedModel;
    SkinnedModelGPU               m_SkinnedGPU;

    std::shared_ptr<ComputeShader> m_SkinningCS;
    std::shared_ptr<VertexShader>  m_SkinnedVS;
    std::shared_ptr<PixelShader>   m_SkinnedPS;

    Transform m_SkinnedTransform;

    // アニメーション
    float m_AnimTime = 0.0f;
    float m_TotalTime = 0.0f;

    // ライト・モデル調整パラメータ
    Renderer* m_Renderer = nullptr;

    float m_ModelPos[3] = { 0.0f, 0.0f, 0.0f };
    float m_ModelRot[3] = { 0.0f, 0.0f, 0.0f };
    float m_ModelScale[3] = { 0.01f, 0.01f, 0.01f };

    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.2f, 0.2f, 0.2f };
};