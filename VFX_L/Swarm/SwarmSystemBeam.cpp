// ============================================================
// SwarmSystemBeam.cpp
// 光線（カプセルの範囲）と光線の標的探し（SwarmBeamTargetCS）。
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

// 光線の起点 / 終点（次の固定ステップから AreaTickCS がカプセル型の範囲へ写す）
void SwarmSystem::SetBeam(uint32_t ch, const Vector3& start, const Vector3& end, float radius, bool active)
{
    if (ch >= Swarm::kMaxBeams) return;
    m_CachedBeamCB.start[ch] = { start.x, start.y, start.z, radius };
    m_CachedBeamCB.end[ch] = { end.x, end.y, end.z, active ? 1.0f : 0.0f };
}

// 光線の標的の依頼（次の Flush の DispatchBeamTargets で効く）
void SwarmSystem::SetBeamTarget(uint32_t ch, uint32_t cmd, const Vector3& origin, const Vector3& dir,
    float length, const Vector3& seek, uint32_t serial, const uint32_t* exclude)
{
    if (ch >= Swarm::kMaxBeams) return;
    m_CachedBeamTargetCB.origin[ch] = { origin.x, origin.y, origin.z, 0.0f };
    m_CachedBeamTargetCB.dir[ch] = { dir.x, dir.y, dir.z, length };
    m_CachedBeamTargetCB.seek[ch] = { seek.x, seek.y, seek.z, 0.0f };
    m_CachedBeamTargetCB.cmd[ch].cmd = cmd;
    m_CachedBeamTargetCB.cmd[ch].serial = serial;
    Swarm::BeamTargetExclude ex;
    if (exclude)
        for (int k = 0; k < 3; ++k) ex.slot[k] = exclude[k];
    m_CachedBeamTargetCB.exclude[ch] = ex;
}

bool SwarmSystem::GetBeamTarget(uint32_t ch, uint32_t serial, Vector3& pos) const
{
    if (ch >= Swarm::kMaxBeams) return false;
    const Swarm::BeamTarget& t = m_BeamTargets[ch];
    if (t.serial != serial || t.valid == 0) return false;
    pos = { t.pos[0], t.pos[1], t.pos[2] };
    return true;
}

int SwarmSystem::QueryBeamTarget(uint32_t ch, uint32_t serial, Vector3& pos, uint32_t& slot) const
{
    if (ch >= Swarm::kMaxBeams) return -1;
    const Swarm::BeamTarget& t = m_BeamTargets[ch];
    if (t.serial != serial) return -1;
    if (t.valid == 0) return 0;
    pos = { t.pos[0], t.pos[1], t.pos[2] };
    slot = t.slot;
    return 1;
}

// ============================================================
// 光線の標的を決める（1 チャンネル 1 グループ）→ staging へ写す。使っている光線が無ければ何もしない
// ============================================================
void SwarmSystem::DispatchBeamTargets()
{
    if (!m_BeamTargetCS || !m_BeamTargetBuffer) return;
    bool any = false;
    for (uint32_t ch = 0; ch < Swarm::kMaxBeams; ++ch)
    {
        any |= (m_CachedBeamTargetCB.cmd[ch].cmd != Swarm::kBeamTargetIdle);
    }
    if (!any) return;

    m_BeamTargetCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_BeamTargetCS->WriteBuffer(m_Context, 4, &m_CachedBeamTargetCB);
    m_BeamTargetCS->Bind(m_Context);
    m_BeamTargetCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_BeamTargetCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_BeamTargetCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 潜っている砂潜りは狙わない
    m_BeamTargetCS->SetUAV(m_Context, "beamTargets", m_BeamTargetUAV.Get());
    m_BeamTargetCS->BindUAVs(m_Context);
    m_Context->Dispatch(Swarm::kMaxBeams, 1, 1);
    m_BeamTargetCS->UnbindSRVs(m_Context);
    m_BeamTargetCS->UnbindUAVs(m_Context);

    m_BeamTargetStaging.Copy(m_Context, m_BeamTargetBuffer.Get());
}

// 一番古い staging を読む（待たない）。読めなければ前回の答えのまま
void SwarmSystem::ReadBeamTargets()
{
    // 読んだ 1 枚は未写しに戻す（同じ答えを 2 度読まない。光線が止まった後に古い物が残らない）
    m_BeamTargetStaging.TryRead(m_Context, [&](const void* data, bool) { memcpy(m_BeamTargets, data, sizeof(m_BeamTargets)); }, true);
}
