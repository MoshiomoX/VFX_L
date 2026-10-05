// ============================================================
// VFXLiquidRenderer.h
// Liquid entry（CPU の経路）が毎フレーム Submit した液溜まりをまとめて描く（2026-10-02）。
//   1 つ = 地面の格子（LIQUID_GRID^2 の四角、SV_VertexID から組む。頂点バッファ無し）。
//   形・色は VFXLiquidPS（GPU の範囲と共用）。Submit された def は毎フレーム表に詰め直す。
//   地形を渡されていれば（戦闘シーン：群れの高さ場）格子の頂点を高さ場に載せ、崖は切る。
//   渡されていなければ（エディタ）instance の高さで平ら。
//   乗算済み alpha の混合（外側の光は alpha 0 で加算）。深度は読むだけ、両面。
//   呼ぶ所：不透明物・群れの後、連番画像・粒子の前
// ============================================================
#pragma once
#include "VFX_Editor/VFXLiquidDef.h"
#include "Swarm/SwarmTypes.h"
#include "Graphics/Light/LightTypes.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>
#include <cstdint>

class CameraBase;
class VertexShader;
class PixelShader;

// HLSL: VFXLiquidVS の LiquidInstance（48 bytes）
struct VFXLiquidInstance
{
    float    position[3] = {};    // 中心（world）。地形が無い時はこの y が地面
    float    radius = 2.0f;       // m
    float    dir[2] = {};         // 投げた向き（xz）。0 = seed で決めた角度
    float    age = 0.0f;          // 出てからの秒
    float    left = 1.0e4f;       // 残りの秒（大きい = 乾かない）
    uint32_t def = 0;             // def 表の何番目か（Submit が入れる）
    uint32_t seed = 0;
    float    _pad[2] = {};
};
static_assert(sizeof(VFXLiquidInstance) == 48, "LiquidInstance layout mismatch");

class VFXLiquidRenderer
{
public:
    bool Initialize(ID3D11Device* device);

    // def は 1 つずつ表に積む（同じ entry でも毎フレーム積み直す。数は少ない）
    void Submit(const VFXLiquidInstance& inst, const VFXLiquidDef& def);

    // 地形（群れの高さ場とその格子の定数）。毎フレーム呼ぶ。heights = null なら平ら
    void SetTerrain(ID3D11ShaderResourceView* heights, const Swarm::FrameCB& grid);

    // light は太陽・霧・影・カメラ位置（cameraPosition はここで入れ直す）。描いたら積んだ物は消える
    void Render(ID3D11DeviceContext* ctx, CameraBase* camera, const LightBuffer& light);
    size_t GetItemCount() const { return m_Items.size(); }

    // VFXLiquidVS / SwarmLiquidVS の b0
    struct CameraCB
    {
        DirectX::SimpleMath::Matrix viewProj;   // row_major なので Transpose しない
        uint32_t hasTerrain = 0;
        float    _pad[3] = {};
    };
    static_assert(sizeof(CameraCB) == 80, "LiquidCameraCB layout mismatch");
    // VFXLiquidPS の b1
    struct FrameCB
    {
        float time = 0.0f;
        float _pad[3] = {};
    };
    static_assert(sizeof(FrameCB) == 16, "LiquidFrameCB layout mismatch");

private:
    bool EnsureCapacity(ID3D11Device* device, size_t count);

    std::vector<VFXLiquidInstance> m_Items;
    std::vector<VFXLiquidDef>      m_Defs;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ItemBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ItemSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DefBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DefSRV;
    size_t m_Capacity = 0;

    ID3D11ShaderResourceView* m_Heights = nullptr;   // 借り物（群れが持つ）
    Swarm::FrameCB m_Grid;
};
