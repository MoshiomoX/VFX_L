// ============================================================
// MiasmaField.cpp
// 瘴気の箱の作り・1 歩（描画とパネルは MiasmaFieldRender.cpp）
// ============================================================
#include "World/MiasmaField.h"
#include "World/GridWorld.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ShaderPath.h"

#include <Windows.h>
#include <DirectXPackedVector.h>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

static_assert(sizeof(MiasmaField::StepCB) == 160, "MiasmaStepCB layout mismatch");
static_assert(sizeof(MiasmaField::MarchCB) == 208, "MiasmaMarchCB layout mismatch");
static_assert(sizeof(MiasmaField::CompositeCB) == 32, "MiasmaCompositeCB layout mismatch");

namespace
{
    uint32_t Hash3(int x, int y, int z, uint32_t seed)
    {
        uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ seed * 2654435761u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return h;
    }
    float Hash01(int x, int y, int z, uint32_t seed) { return (Hash3(x, y, z, seed) & 0xFFFFFF) / 16777215.0f; }

    // 周期 period のマスで繰り返す値の雑音（0..1）。p はマスの単位
    float PeriodicValue(float px, float py, float pz, int period, uint32_t seed)
    {
        const int ix = (int)std::floor(px), iy = (int)std::floor(py), iz = (int)std::floor(pz);
        float fx = px - ix, fy = py - iy, fz = pz - iz;
        fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy); fz = fz * fz * (3.0f - 2.0f * fz);
        auto at = [&](int dx, int dy, int dz)
            { return Hash01((ix + dx) % period, (iy + dy) % period, (iz + dz) % period, seed); };
        const float x00 = at(0, 0, 0) + (at(1, 0, 0) - at(0, 0, 0)) * fx, x10 = at(0, 1, 0) + (at(1, 1, 0) - at(0, 1, 0)) * fx;
        const float x01 = at(0, 0, 1) + (at(1, 0, 1) - at(0, 0, 1)) * fx, x11 = at(0, 1, 1) + (at(1, 1, 1) - at(0, 1, 1)) * fx;
        const float y0 = x00 + (x10 - x00) * fy, y1 = x01 + (x11 - x01) * fy;
        return y0 + (y1 - y0) * fz;
    }

    // 周期 cells のマスで繰り返す Worley（一番近い点までの距離をマスの大きさで割る、0..~1）
    float PeriodicWorley(float px, float py, float pz, int cells, uint32_t seed)
    {
        const int ix = (int)std::floor(px), iy = (int)std::floor(py), iz = (int)std::floor(pz);
        float best = 9.0f;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
                    const int wx = (cx % cells + cells) % cells, wy = (cy % cells + cells) % cells, wz = (cz % cells + cells) % cells;
                    const float qx = cx + Hash01(wx, wy, wz, seed), qy = cy + Hash01(wx, wy, wz, seed + 11u), qz = cz + Hash01(wx, wy, wz, seed + 23u);
                    const float d = (qx - px) * (qx - px) + (qy - py) * (qy - py) + (qz - pz) * (qz - pz);
                    best = (std::min)(best, d);
                }
        return std::sqrt(best);
    }
}

