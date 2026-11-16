// ============================================================
// WindTrees.cpp
// ============================================================
#include "World/WindTrees.h"
#include "World/WindGrid.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Light/PointLightManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Manager/ResourceManager.h"
#include "Camera/CameraBase.h"
#include "ECS/View.h"
#include "ResourcePaths.h"

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

static_assert(sizeof(WindTrees::TreeCB) == 176, "TreeCB layout mismatch");
static_assert(sizeof(WindTrees::SwayCB) == 208, "SwayCB layout mismatch");
static_assert(sizeof(WindTrees::State) == 64, "TreeState layout mismatch");
static_assert(sizeof(TreeBuilder::Vertex) == 48, "TreeVertex layout mismatch");
static_assert(sizeof(TreeBuilder::Leaf) == 64, "TreeLeaf layout mismatch");

bool WindTrees::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    bool ok = true;
    auto load = [&](auto& sh, const wchar_t* path)
        {
            using T = typename std::remove_reference_t<decltype(sh)>::element_type;
            sh = std::make_shared<T>();
            if (FAILED(ShaderPath::Load(sh.get(), device, path))) { sh.reset(); ok = false; }
        };
    load(m_SwayCS, L"Shader/Tree/TreeSwayCS.hlsl");
    load(m_BranchVS, L"Shader/Tree/TreeBranchVS.hlsl");
    load(m_BranchPS, L"Shader/Tree/TreeBranchPS.hlsl");
    load(m_LeafVS, L"Shader/Tree/TreeLeafVS.hlsl");
    load(m_LeafPS, L"Shader/Tree/TreeLeafPS.hlsl");
    load(m_LeafShadowPS, L"Shader/Tree/TreeLeafShadowPS.hlsl");
    if (!ok)
        std::cout << "[Error] WindTrees: shader load failed" << std::endl;
    return ok;
}

void WindTrees::Shutdown()
{
    Clear();
    m_SwayCS.reset();
    m_BranchVS.reset(); m_LeafVS.reset();
    m_BranchPS.reset(); m_LeafPS.reset(); m_LeafShadowPS.reset();
}

void WindTrees::Clear()
{
    m_VertBuf.Reset(); m_IndexBuf.Reset(); m_LeafBuf.Reset(); m_StateBuf.Reset();
    m_VertSRV.Reset(); m_LeafSRV.Reset(); m_StateSRV.Reset(); m_StateUAV.Reset();
    m_Sites.clear();
    m_Stats = {};
    m_Ready = false;
}

bool WindTrees::TreeSite(int i, Vector3& root, float& height) const
{
    if (i < 0 || i >= (int)m_Sites.size()) return false;
    root = m_Sites[(size_t)i].root;
    height = m_Sites[(size_t)i].height;
    return true;
}

void WindTrees::Build(Registry& reg, TerrainGenerator::Biome biome)
{
    Clear();
    if (biome != TerrainGenerator::Biome::Grassland || !m_Device || !m_ReplaceModels) return;

    // 置き換えるモデル → 種類
    std::unordered_map<const Model*, TreeBuilder::Kind> kinds;
    auto add = [&](const char* const* paths, size_t n, TreeBuilder::Kind k)
        {
            for (size_t i = 0; i < n; ++i)
                if (auto m = ResourceManager::Get().LoadModel(paths[i])) kinds[m.get()] = k;
        };
    namespace F = Res::Mdl::Forest;
    add(F::kTrees, std::size(F::kTrees), TreeBuilder::Kind::Broadleaf);
    add(F::kBareTrees, std::size(F::kBareTrees), TreeBuilder::Kind::Bare);
    add(F::kBushes, std::size(F::kBushes), TreeBuilder::Kind::Shrub);

    reg.CreateView<TransformComponent, ModelComponent>()
        .Each([&](Entity, TransformComponent& tf, ModelComponent& mc)
            {
                if (!mc.batched || !mc.visible || !mc.model) return;
                auto it = kinds.find(mc.model.get());
                if (it == kinds.end()) return;
                const Vector3 lo = mc.model->GetBoundsMin(), hi = mc.model->GetBoundsMax();
                Site s;
                s.kind = it->second;
                s.root = Vector3(tf.position.x, tf.position.y + lo.y * tf.scale.y, tf.position.z);
                s.height = (hi.y - lo.y) * tf.scale.y;
                s.yawDeg = tf.rotation.y;
                // 場所から決まる種（同じ地図なら同じ形）
                uint32_t h = (uint32_t)(int32_t)std::floor(tf.position.x * 10.0f) * 73856093u
                    ^ (uint32_t)(int32_t)std::floor(tf.position.z * 10.0f) * 19349663u;
                s.seed = h ^ (h >> 13);
                m_Sites.push_back(s);
                mc.visible = false;   // モデルは描かない（StaticPropRenderer も visible を見る）
            });
    BuildGeometry();
}

