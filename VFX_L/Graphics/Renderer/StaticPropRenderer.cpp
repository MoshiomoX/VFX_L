// ============================================================
// StaticPropRenderer.cpp
// ============================================================
#include "Graphics/Renderer/StaticPropRenderer.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Light/PointLightManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Transform.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Manager/ResourceManager.h"
#include "Camera/CameraBase.h"
#include "ECS/View.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    constexpr UINT kInstanceSlot = 8;   // VS t8（StaticPropVS.hlsl）

    // HLSL の PropInstanceCB と同じ並び（16B）
    struct PropInstanceCB
    {
        uint32_t firstInstance;
        uint32_t _pad[3];
    };
    static_assert(sizeof(PropInstanceCB) == 16, "PropInstanceCB layout mismatch");

    // view * proj（行ベクトル規約）から視錐台の 6 平面を取る（法線は内向き・正規化済み）。
    // 切り取り空間の条件 -w<=x<=w, -w<=y<=w, 0<=z<=w をそのまま平面にするので、右手系・左手系を問わない
    void ExtractFrustumPlanes(const Matrix& m, Vector4 out[6])
    {
        const Vector4 c0(m._11, m._21, m._31, m._41);
        const Vector4 c1(m._12, m._22, m._32, m._42);
        const Vector4 c2(m._13, m._23, m._33, m._43);
        const Vector4 c3(m._14, m._24, m._34, m._44);
        out[0] = c3 + c0;   // 左
        out[1] = c3 - c0;   // 右
        out[2] = c3 + c1;   // 下
        out[3] = c3 - c1;   // 上
        out[4] = c2;        // 近
        out[5] = c3 - c2;   // 遠
        for (int i = 0; i < 6; ++i)
        {
            const float len = std::sqrt(out[i].x * out[i].x + out[i].y * out[i].y + out[i].z * out[i].z);
            if (len > 1e-6f) out[i] /= len;
        }
    }
}

bool StaticPropRenderer::Initialize(ID3D11Device* device)
{
    m_Device = device;
    m_VS = ResourceManager::Get().LoadVS(L"StaticPropVS", L"Shader/StaticPropVS.hlsl");
    if (!m_VS)
    {
        std::cout << "[Error] StaticPropVS load failed" << std::endl;
        return false;
    }
    m_FallbackMaterial = std::make_shared<Material>();
    m_FallbackMaterial->SetVertexShader(ResourceManager::Get().LoadVS(L"Default", L"Shader/VS.hlsl"));
    m_FallbackMaterial->SetPixelShader(ResourceManager::Get().LoadPS(L"Default", L"Shader/PS.hlsl"));
    return true;
}

void StaticPropRenderer::Shutdown()
{
    Clear();
    m_InstanceSRV.Reset();
    m_InstanceBuffer.Reset();
    m_Capacity = 0;
    m_VS.reset();
    m_FallbackMaterial.reset();
    m_Device = nullptr;
}

void StaticPropRenderer::Clear()
{
    m_Instances.clear();
    m_Groups.clear();
    m_Stats = {};
}

// ============================================================
// 登録: batched の実体を集めてモデル毎に並べる
// ============================================================
void StaticPropRenderer::Build(Registry& reg)
{
    Clear();

    struct Entry { std::shared_ptr<Model> model; Instance inst; };
    std::vector<Entry> entries;
    reg.CreateView<TransformComponent, ModelComponent>()
        .Each([&](Entity, TransformComponent& tf, ModelComponent& mc)
            {
                if (!mc.batched || !mc.visible || !mc.model) return;

                // RenderSystem と同じ Transform 経由で行列を作る（回転の規約を揃える）
                Transform t;
                t.SetPosition(tf.position);
                t.SetRotation(tf.rotation);
                t.SetScale(tf.scale);

                Instance in;
                in.world = t.GetWorldMatrix();
                const Vector3 lo = mc.model->GetBoundsMin();
                const Vector3 hi = mc.model->GetBoundsMax();
                in.center = Vector3::Transform((lo + hi) * 0.5f, in.world);
                const float s = (std::max)({
                    Vector3(in.world._11, in.world._12, in.world._13).Length(),
                    Vector3(in.world._21, in.world._22, in.world._23).Length(),
                    Vector3(in.world._31, in.world._32, in.world._33).Length() });
                in.radius = (hi - lo).Length() * 0.5f * s;
                entries.push_back({ mc.model, in });
            });

    std::stable_sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.model.get() < b.model.get(); });

    m_Instances.reserve(entries.size());
    for (const Entry& e : entries)
    {
        if (m_Groups.empty() || m_Groups.back().model != e.model)
        {
            Group g;
            g.model = e.model;
            g.first = (int)m_Instances.size();
            g.minRadius = g.maxRadius = e.inst.radius;
            m_Groups.push_back(g);
        }
        Group& g = m_Groups.back();
        ++g.count;
        g.minRadius = (std::min)(g.minRadius, e.inst.radius);
        g.maxRadius = (std::max)(g.maxRadius, e.inst.radius);
        m_Instances.push_back(e.inst);
    }

    m_Stats.registered = (int)m_Instances.size();
    m_Stats.models = (int)m_Groups.size();
    EnsureCapacity((int)m_Instances.size());

    std::cout << "[StaticProps] " << m_Stats.registered << " instances, " << m_Stats.models << " models" << std::endl;
    for (size_t i = 0; i < m_Groups.size(); ++i)
    {
        const Group& g = m_Groups[i];
        std::cout << "  #" << i << " x" << g.count << " radius " << g.minRadius << " - " << g.maxRadius << " m" << std::endl;
    }
}