// 繰り返す 3D の雑音（体積雲で使う「Perlin-Worley」に近い物）：値の雑音 3 段 + 反転 Worley 2 段。
// 雲のようにもこもこ（Worley）しつつ、つながった筋（値の雑音）もある。最後に 0..1 へ伸ばす
bool MiasmaField::BuildNoise()
{
    const int n = kNoise;
    std::vector<float> v((size_t)n * n * n);
    float lo = 1e9f, hi = -1e9f;
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float u = x / (float)n, w = y / (float)n, t = z / (float)n;
                const float val = 0.5f * PeriodicValue(u * 4, w * 4, t * 4, 4, 1u)
                    + 0.3f * PeriodicValue(u * 8, w * 8, t * 8, 8, 2u)
                    + 0.2f * PeriodicValue(u * 16, w * 16, t * 16, 16, 3u);
                const float wor = 0.65f * (1.0f - PeriodicWorley(u * 4, w * 4, t * 4, 4, 4u))
                    + 0.35f * (1.0f - PeriodicWorley(u * 8, w * 8, t * 8, 8, 5u));
                const float s = 0.45f * val + 0.55f * wor;
                v[((size_t)z * n + y) * n + x] = s;
                lo = (std::min)(lo, s);
                hi = (std::max)(hi, s);
            }
    std::vector<uint8_t> bytes(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        bytes[i] = (uint8_t)std::lround(255.0f * (v[i] - lo) / (std::max)(hi - lo, 1e-4f));

    D3D11_TEXTURE3D_DESC td = {};
    td.Width = td.Height = td.Depth = (UINT)n;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = { bytes.data(), (UINT)n, (UINT)(n * n) };
    Microsoft::WRL::ComPtr<ID3D11Texture3D> tex;
    return SUCCEEDED(m_Device->CreateTexture3D(&td, &sd, &tex))
        && SUCCEEDED(m_Device->CreateShaderResourceView(tex.Get(), nullptr, &m_NoiseSRV));
}

// 砂漠の砂塵の値（瘴気と同じ箱・同じ模擬。違うのは「湧く」代わりに「巻き上げられる」所）
MiasmaField::Settings MiasmaField::DustPreset()
{
    Settings s;
    s.base = 0.06f;          // 普段は地面すれすれの薄い砂の霞だけ
    s.layerH = 0.6f;
    s.noiseScale = 0.05f;
    s.lowBias = 1.0f;
    s.regrow = 0.3f;
    s.decay = 0.35f;         // 巻き上がった砂の雲が消える速さ（約 3 秒）
    s.settle = 0.35f;        // 砂は重いので落ちてくる（瘴気は 0）
    s.windShare = 0.8f;
    s.sourceMul = 1.2f;
    s.burnRate = 0.0f;       // 火では消えない
    s.blastPush = 16.0f;
    s.blastClear = 0.0f;     // 爆発は消すのではなく砂を巻き上げる
    s.liftRate = 1.8f;
    s.blastDust = 3.0f;
    s.liftHeight = 1.5f;
    s.extinction = 1.2f;
    s.colorLow = { 0.58f, 0.36f, 0.16f };   // 砂より少し白っぽい（灰色だと煙に見えた）
    s.colorHigh = { 0.72f, 0.52f, 0.30f };
    s.glow = 0.0f;
    s.bandSoft = 0.10f;
    s.detailScale = 0.2f;
    s.detailScale2 = 0.7f;
    s.erode = 0.75f;
    s.densRef = 0.7f;
    s.detailRise = 0.5f;
    return s;
}

void MiasmaField::SetKind(Kind kind)
{
    if (kind == m_Kind) return;
    m_Saved[(int)m_Kind] = m_Settings;
    m_Kind = kind;
    m_Settings = m_Saved[(int)m_Kind];
    m_NeedReset = true;
}

