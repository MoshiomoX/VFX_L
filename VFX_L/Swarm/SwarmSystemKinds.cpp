// ============================================================
// SwarmSystemKinds.cpp
// 突撃兵・盾兵・凍結（2026-10-08）の GPU 側の見た目。SwarmSystem.cpp が 3000 行を超えたので分けた。
//   ・突撃兵 / 盾兵の描画リスト：SwarmEnemyCompactCS は UAV が 8 本（FL 11.0 の上限）で埋まっているので、
//     SwarmEnemyKindListCS が CompactCS の直後に別に作る
//   ・突撃兵の予告の帯：溜めの間、足元から突進の向きへ地面に赤い帯（SwarmChargeLineVS / PS、RenderOverlay）
//   ・盾兵の盾：特効モデル dun01.FBX を身体の前に（SwarmShieldVS + 雑魚の PS、敵のステンシルのまま）
//   ・凍った敵の氷：特効モデル bingci_02.FBX を足元に前後 2 つ（SwarmIceVS + 雑魚の PS、半透明）
// 動き（溜め・突進・凍結）と当たり（盾の装甲）は CS 側（SwarmEnemyAICS / SwarmHitCS / SwarmAreaDamageCS / SwarmContactCS）
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Camera/CameraBase.h"
#include "Graphics/Model/Model.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Mesh/Mesh.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT kChargeLineSegs = 24;   // SwarmChargeLineVS の LINE_SEGS

    template <class T>
    std::shared_ptr<T> LoadShader(ID3D11Device* device, const wchar_t* path, const char* name)
    {
        auto s = std::make_shared<T>();
        const HRESULT hr = ShaderPath::Load(s.get(), device, path);
        std::cout << "[SwarmSystem] " << name << ": " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
        return SUCCEEDED(hr) ? s : nullptr;
    }

    // 間接描画の引数（DrawIndexedInstancedIndirect の 5 uint / DrawInstancedIndirect の 4 uint）
    ComPtr<ID3D11Buffer> MakeArgs(ID3D11Device* device, const uint32_t* init, UINT count)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * count;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = init;
        ComPtr<ID3D11Buffer> args;
        if (FAILED(device->CreateBuffer(&desc, &sd, &args))) return nullptr;
        return args;
    }
}

