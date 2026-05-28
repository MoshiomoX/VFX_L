#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

class Texture;

// ============================================
// シェーダー基底クラス
// ReflectionによるCBuffer自動作成・テクスチャスロット管理
// 子クラス: VertexShader / PixelShader / ComputeShader
// ============================================
class Shader
{
public:
    // シェーダー種別
    enum Kind
    {
        Vertex,
        Pixel,
        Compute,
    };

protected:
    Shader(Kind kind);

public:
    virtual ~Shader();

    // .csoファイルから読み込み（プリコンパイル済みバイナリ）
    HRESULT Load(ID3D11Device* device, const char* pFileName);

    // .hlslファイルからコンパイル（ランタイム）
    HRESULT Compile(ID3D11Device* device,
                    const std::wstring& path,
                    const std::string& entry = "main");

    // 定数バッファへデータ書き込み（スロット指定）
    void WriteBuffer(ID3D11DeviceContext* context, UINT slot, void* pData);

    // テクスチャ設定
    void SetTexture(ID3D11DeviceContext* context, UINT slot, Texture* tex);

    // シェーダーをパイプラインにバインド（子クラスで実装）
    virtual void Bind(ID3D11DeviceContext* context) = 0;

    // シェーダーのバインド解除（子クラスで実装）
    virtual void Unbind(ID3D11DeviceContext* context) = 0;

    // 有効チェック
    bool IsValid() const { return m_IsValid; }

    // 種別取得
    Kind GetKind() const { return m_Kind; }

private:
    // Reflection処理：CBuffer自動作成 + テクスチャスロット確保 + MakeShader呼出
    HRESULT Make(ID3D11Device* device, void* pData, UINT size);

protected:
    // 子クラスがシェーダーオブジェクトを作成する
    virtual HRESULT MakeShader(ID3D11Device* device,
                               void* pData, UINT size) = 0;

protected:
    Kind m_Kind;
    bool m_IsValid = false;

    // Reflectionで自動作成されたCBuffer配列
    std::vector<ComPtr<ID3D11Buffer>> m_Buffers;

    // テクスチャスロット配列
    std::vector<ID3D11ShaderResourceView*> m_Textures;
    std::vector<ComPtr<ID3D11Buffer>>    m_ConstantBuffers;   // 推荐使用这个
};