void WindTrees::Rebuild()
{
    if (!m_Sites.empty()) BuildGeometry();
}

void WindTrees::BuildGeometry()
{
    m_VertBuf.Reset(); m_IndexBuf.Reset(); m_LeafBuf.Reset(); m_StateBuf.Reset();
    m_VertSRV.Reset(); m_LeafSRV.Reset(); m_StateSRV.Reset(); m_StateUAV.Reset();
    m_Ready = false;
    m_Stats = {};
    if (m_Sites.empty()) return;

    std::vector<TreeBuilder::Vertex> verts;
    std::vector<uint32_t> indices;
    std::vector<TreeBuilder::Leaf> leaves;
    std::vector<State> states;
    states.reserve(m_Sites.size());
    for (size_t i = 0; i < m_Sites.size(); ++i)
    {
        const Site& site = m_Sites[i];
        const float height = site.height * ((site.kind == TreeBuilder::Kind::Shrub) ? 1.0f : m_Settings.treeHeightMul);
        TreeBuilder::Params p;
        p.kind = site.kind;
        p.root = site.root;
        p.height = height;
        p.yawDeg = site.yawDeg;
        p.seed = site.seed;
        p.treeIndex = (uint32_t)i;
        p.leafDensity = m_Settings.leafDensity;
        p.leafSize = m_Settings.leafSize;
        const TreeBuilder::Result r = TreeBuilder::Build(p, verts, indices, leaves);

        State st{};
        st.root = site.root;
        st.height = height;
        st.flex = (site.kind == TreeBuilder::Kind::Shrub) ? m_Settings.shrubFlex : 1.0f;
        st.crown = r.crownCenter;
        states.push_back(st);

        if (site.kind == TreeBuilder::Kind::Broadleaf) ++m_Stats.trees;
        else if (site.kind == TreeBuilder::Kind::Bare) ++m_Stats.bare;
        else ++m_Stats.shrubs;
    }
    m_Stats.vertices = (int)verts.size();
    m_Stats.indices = (int)indices.size();
    m_Stats.leaves = (int)leaves.size();

    auto makeStructured = [&](const void* data, UINT stride, UINT count, bool uav,
        Microsoft::WRL::ComPtr<ID3D11Buffer>& buf, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv) -> bool
        {
            if (count == 0) return true;
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = stride * count;
            bd.Usage = uav ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = stride;
            D3D11_SUBRESOURCE_DATA sd = {};
            sd.pSysMem = data;
            if (FAILED(m_Device->CreateBuffer(&bd, &sd, &buf))) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = DXGI_FORMAT_UNKNOWN;
            sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sv.Buffer.NumElements = count;
            return SUCCEEDED(m_Device->CreateShaderResourceView(buf.Get(), &sv, &srv));
        };
    bool ok = makeStructured(verts.data(), sizeof(TreeBuilder::Vertex), (UINT)verts.size(), false, m_VertBuf, m_VertSRV)
        && makeStructured(leaves.data(), sizeof(TreeBuilder::Leaf), (UINT)leaves.size(), false, m_LeafBuf, m_LeafSRV)
        && makeStructured(states.data(), sizeof(State), (UINT)states.size(), true, m_StateBuf, m_StateSRV);
    if (ok)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = (UINT)states.size();
        ok = SUCCEEDED(m_Device->CreateUnorderedAccessView(m_StateBuf.Get(), &ud, &m_StateUAV));
    }
    if (ok && !indices.empty())
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = indices.data();
        ok = SUCCEEDED(m_Device->CreateBuffer(&bd, &sd, &m_IndexBuf));
    }
    if (!ok)
    {
        std::cout << "[Error] WindTrees: buffer creation failed" << std::endl;
        return;
    }
    m_Ready = true;
    std::cout << "[WindTrees] trees " << m_Stats.trees << " / bare " << m_Stats.bare << " / shrubs " << m_Stats.shrubs
        << ", vertices " << m_Stats.vertices << ", leaves " << m_Stats.leaves << std::endl;
}