// ============================================================
// 読み込み（LoadShaders の最後、雑魚のモデル・描画リストの後）
// ============================================================
bool SwarmSystem::LoadKindAssets(ID3D11Device* device)
{
    m_KindListCS = LoadShader<ComputeShader>(device, L"Shader/Swarm/SwarmEnemyKindListCS.hlsl", "EnemyKindListCS");
    m_ChargeLineVS = LoadShader<VertexShader>(device, L"Shader/Swarm/SwarmChargeLineVS.hlsl", "ChargeLineVS");
    m_ChargeLinePS = LoadShader<PixelShader>(device, L"Shader/Swarm/SwarmChargeLinePS.hlsl", "ChargeLinePS");
    m_ShieldVS = LoadShader<VertexShader>(device, L"Shader/Swarm/SwarmShieldVS.hlsl", "ShieldVS");
    m_IceVS = LoadShader<VertexShader>(device, L"Shader/Swarm/SwarmIceVS.hlsl", "IceVS");
    m_SpikeBallVS = LoadShader<VertexShader>(device, L"Shader/Swarm/SwarmSpikeBallVS.hlsl", "SpikeBallVS");   // トゲ玉（2026-10-17）
    m_MoundVS = LoadShader<VertexShader>(device, L"Shader/Swarm/SwarmMoundVS.hlsl", "MoundVS");   // 砂潜り（2026-10-17）
    m_TornadoVS = LoadShader<VertexShader>(device, L"Shader/Swarm/SwarmTornadoVS.hlsl", "TornadoVS");   // 砂旋（2026-10-17）

    // 突撃兵の帯: { 区間 x 6 頂点, InstanceCount = 突撃兵の一覧（CopyStructureCount）, 0, 0 }
    const uint32_t lineInit[4] = { kChargeLineSegs * 6, 0, 0, 0 };
    m_ChargeLineArgs = MakeArgs(device, lineInit, 4);

    // テクスチャの無い Material は Bind で既定の白を t0 に入れる → 色は VS の頂点色だけで出る
    auto makeProp = [&](std::shared_ptr<VertexShader>& vs, const char* path,
        std::shared_ptr<Material>& mat, std::shared_ptr<Model>& model) -> bool
        {
            if (!vs || !m_EnemyPS) return false;
            model = ResourceManager::Get().LoadModel(path);
            if (!model || model->GetSubMeshes().empty())
            {
                std::cout << "[SwarmSystem] prop mesh missing: " << path << std::endl;
                model.reset();
                return false;
            }
            mat = std::make_shared<Material>();
            mat->SetVertexShader(vs);
            mat->SetPixelShader(m_EnemyPS);
            return true;
        };
    makeProp(m_ShieldVS, Res::VFX::TowerShield, m_ShieldMaterial, m_ShieldModel);
    makeProp(m_IceVS, Res::VFX::IceSpikes, m_IceMaterial, m_IceModel);
    // 砂旋（2026-10-19 作り直し）：コードで作った筒（上下が広く腰が少し細い）+ 専用 PS（2 枚の雲ノイズを螺旋に流す）。
    // t0 / t1 = ノイズ 2 枚（Material の albedo / normal の枠を借りる）。足元の砂埃は SwarmTornadoEmitCS。Swarm/SwarmSystemTornado.cpp
    m_TornadoPS = LoadShader<PixelShader>(device, L"Shader/Swarm/SwarmTornadoPS.hlsl", "TornadoPS");
    m_TornadoEmitCS = LoadShader<ComputeShader>(device, L"Shader/Swarm/SwarmTornadoEmitCS.hlsl", "TornadoEmitCS");   // 無くても砂埃が出ないだけ
    m_WindSourceCS = LoadShader<ComputeShader>(device, L"Shader/Swarm/SwarmWindSourceCS.hlsl", "WindSourceCS");      // 無くても風源が空のまま
    m_TornadoModel.reset();
    m_TornadoMaterial.reset();
    if (m_TornadoVS && m_TornadoPS)
    {
        m_TornadoModel = PrimitiveBuilder::CreateTornadoShell(device, 32, 14, 0.62f, 0.35f);
        if (m_TornadoModel)
        {
            m_TornadoMaterial = std::make_shared<Material>();
            m_TornadoMaterial->SetVertexShader(m_TornadoVS);
            m_TornadoMaterial->SetPixelShader(m_TornadoPS);
            m_TornadoMaterial->SetAlbedoTexture(ResourceManager::Get().LoadTexture(Res::VFX::WindStreaks));   // 風の流線（2026-10-19、雲ノイズ A から）
            m_TornadoMaterial->SetNormalTexture(ResourceManager::Get().LoadTexture(Res::VFX::Noise008));   // 雲ノイズ B（柔らかい方）
        }
    }

    // 盾: submesh 毎（IndexCount が違う）。InstanceCount は盾兵の一覧から
    m_ShieldDrawArgs.clear();
    if (m_ShieldModel)
    {
        for (const auto& sub : m_ShieldModel->GetSubMeshes())
        {
            if (!sub.mesh) { m_ShieldDrawArgs.emplace_back(); continue; }
            const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
            m_ShieldDrawArgs.push_back(MakeArgs(device, init, 5));
        }
    }
    // トゲ玉：コードで作った球 + トゲ（半径 1、砂色の球 + 褐色のトゲ。2026-10-17）。InstanceCount はトゲ玉の一覧から
    m_SpikeBallDrawArgs.clear();
    if (m_SpikeBallVS && m_EnemyPS)
    {
        m_SpikeBallModel = PrimitiveBuilder::CreateSpikeBall(device, 1.0f, 0.45f, 26,
            { 0.60f, 0.47f, 0.27f, 1.0f }, { 0.30f, 0.21f, 0.12f, 1.0f });
        if (m_SpikeBallModel)
        {
            m_SpikeBallMaterial = std::make_shared<Material>();
            m_SpikeBallMaterial->SetVertexShader(m_SpikeBallVS);
            m_SpikeBallMaterial->SetPixelShader(m_EnemyPS);
            for (const auto& sub : m_SpikeBallModel->GetSubMeshes())
            {
                if (!sub.mesh) { m_SpikeBallDrawArgs.emplace_back(); continue; }
                const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
                m_SpikeBallDrawArgs.push_back(MakeArgs(device, init, 5));
            }
        }
    }
    // 砂潜りの砂の山：単位球（砂色）。VS が (半径, 高さ, 半径) に潰して足元に置く
    m_MoundDrawArgs.clear();
    if (m_MoundVS && m_EnemyPS)
    {
        m_MoundModel = PrimitiveBuilder::CreateSphere(device, 1.0f, { 0.72f, 0.58f, 0.36f, 1.0f }, 10);
        if (m_MoundModel)
        {
            m_MoundMaterial = std::make_shared<Material>();
            m_MoundMaterial->SetVertexShader(m_MoundVS);
            m_MoundMaterial->SetPixelShader(m_EnemyPS);
            for (const auto& sub : m_MoundModel->GetSubMeshes())
            {
                if (!sub.mesh) { m_MoundDrawArgs.emplace_back(); continue; }
                const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
                m_MoundDrawArgs.push_back(MakeArgs(device, init, 5));
            }
        }
    }
    // 砂旋: submesh 毎。InstanceCount は砂旋の一覧から
    m_TornadoDrawArgs.clear();
    if (m_TornadoModel)
    {
        for (const auto& sub : m_TornadoModel->GetSubMeshes())
        {
            if (!sub.mesh) { m_TornadoDrawArgs.emplace_back(); continue; }
            const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
            m_TornadoDrawArgs.push_back(MakeArgs(device, init, 5));
        }
    }
    return m_KindListCS && m_ChargeLineVS && m_ChargeLinePS && m_ShieldModel && m_IceModel && m_SpikeBallModel && m_MoundModel && m_TornadoModel;
}

