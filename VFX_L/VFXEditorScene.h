// ============================================================
// VFXEditorScene.h
// VFX 専用の編集シーン
// 戦闘や物理を含まず、パーティクル調整に集中する。
// 投射物追従の確認用に「移動する仮想投射物」を再現できる。
// ============================================================
#pragma once
#include "SceneBase.h"
#include "DebugCamera.h"
#include "CameraBase.h"
#include "GPUParticleSystem.h"
#include "VFXEffect.h"
#include "VFXEditor.h"
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

private:
    CameraBase m_Camera;

    // --- Particle / VFX ---
    GPUParticleSystem        m_ParticleSystem;
    VFXEffect                m_Effect;
    VFXContext               m_VFXContext;
    VFXEditor                m_Editor;
    std::shared_ptr<Texture> m_ParticleTexture;

    float m_TotalTime = 0.0f;

    // --- 統計（Flush でクリアされる前に退避）---
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;

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