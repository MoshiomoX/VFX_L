#pragma once
#include "VFX_Editor/VFXEntry.h"
#include "Particle/GPUParticleEmitter.h"
#include "VFX_Editor/VFXTextureRef.h"
#include <string>
#include <memory>

class Model;
class GPUParticleSystem;

class VFXParticleEntry : public VFXEntry
{
public:
    ~VFXParticleEntry() override;

    EntryType GetType() const override { return EntryType::Particle; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;

    std::unique_ptr<VFXEntry> Clone() const override
    {
        auto copy = std::make_unique<VFXParticleEntry>();
        copy->startTime = startTime;
        copy->duration = duration;
        copy->isPlaying = false;
        copy->emitterData = emitterData;
        copy->sourceModelPath = sourceModelPath;
        // 帯の設定は写すが、style の登録は実例ごと（複製先が自分で登録する）
        copy->trailEnabled = trailEnabled;
        copy->trail = trail;
        copy->trailTex = trailTex;
        // 発射源の登録は実例ごと。複製先は OnPlay で自分の分を登録する
        copy->emitterData.shape.sourceId = -1;
        copy->emitterData.shape.sourceCount = 0;
        return copy;
    }
	json ToJson() const override;
	void FromJson(const json& j) override;

    GPUParticleEmitter emitterData;
    int runtimeID = -1;

    // ============================================
    // 軌跡（帯）。この entry から出た粒子が 1 個ずつ帯を引く。
    // 位置の記録も描画も GPU 上（GPUParticleSystem の Trail）。
    // ここが持つのは見た目の設定と、登録した style の id だけ
    // ============================================
    bool               trailEnabled = false;
    ParticleTrailStyle trail;
    VFXTextureRef      trailTex;          // 帯の貼图（json "trail.tex"）。無ければ白
    // GPUEmitter::trailStyle に入れる値（0 = 帯なし）
    int GetTrailSlot() const { return (trailEnabled && m_TrailStyleId >= 0) ? m_TrailStyleId + 1 : 0; }

    // ============================================
    // Mesh 発射源（Shape = Mesh の時だけ意味を持つ）
    //   sourceModelPath : json "source"。静的モデルを OnPlay で登録する
    //   externalSource  : シーンが実行時に差し込んだ源（参照モデルの骨格等）。
    //                     OnPlay で読み直さず、OnStop でも解除しない（所有はシーン）
    //   followWorld     : 非所有。非 null なら emitter.world にこれを使い、
    //                     effect の worldOffset は足さない（源の世界行列に従う）
    // ============================================
    std::string sourceModelPath;
    bool        externalSource = false;
    const DirectX::SimpleMath::Matrix* followWorld = nullptr;

    void SetExternalSource(int sourceId, int vertexCount, const DirectX::SimpleMath::Matrix* world);
    void ClearExternalSource();

    // Inspector 用。OnImGui は ctx を受け取らないので、描く前に VFXEditor が差し込む
    // （参照モデルの発射源を「Source」の候補に出すため）
    const VFXContext* inspectorCtx = nullptr;

private:
    // 自分で登録した静的モデルの源を（再）登録 / 解除する
    void RegisterFileSource(const VFXContext& ctx);
    void UnregisterFileSource();

    // 帯の style を登録 / 更新 / 解除する。再生中は毎フレーム呼ぶ（Inspector の変更を反映）
    void SyncTrailStyle(const VFXContext& ctx);
    void ReleaseTrailStyle();
    GPUParticleSystem*     m_TrailOwner = nullptr;    // 登録先（解除用）
    int                    m_TrailStyleId = -1;

    std::shared_ptr<Model> m_SourceModel;
    GPUParticleSystem*     m_SourceOwner = nullptr;   // 登録先（解除用）
    int                    m_SourceId = -1;           // 自分で登録した id（external では -1）
    bool                   m_SourceDirty = false;     // Inspector で path が変わった
};