bool MiasmaField::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    if (!device || !context) return false;

    // TEMP-TEST: VFXL_MIASMA=0 で切る（比較用）。=1 は遺跡以外でも出す（シーンが見る）
    char env[8] = {};
    m_Saved[(int)Kind::Miasma] = m_Settings;
    m_Saved[(int)Kind::Dust] = DustPreset();
    if (GetEnvironmentVariableA("VFXL_MIASMA", env, sizeof(env)) > 0 && env[0] == '0')
        m_Settings.enabled = false;
    m_Saved[0].enabled = m_Saved[1].enabled = m_Settings.enabled;

    m_StepCS = std::make_shared<ComputeShader>();
    m_VS = std::make_shared<VertexShader>();
    m_MarchPS = std::make_shared<PixelShader>();
    m_CompositePS = std::make_shared<PixelShader>();
    bool ok = SUCCEEDED(ShaderPath::Load(m_StepCS.get(), device, L"Shader/Miasma/MiasmaStepCS.hlsl"))
        && SUCCEEDED(ShaderPath::Load(m_VS.get(), device, L"Shader/Sky/SkyVS.hlsl"))   // 全画面の三角形は空と同じ物
        && SUCCEEDED(ShaderPath::Load(m_MarchPS.get(), device, L"Shader/Miasma/MiasmaMarchPS.hlsl"))
        && SUCCEEDED(ShaderPath::Load(m_CompositePS.get(), device, L"Shader/Miasma/MiasmaCompositePS.hlsl"));

    // 濃さは 16 bit で足りる（0..8、1 歩の変化は 0.01 前後。延焼の燃料のような細かい戻りは無い）
    D3D11_TEXTURE3D_DESC td = {};
    td.Width = kNX;
    td.Height = kNY;
    td.Depth = kNZ;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R16_FLOAT;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    for (int i = 0; ok && i < 2; ++i)
    {
        ok = SUCCEEDED(device->CreateTexture3D(&td, nullptr, &m_Vol[i]))
            && SUCCEEDED(device->CreateShaderResourceView(m_Vol[i].Get(), nullptr, &m_VolSRV[i]))
            && SUCCEEDED(device->CreateUnorderedAccessView(m_Vol[i].Get(), nullptr, &m_VolUAV[i]));
    }

    // 乗算済みアルファ：結果 = 霧の色 + 下の色 × (1 − a)
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, &m_Blend));
    ok = ok && BuildNoise();

    std::cout << "[Miasma] " << (ok ? "OK" : "FAILED (no miasma)") << std::endl;
    if (!ok) Shutdown();
    return ok;
}

void MiasmaField::Shutdown()
{
    m_StepCS.reset();
    m_VS.reset();
    m_MarchPS.reset();
    m_CompositePS.reset();
    m_Blend.Reset();
    for (int i = 0; i < 2; ++i) { m_Vol[i].Reset(); m_VolSRV[i].Reset(); m_VolUAV[i].Reset(); }
    m_NoiseSRV.Reset();
    m_SolidSRV.Reset();
    m_Staging.Reset();
    m_HalfTex.Reset(); m_HalfRTV.Reset(); m_HalfSRV.Reset();
    m_HalfW = m_HalfH = 0;
    m_Ready = false;
}

void MiasmaField::Build(const GridWorld& grid)
{
    m_Ready = false;
    m_SolidSRV.Reset();
    if (!m_Device || !m_StepCS || !m_Vol[0]) return;
    m_MapOrigin = Vector2(grid.OriginX(), grid.OriginZ());
    m_MapSize = Vector2(grid.WorldWidth(), grid.WorldDepth());

    // 歩けないマス（遺跡の壁・柱・外周）。池は歩ける（霧が水の上を這う）
    const int w = grid.Width(), d = grid.Depth();
    std::vector<uint8_t> solid((size_t)w * d, 0);
    for (int gz = 0; gz < d; ++gz)
        for (int gx = 0; gx < w; ++gx)
            solid[(size_t)gz * w + gx] = grid.IsWalkable(gx, gz) ? 0 : 255;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)d;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = { solid.data(), (UINT)w, 0 };
    Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
    if (FAILED(m_Device->CreateTexture2D(&td, &sd, &t))
        || FAILED(m_Device->CreateShaderResourceView(t.Get(), nullptr, &m_SolidSRV)))
        return;
    m_HasOrigin = false;
    m_NeedReset = true;
    m_PendingDt = 0.0f;
    m_ClearOn = false;
    m_Ready = true;
}

void MiasmaField::SetWind(const WindState& wind, float time)
{
    const Vector3 v = wind.Velocity(time, m_Player.x, m_Player.z);
    m_Wind = Vector2(v.x, v.z);
}

