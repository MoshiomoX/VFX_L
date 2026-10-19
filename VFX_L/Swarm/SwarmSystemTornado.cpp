// ============================================================
// SwarmSystemTornado.cpp
// 砂嵐の術師の砂旋（kEnemyKindTornado）の見た目（2026-10-19 作り直し。ユーザー「風の感じが全く無い」）。
// 体 = PrimitiveBuilder::CreateTornadoShell の筒（上下が広く腰が少し細い）を SwarmTornadoVS / PS で 2 層描く：
// 外側の薄い幕 + 内側の細く速い芯。風の動きは PS が 2 枚の雲ノイズを螺旋に沿って上へ流して削る所から出る
// （80.lv「Creating a tornado in Unity」等の定石）。半透明（premultiplied）、深度は読むだけ、ステンシルは書かない
// （= 赤い陣営の縁取りは出ない）。足元の砂埃は SwarmTornadoEmitCS が TornadoDust の配方で毎フレーム出す。
// 読み込み（モデル・材質・シェーダ）は SwarmSystemKinds.cpp の LoadKindAssets
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Mesh/Mesh.h"
#include "Particle/GPUParticleSystem.h"

using namespace DirectX::SimpleMath;

// ============================================================
// 足元の砂埃（Flush の中、草の火の発射の直後。空きの予約 m_EmitBudget を共有）
// ============================================================
void SwarmSystem::DispatchTornadoEmit(float dt, float totalTime)
{
    m_TornadoTime = totalTime;   // 描画の流れ（スクロール）にも使う
    if (!m_TornadoEmitCS || !m_Particles || m_TornadoDustVfx == 0 || !tornadoLook.dust) return;
    if (!m_VFX.GetRecipeSRV() || !m_VFX.GetEmitterSRV()) return;

    struct GlobalCB { float dt; float total; uint32_t seed; int emitterCount; } gcb;
    gcb.dt = dt;
    gcb.total = totalTime;
    gcb.seed = (uint32_t)(totalTime * 1000.0f) ^ 0x3c6ef372u;
    gcb.emitterCount = 0;

    // SwarmTornadoEmitCS の b2 と同じ並び（32B）
    struct TornadoEmitCB
    {
        float footY, rateMul; uint32_t vfx; float swirl;
        float footRadius; uint32_t slotCount; float pad[2];
    } tcb;
    static_assert(sizeof(TornadoEmitCB) == 32, "TornadoEmitCB: HLSL 側と不一致");
    tcb.footY = -(m_CachedAICB.enemyRadius + m_CachedAICB.enemyCapsuleHalf);
    tcb.rateMul = tornadoLook.dustRate;
    tcb.vfx = m_TornadoDustVfx;
    tcb.swirl = tornadoLook.dustSwirl;
    tcb.footRadius = m_CachedAICB.enemyRadius * m_CachedBomberCB.tornadoScale;
    tcb.slotCount = Swarm::kMaxEnemies;
    tcb.pad[0] = tcb.pad[1] = 0.0f;

    m_TornadoEmitCS->WriteBuffer(m_Context, 0, &gcb);
    m_TornadoEmitCS->WriteBuffer(m_Context, 2, &tcb);
    m_TornadoEmitCS->Bind(m_Context);
    m_TornadoEmitCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_TornadoEmitCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_TornadoEmitCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_TornadoEmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
    m_TornadoEmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
    m_TornadoEmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
    m_TornadoEmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
    m_TornadoEmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
    m_TornadoEmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
    m_TornadoEmitCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_TornadoEmitCS->UnbindSRVs(m_Context);
    m_TornadoEmitCS->UnbindUAVs(m_Context);
}

// ============================================================
// 描画（RenderOverlay：警告の輪の後・HP バーの前。半透明なので不透明物の後）
// 外側の幕 → 内側の芯の 2 回。数は砂旋の一覧（m_TornadoListUAV）から間接引数で
// ============================================================
void SwarmSystem::RenderTornadoes(const Matrix& view, const Matrix& proj)
{
    if (!tornadoLook.enabled || !m_TornadoMaterial || !m_TornadoModel || m_TornadoDrawArgs.empty()) return;

    m_TornadoMaterial->Bind(m_Context);   // VS + PS、t0 / t1 = 2 枚の雲ノイズ
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    EnemyRenderCB cb;
    cb.view = view;
    cb.proj = proj;
    m_TornadoVS->WriteBuffer(m_Context, 0, &cb);
    m_TornadoVS->WriteBuffer(m_Context, 2, &m_CachedAICB);      // 体の半径（足元の高さ）
    m_TornadoVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB);  // 体格・飛ぶ秒

    m_TornadoVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_TornadoVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_TornadoVS->SetSRV(m_Context, "tornadoList", m_TornadoListSRV.Get());
    m_TornadoVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);   // premultiplied
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);               // ステンシル無し = 縁取り無し
    m_Context->RSSetState(rs.CullNone());                                    // 薄い幕なので裏も描く

    const float hitDiameter = 2.0f * m_CachedAICB.enemyRadius * m_CachedBomberCB.tornadoScale;
    const auto& subs = m_TornadoModel->GetSubMeshes();
    for (int layer = 0; layer < 2; ++layer)
    {
        PropMeshCB p = {};
        p.anchor = Vector3::Zero;
        p.scale = tornadoLook.height;
        p.hold = Vector3(hitDiameter * tornadoLook.widthMul, (float)layer, tornadoLook.heave);
        p.yaw = m_TornadoTime;
        p.tint = tornadoLook.tint;
        p.bob = tornadoLook.spin;
        p.alpha = tornadoLook.growTime;
        m_TornadoVS->WriteBuffer(m_Context, 4, &p);
        for (size_t i = 0; i < subs.size() && i < m_TornadoDrawArgs.size(); ++i)
            if (subs[i].mesh && m_TornadoDrawArgs[i])
                subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_TornadoDrawArgs[i].Get(), 0);
    }

    m_TornadoVS->UnbindSRVs(m_Context);   // 次のフレームの Compute が UAV として使うので外す
    rs.Restore(m_Context);
}
