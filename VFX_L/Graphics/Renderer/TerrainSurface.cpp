// ============================================================
// TerrainSurface.cpp
// ============================================================
#include "Graphics/Renderer/TerrainSurface.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/ShaderPath.h"
#include "imgui.h"

using DirectX::SimpleMath::Vector4;

namespace
{
    // Shader/Terrain/TerrainPS.hlsl の TerrainCB（b2）と同じ並び
    struct TerrainCB
    {
        Vector4 layer[TerrainSurface::LayerCount];   // x = m / 回、y = 基準の明るさ
        float tintStrength;
        float normalStrength;
        float flatNy;
        float pathNy;
        float caveY;
        float flipNormalY;
        float _pad[2];
    };
    static_assert(sizeof(TerrainCB) == 112, "TerrainCB layout mismatch");

    // 面毎のテクスチャ（Assets/Texture/Terrain/<名>_albedo.jpg / _normal.jpg）。層の順 = Layer
    // ユーザー 10-03 に推奨の割り当てで決定：草原 地面 G2・坂 D1 / 砂漠 地面 S1・崖 R2 / 洞窟 底 F2・壁 R3 / 遺跡 地面 F1、崖は R3
    const char* const kLayerFiles[3][TerrainSurface::LayerCount] =
    {
        { "Grass002", "Ground002", "CliffRock006", "StoneFloor010", "CliffRock006" },     // 草原
        { "Sand001", "Sand001", "CliffRock005", "StoneFloor010", "CliffRock006" },        // 砂漠
        { "StoneFloor007", "Ground002", "CliffRock006", "StoneFloor010", "CliffRock006" }, // 遺跡
    };

    std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
}

TerrainSurface& TerrainSurface::Get()
{
    static TerrainSurface s;
    return s;
}

bool TerrainSurface::EnsureShaders(ID3D11Device* device)
{
    if (m_VS && m_PS) return true;
    if (m_ShadersTried) return false;
    m_ShadersTried = true;
    m_VS = std::make_shared<VertexShader>();
    m_PS = std::make_shared<PixelShader>();
    if (FAILED(ShaderPath::Load(m_VS.get(), device, L"Shader/VS.hlsl"))
        || FAILED(ShaderPath::Load(m_PS.get(), device, L"Shader/Terrain/TerrainPS.hlsl")))
    {
        std::cout << "[TerrainSurface] shader load failed (ground stays vertex coloured)" << std::endl;
        m_VS.reset();
        m_PS.reset();
        return false;
    }
    return true;
}

bool TerrainSurface::EnsureBiome(ID3D11Device* device, int biome)
{
    BiomeSet& b = m_Biomes[biome];
    if (b.material) return true;
    if (b.tried) return false;
    b.tried = true;

    std::vector<std::wstring> albedo, normal;
    for (int i = 0; i < LayerCount; ++i)
    {
        const std::string base = std::string("Assets/Texture/Terrain/") + kLayerFiles[biome][i];
        albedo.push_back(Widen(base + "_albedo.jpg"));
        normal.push_back(Widen(base + "_normal.jpg"));
    }
    b.albedo = std::make_shared<Texture>();
    b.normal = std::make_shared<Texture>();
    std::vector<DirectX::XMFLOAT4> avg;
    if (!b.albedo->LoadArray(device, albedo, &avg) || !b.normal->LoadArray(device, normal))
    {
        std::cout << "[TerrainSurface] textures for biome " << biome << " failed (ground stays vertex coloured)" << std::endl;
        b.albedo.reset();
        b.normal.reset();
        return false;
    }
    // 平均色（sRGB）→ 線形の明るさ（PS の DecodeAlbedo と同じ 2.2 乗）
    for (int i = 0; i < LayerCount && i < (int)avg.size(); ++i)
    {
        const float r = std::pow(avg[i].x, 2.2f), g = std::pow(avg[i].y, 2.2f), bl = std::pow(avg[i].z, 2.2f);
        b.texLum[i] = 0.2126f * r + 0.7152f * g + 0.0722f * bl;
    }
    b.material = std::make_shared<Material>();
    b.material->SetVertexShader(m_VS);
    b.material->SetPixelShader(m_PS);
    b.material->SetAlbedoTexture(b.albedo);   // t0 = Texture2DArray
    b.material->SetNormalTexture(b.normal);   // t1
    std::cout << "[TerrainSurface] biome " << biome << ": " << LayerCount << " layers loaded" << std::endl;
    return true;
}