void MiasmaField::SetCenter(const Vector3& player, const GridWorld& grid)
{
    m_Player = player;
    // 箱の角はセル（0.5m）に揃える。半ラグランジュの運びが前の箱との差を吸収するので、ずれは何セルでもよい
    const float half = 0.5f * kNX * kCell;
    Vector2 o(std::floor((player.x - half) / kCell) * kCell, std::floor((player.z - half) / kCell) * kCell);
    if (!m_HasOrigin) { m_Origin = m_PrevOrigin = o; m_HasOrigin = true; }
    else m_Origin = o;

    // 箱の中の地面の高さの範囲（光線を縦に切る。17 × 17 点で測る）
    float lo = 1e9f, hi = -1e9f;
    const float span = kNX * kCell;
    for (int j = 0; j <= 16; ++j)
        for (int i = 0; i <= 16; ++i)
        {
            const float h = grid.SampleHeight(m_Origin.x + span * i / 16.0f, m_Origin.y + span * j / 16.0f);
            lo = (std::min)(lo, h);
            hi = (std::max)(hi, h);
        }
    m_YMin = lo - 0.5f;   // 測った点の間の窪みの分
    m_YMax = hi + kTop + 0.5f;
}

void MiasmaField::Clear(const Vector3& pos, float radius)
{
    m_Clear = Vector4(pos.x, pos.z, radius, 1.0f);
    m_ClearOn = true;
}

void MiasmaField::Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* recipeHeat,
    ID3D11ShaderResourceView* windSources, ID3D11ShaderResourceView* height, ID3D11ShaderResourceView* sandState)
{
    if (!Active() || !m_Context || !m_HasOrigin || !areas || !areaStates || !recipeHeat || !windSources || !height) return;

    const auto& s = m_Settings;
    StepCB cb = {};
    cb.origin = m_Origin;
    cb.prevOrigin = m_PrevOrigin;
    cb.mapOrigin = m_MapOrigin;
    cb.mapSize = m_MapSize;
    cb.wind = m_Wind;
    cb.dt = (std::min)(m_PendingDt, 0.1f);   // 一時停止中（Tick 無し）は 0 = 止まる（箱が動いた分の運びだけ）
    cb.reset = m_NeedReset ? 1.0f : 0.0f;
    // 斑は世界の風と同じ速さで流す（目安と運ばれた霧が同じ向きに動く = 戻る力が風と喧嘩しない）
    m_NoiseOffset += m_Wind * (s.windShare * cb.dt);
    cb.noiseOffset = m_NoiseOffset;
    m_DetailOffset += Vector3(m_Wind.x * s.windShare, s.detailRise, m_Wind.y * s.windShare) * cb.dt;   // 細かい雑音も同じ風で流し、上へ立ち上らせる
    cb.noiseScale = s.noiseScale;
    cb.lowBias = s.lowBias;
    cb.base = s.base;
    cb.layerH = s.layerH;
    cb.regrow = s.regrow;
    cb.decay = s.decay;
    cb.settle = s.settle;
    cb.windShare = s.windShare;
    cb.sourceMul = s.sourceMul;
    cb.burnRate = s.burnRate;
    cb.blastPush = s.blastPush;
    cb.blastClear = s.blastClear;
    cb.wallHeight = s.wallHeight;
    cb.playerPush = s.playerPush;
    cb.player = m_Player;
    cb.playerRadius = s.playerRadius;
    cb.clear = m_ClearOn ? m_Clear : Vector4::Zero;
    cb.liftRate = sandState ? s.liftRate : 0.0f;
    cb.blastDust = s.blastDust;
    cb.liftHeight = s.liftHeight;
    m_PendingDt = 0.0f;
    m_ClearOn = false;
    m_NeedReset = false;

    const int next = 1 - m_Cur;
    m_StepCS->WriteBuffer(m_Context, 0, &cb);
    m_StepCS->Bind(m_Context);
    m_StepCS->SetSRV(m_Context, "g_Prev", m_VolSRV[m_Cur].Get());
    m_StepCS->SetSRV(m_Context, "g_Height", height);
    m_StepCS->SetSRV(m_Context, "g_Solid", m_SolidSRV.Get());
    m_StepCS->SetSRV(m_Context, "g_Areas", areas);
    m_StepCS->SetSRV(m_Context, "g_AreaStates", areaStates);
    m_StepCS->SetSRV(m_Context, "g_RecipeHeat", recipeHeat);
    m_StepCS->SetSRV(m_Context, "g_WindSources", windSources);
    m_StepCS->SetSRV(m_Context, "g_Lift", sandState);   // 砂塵：砂の面の舞う強さ（w）。瘴気は null = 0
    m_StepCS->SetUAV(m_Context, "g_Out", m_VolUAV[next].Get());
    m_StepCS->BindUAVs(m_Context);
    m_Context->Dispatch(kNX / 8, kNY / 4, kNZ / 8);
    m_StepCS->UnbindSRVs(m_Context);
    m_StepCS->UnbindUAVs(m_Context);
    m_Cur = next;
    m_PrevOrigin = m_Origin;
}

