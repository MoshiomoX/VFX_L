// ============================================================
// SwarmSystemRenderRings.cpp
// 地面の予告の輪（ボス / 自爆兵 / メテオ）、足元の丸影、HP バー。
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
// 地面の警告の輪（CPU から毎フレーム）
// ============================================================
void SwarmSystem::SetWarnCircles(const WarnCircle* circles, int count)
{
    m_WarnCircleCount = 0;
    if (!m_WarnCircleBuffer || count <= 0) return;
    count = (std::min)(count, kMaxWarnCircles);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_WarnCircleBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, circles, sizeof(WarnCircle) * count);
    m_Context->Unmap(m_WarnCircleBuffer.Get(), 0);
    m_WarnCircleCount = count;
}

// ============================================================
// CPU が置いた地面の警告の輪（Boss のスラム）。輪 1 つ = 地面に載せた 8x8 の格子 1 枚（頂点バッファ無し）
// ============================================================
void SwarmSystem::RenderWarnRings(CameraBase* camera)
{
    if (!warnRing.enabled || m_WarnCircleCount <= 0 || !m_WarnRingVS || !m_BomberRingPS || !m_HeightSRV) return;

    GroundMarkCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = warnRing.fill;
    cb.edge = warnRing.edge;
    cb.back = warnRing.back;
    cb.edgeWidth = warnRing.edgeWidth;
    cb.lift = warnRing.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_WarnRingVS->WriteBuffer(m_Context, 0, &cb);
    m_BomberRingPS->WriteBuffer(m_Context, 0, &cb);
    m_WarnRingVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);   // b1: 格子（高さ場を引く）
    m_WarnRingVS->SetSRV(m_Context, "circles", m_WarnCircleSRV.Get());
    m_WarnRingVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());

    m_WarnRingVS->Bind(m_Context);
    m_BomberRingPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    constexpr UINT kGrid = 8;   // SwarmWarnRingVS の WARN_GRID
    m_Context->DrawInstanced(kGrid * kGrid * 6, (UINT)m_WarnCircleCount, 0, 0);

    m_WarnRingVS->UnbindSRVs(m_Context);   // 高さ場は Compute が書くので外す
    rs.Restore(m_Context);
}

