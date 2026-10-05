// ============================================================
// WeaponSystem.h
// 杖の自動発射。出力源（spells）ごとに独立して処理する。
//   分裂       = 同フレーム内で扇状に複数発（空間展開）
//   二重詠唱   = pendingCasts を残し、delayTimer 後に次を撃つ（時間展開）
// マナは杖で共有なので、出力源が多いと奪い合う。
// 弾は全部 GPU（SwarmSystem）に積む。CPU の Entity の弾は 2026-09-30 に削除した
// （GPU 化の後は到達しない経路だった）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "SpellID.h"
#include <string>
#include <vector>
#include <SimpleMath.h>
#include "Swarm/SwarmTypes.h"

class Registry;
class CollisionSystem;
struct SpellStats;
struct AreaStats;
class SwarmSystem;
class AreaVFXPlayer;
struct VFXContext;
class WeaponSystem
{
public:
    void Update(Registry& reg, float dt, const CollisionSystem& collision);
    void SetSwarm(SwarmSystem* swarm) { m_Swarm = swarm; }
    // 範囲攻撃の見た目の再生先。無ければ判定だけ出る
    void SetAreaVFX(AreaVFXPlayer* player, const VFXContext* ctx) { m_AreaVFX = player; m_AreaVFXCtx = ctx; }

    // ---- デバッグ可視化用：今フレームの照準情報 ----
    struct AimDebug
    {
        bool    hasTarget = false;
        bool    targetIsGpu = false;   // true = 雑魚（GPU）、false = エリート（CPU）
        DirectX::SimpleMath::Vector3 targetPos = { 0, 0, 0 };
        DirectX::SimpleMath::Vector3 muzzle = { 0, 0, 0 };
        DirectX::SimpleMath::Vector3 dir = { 0, 0, 1 };
        float   range = 0.0f;
    };
    const AimDebug& GetAimDebug() const { return m_AimDebug; }
    // 誘発の累計（ImGui・自動テスト用）: 届いた「基本魔法の弾が消えた」数 / それで撃った上級魔法の回数
    uint32_t GetTriggerEventsSeen() const { return m_TriggerEventsSeen; }
    uint32_t GetTriggeredCasts() const { return m_TriggeredCasts; }
    int GetActiveBeamCount() const { return (int)m_Beams.size(); }
    // 光線が標的へ向きを回す速さ（度/秒。2026-10-01 ユーザー指定 90）
    float beamTurnRate = 90.0f;
    // 自動テスト用: i 本目の光線の向き・標的が読めているか・標的の位置。無ければ false
    bool GetBeamDebug(int i, DirectX::SimpleMath::Vector3& dir, bool& hasTarget, DirectX::SimpleMath::Vector3& target) const
    {
        if (i < 0 || i >= (int)m_Beams.size()) return false;
        dir = m_Beams[i].dir; hasTarget = m_Beams[i].hasTarget; target = m_Beams[i].targetPos;
        return true;
    }

private:
    // View 走査中に GPU へ積めないので、発射要求を溜めてから積む
    struct CastRequest
    {
        ItemID  id;
        int     profile;   // ProjectileProfileDB の番号（飛び方・VFX はここから）
        DirectX::SimpleMath::Vector3 muzzle;   // 撃つ位置。atPos なら着弾点
        DirectX::SimpleMath::Vector3 dir;
        float speed, radius, damage, lifetime;
        uint32_t triggerTag = 0;   // この弾が消えたら誘発できる上級魔法（SpellStats::triggerMask）
        bool     atPos = false;    // 誘発の隕石: muzzle に落とす
        float    areaDamageMul = 1.0f;     // 命中・着弾で出す範囲の威力（SpellStats::areaDamageMul = 魔法威力）
        float    areaDurationMul = 1.0f;   // 同じく持続（魔力解放中に撃った弾。持続する範囲だけ伸びる）
    };

