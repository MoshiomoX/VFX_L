// ============================================================
// AmbientOcclusion.h
// 画面空間の環境遮蔽 SSAO（2026-11-02。ユーザー：トゥーンの上に SSAO → 「明暗の境を押す」やり方で）。
//   不透明な物（地形・置物・プレイヤー・雑魚）を描いた深度から、窪み・接地の遮蔽を R8 の 1 枚にする。
//   草は深度に入る前に撮るので遮蔽を作らず、受けるだけ。
//   前向きレンダリングは幾何を描きながら陰影を付けるので、同じフレームの AO は間に合わない →
//   このフレームで作った物を次のフレームの陰影が読む（LightBuffer::aoViewProj で世界座標を前のフレームの画面へ
//   投影するので、動かない物はぴったり合う。動く物は 1 フレーム遅れる）。2 枚を交互に使う。
//   使い方（LightBuffer と Lighting.hlsli の SceneAO）：
//     トゥーン = 遮蔽が明暗の境（toonParams.y）を borderShift × (1 − ao) だけ明るい側へ押す = 窪みが影の段に入る。
//              暗い側の環境光にも ambientMul × 分だけ掛ける
//     写実   = 環境光に ambientMul 分だけ掛ける
//   シーン：描画の最初に BindForShading（t12 と LightBuffer）、アウトラインの後に Render、最後に Unbind、
//   抜ける時に Disable（Renderer は他のシーンと共用）
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
class Renderer;

class AmbientOcclusion
{
public:
    struct Params
    {
        bool  enabled = true;
        float radius = 0.8f;         // 遮蔽を探す半径（世界 m）
        float bias = 0.03f;          // 自分の面を遮蔽と見なさない余裕（m）
        float strength = 1.3f;       // 遮蔽の濃さ（0..2）
        float maxDistance = 70.0f;   // ここより遠い画素は作らない（m）
        float blurTolerance = 0.06f; // ぼかしで隣を混ぜる深度の差（中心の深度に対する割合）
        float borderShift = 0.45f;   // トゥーン：全部遮蔽された所で明暗の境をどれだけ明るい側へ押すか（N・L の単位）
        float ambientMul = 0.3f;     // 環境光に掛ける割合（0 = 掛けない、1 = ao そのまま）
        bool  debugView = false;     // 調整用：ao を灰色で表示
    };

    static constexpr UINT kSlot = 12;   // PS の t12（Lighting.hlsli の g_SceneAO）

    bool Initialize(ID3D11Device* device);
    // 描画の最初：前のフレームの結果を t12 に張り、LightBuffer に行列と係数を入れる
    void BindForShading(ID3D11DeviceContext* context, Renderer& renderer);
    // 不透明な物の後（アウトラインの後、草の前）：今の深度から次のフレームの分を作る
    void Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera);
    // フレームの最後（次のフレームで RTV にする前に外す）
    void Unbind(ID3D11DeviceContext* context);
    // シーンを抜ける時：LightBuffer の aoParams を 0 に（他のシーンは読まない）
    void Disable(Renderer& renderer);

    Params& GetParams() { return m_Params; }
    bool IsReady() const { return m_VS && m_PS && m_BlurPS; }
    void DrawImGui();

private:
    bool EnsureTargets(ID3D11Device* device, UINT width, UINT height);

    struct SSAOCB
    {
        float depthA, depthB, proj11, proj22;
        float radius, bias, strength, maxDistance;
        DirectX::SimpleMath::Matrix invView;   // view → world（核の回転を世界に固定する。2026-11-02）
    };
    static_assert(sizeof(SSAOCB) == 96, "SSAOCB layout mismatch");
    struct BlurCB
    {
        float depthA, depthB, tolerance, pad;
    };
    static_assert(sizeof(BlurCB) == 16, "SSAOBlurCB layout mismatch");

    Params m_Params;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;
    std::shared_ptr<PixelShader>  m_BlurPS;

    // 生の AO（ぼかしの入力）と、ぼかした結果 2 枚（今作る物 / 前のフレームの物 = 陰影が読む）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_RawTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RawRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_RawSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Tex[2];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RTV[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV[2];
    DirectX::SimpleMath::Matrix m_ViewProj[2];   // それぞれを作った時の view × proj
    bool m_Valid[2] = { false, false };
    int  m_Cur = 0;                               // 陰影が読む方
    UINT m_Width = 0, m_Height = 0;
};
