// ============================================================
// SwarmSystemRender.cpp
// 描画の本体（RenderOpaque / RenderOverlay）と死骸・液面・Sprite の描画。
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Renderer/Renderer.h"   // DissolveCB / MVPBuffer
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
// 砕け散る部品を描く。雑魚と同じ網・同じ PS・同じテクスチャの切り替え（自爆兵は自分のテクスチャ、幽霊は半透明）。
// VS だけ SwarmCorpseVS に差し替え、submesh（部品）毎に CorpseCB.part を入れて間接描画
// ============================================================
void SwarmSystem::RenderCorpses(const Matrix& view, const Matrix& proj)
{
    if (!corpse.enabled || !m_CorpseVS || !m_EnemyMaterial || !m_EnemyModel || m_CorpseDrawArgs[0].empty()) return;

    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    MVPBuffer cb;   // World は単位行列のまま
    cb.View = view;
    cb.Projection = proj;
    // Material::Bind は VS も入れ直すので、その後で砕け散りの VS に差し替える
    auto bindVS = [&]()
        {
            m_EnemyMaterial->Bind(m_Context);
            m_Context->PSSetSamplers(0, 1, &samp);
            m_CorpseVS->Bind(m_Context);
            m_CorpseVS->WriteBuffer(m_Context, 0, &cb);
            m_CorpseVS->WriteBuffer(m_Context, 2, &m_CachedAICB);     // 体の大きさ
            m_CorpseVS->WriteBuffer(m_Context, 5, &m_CachedKindCB); // エリート・Boss の倍率、幽霊の見た目
            m_CorpseVS->SetSRV(m_Context, "corpses", m_CorpseSRV.Get());
        };
    bindVS();

    CorpseCB ccb;
    ccb.time = m_AnimClock;
    ccb.life = corpse.life;
    ccb.gravity = corpse.gravity;
    ccb.fling = corpse.fling;
    ccb.up = corpse.up;
    ccb.spin = corpse.spin;
    ccb.bounce = corpse.bounce;
    ccb.flash = corpse.flash;
    ccb.flashTime = corpse.flashTime;
    ccb.shrinkStart = corpse.shrinkStart;
    for (int p = 0; p < 8; ++p) ccb.pivot[p] = m_PartPivots[p];

    const auto& subs = m_EnemyModel->GetSubMeshes();
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
    {
        if (k == Swarm::kDrawListGhost)
        {
            bindVS();   // 雑魚のテクスチャに戻す
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().AlphaBlend(), bf, 0xFFFFFFFF);
        }
        else if (Texture* albedo = ListAlbedo(k))
            m_EnemyPS->SetTexture(m_Context, 0, albedo);
        else if (k != Swarm::kDrawListMob)
            bindVS();   // 自分のテクスチャが無い種類は雑魚のテクスチャに戻す
        m_CorpseVS->SetSRV(m_Context, "corpseList", m_CorpseListSRV[k].Get());

        for (size_t i = 0; !m_ListHideBody[k] && i < subs.size() && i < m_CorpseDrawArgs[k].size(); ++i)   // 飾りだけの敵は体を砕かない
        {
            if (!subs[i].mesh || !m_CorpseDrawArgs[k][i]) continue;
            ccb.part = (uint32_t)i;
            m_CorpseVS->WriteBuffer(m_Context, 4, &ccb);
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_CorpseDrawArgs[k][i].Get(), 0);
        }
        RenderCorpseDecorations(k, ccb);   // 部品に付いた飾りも一緒に飛ぶ
        if (k == Swarm::kDrawListGhost)
        {
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().Opaque(), bf, 0xFFFFFFFF);
        }
    }
    // 次のフレームの Compute が UAV として使うので外す
    m_CorpseVS->UnbindSRVs(m_Context);
}