bool TerrainSurface::Apply(ID3D11Device* device, Model& model, int biome, const float refLum[LayerCount])
{
    if (!params.enabled || !device || biome < 0 || biome >= kBiomes) return false;
    // TEMP-TEST: VFXL_NO_TERRAIN_TEX で頂点色だけの地面（前後の比較用）
    {
        char* off = nullptr;
        size_t len = 0;
        if (_dupenv_s(&off, &len, "VFXL_NO_TERRAIN_TEX") == 0 && off)
        {
            free(off);
            return false;
        }
    }
    m_Device = device;
    if (!EnsureShaders(device) || !EnsureBiome(device, biome)) return false;

    m_Biome = biome;
    for (int i = 0; i < LayerCount; ++i) m_RefLum[i] = refLum[i];
    WriteCB();
    model.SetSingleMaterial(m_Biomes[biome].material);
    return true;
}

void TerrainSurface::WriteCB()
{
    if (!m_PS || !m_Device) return;
    TerrainCB cb = {};
    const BiomeSet& b = m_Biomes[m_Biome];
    for (int i = 0; i < LayerCount; ++i)
    {
        // 明るさ合わせ: テクスチャの平均を元の配色の明るさへ（色相はテクスチャのまま）。極端にならないよう 0.4〜2.5 倍
        const float match = (b.texLum[i] > 1e-4f) ? m_RefLum[i] / b.texLum[i] : 1.0f;
        const float gain = std::clamp(1.0f + (match - 1.0f) * params.matchBrightness, 0.4f, 2.5f);
        m_Gain[i] = gain;
        cb.layer[i] = Vector4(params.tile[i], m_RefLum[i], gain, 0.0f);
    }
    cb.tintStrength = params.tintStrength;
    cb.normalStrength = params.normalStrength;
    cb.flatNy = params.flatNy;
    cb.pathNy = params.pathNy;
    cb.caveY = params.caveY;
    cb.flipNormalY = params.flipNormalY ? 1.0f : 0.0f;

    ID3D11DeviceContext* ctx = nullptr;
    m_Device->GetImmediateContext(&ctx);
    if (!ctx) return;
    m_PS->WriteBuffer(ctx, 2, &cb);   // PixelShader::Bind が毎回 b2 も積む（書いた中身はそのまま残る）
    ctx->Release();
}

void TerrainSurface::DrawImGui()
{
    if (!ImGui::TreeNode("Ground Textures")) return;
    ImGui::Checkbox("Enabled (next Regenerate)", &params.enabled);
    ImGui::TextDisabled("3dtextures.me stylized, CC0 (Assets/Texture/Terrain). biome %d", m_Biome);
    bool changed = false;
    static const char* kNames[LayerCount] = { "Ground", "Path (ramps)", "Cliff", "Cave floor", "Rock (cave)" };
    for (int i = 0; i < LayerCount; ++i)
    {
        ImGui::PushID(i);
        changed |= ImGui::DragFloat(kNames[i], &params.tile[i], 0.05f, 0.5f, 30.0f, "%.2f m / tile");
        ImGui::SameLine();
        ImGui::TextDisabled("ref %.3f tex %.3f gain %.2f", m_RefLum[i], m_Biomes[m_Biome].texLum[i], m_Gain[i]);
        ImGui::PopID();
    }
    changed |= ImGui::SliderFloat("Match old brightness", &params.matchBrightness, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Tint (old colour patches)", &params.tintStrength, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Normal Strength", &params.normalStrength, 0.0f, 3.0f);
    changed |= ImGui::SliderFloat("Flat above ny", &params.flatNy, 0.5f, 1.0f);
    changed |= ImGui::SliderFloat("Path above ny", &params.pathNy, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Cave below y", &params.caveY, 0.1f, -20.0f, 20.0f);
    changed |= ImGui::Checkbox("Flip normal Y", &params.flipNormalY);
    if (changed) WriteCB();
    ImGui::TreePop();
}