bool MiasmaField::DebugReadColumn(const Vector3& pos, float& sum, float& topH, float* cells) const
{
    sum = topH = 0.0f;
    if (!Active() || !m_Device || !m_Context) return false;
    const int ix = (int)std::floor((pos.x - m_Origin.x) / kCell);
    const int iz = (int)std::floor((pos.z - m_Origin.y) / kCell);
    if (ix < 0 || iz < 0 || ix >= kNX || iz >= kNZ) return false;
    if (!m_Staging)
    {
        D3D11_TEXTURE3D_DESC td = {};
        td.Width = 1;
        td.Height = kNY;
        td.Depth = 1;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R16_FLOAT;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_Device->CreateTexture3D(&td, nullptr, &m_Staging))) return false;
    }
    D3D11_BOX box = { (UINT)ix, 0, (UINT)iz, (UINT)ix + 1, (UINT)kNY, (UINT)iz + 1 };
    m_Context->CopySubresourceRegion(m_Staging.Get(), 0, 0, 0, 0, m_Vol[m_Cur].Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    for (int y = 0; y < kNY; ++y)
    {
        const uint16_t half = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData) + (size_t)y * m.RowPitch);
        const float v = DirectX::PackedVector::XMConvertHalfToFloat(half);
        if (cells) cells[y] = v;
        sum += v * kCellY;
        if (v > 0.15f) topH = (y + 0.5f) * kCellY;   // 霧が届いている一番上
    }
    m_Context->Unmap(m_Staging.Get(), 0);
    return true;
}

bool MiasmaField::DebugDumpSlice(const char* path, int y, float maxV) const
{
    if (!Active() || !m_Device || !m_Context || y < 0 || y >= kNY) return false;
    D3D11_TEXTURE3D_DESC td = {};
    td.Width = kNX;
    td.Height = 1;
    td.Depth = kNZ;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R16_FLOAT;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture3D> st;
    if (FAILED(m_Device->CreateTexture3D(&td, nullptr, &st))) return false;
    D3D11_BOX box = { 0, (UINT)y, 0, (UINT)kNX, (UINT)y + 1, (UINT)kNZ };
    m_Context->CopySubresourceRegion(st.Get(), 0, 0, 0, 0, m_Vol[m_Cur].Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(m_Context->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    std::vector<uint8_t> img((size_t)kNX * kNZ);
    for (int z = 0; z < kNZ; ++z)
        for (int x = 0; x < kNX; ++x)
        {
            const uint16_t h = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData) + (size_t)z * m.DepthPitch + (size_t)x * 2);
            const float v = DirectX::PackedVector::XMConvertHalfToFloat(h);
            img[(size_t)(kNZ - 1 - z) * kNX + x] = (uint8_t)(std::min)(255.0f, (std::max)(0.0f, v / maxV * 255.0f));   // 北が上
        }
    m_Context->Unmap(st.Get(), 0);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    fprintf(f, "P5\n%d %d\n255\n", kNX, kNZ);
    fwrite(img.data(), 1, img.size(), f);
    fclose(f);
    return true;
}
