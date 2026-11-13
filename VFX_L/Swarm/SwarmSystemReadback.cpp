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
    m_Readback.RequestCopy(m_Context, m_CounterBuffer.Get());

    // 誘発の環も写す（読むのは 2 フレーム後の Flush）
    // stale = false：KillAll の後に写した物（中身は読む）
    if (m_TriggerBuffer)
    {
        m_Context->CopyResource(m_TriggerStaging[m_TriggerStagingWrite].Get(), m_TriggerBuffer.Get());
        m_TriggerStagingFilled[m_TriggerStagingWrite] = true;
        m_TriggerStagingStale[m_TriggerStagingWrite] = false;
        m_TriggerStagingWrite = (m_TriggerStagingWrite + 1) % kTriggerStaging;
    }
    // 分裂の環も
    if (m_SplitBuffer)
    {
        m_Context->CopyResource(m_SplitStaging[m_SplitStagingWrite].Get(), m_SplitBuffer.Get());
        m_SplitStagingFilled[m_SplitStagingWrite] = true;
        m_SplitStagingStale[m_SplitStagingWrite] = false;
        m_SplitStagingWrite = (m_SplitStagingWrite + 1) % kTriggerStaging;
    }
    // ダメージの数字の環も
    if (m_DamageTextBuffer)
    {
        m_Context->CopyResource(m_DamageTextStaging[m_DamageTextStagingWrite].Get(), m_DamageTextBuffer.Get());
        m_DamageTextStagingFilled[m_DamageTextStagingWrite] = true;
        m_DamageTextStagingStale[m_DamageTextStagingWrite] = false;
        m_DamageTextStagingWrite = (m_DamageTextStagingWrite + 1) % kTriggerStaging;
    }
    CopyLifestealForCPU();   // 吸血の累計
    CopyCloseTargetForCPU(); // 優先度なしの一番近い敵
}