bool StaticPropRenderer::EnsureCapacity(int count)
{
    if (count <= m_Capacity) return true;
    if (!m_Device) return false;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(Matrix) * count);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Matrix);

    Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
    if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &buf)))
    {
        std::cout << "[Error] StaticProps instance buffer create failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.FirstElement = 0;
    sd.Buffer.NumElements = (UINT)count;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_Device->CreateShaderResourceView(buf.Get(), &sd, &srv)))
    {
        std::cout << "[Error] StaticProps instance SRV create failed" << std::endl;
        return false;
    }

    m_InstanceBuffer = buf;
    m_InstanceSRV = srv;
    m_Capacity = count;
    return true;
}

// ============================================================
// 描画: 間引き → 見える分を詰める → モデル毎に instanced
// ============================================================
void StaticPropRenderer::Render(Renderer& renderer)
{
    m_Stats.drawn = 0;
    m_Stats.drawCalls = 0;

    CameraBase* cam = renderer.GetCamera();
    ID3D11DeviceContext* ctx = renderer.GetContext();
    if (!m_Settings.enabled || m_Instances.empty() || !cam || !ctx || !m_VS || !m_InstanceBuffer)
        return;

    const Matrix view = cam->GetViewMatrix();
    const Matrix proj = cam->GetProjectionMatrix();
    const Vector3 eye = cam->GetPosition();
    Vector4 planes[6];
    ExtractFrustumPlanes(view * proj, planes);

    // ---- 間引いて詰める ----
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(m_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    Matrix* dst = static_cast<Matrix*>(mapped.pData);

    const bool frustum = m_Settings.frustumCull;
    const float distPerRadius = m_Settings.distPerRadius;
    const float maxDist = m_Settings.maxDistance;
    int written = 0;
    for (Group& g : m_Groups)
    {
        g.drawStart = written;
        for (int i = g.first; i < g.first + g.count; ++i)
        {
            const Instance& in = m_Instances[i];
            const Vector3 d = in.center - eye;
            const float dist2 = d.LengthSquared();
            if (maxDist > 0.0f && dist2 > (maxDist + in.radius) * (maxDist + in.radius)) continue;
            if (distPerRadius > 0.0f)
            {
                const float lim = in.radius * distPerRadius;
                if (dist2 > lim * lim) continue;
            }
            if (frustum)
            {
                bool outside = false;
                for (int p = 0; p < 6 && !outside; ++p)
                {
                    const Vector4& pl = planes[p];
                    outside = pl.x * in.center.x + pl.y * in.center.y + pl.z * in.center.z + pl.w < -in.radius;
                }
                if (outside) continue;
            }
            dst[written++] = in.world;
        }
        g.drawCount = written - g.drawStart;
    }
    ctx->Unmap(m_InstanceBuffer.Get(), 0);
    m_Stats.drawn = written;
    if (written == 0) return;

    // ---- 共通の状態（Renderer::DrawMesh と同じ中身を、フレームに 1 回だけ）----
    MVPBuffer mvp;
    mvp.World = Matrix::Identity;   // StaticPropVS は使わない
    mvp.View = view;
    mvp.Projection = proj;
    m_VS->WriteBuffer(ctx, 0, &mvp);

    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = eye;
    DissolveCB dissolve = {};
    dissolve.threshold = -1.0f;   // 溶解無し

    ID3D11ShaderResourceView* srv = m_InstanceSRV.Get();
    ctx->VSSetShaderResources(kInstanceSlot, 1, &srv);
    ID3D11ShaderResourceView* noNoise = nullptr;
    ctx->PSSetShaderResources(5, 1, &noNoise);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    ctx->PSSetSamplers(0, 1, &samp);
    auto& lights = PointLightManager::Get();
    lights.BindPS(ctx);

    std::vector<PixelShader*> lit;   // 今フレーム光源 CB を書いた PS（数種類しか無い）
    for (const Group& g : m_Groups)
    {
        if (g.drawCount == 0) continue;
        PropInstanceCB icb = { (uint32_t)g.drawStart, { 0, 0, 0 } };
        m_VS->WriteBuffer(ctx, 1, &icb);

        for (const auto& sub : g.model->GetSubMeshes())
        {
            Material* mat = g.model->GetMaterial(sub.materialIndex);
            if (!mat || !mat->HasPS()) mat = m_FallbackMaterial.get();
            mat->Bind(ctx);     // PS + 貼図 t0-t4（材質の VS も積まれるので直後に差し替える）
            m_VS->Bind(ctx);    // VS + b0/b1

            PixelShader* ps = mat->GetPS();
            if (std::find(lit.begin(), lit.end(), ps) == lit.end())
            {
                ps->WriteBuffer(ctx, 0, &light);
                ps->WriteBuffer(ctx, 1, &dissolve);   // b1 を持たない PS では何もしない
                lit.push_back(ps);
            }
            sub.mesh->DrawInstanced(ctx, (UINT)g.drawCount);
            ++m_Stats.drawCalls;
        }
    }

    // 次フレームの Update で CS が UAV にするので外す（HAZARD 警告を出さない）
    lights.UnbindPS(ctx);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(kInstanceSlot, 1, &nullSRV);
}

// ============================================================
// 影図へ深度だけ
// ============================================================
void StaticPropRenderer::RenderDepth(ID3D11DeviceContext* ctx, const Matrix& view, const Matrix& proj)
{
    if (!m_Settings.enabled || m_Instances.empty() || !ctx || !m_VS || !m_InstanceBuffer)
        return;

    Vector4 planes[6];
    ExtractFrustumPlanes(view * proj, planes);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(m_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    Matrix* dst = static_cast<Matrix*>(mapped.pData);
    int written = 0;
    for (Group& g : m_Groups)
    {
        g.drawStart = written;
        for (int i = g.first; i < g.first + g.count; ++i)
        {
            const Instance& in = m_Instances[i];
            bool outside = false;
            for (int p = 0; p < 6 && !outside; ++p)
            {
                if (p == 4) continue;   // 近い面
                const Vector4& pl = planes[p];
                outside = pl.x * in.center.x + pl.y * in.center.y + pl.z * in.center.z + pl.w < -in.radius;
            }
            if (!outside) dst[written++] = in.world;
        }
        g.drawCount = written - g.drawStart;
    }
    ctx->Unmap(m_InstanceBuffer.Get(), 0);
    if (written == 0) return;

    MVPBuffer mvp;
    mvp.World = Matrix::Identity;
    mvp.View = view;
    mvp.Projection = proj;
    m_VS->WriteBuffer(ctx, 0, &mvp);

    ID3D11ShaderResourceView* srv = m_InstanceSRV.Get();
    ctx->VSSetShaderResources(kInstanceSlot, 1, &srv);
    m_VS->Bind(ctx);
    ctx->PSSetShader(nullptr, nullptr, 0);
    for (const Group& g : m_Groups)
    {
        if (g.drawCount == 0) continue;
        PropInstanceCB icb = { (uint32_t)g.drawStart, { 0, 0, 0 } };
        m_VS->WriteBuffer(ctx, 1, &icb);
        for (const auto& sub : g.model->GetSubMeshes())
            sub.mesh->DrawInstanced(ctx, (UINT)g.drawCount);
    }
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(kInstanceSlot, 1, &nullSRV);
}

// ============================================================
// ImGui（場面の Terrain 欄の中から呼ぶ）
// ============================================================
void StaticPropRenderer::DrawImGui()
{
    ImGui::Checkbox("Draw Props", &m_Settings.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Frustum Cull", &m_Settings.frustumCull);
    ImGui::DragFloat("Dist / Radius", &m_Settings.distPerRadius, 1.0f, 0.0f, 500.0f, "%.0f m per 1m");
    ImGui::DragFloat("Max Distance", &m_Settings.maxDistance, 1.0f, 0.0f, 500.0f, "%.0f m (0 = off)");
    ImGui::Text("props %d / %d drawn, %d models, %d draw calls",
        m_Stats.drawn, m_Stats.registered, m_Stats.models, m_Stats.drawCalls);
}
