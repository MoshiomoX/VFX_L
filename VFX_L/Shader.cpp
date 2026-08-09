#include "Shader.h"
#include "Texture.h"
#include <iostream>
#include <stdio.h>
#include <algorithm>

// ============================================
// コンストラクタ
// ============================================
Shader::Shader(Kind kind)
    : m_Kind(kind)
{}

// ============================================
// デストラクタ
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

    FILE* fp = nullptr;
    fopen_s(&fp, pFileName, "rb");
    if (!fp)
    {
        std::cout << "[Error] Shader file not found: " << pFileName << std::endl;
        return E_FAIL;
    }

    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* pData = new char[fileSize];
    fread(pData, fileSize, 1, fp);
    fclose(fp);

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

    static const char* pTargetList[] =
    {
        "vs_5_0",
        "ps_5_0",
        "cs_5_0",
    };

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

    hr = Make(device, blob->GetBufferPointer(), static_cast<UINT>(blob->GetBufferSize()));

    if (SUCCEEDED(hr))
    {
        m_IsValid = true;
        std::wcout << L"[OK] Shader compiled: " << path << std::endl;
    }

    return hr;
}

// ============================================
// Reflection 処理
// CBuffer 自動作成 + SRV/UAV スロット自動検出
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
    m_SRVSlotMap.clear();
    m_UAVSlotMap.clear();
    m_MaxSRVSlot = 0;
    m_MaxUAVSlot = 0;

    std::cout << "=== Shader Reflection ===" << std::endl;

    for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC bindDesc;
        pReflection->GetResourceBindingDesc(i, &bindDesc);

        // --- CBuffer：自動作成 ---
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
                if (m_Buffers.size() <= bindDesc.BindPoint)
                {
                    m_Buffers.resize(bindDesc.BindPoint + 1);
                }
                m_Buffers[bindDesc.BindPoint] = buffer;
            }
        }
        // --- SRV：スロット情報を記録（t レジスタ）---
        else if (bindDesc.Type == D3D_SIT_STRUCTURED ||
            bindDesc.Type == D3D_SIT_TEXTURE ||
            bindDesc.Type == D3D_SIT_TBUFFER)
        {
            std::cout << "  [SRV] " << bindDesc.Name
                << " (t" << bindDesc.BindPoint << ")" << std::endl;

            m_SRVSlotMap[bindDesc.Name] = bindDesc.BindPoint;
            if (bindDesc.BindPoint > m_MaxSRVSlot)
                m_MaxSRVSlot = bindDesc.BindPoint;
        }
        // --- UAV：スロット情報を記録（u レジスタ）---
        else if (bindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
            bindDesc.Type == D3D_SIT_UAV_RWTYPED ||
            bindDesc.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
            bindDesc.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
            bindDesc.Type == D3D_SIT_UAV_RWBYTEADDRESS)
        {
            std::cout << "  [UAV] " << bindDesc.Name
                << " (u" << bindDesc.BindPoint << ")" << std::endl;

            m_UAVSlotMap[bindDesc.Name] = bindDesc.BindPoint;
            if (bindDesc.BindPoint > m_MaxUAVSlot)
                m_MaxUAVSlot = bindDesc.BindPoint;
        }
    }

    std::cout << "=========================" << std::endl;

    m_Textures.resize(shaderDesc.BoundResources, nullptr);

    return MakeShader(device, pData, size);
}

