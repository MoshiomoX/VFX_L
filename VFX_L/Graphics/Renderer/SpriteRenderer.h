// ============================================================
// SpriteRenderer.h
// 汎用 2D スプライト描画（インスタンシング、スクリーン座標）
// UI 全般で使い回す。Begin → Draw を複数回 → End の流れ。
// 同一テクスチャ内でまとめて 1 draw call になる。
// テクスチャが変わる時だけ自動で描画を区切る。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <SimpleMath.h>

class Texture;
class VertexShader;
class PixelShader;

class SpriteRenderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
        UINT maxSprites = 4096);
    void Shutdown();

    // 画面サイズを設定（リサイズ時に呼ぶ）
    void SetScreenSize(float width, float height);

    // 描画開始・終了
    void Begin();
    void End();

    // 矩形を描く（position は左上、ピクセル単位）
    void Draw(std::shared_ptr<Texture> tex,
        const DirectX::SimpleMath::Vector2& position,
        const DirectX::SimpleMath::Vector2& size,
        const DirectX::SimpleMath::Vector4& color = { 1, 1, 1, 1 },
        const DirectX::SimpleMath::Vector4& uvRect = { 0, 0, 1, 1 });
	// 回転付き。pivot は 0-1 の比率で指定（0,0=左上、1,1=右下）
    void Draw(std::shared_ptr<Texture> tex,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size,
        const DirectX::SimpleMath::Vector4& color,
        float radians,
        const DirectX::SimpleMath::Vector2& pivot);
    UINT GetLastDrawCalls() const { return m_LastDrawCalls; }
    UINT GetLastSpriteCount() const { return m_LastSpriteCount; }

private:
    // HLSL の SpriteInstance と一致（48 bytes）
    struct SpriteInstance
    {
        DirectX::SimpleMath::Vector2 position;
        DirectX::SimpleMath::Vector2 size;
        DirectX::SimpleMath::Vector4 color;
        DirectX::SimpleMath::Vector4 uvRect;

        DirectX::SimpleMath::Vector2 pivot = { 0.0f, 0.0f };
        float cosA = 1.0f;
        float sinA = 0.0f;
    };

    struct SpriteCB
    {
        DirectX::SimpleMath::Vector2 screenSize;
        DirectX::SimpleMath::Vector2 pad0;
    };

    void Flush();   // 溜めた分を 1 回で描画する

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_InstanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_InstanceSRV;

    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;

    std::vector<SpriteInstance> m_Batch;
    std::shared_ptr<Texture>    m_CurrentTexture;   // バッチ中のテクスチャ

    DirectX::SimpleMath::Vector2 m_ScreenSize = { 1600.0f, 900.0f };

    UINT m_MaxSprites = 0;
    UINT m_LastDrawCalls = 0;
    UINT m_LastSpriteCount = 0;
    bool m_InBegin = false;
};