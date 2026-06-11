#pragma once
#include "SceneBase.h"
#include "CameraBase.h"
#include "GPUParticleSystem.h"
#include "VFXEffect.h"
#include "VFXEditor.h"
#include "SkinnedModel.h"
#include "SkinnedModelGPU.h"

#include "Model.h"
#include "Skybox.h"

class TestScene : public SceneBase
{
public:
    void Init() override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void DrawDebugUI();   // PBR 調節パネル

    void InspectModel(const std::string& filepath);


private:
    CameraBase m_Camera;

    GPUParticleSystem m_GPUParticleSystem;
    VFXEffect m_Effect;
    VFXContext m_Context;
    VFXEditor m_Editor;
	Skybox m_Skybox;
    std::shared_ptr<Texture> m_ParticleTexture;


    std::shared_ptr<ComputeShader> m_SkinningCS;
    std::shared_ptr<VertexShader>  m_SkinnedVS;
    std::shared_ptr<PixelShader>   m_SkinnedPS;
    Transform                      m_SkinnedTransform;

    std::shared_ptr<SkinnedModel> m_SkinnedModel;
    SkinnedModelGPU               m_SkinnedGPU;
    float m_TotalTime = 0.0f;

    // ===== PBR テスト：Rock_2 =====
    std::shared_ptr<Model> m_Model;
    Transform m_ModelTransform;

    // モデル調節パラメータ
    float m_ModelPos[3] = { 0.0f, 0.0f, 0.0f };
    float m_ModelRot[3] = { 0.0f, 0.0f, 0.0f };
    float m_ModelScale[3] = { 0.01f, 0.01f, 0.01f };

    // ライト調節パラメータ
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_LightColor[3] = { 1.0f, 1.0f, 1.0f };
    float m_LightIntensity = 1.0f;
    float m_AmbientColor[3] = { 0.2f, 0.2f, 0.2f };

    // ライト調節を Render の Renderer に反映するため保持
    Renderer* m_Renderer = nullptr;

	// アニメーション再生用
    float                               m_AnimTime = 0.0f;
    std::vector<DirectX::SimpleMath::Matrix> m_Palette;
};