// ============================================================
// 液溜まり（GPU の範囲の Liquid entry）
// 範囲の全槽を地面の格子 1 枚ずつ（LIQUID_GRID^2 の四角、頂点バッファ無し）。
// 死んだ槽・レシピに液体が無い範囲は VS が潰す。PS は CPU の経路と同じ VFXLiquidPS。
// 深度テストあり・書き込み無し、乗算済み alpha（外側の光は alpha 0 = 加算）。
// 雑魚・オーブの後、足元の丸い影の前（雑魚の影が液面に落ちる）。点光源はまだ繋がっている
// ============================================================
void SwarmSystem::RenderLiquids(CameraBase* camera, const LightBuffer& light)
{
    if (!liquids || !m_LiquidVS || !m_LiquidPS || !m_HeightSRV) return;
    if (m_VFX.GetLiquidDefCount() == 0 || !m_VFX.GetLiquidDefSRV() || !m_VFX.GetRecipeLiquidSRV()) return;

    VFXLiquidRenderer::CameraCB cam;
    cam.viewProj = camera->GetViewMatrix() * camera->GetProjectionMatrix();
    cam.hasTerrain = 1u;
    VFXLiquidRenderer::FrameCB frame;
    frame.time = m_AnimClock;
    LightBuffer l = light;   // cameraPosition は Render が入れ直した物

    m_LiquidVS->WriteBuffer(m_Context, 0, &cam);
    m_LiquidVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);   // 地形の格子
    m_LiquidPS->WriteBuffer(m_Context, 0, &l);
    m_LiquidPS->WriteBuffer(m_Context, 1, &frame);

    m_LiquidVS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "areaTrack", m_AreaTrackSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "recipeLiquid", m_VFX.GetRecipeLiquidSRV());
    m_LiquidVS->SetSRV(m_Context, "liquidDefs", m_VFX.GetLiquidDefSRV());
    m_LiquidPS->SetSRV(m_Context, "liquidDefs", m_VFX.GetLiquidDefSRV());

    m_LiquidVS->Bind(m_Context);
    m_LiquidPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    constexpr UINT kVerts = 16 * 16 * 6;   // LiquidCommon.hlsli の LIQUID_VERTS
    m_Context->DrawInstanced(kVerts, Swarm::kMaxAreas, 0, 0);

    // 次のフレームの Compute が UAV として使うので外す（areaTrack・areas）
    m_LiquidVS->UnbindSRVs(m_Context);
    m_LiquidPS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 範囲の連番画像を描く。環の全枠を 6 頂点ずつ描き、空の枠は VS が潰す
// 混合は乗算済み alpha（加算の物は alpha 0）。深度は読むだけ
// ============================================================
void SwarmSystem::RenderSprites(CameraBase* camera)
{
    if (!camera || !m_SpriteVS || !m_SpritePS || m_VFX.GetSpriteDefCount() == 0) return;
    if (!m_VFX.GetSpriteArraySRV() || !m_VFX.GetSpriteDefSRV()) return;

    VFXSpriteCameraCB cb = VFXSpriteCameraCB::From(camera);
    m_SpriteVS->WriteBuffer(m_Context, 0, &cb);
    m_SpriteVS->Bind(m_Context);
    m_SpritePS->Bind(m_Context);

    ID3D11ShaderResourceView* vsSRV[2] = { m_SpriteSRV.Get(), m_VFX.GetSpriteDefSRV() };
    m_Context->VSSetShaderResources(0, 2, vsSRV);
    ID3D11ShaderResourceView* psSRV = m_VFX.GetSpriteArraySRV();
    m_Context->PSSetShaderResources(0, 1, &psSRV);
    ID3D11SamplerState* samp = RenderStates::Get().PointClamp();
    m_Context->PSSetSamplers(0, 1, &samp);

    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    RenderStates::Get().ApplyAlphaBlend(m_Context);

    m_Context->DrawInstanced(6, kMaxSprites, 0, 0);

    // 次のフレームで CS が UAV として使うので必ず外す
    ID3D11ShaderResourceView* nulls[2] = {};
    m_Context->VSSetShaderResources(0, 2, nulls);
    m_Context->PSSetShaderResources(0, 1, nulls);
    RenderStates::Get().Restore(m_Context);
}
// ============================================================
// デバッグ: 判定球のワイヤーフレーム
// 全スロットを DrawInstanced し、死んだ物は VS 側でクリップ外へ畳む。
// 8192 × 64 頂点 = 約 52 万頂点。デバッグ用途なら気にしない
// ============================================================
void SwarmSystem::RenderDebug(CameraBase* camera)
{
    if (!camera || !m_DebugVS || !m_DebugPS) return;

    DebugCB cb;
    cb.view = camera->GetViewMatrix().Transpose();
    cb.proj = camera->GetProjectionMatrix().Transpose();
    m_DebugVS->WriteBuffer(m_Context, 0, &cb);

    m_DebugVS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
    m_DebugVS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());

    m_DebugVS->Bind(m_Context);
    m_DebugPS->Bind(m_Context);

    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    m_Context->DrawInstanced(64, Swarm::kMaxProjectiles, 0, 0);

    m_DebugVS->UnbindSRVs(m_Context);
    if (m_DebugEnemyVS)
    {
        m_DebugEnemyVS->WriteBuffer(m_Context, 0, &cb);
        m_DebugEnemyVS->WriteBuffer(m_Context, 1, &m_CachedAICB);   // 半径と直線部の長さ
        m_DebugEnemyVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_DebugEnemyVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_DebugEnemyVS->Bind(m_Context);

        m_Context->DrawInstanced(128, Swarm::kMaxEnemies, 0, 0);

        m_DebugEnemyVS->UnbindSRVs(m_Context);
    }
    RenderStates::Get().Restore(m_Context);
}
// ============================================================
// 描画の全部（不透明な物 → 半透明・重ね描き）。画面のアウトラインを間に挟むシーンは 2 つを分けて呼ぶ
// ============================================================
void SwarmSystem::Render(CameraBase* camera, const LightBuffer& light)
{
    RenderOpaque(camera, light);
    RenderOverlay(camera, light);
}

