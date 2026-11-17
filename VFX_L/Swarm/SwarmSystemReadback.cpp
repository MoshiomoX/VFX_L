// ============================================================
// SwarmSystemReadback.cpp
// staging への写しと CPU への読み戻し（連鎖イベント・分裂・ダメージ数字・ボス・被弾・範囲の誕生）、デバッグ用の全体読み戻し。
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Renderer/Renderer.h"   // DissolveCB
#include "Camera/CameraBase.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Light/PointLightManager.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "World/GridWorld.h"
#include "VFX_Editor/VFXSpriteRenderer.h"   // VFXSpriteCameraCB
#include "VFX_Editor/VFXLiquidRenderer.h"   // 液溜まりの CameraCB / FrameCB（CPU の経路と同じ並び）

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

// ============================================================
// counter の写しを発行（待たない）
// ============================================================
void SwarmSystem::RequestReadback()
{
    m_CounterStaging.Copy(m_Context, m_CounterBuffer.Get());

    // 誘発・敵の出来事・ダメージの数字の環も写す（読むのは 2 フレーム後の Flush。写した 1 枚は KillAll の後の物）
    m_TriggerStaging.Copy(m_Context, m_TriggerBuffer.Get());
    m_EnemyEventStaging.Copy(m_Context, m_EnemyEventBuffer.Get());
    m_DamageTextStaging.Copy(m_Context, m_DamageTextBuffer.Get());
    CopyLifestealForCPU();   // 吸血の累計
    CopyNearestTargetForCPU(); // 優先度なしの一番近い敵
}

namespace
{
    // 先頭 16B = 今までの総数、以降 T × N の環を読み、前に読んだ所からの新しい分を out へ足す。
    // 総数は GPU で永久に累積されるので、読めなかったフレームがあっても次に読めた時に取りこぼさない
    // （環の長さを超えて溜まった分だけ捨てる）。stale = KillAll の前の写し：読んだ位置だけ進めて捨てる
    template <class T, uint32_t N>
    void ReadEventRing(const void* data, bool stale, uint32_t& readPos, std::vector<T>& out)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        const uint32_t total = *reinterpret_cast<const uint32_t*>(bytes);
        const auto* ring = reinterpret_cast<const T*>(bytes + 16);

        uint32_t from = readPos;
        if (total - from > N) from = total - N;   // 溢れた分は捨てる
        if (stale) from = total;
        for (uint32_t n = from; n != total; ++n)
            out.push_back(ring[n % N]);
        readPos = total;
    }

    // 取り出し待ちが環の長さを超えたら古い物から捨てる
    template <class T>
    void TrimEvents(std::vector<T>& v, uint32_t maxCount)
    {
        if (v.size() > maxCount)
            v.erase(v.begin(), v.end() - maxCount);
    }
}

// ============================================================
// 誘発の環を読む（一番古い staging、待たない）
// ============================================================
void SwarmSystem::ReadTriggerEvents()
{
    if (!m_TriggerStaging.TryRead(m_Context, [&](const void* data, bool stale)
        { ReadEventRing<Swarm::TriggerEvent, Swarm::kMaxTriggerEvents>(data, stale, m_TriggerRead, m_TriggerEvents); }))
        return;
    // 誰も取り出さない（杖が無いシーン）でも溜まり続けないように
    TrimEvents(m_TriggerEvents, Swarm::kMaxTriggerEvents);
}

// 敵の出来事の環を読む（誘発の環と同じ作り）
void SwarmSystem::ReadEnemyEvents()
{
    if (!m_EnemyEventStaging.TryRead(m_Context, [&](const void* data, bool stale)
        { ReadEventRing<Swarm::EnemyEvent, Swarm::kMaxEnemyEvents>(data, stale, m_EnemyEventRead, m_EnemyEvents); }))
        return;
    TrimEvents(m_EnemyEvents, Swarm::kMaxEnemyEvents);
}

// ダメージの数字の環を読む（誘発の環と同じ作り）
void SwarmSystem::ReadDamageTexts()
{
    if (!m_DamageTextStaging.TryRead(m_Context, [&](const void* data, bool stale)
        { ReadEventRing<Swarm::DamageText, Swarm::kMaxDamageTexts>(data, stale, m_DamageTextRead, m_DamageTexts); }))
        return;
    TrimEvents(m_DamageTexts, Swarm::kMaxDamageTexts);
}

// ============================================================
// Boss の様子を読む（一番古い staging、待たない）。読めなければ前回の値のまま
// ============================================================
void SwarmSystem::ReadBossInfo()
{
    m_BossStaging.TryRead(m_Context, [&](const void* data, bool) { memcpy(&m_BossInfo, data, sizeof(Swarm::BossInfo)); });
}

