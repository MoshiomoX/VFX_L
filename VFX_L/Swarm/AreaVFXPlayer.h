// ============================================================
// AreaVFXPlayer.h
// CPU から出した範囲攻撃の見た目を再生する。
//
// 範囲の判定は GPU（SwarmSystem）だが、CPU から出した範囲は位置を CPU が
// 知っているので、見た目は普通の VFXEffect を再生する。Mesh entry（地面の法環）も
// 粒子も使える。弾の命中で GPU が出した範囲はここを通らない（GPU 側で粒子だけ出る）。
//
// ※GPUParticleSystem::Flush より前に Update すること（emitter を積むため）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class VFXEffect;
struct VFXContext;

class AreaVFXPlayer
{
public:
    // VFXEffect が不完全型のままこのヘッダを使えるように、cpp 側で定義する
    AreaVFXPlayer();
    ~AreaVFXPlayer();

    // vfxFile: Assets/Data/VFXData/ の json のファイル名。空や読めない時は何もしない。
    // duration 秒後に Stop する（loop の特効でも止まる）。follow = 毎フレーム followPos へ動かす
    void Play(const std::string& vfxFile, const DirectX::SimpleMath::Vector3& pos,
        float duration, bool follow, const VFXContext& ctx);

    void Update(float dt, const DirectX::SimpleMath::Vector3& followPos);

    void StopAll();
    size_t GetActiveCount() const { return m_Active.size(); }

    // json を編集し直した時用。次の Play で読み直す
    void ClearTemplates() { m_Templates.clear(); }

private:
    struct Instance
    {
        std::unique_ptr<VFXEffect> effect;
        float timeLeft = 0.0f;
        bool  follow = false;
        bool  stopped = false;   // Stop を送った後（消えるのを待っている）
    };

    std::shared_ptr<VFXEffect> GetTemplate(const std::string& vfxFile);

    std::vector<std::pair<std::string, std::shared_ptr<VFXEffect>>> m_Templates;
    std::vector<Instance> m_Active;
};