// ============================================================
// 雑魚の本描画（不透明：雑魚・砕け散り・経験値オーブ。深度を書く）
// 頂点/インデックスは1体分、インスタンス数 = プール全体。
// 生死は VS が state を見て判断する（死んだ槽は near 面の外へ畳む）
// ============================================================
void SwarmSystem::RenderOpaque(CameraBase* camera, const LightBuffer& light)
{
    if (!camera || !m_EnemyMaterial || !m_EnemyModel) return;

    // ---- 活きスロットの一覧 → 描画 instance 数 ----
    // 描画の直前に作る（Flush 後の state が確定している）。
    // UAV を initialCount = 0 で bind すると append counter が 0 に戻る
    const bool indirect = m_EnemyCompactCS && m_AliveListUAV && !m_EnemyDrawArgs[0].empty();
    if (indirect)
    {
        // Boss の様子は毎回 0 から（居なければ alive = 0 のまま）
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_BossInfoUAV.Get(), zero);

        m_EnemyCompactCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyCompactCS->Bind(m_Context);
        m_EnemyCompactCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyCompactCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
        m_EnemyCompactCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_EnemyCompactCS->SetSRV(m_Context, "enemyMaxHp", m_EnemyMaxHpSRV.Get());
        m_EnemyCompactCS->SetUAV(m_Context, "aliveList", m_AliveListUAV.Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "mobList", m_KindListUAV[Swarm::kDrawListMob].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "bomberList", m_KindListUAV[Swarm::kDrawListBomber].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "ghostList", m_KindListUAV[Swarm::kDrawListGhost].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "splitterList", m_KindListUAV[Swarm::kDrawListSplitter].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "bruteList", m_KindListUAV[Swarm::kDrawListBrute].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "bossInfo", m_BossInfoUAV.Get());
        m_EnemyCompactCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
        m_EnemyCompactCS->UnbindSRVs(m_Context);
        m_EnemyCompactCS->UnbindUAVs(m_Context);
        // 突撃兵・盾兵の一覧（CompactCS の UAV が足りない分）
        DispatchKindLists();
        // 普通の雑魚を近い / 遠いに分ける（LOD）
        m_LodActive = DispatchLodSplit(camera->GetPosition());

        // Boss の様子を staging へ（読むのは 2 フレーム後の Flush）
        m_BossStaging.Copy(m_Context, m_BossInfoBuffer.Get());

        // 種類毎の一覧の長さ → その種類の submesh 毎の InstanceCount
        for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
            for (auto& args : m_EnemyDrawArgs[k])
                if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_KindListUAV[k].Get());
        if (m_LodActive) CopyLodCounts();   // 雑魚の部品毎の引数を近い方の長さで上書き + 遠い方
        // HP バーも同じ数だけ（DrawInstancedIndirect の InstanceCount も 2 番目 = 4 バイト目）
        if (m_HpBarArgs)
            m_Context->CopyStructureCount(m_HpBarArgs.Get(), sizeof(uint32_t) * 1, m_AliveListUAV.Get());
        if (m_BomberRingArgs)
            m_Context->CopyStructureCount(m_BomberRingArgs.Get(), sizeof(uint32_t) * 1,
                m_KindListUAV[Swarm::kDrawListBomber].Get());
        if (m_DashBomberRingArgs)   // 突撃爆弾兵の警告の輪
            m_Context->CopyStructureCount(m_DashBomberRingArgs.Get(), sizeof(uint32_t) * 1,
                m_KindListUAV[Swarm::kDrawListDashBomber].Get());
        // 突撃兵の予告の帯・盾兵の盾も一覧の長さだけ
        if (m_ChargeLineArgs)
            m_Context->CopyStructureCount(m_ChargeLineArgs.Get(), sizeof(uint32_t) * 1,
                m_KindListUAV[Swarm::kDrawListCharger].Get());
        for (auto& args : m_ShieldDrawArgs)
            if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1,
                m_KindListUAV[Swarm::kDrawListShield].Get());
        for (auto& args : m_SpikeBallDrawArgs)   // トゲ玉の球
            if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_SpikeBallListUAV.Get());
        for (auto& args : m_MoundDrawArgs)   // 砂潜りの砂の山
            if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_KindListUAV[Swarm::kDrawListBurrower].Get());
        for (auto& args : m_IceDrawArgs)     // 凍った敵の氷：生きている敵だけ（凍っていない分は VS が畳む）
            if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_AliveListUAV.Get());
        CopyListDecoCounts();   // 描画リスト毎の飾り（面の Boss・遺跡の敵）と遺跡の敵の予告の帯
    }

    // sampler は雑魚とオーブで共通。Material::Bind は sampler を触らないので自分で入れる
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();

    // VS/PS/既定テクスチャをまとめて bind
    m_EnemyMaterial->Bind(m_Context);
    m_Context->PSSetSamplers(0, 1, &samp);
    PointLightManager::Get().BindPS(m_Context);   // t6/t7（雑魚とオーブ共通）

    // VS b0: VS.hlsl と同じ row_major なので Transpose しない
    MVPBuffer cb;   // World は単位行列のまま
    cb.View = camera->GetViewMatrix();
    cb.Projection = camera->GetProjectionMatrix();
    m_EnemyVS->WriteBuffer(m_Context, 0, &cb);
    // VS b2: 被弾の閃光が g_HitStun / g_HitFlash を読む（b1 の FrameCB は VS では未使用）
    m_EnemyVS->WriteBuffer(m_Context, 2, &m_CachedAICB);
    // VS b5: 自爆兵の点滅と膨らみ
    m_EnemyVS->WriteBuffer(m_Context, 5, &m_CachedKindCB);

    // PS b0: 光。cameraPosition は Renderer が DrawMesh の中でしか詰めないので自分で入れる
    LightBuffer l = light;
    l.cameraPosition = camera->GetPosition();
    m_EnemyPS->WriteBuffer(m_Context, 0, &l);
    // PS b1: 溶解は使わない（threshold < 0 で OFF）。
    // ※この PS は Renderer と別のオブジェクトなので、書かないと b1 が未定義のまま
    //   → threshold >= 0 と解釈されて全ピクセルが clip され、雑魚が丸ごと消える
    DissolveCB dcb = {};
    dcb.threshold = -1.0f;
    m_EnemyPS->WriteBuffer(m_Context, 1, &dcb);

    m_EnemyVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_EnemyVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_EnemyVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_EnemyVS->SetSRV(m_Context, "enemyStatus", m_EnemyStatusSRV.Get());   // 凍った敵は姿勢を止めて氷色に
    if (m_IceMapSRV && m_PondTopSRV)
    {
        m_EnemyVS->SetSRV(m_Context, "g_IceMap", m_IceMapSRV.Get());      // 氷の上の体を持ち上げる
        m_EnemyVS->SetSRV(m_Context, "g_PondTop", m_PondTopSRV.Get());
    }
    m_EnemyVS->SetSRV(m_Context, "enemyPoison", m_EnemyPoisonSRV.Get()); // 毒素が残っている敵は緑に
    // 部品アニメの表（無ければ未 bind のまま。VS は enabled = 0 で読まない）
    if (m_PartAnimSRV)
        m_EnemyVS->SetSRV(m_Context, "partAnim", m_PartAnimSRV.Get());
    m_EnemyAnim.time = m_AnimClock;
    m_EnemyAnim.walkRate = enemyWalkAnimRate;

    // VS は一覧（aliveList の名前で繋ぐ）経由でしかスロットを引かないので、間接引数が無い時は描かない。
    // 種類毎に一覧とテクスチャ（t0）を差し替えて同じメッシュを描く。雑魚のテクスチャは Material::Bind が入れた物
    const auto& subs = m_EnemyModel->GetSubMeshes();
    // 陣営のアウトライン：敵はステンシルに 2 を書く（PostProcess/Outline が赤い線にする）。描き終えたら元へ
    m_Context->OMSetDepthStencilState(RenderStates::Get().DepthStencilWrite(), 2);
    for (uint32_t k = 0; indirect && k < Swarm::kEnemyKinds; ++k)
    {
        const bool lodMob = m_LodActive && k == Swarm::kDrawListMob;
        m_EnemyVS->SetSRV(m_Context, "aliveList", lodMob ? m_LodNearSRV.Get() : m_KindListSRV[k].Get());
        if (Texture* albedo = ListAlbedo(k))
            m_EnemyPS->SetTexture(m_Context, 0, albedo);
        else if (k != Swarm::kDrawListMob && k != Swarm::kDrawListGhost)
        {
            m_EnemyMaterial->Bind(m_Context);   // 自分のテクスチャが無い種類は雑魚のテクスチャに戻す
            m_Context->PSSetSamplers(0, 1, &samp);
        }
        if (k == Swarm::kDrawListGhost)
        {
            // 幽霊：雑魚のテクスチャに戻し、alpha blend（乗算済み: 色はそのまま足され、alpha 分だけ後ろが消える = 光る半透明）。
            // 深度は書く（後から描くオーブ・粒子が幽霊の奥にある時に正しく隠れる）
            m_EnemyMaterial->Bind(m_Context);
            m_Context->PSSetSamplers(0, 1, &samp);
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().AlphaBlend(), bf, 0xFFFFFFFF);
        }

        for (size_t i = 0; !m_ListHideBody[k] && i < subs.size() && i < m_EnemyDrawArgs[k].size(); ++i)   // 飾りだけの敵は体を描かない
        {
            if (!subs[i].mesh || !m_EnemyDrawArgs[k][i]) continue;
            // VS b4: どの部品を描いているか（部品アニメの表の列）
            m_EnemyAnim.part = (uint32_t)i;
            m_EnemyVS->WriteBuffer(m_Context, 4, &m_EnemyAnim);
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_EnemyDrawArgs[k][i].Get(), 0);
        }
        if (lodMob) RenderLodFar();   // 遠い雑魚は合わせた網で 1 回
        RenderListDecorations(k);   // 体と同じ VS で、部品に付いた飾り・体の周りを回る岩・翼（Boss・遺跡の敵）
        if (k == Swarm::kDrawListGhost)
        {
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().Opaque(), bf, 0xFFFFFFFF);
        }
    }
    // 次のフレームの Compute が UAV として使うので必ず外す
    m_EnemyVS->UnbindSRVs(m_Context);
    // 盾兵の盾（敵の一部なのでステンシル 2 のまま。赤い陣営の縁取りが盾まで回る）
    if (indirect)
    {
        RenderShields(cb.View, cb.Projection);
        RenderSpikeBalls(cb.View, cb.Projection);   // トゲ玉の体
        RenderMounds(cb.View, cb.Projection);       // 砂潜りの砂の山
        // 砂旋の体は CPU のエフェクト（Enemy/SandTornadoFx）が描くので、ここでは描かない
    }
    m_Context->OMSetDepthStencilState(RenderStates::Get().DepthDefault(), 0);   // 砕け散り・オーブはステンシル無し

    // ---- 死んだ敵の砕け散り（同じ網・同じ PS。VS だけ差し替え）----
    RenderCorpses(cb.View, cb.Projection);

    // ---- 経験値オーブ（自発光の宝石）----
    // 全スロットを DrawInstanced し、死んだ物は VS が潰す
    if (m_OrbMaterial && m_OrbModel)
    {
        m_OrbMaterial->Bind(m_Context);
        m_Context->PSSetSamplers(0, 1, &samp);
        m_OrbVS->WriteBuffer(m_Context, 0, &cb);   // 同じ row_major View/Proj

        const auto& ol = orbLook;
        OrbLookCB look = {
            m_AnimClock, ol.scale, ol.bobHeight, ol.bobSpeed,
            ol.spinSpeed, ol.pulseAmount, ol.pulseSpeed, ol.fullPullSpeed,
            ol.stretchPerSpeed, ol.stretchMax, ol.tiltMax, ol.pullGlow,
            ol.idleColor, ol.pullColor };
        m_OrbVS->WriteBuffer(m_Context, 4, &look);

        // PS b0: 太陽の向き・カメラ・霧（雑魚と同じ l）、b1: 光り方
        OrbShadeCB shade = {
            ol.emissive, ol.facet, ol.rimGain, ol.rimPower,
            ol.glintGain, ol.glintPower, { 0.0f, 0.0f },
            ol.rimColor };
        m_OrbPS->WriteBuffer(m_Context, 0, &l);
        m_OrbPS->WriteBuffer(m_Context, 1, &shade);

        m_OrbVS->SetSRV(m_Context, "orbs", m_OrbSRV.Get());
        m_OrbVS->SetSRV(m_Context, "orbStates", m_OrbStateSRV.Get());

        for (const auto& sub : m_OrbModel->GetSubMeshes())
        {
            if (sub.mesh)
                sub.mesh->DrawInstanced(m_Context, Swarm::kMaxOrbs);
        }

        m_OrbVS->UnbindSRVs(m_Context);
    }

    // ---- 凍った敵の氷（半透明。深度は読むだけなので不透明物の一番最後）----
    // 生きている敵の一覧から描くので、一覧を作った時だけ（敵の体と同じ）
    if (indirect)
        RenderIce(cb.View, cb.Projection);
}

