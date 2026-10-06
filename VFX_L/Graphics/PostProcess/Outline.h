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

        // ---- 陣営の線（2026-10-06、ユーザー：味方は少し太い青、敵は赤）----
        // 描く時にステンシルへ書いた番号（RenderSystem::kStencilFriend / kStencilEnemy）で線の色を選ぶ。
        // 線は手前側の画素に乗るので、その画素のステンシル = その物の陣営
        bool  factionEnabled = true;
        DirectX::SimpleMath::Vector3 friendColor = { 0.20f, 0.50f, 1.00f };   // 味方（線形）
        DirectX::SimpleMath::Vector3 enemyColor = { 1.00f, 0.10f, 0.08f };    // 敵
        float friendThicknessMul = 1.6f;   // 味方の線の太さ（thickness に掛ける）
        float enemyThicknessMul = 1.2f;
        float factionStrength = 1.0f;      // 陣営の線の濃さ（0〜1）
    };

    bool Initialize(ID3D11Device* device);
    void Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera);

    Params& GetParams() { return m_Params; }
    bool IsReady() const { return m_VS && m_PS && m_Blend; }
    void DrawImGui();

private:
    struct OutlineCB
    {
        float depthA, depthB, thickness, threshold;
        float fadeStart, fadeEnd, strength, debugView;
        DirectX::SimpleMath::Vector3 tint; float factionStrength;
        DirectX::SimpleMath::Vector3 friendColor; float friendThickness;   // 画素単位（thickness × 倍率）
        DirectX::SimpleMath::Vector3 enemyColor; float enemyThickness;
    };
    static_assert(sizeof(OutlineCB) == 80, "OutlineCB layout mismatch");

    Params m_Params;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;
    // 乗算済みアルファ（結果 = 出力 + 下の色 × (1 − a)）。線の色へ a だけ寄せる。
    // 2026-10-04 の第 1 版は乗算（下の色 × tint）だったが、青 / 赤の陣営の線は暗くするだけでは出せないので変えた
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_Blend;
};
