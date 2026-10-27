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
    // duration 秒後に Stop する（loop のエフェクトでも止まる）。follow = 毎フレーム followPos へ動かす
    // 戻り値はインスタンスの番号（0 = 出せなかった）。SetInstance / StopInstance で後から動かせる
    uint32_t Play(const std::string& vfxFile, const DirectX::SimpleMath::Vector3& pos,
        float duration, bool follow, const VFXContext& ctx);
    // 同上 + 色を掛けて出す（粒子の開始 / 終了色と点光源。水晶玉の光球を貯蔵した魔法の色で染める。2026-10-06）
    uint32_t Play(const std::string& vfxFile, const DirectX::SimpleMath::Vector3& pos,
        float duration, bool follow, const VFXContext& ctx, const DirectX::SimpleMath::Vector3& tint);

    // 光線など、位置と終点を毎フレーム外から入れる物（follow は無視される）。番号が古ければ何もしない
    void SetInstance(uint32_t handle, const DirectX::SimpleMath::Vector3& pos, const DirectX::SimpleMath::Vector3& beamEnd);
    // 光線の太さの倍率（Beam entry の width に掛ける。拡大鏡の分。Play の直後に 1 回。2026-10-13）
    void SetBeamWidth(uint32_t handle, float widthMul);
    // 早めに止める（発射を止めて、出ている物は自然に消える）
    void StopInstance(uint32_t handle);
    // Y 軸回りに回す（Play の直後に 1 回。Boss の門の渦を門の向きに合わせる）
    void RotateInstance(uint32_t handle, float yawDeg);
    // TEMP-TEST（2026-10-27 突風の全体テスト）：再生中のインスタンス（無ければ null）
    VFXEffect* DebugEffect(uint32_t handle) { for (auto& inst : m_Active) if (inst.handle == handle) return inst.effect.get(); return nullptr; }
    // 全体を s 倍の大きさに（Play の直後に 1 回。拡大鏡で広がった竜巻・突風。2026-10-22）
    void ScaleInstance(uint32_t handle, float s);
    // 出現 / 消える途中の度合い 0〜1（VFXEffect::SetPresence。竜巻が足元から伸び上がる / ほどける。2026-10-22）
    void SetPresence(uint32_t handle, float k, bool leaving);

    // 時間軸を伸び縮みさせる（Play の直後、まだ Update していないインスタンスに使う）。
    // 各 entry の開始 / 終了の時刻 t を、t <= split なら t × before、それより後は
    // split × before + (t - split) × after に写す。光線の溜め（split = 溜め）を短く・光線を長くする時用
    // （魔力解放、2026-10-02）。持続が -1（無限）の entry は開始だけ写す
    void RemapTimeline(uint32_t handle, float split, float before, float after);

    void Update(float dt, const DirectX::SimpleMath::Vector3& followPos);

    // 一時停止中（Update を呼ばないフレーム）に毎フレーム呼ぶ（2026-10-25）。時間を進めず粒子も出さず、
    // 再生中のインスタンスの Mesh / Sprite / 光線 / 液溜まり / 点光源を今の状態のままもう一度積む
    // （各 Renderer と点光源の表は毎フレーム空になるので、呼ばないと止めている間それらだけ消える）。
    // Update と同じフレームには呼ばない（二重に積まれる）
    void SubmitStill();

    // 全インスタンスをその場で捨てる。粒子系が生きているうちに呼ぶ（シーンの Shutdown。entry の後始末が粒子系へ届く）。
    // 呼んだ後は粒子系に何も残らないので、このクラスの破棄が粒子系より後でも構わない
    void StopAll();
    size_t GetActiveCount() const { return m_Active.size(); }

    // json を編集し直した時用。次の Play で読み直す
    void ClearTemplates() { m_Templates.clear(); }
    // 雛形を先に読んでおく（Mesh entry の FBX もこの時に読む）。初回の Play で読むと Debug では
    // 1 フレーム飛んで、短いエフェクトが途中から始まる（突風の渦、2026-10-13）。シーンの Init で CPU の
    // エフェクトを全部通しておく
    void Preload(const std::string& vfxFile) { GetTemplate(vfxFile); }

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