// ============================================================
// 半透明・重ね描き（液面・足元の影・警告の輪・HP バー。深度は読むだけ）。
// トゥーンのアウトラインは RenderOpaque との間に描くので、アウトラインがこれらの上に乗らない
// ============================================================
void SwarmSystem::RenderOverlay(CameraBase* camera, const LightBuffer& light)
{
    if (!camera || !m_EnemyMaterial || !m_EnemyModel) return;
    const bool indirect = m_EnemyCompactCS && m_AliveListUAV && !m_EnemyDrawArgs[0].empty();
    LightBuffer l = light;
    l.cameraPosition = camera->GetPosition();

    // ---- 液溜まり（Liquid entry。点光源を外す前に：毒の池の緑の光が液面に映る）----
    PointLightManager::Get().BindPS(m_Context);   // 間に草などが入っても液面が点光源を読めるように
    RenderLiquids(camera, l);
    PointLightManager::Get().UnbindPS(m_Context);

    // ---- 足元の丸い影 → 足元の警告の輪 → 頭上の HP バー（雑魚の後。深度は読むだけ）----
    if (indirect)
        RenderBlobShadows(camera);
    if (indirect)
        RenderBomberRings(camera);
    if (indirect)
        RenderChargeLines(camera);
    RenderDropRings(camera);
    RenderWarnRings(camera);
    if (indirect)
        RenderHpBars(camera);
}
