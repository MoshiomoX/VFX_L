// ============================================================
// BossAttacks.h
// 面の Boss の技（2026-10-03、ユーザーが選んだのは「スラムの警告の輪」）。Boss の体は GPU の雑魚と同じなので、
// 技の拍子と当たりは CPU が決める（GPU のリードバックの Boss の位置・HP を使う）:
//   Boss が生きていてプレイヤーが range m 以内なら interval 秒毎（HP が enrageHpRatio を切ったら intervalEnraged）に 1 回、
//   プレイヤーの足元へ spacing 秒おきに count 個の輪を置く（置いた瞬間のプレイヤーの位置。動いていれば外れる）。
//   輪は warnTime 秒で中の円が縁まで育ち、そこで爆発（Blast）。当たり・見た目・音はシーンが
//   （CollisionTestScene::UpdateBossAttacks。被弾の無敵・ノックバックは雑魚と同じ道）。
// 輪の見た目は SwarmSystem::SetWarnCircles（地面に沿う格子、色は SwarmSystem::warnRing）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>
#include <vector>

class GridWorld;

class BossAttacks
{
public:
    struct Ring
    {
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ
        float radius = 3.0f;
        float age = 0.0f;
    };
    struct Blast
    {
        DirectX::SimpleMath::Vector3 center;
        float radius = 3.0f;
        float damage = 0.0f;
    };

    // ---- 調整値（Enemies パネルの「Boss Attacks」）----
    bool  enabled = true;
    float firstDelay = 3.0f;        // 現れてから最初の技まで（秒）
    float interval = 6.0f;          // 技と技の間（秒。前の技を始めた時から）
    float intervalEnraged = 4.0f;   // HP が enrageHpRatio を切った後
    float enrageHpRatio = 0.5f;
    int   count = 3;                // 1 回の技の輪の数
    float spacing = 0.45f;          // 輪を置く間隔（秒）
    float warnTime = 1.2f;          // 輪が出てから爆発まで（秒）
    float radius = 3.0f;            // m
    float damage = 25.0f;           // 難度の倍率 1 の時。当たると × MobSpawner の今のダメージ倍率
    float range = 35.0f;            // Boss とプレイヤーがこれより離れていたら技を出さない（洞の奥から外のプレイヤーを叩かない）

    void Reset();
    // 毎フレーム（gameplay）。戻り値 = このフレームに新しく置いた輪の数（警告の音用）。爆発は outBlasts へ
    int Update(float dt, bool bossAlive, float bossHpRatio, const DirectX::SimpleMath::Vector3& bossPos,
        const DirectX::SimpleMath::Vector3& player, float damageMul, const GridWorld& grid, std::vector<Blast>& outBlasts);

    const std::vector<Ring>& Rings() const { return m_Rings; }
    float Progress(const Ring& r) const { return (warnTime > 0.0f) ? r.age / warnTime : 1.0f; }

    void QueueVolley() { m_DebugVolley = true; }   // パネルのボタン・自動テスト：次の Update で（距離・間隔を見ずに）1 回
    // 自動テストの記録用（累計）
    uint32_t volleys = 0, ringsPlaced = 0, blasts = 0;

    void DrawImGui();

private:
    std::vector<Ring> m_Rings;
    float m_Timer = 0.0f;           // 次の技まで
    int   m_Pending = 0;            // この技でまだ置いていない輪
    float m_PendingTimer = 0.0f;    // 次の輪まで
    bool  m_WasAlive = false;
    bool  m_DebugVolley = false;
};