    // 1回の詠唱ぶんの発射要求を積む（分裂の扇状展開もここで行う）。
    // durationMul = 撃った時の魔力解放の持続倍率（ManaComponent::DurationMul）
    void QueueOneCast(const SpellStats& s,
        const DirectX::SimpleMath::Vector3& muzzle,
        const DirectX::SimpleMath::Vector3& dir, float durationMul);
    // 上級魔法 1 回分（誘発。impact に落とす。分裂は着弾点を輪に並べる）
    void QueueTriggeredCast(const SpellStats& s,
        const DirectX::SimpleMath::Vector3& impact,
        const DirectX::SimpleMath::Vector3& muzzle, float durationMul);
    std::vector<Swarm::TriggerEvent> m_TriggerEvents;   // 今フレームにリードバックで届いた誘発
    uint32_t m_TriggerEventsSeen = 0;
    uint32_t m_TriggeredCasts = 0;

    // ---- 光線（上級の範囲魔法、AreaProfile::Kind::Beam。2026-09-30）----
    // 誘発された時に始まり、溜め → 光線（GPU のカプセル型範囲 1 個 + Beam entry のエフェクト）→ 終了。
    // 起点は毎フレーム杖先、終点は向き × 射程を地形で切った所。GPU には SwarmSystem::SetBeam で毎フレーム渡す
    struct ActiveBeam
    {
        uint32_t channel = 0;        // Swarm::BeamCB の添字（同時に kMaxBeams 本まで）
        DirectX::SimpleMath::Vector3 dir = { 0, 0, 1 };
        float charge = 0.0f;         // 残りの溜め（この間は判定が無い）
        float timeLeft = 0.0f;       // 光線の残り秒
        float length = 18.0f;
        float radius = 0.6f;
        float halfHeight = 1.2f;
        float damage = 0.0f;         // 1 tick
        float tickInterval = 0.1f;
        uint32_t flags = 0;          // kAreaCapsule | channel | (stun)
        bool spawned = false;        // GPU の範囲を出したか（溜めが終わった時に 1 回）
        uint32_t vfxHandle = 0;      // AreaVFXPlayer のインスタンス
        std::string sound;           // 溜め終わり（撃った瞬間）の音 = 範囲の sound
        // 標的（2026-10-01）: 始めに seek（弾が消えた所）に一番近い敵を GPU で捕まえ、その敵へ向きを回す。
        // 死んだら射程内で向きに一番近い敵へ乗り換える（SwarmBeamTargetCS）。回し方は beamTurnRate 度/秒
        DirectX::SimpleMath::Vector3 seek = { 0, 0, 0 };
        uint32_t serial = 0;         // SwarmSystem::SetBeamTarget の通し番号
        bool targetStarted = false;  // 開始の依頼を出した（以後は追跡）
        bool hasTarget = false;      // 最後のフレームで標的が読めた（自動テストの記録用）
        DirectX::SimpleMath::Vector3 targetPos = { 0, 0, 0 };
    };
    std::vector<ActiveBeam> m_Beams;
    uint32_t m_BeamSerial = 0;
    // 誘発で光線を始める（チャンネルが空いていなければ false = 撃たない）
    // castSpeed / durationMul = 撃った時の魔力解放（溜めを castSpeed 倍速く、光線を durationMul 倍長く。エフェクトの時間軸も合わせる）
    bool StartBeam(const AreaStats& a, const DirectX::SimpleMath::Vector3& muzzle,
        const DirectX::SimpleMath::Vector3& impact, float castSpeed = 1.0f, float durationMul = 1.0f);
    // 溜め・判定の出現・起点 / 終点の更新・終了
    void UpdateBeams(float dt, const DirectX::SimpleMath::Vector3& muzzle, const CollisionSystem& collision);

    SwarmSystem* m_Swarm = nullptr;
    AreaVFXPlayer* m_AreaVFX = nullptr;
    const VFXContext* m_AreaVFXCtx = nullptr;
    std::vector<CastRequest> m_Requests;
    AimDebug m_AimDebug;   // 可視化用
};