// ============================================================
// 突撃兵・盾兵の描画リスト（RenderOpaque の CompactCS の直後）
// ============================================================
void SwarmSystem::DispatchKindLists()
{
    if (!m_KindListCS) return;
    m_KindListCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_KindListCS->Bind(m_Context);
    m_KindListCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_KindListCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    // initialCount = 0 で bind → append の数が 0 に戻る
    m_KindListCS->SetUAV(m_Context, "chargerList", m_KindListUAV[Swarm::kDrawListCharger].Get(), 0);
    m_KindListCS->SetUAV(m_Context, "shieldList", m_KindListUAV[Swarm::kDrawListShield].Get(), 0);
    m_KindListCS->SetUAV(m_Context, "spikeBallList", m_SpikeBallListUAV.Get(), 0);   // トゲ玉（2026-10-17）
    m_KindListCS->SetUAV(m_Context, "burrowerList", m_KindListUAV[Swarm::kDrawListBurrower].Get(), 0);   // 砂潜り（2026-10-17）
    m_KindListCS->SetUAV(m_Context, "sandMageList", m_KindListUAV[Swarm::kDrawListSandMage].Get(), 0);   // 砂嵐の術師（2026-10-17）
    m_KindListCS->SetUAV(m_Context, "tornadoList", m_TornadoListUAV.Get(), 0);   // 砂旋（2026-10-17）
    m_KindListCS->SetUAV(m_Context, "dashBomberList", m_KindListUAV[Swarm::kDrawListDashBomber].Get(), 0);   // 突撃爆弾兵（2026-10-18）
    m_KindListCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_KindListCS->UnbindSRVs(m_Context);
    m_KindListCS->UnbindUAVs(m_Context);
}

// ============================================================
// 盾兵の盾（RenderOpaque の雑魚の後。深度とステンシル 2 を書く = 赤い陣営の縁取りが盾まで回る）
// ============================================================
void SwarmSystem::RenderShields(const Matrix& view, const Matrix& proj)
{
    if (!shieldLook.enabled || !m_ShieldMaterial || !m_ShieldModel || m_ShieldDrawArgs.empty()) return;

    m_ShieldMaterial->Bind(m_Context);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    EnemyRenderCB cb;
    cb.view = view;
    cb.proj = proj;
    m_ShieldVS->WriteBuffer(m_Context, 0, &cb);
    m_ShieldVS->WriteBuffer(m_Context, 2, &m_CachedAICB);      // 体の半径（足元の高さ）・被弾の閃光
    m_ShieldVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB);  // 体格・凍結の色

    // 盾の中心を hold に置き、高さが shieldLook.height になる倍率
    const Vector3 bmin = m_ShieldModel->GetBoundsMin(), bmax = m_ShieldModel->GetBoundsMax();
    const float h = (std::max)(bmax.y - bmin.y, 1e-4f);
    PropMeshCB p = {};
    p.anchor = (bmin + bmax) * 0.5f;
    p.scale = shieldLook.height / h;
    p.hold = shieldLook.hold;
    p.yaw = DirectX::XMConvertToRadians(shieldLook.yawDeg);
    p.tint = shieldLook.tint;
    p.bob = shieldLook.bob;
    p.alpha = 1.0f;
    m_ShieldVS->WriteBuffer(m_Context, 4, &p);

    m_ShieldVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_ShieldVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_ShieldVS->SetSRV(m_Context, "shieldList", m_KindListSRV[Swarm::kDrawListShield].Get());
    m_ShieldVS->SetSRV(m_Context, "enemySlow", m_EnemySlowSRV.Get());
    m_ShieldVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    // 板のような薄い盾なので裏も描く
    m_Context->RSSetState(RenderStates::Get().CullNone());
    const auto& subs = m_ShieldModel->GetSubMeshes();
    for (size_t i = 0; i < subs.size() && i < m_ShieldDrawArgs.size(); ++i)
        if (subs[i].mesh && m_ShieldDrawArgs[i])
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_ShieldDrawArgs[i].Get(), 0);
    m_Context->RSSetState(RenderStates::Get().CullBack());

    // 次のフレームの Compute が UAV として使うので外す
    m_ShieldVS->UnbindSRVs(m_Context);
}