void WindTrees::Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* windSources,
    const Vector4* gustA, const Vector4* gustB, uint32_t gustCount)
{
    ID3D11DeviceContext* ctx = m_Context;
    const float dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 止まる
    m_PendingDt = 0.0f;
    m_Time += dt;
    if (!Ready() || !ctx || !m_SwayCS || dt <= 0.0f || !areas || !areaStates || !windSources) return;

    const auto& s = m_Settings;
    SwayCB cb = {};
    cb.dt = dt;
    cb.time = m_Time;
    cb.treeCount = (uint32_t)m_Sites.size();
    cb.gustCount = (std::min)(gustCount, 4u);
    cb.windDir = m_Wind.dir;
    cb.windSpeed = m_Wind.speed * m_Wind.strength;
    cb.windGust = m_Wind.gust * m_Wind.strength;
    cb.windWaveScale = m_Wind.waveScale;
    cb.windWaveSpeed = m_Wind.waveSpeed;
    cb.windTime = m_WindTime;
    cb.gustSpeed = s.gustSpeed;
    cb.leanPerWind2 = s.leanPerWind2;
    cb.maxLean = s.maxLean;
    cb.freq = (std::max)(s.freq, 0.05f);
    cb.damping = s.damping;
    cb.windRef = s.windRef;
    cb.turbulence = s.turbulence;
    cb.blastAccel = s.blastAccel;
    cb.shakeDecay = s.shakeDecay;
    for (uint32_t i = 0; i < cb.gustCount; ++i) { cb.gustA[i] = gustA[i]; cb.gustB[i] = gustB[i]; }

    m_SwayCS->WriteBuffer(ctx, 0, &cb);
    m_SwayCS->Bind(ctx);
    m_SwayCS->SetSRV(ctx, "g_Areas", areas);
    m_SwayCS->SetSRV(ctx, "g_AreaStates", areaStates);
    m_SwayCS->SetSRV(ctx, "g_WindSources", windSources);
    WindGrid::BindCurrent(*m_SwayCS, ctx);
    m_SwayCS->SetUAV(ctx, "g_States", m_StateUAV.Get());
    m_SwayCS->BindUAVs(ctx);
    ctx->Dispatch((cb.treeCount + 63) / 64, 1, 1);
    m_SwayCS->UnbindSRVs(ctx);
    m_SwayCS->UnbindUAVs(ctx);
}

void WindTrees::FillCB(TreeCB& cb, const Matrix& viewProj, const Vector3& camPos) const
{
    const auto& s = m_Settings;
    cb.viewProj = viewProj;
    cb.camPos = camPos;
    cb.time = m_Time;
    cb.barkColor = s.barkColor;
    cb.branchIdle = s.branchIdle;
    cb.leafColor = s.leafColor;
    cb.branchWind = s.branchWind;
    cb.leafColor2 = s.leafColor2;
    cb.flutterIdle = s.flutterIdle;
    cb.bareColor = s.bareColor;
    cb.flutterWind = s.flutterWind;
    cb.crownMix = s.crownMix;
    cb.innerDark = s.innerDark;
    cb.leafWidth = s.leafWidth;
    cb.shakeAmp = s.shakeAmp;
    cb.leafStride = 1;
    cb.leafScale = 1.0f;
    cb.leafCount = (uint32_t)m_Stats.leaves;
}

