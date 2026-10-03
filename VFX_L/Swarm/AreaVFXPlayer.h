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
    // 戻り値は実例の番号（0 = 出せなかった）。SetInstance / StopInstance で後から動かせる
    uint32_t Play(const std::string& vfxFile, const DirectX::SimpleMath::Vector3& pos,
        float duration, bool follow, const VFXContext& ctx);

    // 光線など、位置と終点を毎フレーム外から入れる物（follow は無視される）。番号が古ければ何もしない
    void SetInstance(uint32_t handle, const DirectX::SimpleMath::Vector3& pos, const DirectX::SimpleMath::Vector3& beamEnd);
    // 早めに止める（発射を止めて、出ている物は自然に消える）
    void StopInstance(uint32_t handle);
    // Y 軸回りに回す（Play の直後に 1 回。Boss の門の渦を門の向きに合わせる）
    void RotateInstance(uint32_t handle, float yawDeg);

    // 時間軸を伸び縮みさせる（Play の直後、まだ Update していない実例に使う）。
    // 各 entry の開始 / 終了の時刻 t を、t <= split なら t × before、それより後は
    // split × before + (t - split) × after に写す。光線の溜め（split = 溜め）を短く・光線を長くする時用
    // （魔力解放、2026-10-02）。持続が -1（無限）の entry は開始だけ写す
    void RemapTimeline(uint32_t handle, float split, float before, float after);

    void Update(float dt, const DirectX::SimpleMath::Vector3& followPos);

    void StopAll();
    size_t GetActiveCount() const { return m_Active.size(); }

    // json を編集し直した時用。次の Play で読み直す
    void ClearTemplates() { m_Templates.clear(); }

private:
    struct Instance
    {
        std::unique_ptr<VFXEffect> effect;
        uint32_t handle = 0;
        float timeLeft = 0.0f;
        bool  follow = false;
        bool  stopped = false;   // Stop を送った後（消えるのを待っている）
    };

    std::shared_ptr<VFXEffect> GetTemplate(const std::string& vfxFile);

    std::vector<std::pair<std::string, std::shared_ptr<VFXEffect>>> m_Templates;
    std::vector<Instance> m_Active;
    uint32_t m_NextHandle = 1;
};