// ============================================================
// 凍った敵の氷（RenderOpaque の最後。半透明・深度は読むだけ）。
// 池全体 x 2 を DrawInstanced し、凍っていない・死んだ槽は VS が畳む
// ============================================================
void SwarmSystem::RenderIce(const Matrix& view, const Matrix& proj)
{
    if (!iceLook.enabled || !m_IceMaterial || !m_IceModel) return;

    m_IceMaterial->Bind(m_Context);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    EnemyRenderCB cb;
    cb.view = view;
    cb.proj = proj;
    m_IceVS->WriteBuffer(m_Context, 0, &cb);
    m_IceVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);      // 池の大きさ
    m_IceVS->WriteBuffer(m_Context, 2, &m_CachedAICB);         // 体の半径（足元の高さ）
    m_IceVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB);     // 体格

    // 塊の底の中心を足元に置き、高さが iceLook.height になる倍率
    const Vector3 bmin = m_IceModel->GetBoundsMin(), bmax = m_IceModel->GetBoundsMax();
    const float h = (std::max)(bmax.y - bmin.y, 1e-4f);
    PropMeshCB p = {};
    p.anchor = Vector3((bmin.x + bmax.x) * 0.5f, bmin.y, (bmin.z + bmax.z) * 0.5f);
    p.scale = iceLook.height / h;
    p.hold = Vector3(iceLook.spread, iceLook.sink, 0.0f);
    p.tint = iceLook.color;
    p.alpha = iceLook.alpha;
    m_IceVS->WriteBuffer(m_Context, 4, &p);

    m_IceVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_IceVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_IceVS->SetSRV(m_Context, "enemySlow", m_EnemySlowSRV.Get());
    m_IceVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    auto& rs = RenderStates::Get();
    const float bf[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), bf, 0xFFFFFFFF);   // 乗算済み: 色は足され、alpha 分だけ奥が消える
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    for (const auto& sub : m_IceModel->GetSubMeshes())
        if (sub.mesh)
            sub.mesh->DrawInstanced(m_Context, Swarm::kMaxEnemies * 2);
    m_Context->OMSetBlendState(rs.Opaque(), bf, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthDefault(), 0);

    m_IceVS->UnbindSRVs(m_Context);
}