void WindTrees::RenderDepth(ID3D11DeviceContext* ctx, const Matrix& view, const Matrix& proj, int cascade)
{
    if (!Ready() || !ctx || !m_BranchVS || !m_LeafVS || !m_LeafShadowPS) return;
    TreeCB cb = {};
    Matrix inv = view.Invert();
    FillCB(cb, view * proj, inv.Translation());
    // 遠い段は葉を間引き、面積が同じになるように大きくする
    uint32_t stride = 1;
    for (int c = 0; c < cascade; ++c) stride *= (uint32_t)std::clamp(m_Settings.shadowThin, 1, 4);
    cb.leafStride = stride;
    cb.leafScale = std::sqrt((float)stride);
    const UINT leafDraw = (UINT)((m_Stats.leaves + stride - 1) / stride);

    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 幹・枝（PS 無し）
    if (m_IndexBuf)
    {
        m_BranchVS->WriteBuffer(ctx, 0, &cb);
        m_BranchVS->Bind(ctx);
        m_BranchVS->SetSRV(ctx, "g_Verts", m_VertSRV.Get());
        m_BranchVS->SetSRV(ctx, "g_States", m_StateSRV.Get());
        ctx->PSSetShader(nullptr, nullptr, 0);
        ctx->IASetIndexBuffer(m_IndexBuf.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->DrawIndexed((UINT)m_Stats.indices, 0, 0);
        m_BranchVS->UnbindSRVs(ctx);
    }
    // 葉（形で切り抜く）
    if (m_Stats.leaves > 0)
    {
        m_LeafVS->WriteBuffer(ctx, 0, &cb);
        m_LeafVS->Bind(ctx);
        m_LeafShadowPS->Bind(ctx);
        m_LeafVS->SetSRV(ctx, "g_Leaves", m_LeafSRV.Get());
        m_LeafVS->SetSRV(ctx, "g_States", m_StateSRV.Get());
        ctx->DrawInstanced(6, leafDraw, 0, 0);
        m_LeafVS->UnbindSRVs(ctx);
        ctx->PSSetShader(nullptr, nullptr, 0);
    }
}

void WindTrees::RenderBranches(Renderer& renderer, CameraBase& camera)
{
    ID3D11DeviceContext* ctx = m_Context;
    if (!Ready() || !ctx || !m_BranchVS || !m_BranchPS || !m_IndexBuf) return;

    TreeCB cb = {};
    FillCB(cb, camera.GetViewMatrix() * camera.GetProjectionMatrix(), camera.GetPosition());
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = camera.GetPosition();

    m_BranchVS->WriteBuffer(ctx, 0, &cb);
    m_BranchPS->WriteBuffer(ctx, 0, &cb);
    m_BranchPS->WriteBuffer(ctx, 1, &light);
    m_BranchVS->Bind(ctx);
    m_BranchPS->Bind(ctx);
    m_BranchVS->SetSRV(ctx, "g_Verts", m_VertSRV.Get());
    m_BranchVS->SetSRV(ctx, "g_States", m_StateSRV.Get());
    PointLightManager::Get().BindPS(ctx);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(m_IndexBuf.Get(), DXGI_FORMAT_R32_UINT, 0);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(rs.DepthDefault(), 0);
    ctx->RSSetState(rs.CullNone());

    ctx->DrawIndexed((UINT)m_Stats.indices, 0, 0);

    m_BranchVS->UnbindSRVs(ctx);
    PointLightManager::Get().UnbindPS(ctx);
    rs.Restore(ctx);
}

void WindTrees::RenderLeaves(Renderer& renderer, CameraBase& camera)
{
    ID3D11DeviceContext* ctx = m_Context;
    if (!Ready() || !ctx || !m_LeafVS || !m_LeafPS || m_Stats.leaves <= 0) return;

    TreeCB cb = {};
    FillCB(cb, camera.GetViewMatrix() * camera.GetProjectionMatrix(), camera.GetPosition());
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = camera.GetPosition();

    m_LeafVS->WriteBuffer(ctx, 0, &cb);
    m_LeafPS->WriteBuffer(ctx, 0, &cb);
    m_LeafPS->WriteBuffer(ctx, 1, &light);
    m_LeafVS->Bind(ctx);
    m_LeafPS->Bind(ctx);
    m_LeafVS->SetSRV(ctx, "g_Leaves", m_LeafSRV.Get());
    m_LeafVS->SetSRV(ctx, "g_States", m_StateSRV.Get());
    PointLightManager::Get().BindPS(ctx);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(rs.Opaque(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(rs.DepthDefault(), 0);
    ctx->RSSetState(rs.CullNone());   // 葉は両面

    ctx->DrawInstanced(6, (UINT)m_Stats.leaves, 0, 0);

    m_LeafVS->UnbindSRVs(ctx);
    PointLightManager::Get().UnbindPS(ctx);
    rs.Restore(ctx);
}