// ============================================
// CBuffer データ書き込み
// ============================================
void Shader::WriteBuffer(ID3D11DeviceContext* context, UINT slot, void* pData)
{
    if (!context || !pData)
        return;

    if (slot < m_Buffers.size() && m_Buffers[slot])
    {
        context->UpdateSubresource(m_Buffers[slot].Get(), 0, nullptr, pData, 0, 0);

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

// ============================================
// SRV バインド（名前指定）
// ============================================
void Shader::SetSRV(ID3D11DeviceContext* context, const std::string& name, ID3D11ShaderResourceView* srv)
{
    int slot = FindSRVSlot(name);
    if (slot < 0)
    {
        std::cout << "[Warning] SRV not found: " << name << std::endl;
        return;
    }
    SetSRV(context, static_cast<UINT>(slot), srv);
}

// ============================================
// SRV バインド（スロット指定、即時）
// ============================================
void Shader::SetSRV(ID3D11DeviceContext* context, UINT slot, ID3D11ShaderResourceView* srv)
{
    if (!context) return;

    switch (m_Kind)
    {
    case Vertex:
        context->VSSetShaderResources(slot, 1, &srv);
        break;
    case Pixel:
        context->PSSetShaderResources(slot, 1, &srv);
        break;
    case Compute:
        context->CSSetShaderResources(slot, 1, &srv);
        break;
    }
}

// ============================================
// UAV 登録（名前指定、キューに溜める）
// ============================================
void Shader::SetUAV(ID3D11DeviceContext* context, const std::string& name,
    ID3D11UnorderedAccessView* uav, UINT initialCount)
{
    int slot = FindUAVSlot(name);
    if (slot < 0)
    {
        std::cout << "[Warning] UAV not found: " << name << std::endl;
        return;
    }
    SetUAV(context, static_cast<UINT>(slot), uav, initialCount);
}

// ============================================
// UAV 登録（スロット指定、キューに溜める）
// ============================================
void Shader::SetUAV(ID3D11DeviceContext* context, UINT slot,
    ID3D11UnorderedAccessView* uav, UINT initialCount)
{
    m_PendingUAVs.push_back({ slot, uav, initialCount });
}

// ============================================
// UAV 一括バインド（溜めたキューをまとめて CSSetUnorderedAccessViews）
// AppendStructuredBuffer の内部カウンタを壊さないために必須
// ============================================
void Shader::BindUAVs(ID3D11DeviceContext* context)
{
    if (m_PendingUAVs.empty()) return;
    if (!context) { m_PendingUAVs.clear(); return; }

    // スロット範囲を求める
    UINT minSlot = m_PendingUAVs[0].slot;
    UINT maxSlot = m_PendingUAVs[0].slot;
    for (auto& p : m_PendingUAVs)
    {
        if (p.slot < minSlot) minSlot = p.slot;
        if (p.slot > maxSlot) maxSlot = p.slot;
    }

    UINT count = maxSlot - minSlot + 1;

    // 配列を初期化（nullptr = バインドしない、initialCount = -1 = リセットしない）
    std::vector<ID3D11UnorderedAccessView*> uavs(count, nullptr);
    std::vector<UINT> initialCounts(count, (UINT)-1);

    // キューの内容を配列に埋め込む
    for (auto& p : m_PendingUAVs)
    {
        UINT idx = p.slot - minSlot;
        uavs[idx] = p.uav;
        initialCounts[idx] = p.initialCount;
    }

    // 一括バインド
    context->CSSetUnorderedAccessViews(minSlot, count, uavs.data(), initialCounts.data());

    m_PendingUAVs.clear();
}

// ============================================
// SRV 一括解除
// ============================================
void Shader::UnbindSRVs(ID3D11DeviceContext* context)
{
    if (!context) return;

    for (UINT i = 0; i <= m_MaxSRVSlot; ++i)
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        switch (m_Kind)
        {
        case Vertex:
            context->VSSetShaderResources(i, 1, &nullSRV);
            break;
        case Pixel:
            context->PSSetShaderResources(i, 1, &nullSRV);
            break;
        case Compute:
            context->CSSetShaderResources(i, 1, &nullSRV);
            break;
        }
    }
}

// ============================================
// UAV 一括解除
// ============================================
void Shader::UnbindUAVs(ID3D11DeviceContext* context)
{
    if (!context) return;

    for (UINT i = 0; i <= m_MaxUAVSlot; ++i)
    {
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        UINT keep = (UINT)-1;
        context->CSSetUnorderedAccessViews(i, 1, &nullUAV, &keep);
    }
}

// ============================================
// スロット逆引き
// ============================================
int Shader::FindSRVSlot(const std::string& name) const
{
    auto it = m_SRVSlotMap.find(name);
    return (it != m_SRVSlotMap.end()) ? static_cast<int>(it->second) : -1;
}

int Shader::FindUAVSlot(const std::string& name) const
{
    auto it = m_UAVSlotMap.find(name);
    return (it != m_UAVSlotMap.end()) ? static_cast<int>(it->second) : -1;
}