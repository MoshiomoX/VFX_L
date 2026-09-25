#pragma once
#include "VFX_Editor/VFXEntry.h"
#include "VFX_Editor/VFXTextureRef.h"
#include "Particle/GPUParticle.h"
#include <memory>

class GPUParticleSystem;

// ============================================================
// Trail entry：特効の位置（+ offset）が引く帯。
//
//   Particle entry の Trail は粒子 1 個ずつが自分の動きで引くので、
//   止まっている粒子（特効と一緒に動くだけの芯など）には帯が出ない。
//   こちらは特効そのものの移動で引く。
//
//   点の追加（Min Distance ごと）・寿命切れ・帯への展開は全部 GPU
//   （GPUParticleSystem の EffectTrail）。CPU は毎フレーム先頭の位置を 1 つ渡すだけ。
//   止めた時・瞬間移動した時は今の帯を切り離し（その場で縮んで消える）、
//   次の位置から新しい帯を始める。
//
//   GPU の弾（SwarmVFXTable）にはまだ写さない（次の段階）
// ============================================================
class VFXTrailEntry : public VFXEntry
{
public:
    ~VFXTrailEntry() override;

    EntryType GetType() const override { return EntryType::Trail; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;
    std::unique_ptr<VFXEntry> Clone() const override;
    json ToJson() const override;
    void FromJson(const json& j) override;

    // 今フレームの先頭を渡す（VFXEffect::CollectAndDispatch から毎フレーム。再生中のみ効く）。
    // jumped = 前フレームの位置と繋がない（再生直後・瞬間移動）
    void Submit(const DirectX::SimpleMath::Vector3& worldOffset, bool jumped);

    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 };
    float              minDistance = 0.2f;   // 先頭がこれだけ動いたら点を 1 つ足す（m）
    ParticleTrailStyle style;                // lifetime = 1 点の寿命（秒）。inherit 系は使わない
    VFXTextureRef      tex;                  // 帯の貼图（json "tex"）。無ければ白

private:
    void SyncStyle(const VFXContext& ctx);   // style を登録 / 更新（Inspector の変更を反映）
    void ReleaseStyle();
    void DetachTrail();                      // 今の帯を手放す。帯はその場で縮んで消える

    GPUParticleSystem* m_Owner = nullptr;    // style と帯の登録先（解除用）
    int                m_StyleId = -1;
    int                m_TrailId = -1;
};
