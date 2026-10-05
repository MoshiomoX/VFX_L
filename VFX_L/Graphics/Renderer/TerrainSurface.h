// ============================================================
// TerrainSurface.h
// 戦闘の地形のテクスチャ（2026-10-03、ユーザー：地面が単色で寂しい → 3dtextures.me のスタイライズドしたテクスチャ、CC0）。
// 段々の地面（CreateSteppedGrid）と台地・坂・洞の天井の箱（HexahedronBatch）に、Shader/Terrain/TerrainPS の
// マテリアルを付ける。面毎に 5 つの層（地面 / 道 / 崖 / 洞の底 / 岩）から 1 つ、Texture2DArray の 1 層を
// 世界座標の三方向投影で貼る。層は頂点の UV.x（= 層 + 1。0 = 自動）か、法線と高さで自動に決まる。
// 色はテクスチャそのまま、元の頂点色の明るさの比（層毎の基準 refLum に対する）を薄く掛けて色むらを残す。
// 面（Biome）毎にテクスチャの組が違う（草原 = 草・土の道 / 砂漠 = 砂・砂岩 / 遺跡 = 石畳）。洞の底・岩は共通
// ============================================================
#pragma once
#include <d3d11.h>
#include <SimpleMath.h>
#include <memory>

class Model;
class Material;
class Texture;
class VertexShader;
class PixelShader;

class TerrainSurface
{
public:
    enum Layer { Ground = 0, Path, Cliff, CaveFloor, Rock, LayerCount };

    struct Params
    {
        bool  enabled = true;                              // false なら Apply しない（次の Regenerate から）
        float tile[LayerCount] = { 4.0f, 3.0f, 5.0f, 4.0f, 5.0f };   // テクスチャ 1 回分の m
        float tintStrength = 0.8f;    // 元の頂点色の明暗をどれだけ残すか（0 = テクスチャの色だけ）
        float matchBrightness = 1.0f; // テクスチャの平均の明るさを元の配色の明るさへ寄せる（1 = 揃える、0 = テクスチャのまま）
        float normalStrength = 1.0f;
        float flatNy = 0.97f;         // 法線の y がこれより上 = 平ら（地面 / 洞の底）
        float pathNy = 0.5f;          // これより上 = 坂（道）、下 = 崖 / 岩
        float caveY = -1.0f;          // 世界の y がこれより下 = 洞窟の中
        bool  flipNormalY = false;    // 法線の緑を逆に（DirectX 式の法線マップの時）
    };
    Params params;

    static TerrainSurface& Get();

    // 地形のモデルにテクスチャのマテリアルを付ける。biome = TerrainGenerator::Biome の番号、
    // refLum = 層毎の「元の頂点色の基準の明るさ」（その面の配色から）。失敗したら何もしない（頂点色のまま）
    bool Apply(ID3D11Device* device, Model& model, int biome, const float refLum[LayerCount]);

    // Terrain パネルの「Ground Textures」。変えたら定数を書き直す
    void DrawImGui();

private:
    static constexpr int kBiomes = 3;
    struct BiomeSet
    {
        std::shared_ptr<Texture> albedo, normal;
        std::shared_ptr<Material> material;
        float texLum[LayerCount] = {};   // 層毎のテクスチャの平均の明るさ（線形）
        bool tried = false;
    };

    bool EnsureShaders(ID3D11Device* device);
    bool EnsureBiome(ID3D11Device* device, int biome);
    void WriteCB();

    ID3D11Device* m_Device = nullptr;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;
    bool m_ShadersTried = false;
    BiomeSet m_Biomes[kBiomes];
    float m_RefLum[LayerCount] = { 0.2f, 0.3f, 0.2f, 0.12f, 0.25f };
    int m_Biome = 0;
    float m_Gain[LayerCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };   // パネルの表示用（WriteCB が出した明るさ合わせの倍率）
};
