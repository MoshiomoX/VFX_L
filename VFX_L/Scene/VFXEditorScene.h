// ============================================================
// VFXEditorScene.h
// VFX 専用の編集シーン
// 戦闘や物理を含まず、パーティクル調整に集中する。
// 投射物追従の確認用に「移動する仮想投射物」を再現できる。
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/DebugCamera.h"
#include "Camera/CameraBase.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Transform.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXEditor.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/SkinnedModelGPU.h"
#include "Graphics/Shader/ComputeShader.h"
#include <memory>

class VFXEditorScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void DrawSceneUI();
    void UpdateFakeProjectile(float dt);   // 投射物追従テスト
    void SubmitBurst();      // 一括発射の emitter を積む
    void DrawStressUI();     // 負荷テストのパネル

private:

    // ============================================================
    // 一括発射テスト
    // 
    // プール（10万）を1フレームで埋めきる。
    //   dead list の空き数を超えて要求した時に何が起きるかを
    //   実際に目で確認するためのもの。
    //   EmitCS 側の護欄が効いていれば、空き数までしか出ない。
    // ============================================================
    bool   m_BurstPending = false;   // 次の Flush で1回だけ撃つ
    bool   m_BurstLoop = false;   // 毎フレーム撃ち続ける（枯渇維持）
    int    m_BurstCount = 100000;
    float  m_BurstSpeed[2] = { 2.0f, 14.0f };    // min / max
    float  m_BurstLife[2] = { 2.0f,  6.0f };
    float  m_BurstSize[2] = { 0.20f, 0.02f };   // start / end
    float  m_BurstOrigin[3] = { 0.0f, 1.0f, 0.0f };
    float  m_BurstGravity = -1.5f;
    float  m_BurstDrag = 0.4f;
    float m_LightIntensity = 1.0f;


    size_t m_LastBurstRequest = 0;    // 直前に要求した発射数

    // ---- Flush の CPU 時間（GPU を待っていないかの指標）----
    double m_FlushMs = 0.0;
    double m_FlushMsAvg = 0.0;
    double m_FlushMsPeak = 0.0;
    CameraBase m_Camera;

    // --- Particle / VFX ---
    GPUParticleSystem        m_ParticleSystem;
    VFXEffect                m_Effect;
    VFXContext               m_VFXContext;
    VFXEditor                m_Editor;
    VFXMeshRenderer          m_MeshRenderer;
    std::shared_ptr<Texture> m_ParticleTexture;

    float m_TotalTime = 0.0f;

    // --- 統計（Flush でクリアされる前に退避）---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;

    // ---- 参照用モデル（VFX の大きさ・光り方を見比べる）----
       // ---- 参照用モデル（骨付き。SkinningCS → 材質付きで描く）----
    std::shared_ptr<SkinnedModel>  m_SkinnedModel;
    SkinnedModelGPU                m_SkinnedGPU;
    std::shared_ptr<ComputeShader> m_SkinningCS;
    float m_AnimTime = 0.0f;
    bool  m_AnimPlay = true;
    float m_AnimSpeed = 1.0f;
    Transform m_ModelTransform;
    Matrix    m_ModelWorld;                 // 毎フレーム更新。Mesh 発射の followWorld が指す
    // 参照モデルの submesh を全部 Mesh 発射源として登録した物。
    // VFXContext::refSources が指すので、シーンより先に消えない
    std::vector<VFXRefEmitSource> m_RefSources;
    void RegisterRefSources();              // Init で 1 回
    void UseRefModelAsSource(int submesh);  // 選択中の Particle entry の源にする
    bool  m_ShowModel = true;
    float m_ModelPos[3] = { 0.0f, 0.0f, 0.0f };
    float m_ModelRot[3] = { 0.0f, 0.0f, 0.0f };
    float m_ModelScale[3] = { 0.01f, 0.01f, 0.01f };

    void SetupPBRMaterials();

    // --- 投射物追従テスト ---
    bool    m_FakeProjectileOn = false;   // 仮想投射物を動かすか
    Vector3 m_FakePos = { 0.0f, 1.0f, 0.0f };
    Vector3 m_FakeStart = { -8.0f, 1.0f, 0.0f };
    Vector3 m_FakeDir = { 1.0f, 0.0f, 0.0f };
    float   m_FakeSpeed = 12.0f;
    float   m_FakeRange = 16.0f;   // これだけ進んだら最初へ戻る
    float   m_FakeTravel = 0.0f;
    bool    m_ShowFakeMarker = true;

    // --- 表示調整 ---
    float m_ManualOffset[3] = { 0.0f, 0.0f, 0.0f };   // 手動オフセット（追従OFF時）
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
    float m_AmbientColor[3] = { 0.2f, 0.2f, 0.2f };
};