// ============================================================
// 誘発の環を読む（一番古い staging、待たない）。総数は GPU で永久に累積されるので、
// 読めなかったフレームがあっても次に読めた時に取りこぼさない（環の長さを超えて溜まった分だけ捨てる）
// ============================================================
void SwarmSystem::ReadTriggerEvents()
{
    const int readIndex = m_TriggerStagingWrite;   // 次に書く = 一番古い
    if (!m_TriggerStagingFilled[readIndex] || !m_TriggerStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_TriggerStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;

    const auto* bytes = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t total = *reinterpret_cast<const uint32_t*>(bytes);
    const auto* ring = reinterpret_cast<const Swarm::TriggerEvent*>(bytes + 16);

    uint32_t from = m_TriggerRead;
    if (total - from > Swarm::kMaxTriggerEvents) from = total - Swarm::kMaxTriggerEvents;   // 溢れた分は捨てる
    if (m_TriggerStagingStale[readIndex]) from = total;   // KillAll の前の写し：読んだ所だけ進めて捨てる
    m_TriggerStagingStale[readIndex] = false;
    for (uint32_t n = from; n != total; ++n)
        m_TriggerEvents.push_back(ring[n % Swarm::kMaxTriggerEvents]);
    m_TriggerRead = total;

    m_Context->Unmap(m_TriggerStaging[readIndex].Get(), 0);

    // 誰も取り出さない（杖が無いシーン）でも溜まり続けないように
    if (m_TriggerEvents.size() > Swarm::kMaxTriggerEvents)
        m_TriggerEvents.erase(m_TriggerEvents.begin(), m_TriggerEvents.end() - Swarm::kMaxTriggerEvents);
}

// ============================================================
// 分裂の環を読む（誘発の環と同じ作り。一番古い staging、待たない）
// ============================================================
void SwarmSystem::ReadSplitEvents()
{
    const int readIndex = m_SplitStagingWrite;   // 次に書く = 一番古い
    if (!m_SplitStagingFilled[readIndex] || !m_SplitStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_SplitStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;

    const auto* bytes = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t total = *reinterpret_cast<const uint32_t*>(bytes);
    const auto* ring = reinterpret_cast<const Swarm::SplitEvent*>(bytes + 16);

    uint32_t from = m_SplitRead;
    if (total - from > Swarm::kMaxSplitEvents) from = total - Swarm::kMaxSplitEvents;   // 溢れた分は捨てる
    if (m_SplitStagingStale[readIndex]) from = total;   // KillAll の前の写し（ReadTriggerEvents と同じ）
    m_SplitStagingStale[readIndex] = false;
    for (uint32_t n = from; n != total; ++n)
        m_SplitEvents.push_back(ring[n % Swarm::kMaxSplitEvents]);
    m_SplitRead = total;

    m_Context->Unmap(m_SplitStaging[readIndex].Get(), 0);

    if (m_SplitEvents.size() > Swarm::kMaxSplitEvents)
        m_SplitEvents.erase(m_SplitEvents.begin(), m_SplitEvents.end() - Swarm::kMaxSplitEvents);
}

void SwarmSystem::ReadDamageTexts()
{
    const int readIndex = m_DamageTextStagingWrite;   // 次に書く = 一番古い
    if (!m_DamageTextStagingFilled[readIndex] || !m_DamageTextStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_DamageTextStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;

    const auto* bytes = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t total = *reinterpret_cast<const uint32_t*>(bytes);
    const auto* ring = reinterpret_cast<const Swarm::DamageText*>(bytes + 16);

    uint32_t from = m_DamageTextRead;
    if (total - from > Swarm::kMaxDamageTexts) from = total - Swarm::kMaxDamageTexts;   // 溢れた分は捨てる
    if (m_DamageTextStagingStale[readIndex]) from = total;   // KillAll の前の写し（ReadTriggerEvents と同じ）
    m_DamageTextStagingStale[readIndex] = false;
    for (uint32_t n = from; n != total; ++n)
        m_DamageTexts.push_back(ring[n % Swarm::kMaxDamageTexts]);
    m_DamageTextRead = total;

    m_Context->Unmap(m_DamageTextStaging[readIndex].Get(), 0);

    if (m_DamageTexts.size() > Swarm::kMaxDamageTexts)
        m_DamageTexts.erase(m_DamageTexts.begin(), m_DamageTexts.end() - Swarm::kMaxDamageTexts);
}

// ============================================================
// Boss の様子を読む（GPUReadback::TryRead と同じ。一番古い staging、待たない）
// ============================================================
void SwarmSystem::ReadBossInfo()
{
    const int readIndex = m_BossStagingWrite;   // 次に書く = 一番古い
    if (!m_BossStagingFilled[readIndex] || !m_BossStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_BossStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;   // まだ。前回の値のまま
    memcpy(&m_BossInfo, mapped.pData, sizeof(Swarm::BossInfo));
    m_Context->Unmap(m_BossStaging[readIndex].Get(), 0);
}

// ============================================================
// プレイヤーが受けた打撃の向きを読む（一番古い staging、待たない）。
// 累計なので前回値との差分を足す（読めなかったフレームの分も次で拾える）。
// int の和は回り込んでも uint の引き算 → int で正しい差になる
// ============================================================
void SwarmSystem::ReadPlayerHits()
{
    const int readIndex = m_PlayerHitStagingWrite;   // 次に書く = 一番古い
    if (!m_PlayerHitStagingFilled[readIndex] || !m_PlayerHitStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_PlayerHitStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    Swarm::PlayerHitInfo now;
    memcpy(&now, mapped.pData, sizeof(now));
    m_Context->Unmap(m_PlayerHitStaging[readIndex].Get(), 0);

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
    const int readIndex = m_AreaBirthStagingWrite;   // 次に書く = 一番古い
    if (!m_AreaBirthStagingFilled[readIndex] || !m_AreaBirthStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_AreaBirthStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    std::array<uint32_t, kAreaBirthSlots> now;
    memcpy(now.data(), mapped.pData, sizeof(uint32_t) * kAreaBirthSlots);
    m_Context->Unmap(m_AreaBirthStaging[readIndex].Get(), 0);
    // KillAll の前の写し：差分の基準だけ進めて、その間に生まれた範囲（音・揺れ）は捨てる
    if (!m_AreaBirthStagingStale[readIndex])
    {
        m_PendingShakes += now[0] - m_LastAreaBirths[0];
        for (uint32_t k = 0; k < Swarm::kAreaBirthKinds; ++k)
            m_PendingAreaBirths[k] += now[1 + k] - m_LastAreaBirths[1 + k];
    }
    m_AreaBirthStagingStale[readIndex] = false;
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
    // 減速・凍結（enemySlow、float4）
    if (outStatus && m_EnemySlowBuffer)
    {
        outStatus->resize(Swarm::kMaxEnemies);
        if (!readback(m_EnemySlowBuffer.Get(), (UINT)(sizeof(float) * 4 * Swarm::kMaxEnemies), outStatus->data()))
            outStatus->clear();
    }
    return true;
}