// ============================================================
// プレイヤーが受けた打撃の向きを読む（一番古い staging、待たない）。
// 累計なので前回値との差分を足す（読めなかったフレームの分も次で拾える）。
// int の和は回り込んでも uint の引き算 → int で正しい差になる
// ============================================================
void SwarmSystem::ReadPlayerHits()
{
    Swarm::PlayerHitInfo now;
    if (!m_PlayerHitStaging.TryRead(m_Context, [&](const void* data, bool) { memcpy(&now, data, sizeof(now)); }))
        return;

    auto diff = [](int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); };
    const float s = 1.0f / Swarm::kHitDirScale;
    PlayerHits& p = m_PendingPlayerHits;
    p.meleeDir += Vector2((float)diff(now.meleeX, m_LastPlayerHits.meleeX), (float)diff(now.meleeZ, m_LastPlayerHits.meleeZ)) * s;
    p.blastDir += Vector2((float)diff(now.blastX, m_LastPlayerHits.blastX), (float)diff(now.blastZ, m_LastPlayerHits.blastZ)) * s;
    p.melee += now.meleeCount - m_LastPlayerHits.meleeCount;
    p.blasts += now.blastCount - m_LastPlayerHits.blastCount;
    p.spikeDir += Vector2((float)diff(now.spikeX, m_LastPlayerHits.spikeX), (float)diff(now.spikeZ, m_LastPlayerHits.spikeZ)) * s;   // トゲ玉
    p.spikes += now.spikeCount - m_LastPlayerHits.spikeCount;
    p.windDir += Vector2((float)diff(now.windX, m_LastPlayerHits.windX), (float)diff(now.windZ, m_LastPlayerHits.windZ)) * s;   // 砂旋
    p.winds += now.windCount - m_LastPlayerHits.windCount;
    m_LastPlayerHits = now;
}

SwarmSystem::PlayerHits SwarmSystem::ConsumePlayerHits()
{
    const PlayerHits h = m_PendingPlayerHits;
    m_PendingPlayerHits = {};
    return h;
}

// 生まれた範囲の累計を読み、差分を溜める（ReadPlayerHits と同じ輪転）
void SwarmSystem::ReadAreaBirths()
{
    std::array<uint32_t, kAreaBirthSlots> now = {};
    bool stale = false;
    if (!m_AreaBirthStaging.TryRead(m_Context, [&](const void* data, bool st)
        {
            memcpy(now.data(), data, sizeof(uint32_t) * kAreaBirthSlots);
            stale = st;
        }))
        return;
    // KillAll の前の写し：差分の基準だけ進めて、その間に生まれた範囲（音・揺れ）は捨てる
    if (!stale)
    {
        m_PendingShakes += now[0] - m_LastAreaBirths[0];
        for (uint32_t k = 0; k < Swarm::kAreaBirthKinds; ++k)
            m_PendingAreaBirths[k] += now[1 + k] - m_LastAreaBirths[1 + k];
    }
    m_LastAreaBirths = now;
}

uint32_t SwarmSystem::ConsumeShakeAreas()
{
    const uint32_t n = m_PendingShakes;
    m_PendingShakes = 0;
    return n;
}

void SwarmSystem::ConsumeAreaBirths(std::array<uint32_t, Swarm::kAreaBirthKinds>& out)
{
    for (uint32_t k = 0; k < Swarm::kAreaBirthKinds; ++k) out[k] += m_PendingAreaBirths[k];
    m_PendingAreaBirths = {};
}

// ============================================================
// プレイヤーの被弾を取り出す（取ったら 0 に戻す）
// ============================================================
float SwarmSystem::ConsumePlayerDamage()
{
    const float d = m_PendingPlayerDamage;
    m_PendingPlayerDamage = 0.0f;
    return d;
}
// ============================================================
// 取得した経験値を取り出す（取ったら 0 に戻す）
// ============================================================
float SwarmSystem::ConsumeExp()
{
    const float e = m_PendingExp;
    m_PendingExp = 0.0f;
    return e;
}

// ============================================================
// TEMP-TEST: 敵の池と状態を丸ごと読み戻す（staging を作って CopyResource → Map。GPU を待つので自動テストだけ）
// ============================================================
bool SwarmSystem::DebugReadEnemies(std::vector<Swarm::Enemy>& outEnemies, std::vector<uint32_t>& outStates,
    std::vector<Swarm::EnemyExtra>* outExtras, std::vector<DirectX::SimpleMath::Vector4>* outStatus)
{
    if (!m_Device || !m_Context || !m_EnemyBuffer || !m_EnemyStateBuffer) return false;

    auto readback = [&](ID3D11Buffer* src, UINT bytes, void* dst) -> bool
        {
            D3D11_BUFFER_DESC sd = {};
            sd.ByteWidth = bytes;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Buffer> staging;
            if (FAILED(m_Device->CreateBuffer(&sd, nullptr, &staging))) return false;
            m_Context->CopyResource(staging.Get(), src);
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (FAILED(m_Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
            memcpy(dst, m.pData, bytes);
            m_Context->Unmap(staging.Get(), 0);
            return true;
        };

    outEnemies.resize(Swarm::kMaxEnemies);
    outStates.resize(Swarm::kMaxEnemies);
    if (!readback(m_EnemyBuffer.Get(), (UINT)(sizeof(Swarm::Enemy) * Swarm::kMaxEnemies), outEnemies.data())) return false;
    if (!readback(m_EnemyStateBuffer.Get(), (UINT)(sizeof(uint32_t) * Swarm::kMaxEnemies), outStates.data())) return false;
    // 種類（並行バッファ）。無ければ空のまま（呼ぶ側は全部雑魚として扱う）
    if (outExtras && m_EnemyExtraBuffer)
    {
        outExtras->resize(Swarm::kMaxEnemies);
        if (!readback(m_EnemyExtraBuffer.Get(), (UINT)(sizeof(Swarm::EnemyExtra) * Swarm::kMaxEnemies), outExtras->data()))
            outExtras->clear();
    }
    // 減速・凍結（enemyStatus、float4）
    if (outStatus && m_EnemyStatusBuffer)
    {
        outStatus->resize(Swarm::kMaxEnemies);
        if (!readback(m_EnemyStatusBuffer.Get(), (UINT)(sizeof(float) * 4 * Swarm::kMaxEnemies), outStatus->data()))
            outStatus->clear();
    }
    return true;
}
