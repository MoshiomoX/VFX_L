// ============================================================
// Outline.h
// 画面のアウトライン（トゥーンレンダリング、2026-10-04 ユーザー指定：全画面のアウトライン + 二段の陰影 + 冷たい暗部、全部の物に）。
//   シーンの不透明な物（地形・木石・プレイヤー・雑魚）を描いた後、草・半透明・粒子の前に 1 回。
//   深度（MSAA の 0 番目の sample）から輪郭と出っ張った折れ目を探し、下の色に lineTint を掛けて暗くする
//   （乗算の混ぜ方。黒で塗らないので色付きの場所では色の濃い線になる）。
//   深度を SRV で読む間は DSV を外す → 終わったら Graphics::RestoreRenderTarget
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class VertexShader;
class PixelShader;
class CameraBase;
class Graphics;

class Outline
{
public:
    struct Params
    {
        bool  enabled = true;
        // 1 px だと 1600×900 でほとんど見えなかった（自動テストの拡大で確認）→ 2 px
        float thickness = 2.0f;      // 線の太さ（画面の高さ 1080 の時の px。解像度に比例）
        float threshold = 0.035f;    // 深度の段差がこの割合を超えたら線（小さいほど線が増える）
        float fadeStart = 35.0f;     // m。遠くは線を薄く（遠景の地面が線だらけにならない）
        float fadeEnd = 90.0f;
        float strength = 0.9f;
        DirectX::SimpleMath::Vector3 tint = { 0.06f, 0.05f, 0.10f };   // 線が下の色に掛ける色（線形。少し青紫）
        bool  debugView = false;     // 調整用：目からの距離を縞で見せる
    };

    bool Initialize(ID3D11Device* device);
    void Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera);

    Params& GetParams() { return m_Params; }
    bool IsReady() const { return m_VS && m_PS && m_Multiply; }
    void DrawImGui();

private:
    struct OutlineCB
    {
        float depthA, depthB, thickness, threshold;
        float fadeStart, fadeEnd, strength, debugView;
        DirectX::SimpleMath::Vector3 tint; float pad1;
    };
    static_assert(sizeof(OutlineCB) == 48, "OutlineCB layout mismatch");

    Params m_Params;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_Multiply;
};
