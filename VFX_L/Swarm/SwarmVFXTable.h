// ============================================================
// SwarmVFXTable.h
// VFXId → GPU 側の配方（Recipe）と各種テンプレート表。
//
// 起動時に VFXDatabase の全 JSON を読み、entry を種類ごとの表へ振り分ける:
//   Particle → GPUEmitter 表（今すぐ消費される）
//   Mesh     → モデル表（表だけ。描画は敵AIの後に接続）
//   Light    → 光源表（同上）
// Recipe は「この vfxType が各表のどこからどこまでを持つか」。
//
// GPU 側の効果には時間軸が無い（一弾一スレッド、無状態）。
//   entry の startTime / duration は無視される。
//   ずれている JSON は読み込み時に警告し、VFXEffect に印を付けて
//   編集器が表示できるようにする。
// ============================================================
#pragma once
#include "VFX_Editor/VFXId.h"
#include "Particle/GPUParticleEmitter.h"
#include "VFX_Editor/SpriteSheets.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

class GPUParticleSystem;

namespace Swarm
{
    // Shader/Common/SwarmCommon.hlsli の SwarmRecipe と一致（32 bytes）
    struct VFXRecipe
    {
        uint32_t particleStart = 0;
        uint32_t particleCount = 0;
        uint32_t modelStart = 0;
        uint32_t modelCount = 0;
        uint32_t lightStart = 0;
        uint32_t lightCount = 0;
        uint32_t spriteStart = 0;   // Sprite entry の表（GPU の範囲だけが使う。元は _pad）
        uint32_t spriteCount = 0;
    };
    static_assert(sizeof(VFXRecipe) == 32, "SwarmRecipe layout mismatch");

    // Shader/Common/SwarmSprite.hlsli の SwarmSpriteDef（80 bytes）。Sprite entry 1 つ分
    struct VFXSpriteDef
    {
        float    offset[3] = {};   // 範囲の中心からのずらし
        float    height = 2.0f;    // m
        float    color[4] = { 1, 1, 1, 1 };
        float    cellUV[2] = {};   // 1 コマの大きさ（配列の 1 枚に対する uv）
        float    pivot[2] = { 0.5f, 0.5f };
        uint32_t cols = 1;
        uint32_t frameCount = 1;
        float    frameTime = 0.05f;   // 速さ込みの秒 / コマ
        float    life = 1.0f;         // 1 回の再生が生きる秒数
        uint32_t facing = 0;
        uint32_t flags = 0;           // bit0 加算 / bit1 繰り返す / bits 8-15 配列の何枚目か
        float    rotation = 0.0f;     // ラジアン
        float    aspect = 1.0f;       // 幅 / 高さ
    };
    static_assert(sizeof(VFXSpriteDef) == 80, "SwarmSpriteDef layout mismatch");

    // SwarmSprite（32 bytes）。再生中の 1 つ
    struct SpriteInstance
    {
        float    position[3] = {};
        float    age = 0.0f;
        uint32_t def = 0;
        uint32_t alive = 0;
        float    sizeScale = 0.0f;   // 範囲の大きさの倍率（拡大鏡）。0 = 1
        uint32_t _pad = 0;
    };
    static_assert(sizeof(SpriteInstance) == 32, "SwarmSprite layout mismatch");

    // ---- 将来の表（今は形だけ。描画側が出来たら埋める）----
    struct VFXModelEntry
    {
        uint32_t meshId = 0;
        float    scale = 1.0f;
        float    _pad[2] = {};
        float    offset[4] = {};
    };
    static_assert(sizeof(VFXModelEntry) == 32, "VFXModelEntry layout mismatch");

    struct VFXLightEntry
    {
        float color[4] = { 1, 1, 1, 1 };
        float radius = 3.0f;
        float intensity = 1.0f;
        float _pad[2] = {};
    };
    static_assert(sizeof(VFXLightEntry) == 32, "VFXLightEntry layout mismatch");
}

class SwarmVFXTable
{
public:
    // VFXDatabase を全部読んで表を組み、GPU へ上げる。
    // colorKey は particles の静的区へ登録する
    bool Build(ID3D11Device* device, GPUParticleSystem* particles);

    // VFXId → Recipe 表の index。無ければ 0（None の空配方）
    uint32_t IndexOf(VFXId id) const;

    ID3D11ShaderResourceView* GetRecipeSRV()   const { return m_RecipeSRV.Get(); }
    ID3D11ShaderResourceView* GetEmitterSRV()  const { return m_EmitterSRV.Get(); }
    ID3D11ShaderResourceView* GetModelSRV()    const { return m_ModelSRV.Get(); }
    ID3D11ShaderResourceView* GetLightSRV()    const { return m_LightSRV.Get(); }
    ID3D11ShaderResourceView* GetSpriteDefSRV()   const { return m_SpriteDefSRV.Get(); }
    ID3D11ShaderResourceView* GetSpriteArraySRV() const { return m_SpriteArraySRV.Get(); }
    int GetSpriteDefCount() const { return m_SpriteDefCount; }

    // ImGui 表示用
    int GetRecipeCount()  const { return (int)m_Index.size(); }
    int GetEmitterCount() const { return m_EmitterCount; }
    int GetWarningCount() const { return m_Warnings; }

private:
    bool UploadImmutable(ID3D11Device* device, const void* data, UINT stride, UINT count,
        Microsoft::WRL::ComPtr<ID3D11Buffer>& buf,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv, const char* name);
    // Sprite entry が使う貼图を Texture2DArray にまとめる（1 枚 = 1 貼图、左上に詰める）
    bool BuildSpriteArray(ID3D11Device* device, const std::vector<const SpriteSheets::Info*>& sheets);

    std::vector<std::pair<VFXId, uint32_t>> m_Index;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_RecipeBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_RecipeSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EmitterBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_EmitterSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ModelBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ModelSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_LightBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_LightSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpriteDefBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpriteDefSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_SpriteArray;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpriteArraySRV;
    int m_SpriteDefCount = 0;
    int m_SpriteSliceW = 1;
    int m_SpriteSliceH = 1;

    int m_EmitterCount = 0;
    int m_Warnings = 0;
};