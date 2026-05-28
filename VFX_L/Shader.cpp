#include "Shader.h"
#include "Texture.h"
#include <iostream>
#include <stdio.h>

// ============================================
// コンストラクタ
// ============================================
Shader::Shader(Kind kind)
    : m_Kind(kind)
{}

// ============================================
// デストラクタ（CBuffer解放）
// ============================================
Shader::~Shader()
{

    m_Buffers.clear();
    m_Textures.clear();
}

// ============================================
// .csoファイルから読み込み
// ============================================
HRESULT Shader::Load(ID3D11Device* device, const char* pFileName)
{
    if (!device || !pFileName)
        return E_INVALIDARG;

    // ファイルを開く
    FILE* fp = nullptr;
    fopen_s(&fp, pFileName, "rb");
    if (!fp)
    {
        std::cout << "[Error] Shader file not found: " << pFileName << std::endl;
        return E_FAIL;
    }

    // ファイルサイズ取得
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // メモリに読み込み
    char* pData = new char[fileSize];
    fread(pData, fileSize, 1, fp);
    fclose(fp);

    // シェーダー作成（Reflection含む）
    HRESULT hr = Make(device, pData, static_cast<UINT>(fileSize));

    delete[] pData;

    if (SUCCEEDED(hr))
    {
        m_IsValid = true;
        std::cout << "[OK] Shader loaded (cso): " << pFileName << std::endl;
    }
    else
    {
        std::cout << "[Error] Shader make failed: " << pFileName << std::endl;
    }

    return hr;
}

// ============================================
// .hlslファイルからランタイムコンパイル
// ============================================
HRESULT Shader::Compile(ID3D11Device* device,
    const std::wstring& path,
    const std::string& entry)
{
    if (!device)
        return E_INVALIDARG;

    // ターゲットプロファイル
    static const char* pTargetList[] =
    {
        "vs_5_0",   // Vertex
        "ps_5_0",   // Pixel
        "cs_5_0",   // Compute
    };

    // コンパイルフラグ
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(),
        pTargetList[m_Kind],
        flags, 0,
        &blob,
        &errorBlob);

    if (FAILED(hr))
    {
        std::wcout << L"[Error] Shader compile failed: " << path << std::endl;
        if (errorBlob)
        {
            std::cout << static_cast<char*>(errorBlob->GetBufferPointer()) << std::endl;
        }
        return hr;
    }

    // シェーダー作成（Reflection含む）
    hr = Make(device, blob->GetBufferPointer(), static_cast<UINT>(blob->GetBufferSize()));

    if (SUCCEEDED(hr))
    {
        m_IsValid = true;
        std::wcout << L"[OK] Shader compiled: " << path << std::endl;
    }

    return hr;
}

// ============================================
// Reflection処理
// CBuffer自動作成 + テクスチャスロット確保 + MakeShader呼出
// ============================================
HRESULT Shader::Make(ID3D11Device* device, void* pData, UINT size)
{
    HRESULT hr;

    ComPtr<ID3D11ShaderReflection> pReflection;
    hr = D3DReflect(pData, size, IID_PPV_ARGS(&pReflection));
    if (FAILED(hr))
    {
        std::cout << "[Error] D3DReflect failed" << std::endl;
        return hr;
    }

    D3D11_SHADER_DESC shaderDesc;
    pReflection->GetDesc(&shaderDesc);

    m_Buffers.clear();

    std::cout << "=== Shader Reflection ===" << std::endl;

    for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC bindDesc;
        pReflection->GetResourceBindingDesc(i, &bindDesc);

        if (bindDesc.Type == D3D_SIT_CBUFFER)
        {
            ID3D11ShaderReflectionConstantBuffer* cbuf =
                pReflection->GetConstantBufferByName(bindDesc.Name);

            D3D11_SHADER_BUFFER_DESC bufDesc;
            cbuf->GetDesc(&bufDesc);

            std::cout << "  [CBuffer] " << bindDesc.Name
                << " (b" << bindDesc.BindPoint
                << ", size=" << bufDesc.Size << ")" << std::endl;

            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = (bufDesc.Size + 15) & ~15;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

            ComPtr<ID3D11Buffer> buffer;
            hr = device->CreateBuffer(&bd, nullptr, &buffer);
            if (SUCCEEDED(hr))
            {
                // 按 register(bX) 索引存放（关键修复）
                if (m_Buffers.size() <= bindDesc.BindPoint)
                {
                    m_Buffers.resize(bindDesc.BindPoint + 1);
                }
                m_Buffers[bindDesc.BindPoint] = buffer;
            }
        }
        else if (bindDesc.Type == D3D_SIT_STRUCTURED)
        {
            std::cout << "  [StructuredBuffer] " << bindDesc.Name
                << " (t" << bindDesc.BindPoint << ")" << std::endl;
        }
        else if (bindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
            bindDesc.Type == D3D_SIT_UAV_RWTYPED)
        {
            std::cout << "  [UAV] " << bindDesc.Name
                << " (u" << bindDesc.BindPoint << ")" << std::endl;
        }
    }

    std::cout << "=========================" << std::endl;

    m_Textures.resize(shaderDesc.BoundResources, nullptr);

    return MakeShader(device, pData, size);
}
// ============================================
// CBufferへのデータ書き込み
// ============================================
void Shader::WriteBuffer(ID3D11DeviceContext* context, UINT slot, void* pData)
{
    if (!context || !pData)
        return;

    if (slot < m_Buffers.size() && m_Buffers[slot])
    {
        // 1. 更新 buffer 数据
        context->UpdateSubresource(m_Buffers[slot].Get(), 0, nullptr, pData, 0, 0);

        // 2. 根据 shader 类型，把 cbuffer 绑定到对应阶段
        switch (m_Kind)
        {
        case Compute:
            context->CSSetConstantBuffers(slot, 1, m_Buffers[slot].GetAddressOf());
            break;
        case Vertex:
            context->VSSetConstantBuffers(slot, 1, m_Buffers[slot].GetAddressOf());
            break;
        case Pixel:
            context->PSSetConstantBuffers(slot, 1, m_Buffers[slot].GetAddressOf());
            break;
        }
    }
}

// ============================================
// テクスチャ設定
// ============================================
void Shader::SetTexture(ID3D11DeviceContext* context, UINT slot, Texture* tex)
{
    if (!context)
        return;

    if (slot >= m_Textures.size())
        return;

    ID3D11ShaderResourceView* pSRV = tex ? tex->GetSRV() : nullptr;
    m_Textures[slot] = pSRV;

    // シェーダー種別に応じたスロットにバインド
    switch (m_Kind)
    {
    case Vertex:
        context->VSSetShaderResources(slot, 1, &pSRV);
        break;
    case Pixel:
        context->PSSetShaderResources(slot, 1, &pSRV);
        break;
    case Compute:
        context->CSSetShaderResources(slot, 1, &pSRV);
        break;
    }
}