// ============================================================
// 雑魚の足元の丸い影
// 活きている雑魚 1 体につき 1 枚の地面の板（6 頂点、頂点バッファ無し）。数は HP バーと同じ間接引数。
// 四隅を高さ場に載せる（b1 の格子、b2 の centerAboveFeet、t3 の高さ場）。深度テストあり・書き込み無し、
// premultiplied の AlphaBlend（黒 x a = 地面を a だけ暗くする）
// ============================================================
void SwarmSystem::RenderBlobShadows(CameraBase* camera)
{
    if (!blobShadow.enabled || !m_BlobShadowVS || !m_BlobShadowPS || !m_HpBarArgs || !m_HeightSRV) return;

    BlobShadowCB cb = {};
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.radius = blobShadow.radius;
    cb.strength = blobShadow.strength;
    cb.lift = blobShadow.lift;
    cb.softness = blobShadow.softness;
    cb.clamp = blobShadow.clamp;
    m_BlobShadowVS->WriteBuffer(m_Context, 0, &cb);
    m_BlobShadowPS->WriteBuffer(m_Context, 0, &cb);
    m_BlobShadowVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);
    m_BlobShadowVS->WriteBuffer(m_Context, 2, &m_CachedAICB);
    m_BlobShadowVS->WriteBuffer(m_Context, 4, &m_CachedKindCB);   // 氷の上で持ち上げる（g_IceOn）
    if (m_IceMapSRV && m_PondTopSRV)
    {
        m_BlobShadowVS->SetSRV(m_Context, "g_IceMap", m_IceMapSRV.Get());
        m_BlobShadowVS->SetSRV(m_Context, "g_PondTop", m_PondTopSRV.Get());
    }

    m_BlobShadowVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "aliveList", m_AliveListSRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    m_BlobShadowVS->Bind(m_Context);
    m_BlobShadowPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstancedIndirect(m_HpBarArgs.Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_BlobShadowVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 点火した自爆兵の足元の警告の輪
// 自爆兵 1 体につき 1 枚の地面の板（6 頂点、頂点バッファ無し）。数は自爆兵の一覧から間接引数で。
// 点火していない・死んだ分は VS が捨てる。深度テストあり・書き込み無し、premultiplied の AlphaBlend
// ============================================================
void SwarmSystem::RenderBomberRings(CameraBase* camera)
{
    if (!bomberRing.enabled || !m_BomberRingVS || !m_BomberRingPS || !m_BomberRingArgs) return;

    GroundMarkCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = bomberRing.fill;
    cb.edge = bomberRing.edge;
    cb.back = bomberRing.back;
    cb.edgeWidth = bomberRing.edgeWidth;
    cb.lift = bomberRing.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_BomberRingVS->WriteBuffer(m_Context, 0, &cb);
    m_BomberRingPS->WriteBuffer(m_Context, 0, &cb);
    // b2: centerAboveFeet（体の中心から足元までの高さ）、b4: 爆発半径と導火線の長さ
    m_BomberRingVS->WriteBuffer(m_Context, 2, &m_CachedAICB);
    m_BomberRingVS->WriteBuffer(m_Context, 4, &m_CachedKindCB);

    m_BomberRingVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_BomberRingVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_BomberRingVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    m_BomberRingPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    // 自爆兵の一覧、次に突撃爆弾兵の一覧（VS が種類で半径と導火線の長さを選ぶ）
    const struct { uint32_t list; ID3D11Buffer* args; } passes[] = {
        { Swarm::kDrawListBomber, m_BomberRingArgs.Get() },
        { Swarm::kDrawListDashBomber, m_DashBomberRingArgs.Get() },
    };
    for (const auto& pass : passes)
    {
        if (!pass.args) continue;
        m_BomberRingVS->SetSRV(m_Context, "bomberList", m_KindListSRV[pass.list].Get());
        m_BomberRingVS->Bind(m_Context);
        m_Context->DrawInstancedIndirect(pass.args, 0);
    }

    // 次のフレームの Compute が UAV として使うので外す
    m_BomberRingVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 隕石（DROP の弾）が落ちる所の警告の輪
// 弾の全スロットを 1 枚ずつ（6 頂点、頂点バッファ無し）。DROP でない・死んだ分は VS が捨てる。
// 着弾点は生成時に決めた軌道の終点（paths の p3）、半径は弾の hitArea の定義から。
// 状態は自爆兵の輪と同じ（深度テストあり・書き込み無し、premultiplied の AlphaBlend）
// ============================================================
void SwarmSystem::RenderDropRings(CameraBase* camera)
{
    if (!dropRing.enabled || !m_DropRingVS || !m_BomberRingPS) return;

    GroundMarkCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = dropRing.fill;
    cb.edge = dropRing.edge;
    cb.back = dropRing.back;
    cb.edgeWidth = dropRing.edgeWidth;
    cb.lift = dropRing.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_DropRingVS->WriteBuffer(m_Context, 0, &cb);
    m_BomberRingPS->WriteBuffer(m_Context, 0, &cb);
    m_DropRingVS->WriteBuffer(m_Context, 2, &m_CachedAICB);   // b2: centerAboveFeet（着弾点から地面へ下ろす）

    m_DropRingVS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "paths", m_PathSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());

    m_DropRingVS->Bind(m_Context);
    m_BomberRingPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstanced(6, Swarm::kMaxProjectiles, 0, 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_DropRingVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 雑魚の HP バー
// 活きスロット 1 体につき 1 枚の板（6 頂点、頂点バッファ無し）。
// 数は雑魚の本描画と同じ aliveList から間接引数で決まる。
// 深度テストあり・書き込み無し: 手前の雑魚や壁に隠れるが、バー同士は上書きし合わない。
// 半透明（減った部分の背景）なので AlphaBlend
// ============================================================
void SwarmSystem::RenderHpBars(CameraBase* camera)
{
    if (!hpBar.enabled || !m_HpBarVS || !m_HpBarPS || !m_HpBarArgs || !m_EnemyMaxHpSRV) return;

    HpBarCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.width = hpBar.width;
    cb.height = hpBar.height;
    cb.offset = hpBar.offset;
    cb.border = hpBar.border;
    cb.fill = hpBar.fill;
    cb.back = hpBar.back;
    cb.edge = hpBar.edge;
    m_HpBarVS->WriteBuffer(m_Context, 0, &cb);
    m_HpBarVS->WriteBuffer(m_Context, 2, &m_CachedAICB);      // 足元の高さ（半径・カプセル）
    if (m_IceMapSRV && m_PondTopSRV)
    {
        m_HpBarVS->SetSRV(m_Context, "g_IceMap", m_IceMapSRV.Get());
        m_HpBarVS->SetSRV(m_Context, "g_PondTop", m_PondTopSRV.Get());
    }
    m_HpBarVS->WriteBuffer(m_Context, 4, &m_CachedKindCB);  // エリートの体格
    m_HpBarPS->WriteBuffer(m_Context, 0, &cb);

    m_HpBarVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_HpBarVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_HpBarVS->SetSRV(m_Context, "aliveList", m_AliveListSRV.Get());
    m_HpBarVS->SetSRV(m_Context, "enemyMaxHp", m_EnemyMaxHpSRV.Get());
    m_HpBarVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    m_HpBarVS->Bind(m_Context);
    m_HpBarPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstancedIndirect(m_HpBarArgs.Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_HpBarVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}