// ============================================================
// 突撃兵の予告の帯（RenderOverlay、自爆兵の輪の後）。溜めていない突撃兵は VS が捨てる
// ============================================================
void SwarmSystem::RenderChargeLines(CameraBase* camera)
{
    if (!chargeLine.enabled || !m_ChargeLineVS || !m_ChargeLinePS || !m_ChargeLineArgs || !m_HeightSRV) return;

    BomberRingCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = chargeLine.fill;
    cb.edge = chargeLine.edge;
    cb.back = chargeLine.back;
    cb.edgeWidth = chargeLine.edgeWidth;
    cb.lift = chargeLine.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_ChargeLineVS->WriteBuffer(m_Context, 0, &cb);
    m_ChargeLinePS->WriteBuffer(m_Context, 0, &cb);
    m_ChargeLineVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);    // 格子（高さ場を引く）
    m_ChargeLineVS->WriteBuffer(m_Context, 2, &m_CachedAICB);       // 体の半径
    m_ChargeLineVS->WriteBuffer(m_Context, 4, &m_CachedBomberCB);   // 溜めの長さ・突進の距離

    m_ChargeLineVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_ChargeLineVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_ChargeLineVS->SetSRV(m_Context, "chargerList", m_KindListSRV[Swarm::kDrawListCharger].Get());
    m_ChargeLineVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_ChargeLineVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());

    m_ChargeLineVS->Bind(m_Context);
    m_ChargeLinePS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstancedIndirect(m_ChargeLineArgs.Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_ChargeLineVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// トゲ玉の体（RenderOpaque の盾の後、ステンシル 2 のまま。2026-10-17）。
// 雑魚のメッシュは描かず（CompactCS が mobList に入れない）、コードで作った球 + トゲ（PrimitiveBuilder::CreateSpikeBall、半径 1）を
// 体格に合わせて拡大し、速度の向きに転がして描く。色は頂点色 × spikeBallLook.tint
// ============================================================
void SwarmSystem::RenderSpikeBalls(const Matrix& view, const Matrix& proj)
{
    if (!spikeBallLook.enabled || !m_SpikeBallMaterial || !m_SpikeBallModel || m_SpikeBallDrawArgs.empty()) return;

    m_SpikeBallMaterial->Bind(m_Context);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    EnemyRenderCB cb;
    cb.view = view;
    cb.proj = proj;
    m_SpikeBallVS->WriteBuffer(m_Context, 0, &cb);
    m_SpikeBallVS->WriteBuffer(m_Context, 2, &m_CachedAICB);      // 体の半径（足元の高さ）・被弾の閃光
    m_SpikeBallVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB);  // 体格・凍結の色

    PropMeshCB p = {};
    p.anchor = Vector3::Zero;
    p.scale = spikeBallLook.radius;                        // メッシュの半径 1 → m（体格 1 の時）
    p.hold = Vector3(0.0f, spikeBallLook.radius, 0.0f);    // 中心 = 足元 + 半径
    p.yaw = 0.0f;
    p.tint = spikeBallLook.tint;
    p.bob = spikeBallLook.rollRate;
    p.alpha = 1.0f;
    m_SpikeBallVS->WriteBuffer(m_Context, 4, &p);

    m_SpikeBallVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_SpikeBallVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_SpikeBallVS->SetSRV(m_Context, "spikeBallList", m_SpikeBallListSRV.Get());
    m_SpikeBallVS->SetSRV(m_Context, "enemySlow", m_EnemySlowSRV.Get());
    m_SpikeBallVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    const auto& subs = m_SpikeBallModel->GetSubMeshes();
    for (size_t i = 0; i < subs.size() && i < m_SpikeBallDrawArgs.size(); ++i)
        if (subs[i].mesh && m_SpikeBallDrawArgs[i])
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_SpikeBallDrawArgs[i].Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_SpikeBallVS->UnbindSRVs(m_Context);
}

// ============================================================
// 砂潜りの砂の山（RenderOpaque のトゲ玉の後、ステンシル 2 のまま。2026-10-17）。
// 潜っている間は体が地面の下（SwarmEnemyVS）なので、代わりに足元の低い丘を描く。出ている間は VS が畳む
// ============================================================
void SwarmSystem::RenderMounds(const Matrix& view, const Matrix& proj)
{
    if (!moundLook.enabled || !m_MoundMaterial || !m_MoundModel || m_MoundDrawArgs.empty()) return;

    m_MoundMaterial->Bind(m_Context);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    EnemyRenderCB cb;
    cb.view = view;
    cb.proj = proj;
    m_MoundVS->WriteBuffer(m_Context, 0, &cb);
    m_MoundVS->WriteBuffer(m_Context, 2, &m_CachedAICB);      // 体の半径（足元の高さ）
    m_MoundVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB);  // 体格・出ている秒

    PropMeshCB p = {};
    p.anchor = Vector3::Zero;
    p.scale = moundLook.radius;
    p.hold = Vector3(0.0f, moundLook.height, 0.0f);
    p.yaw = 0.0f;
    p.tint = moundLook.tint;
    p.bob = moundLook.bob;
    p.alpha = 1.0f;
    m_MoundVS->WriteBuffer(m_Context, 4, &p);

    m_MoundVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_MoundVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_MoundVS->SetSRV(m_Context, "burrowerList", m_KindListSRV[Swarm::kDrawListBurrower].Get());
    m_MoundVS->SetSRV(m_Context, "enemySlow", m_EnemySlowSRV.Get());
    m_MoundVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    const auto& subs = m_MoundModel->GetSubMeshes();
    for (size_t i = 0; i < subs.size() && i < m_MoundDrawArgs.size(); ++i)
        if (subs[i].mesh && m_MoundDrawArgs[i])
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_MoundDrawArgs[i].Get(), 0);

    m_MoundVS->UnbindSRVs(m_Context);   // 次のフレームの Compute が UAV として使うので外す
}
