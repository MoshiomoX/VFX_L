#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <functional>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

class Texture;

// 名前 → スロットの表を std::string を作らずに引くための hash（C++20 の異種キー検索）
struct ShaderNameHash
{
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

// ============================================
// シェーダー基底クラス
// Reflection による CBuffer 自動作成
// + SRV/UAV スロット自動検出・名前指定バインド
// ============================================
class Shader
{
public:
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

    HRESULT Load(ID3D11Device* device, const char* pFileName);
    HRESULT Compile(ID3D11Device* device,
        const std::wstring& path,
        const std::string& entry = "main");

    // CBuffer データ書き込み（スロット番号指定）
    void WriteBuffer(ID3D11DeviceContext* context, UINT slot, void* pData);

    // テクスチャ設定（スロット番号指定）
    void SetTexture(ID3D11DeviceContext* context, UINT slot, Texture* tex);

    // --- SRV バインド（名前指定 or スロット指定）---
    // 名前は string_view（文字列リテラルを渡しても毎回 std::string を作らない）
    void SetSRV(ID3D11DeviceContext* context, std::string_view name, ID3D11ShaderResourceView* srv);
    void SetSRV(ID3D11DeviceContext* context, UINT slot, ID3D11ShaderResourceView* srv);

    // --- UAV 登録（名前指定 or スロット指定、即時バインドしない）---
    // BindUAVs() で一括バインドする
    void SetUAV(ID3D11DeviceContext* context, std::string_view name,
        ID3D11UnorderedAccessView* uav, UINT initialCount = (UINT)-1);
    void SetUAV(ID3D11DeviceContext* context, UINT slot,
        ID3D11UnorderedAccessView* uav, UINT initialCount = (UINT)-1);

    // --- UAV 一括バインド（SetUAV で溜めたものをまとめてバインド）---
    void BindUAVs(ID3D11DeviceContext* context);

    // --- 一括解除（Dispatch/Draw 後に呼ぶ）---
    void UnbindSRVs(ID3D11DeviceContext* context);
    void UnbindUAVs(ID3D11DeviceContext* context);

    // スロット番号の逆引き（名前 → スロット、見つからなければ -1）
    int FindSRVSlot(std::string_view name) const;
    int FindUAVSlot(std::string_view name) const;

    // 検出済みスロット数の取得（デバッグ用）
    UINT GetSRVSlotCount() const { return m_MaxSRVSlot + 1; }
    UINT GetUAVSlotCount() const { return m_MaxUAVSlot + 1; }

    virtual void Bind(ID3D11DeviceContext* context) = 0;
    virtual void Unbind(ID3D11DeviceContext* context) = 0;

    bool IsValid() const { return m_IsValid; }
    Kind GetKind() const { return m_Kind; }

private:
    HRESULT Make(ID3D11Device* device, void* pData, UINT size);

protected:
    virtual HRESULT MakeShader(ID3D11Device* device,
        void* pData, UINT size) = 0;

protected:
    Kind m_Kind;
    bool m_IsValid = false;

    // CBuffer
    std::vector<ComPtr<ID3D11Buffer>> m_Buffers;
    std::vector<std::vector<uint8_t>> m_BufferShadow;   // 最後に WriteBuffer で書いた中身（同じなら書き直さない）

    // --- SRV スロット情報（Reflection で自動検出）---
    std::unordered_map<std::string, UINT, ShaderNameHash, std::equal_to<>> m_SRVSlotMap;
    UINT m_MaxSRVSlot = 0;
    bool m_HasSRV = false;   // t レジスタを 1 つでも使うか（SetTexture の範囲の判定）

    // --- UAV スロット情報（Reflection で自動検出）---
    std::unordered_map<std::string, UINT, ShaderNameHash, std::equal_to<>> m_UAVSlotMap;
    UINT m_MaxUAVSlot = 0;

    // --- UAV 一括バインド用キュー ---
    struct PendingUAV {
        UINT slot;
        ID3D11UnorderedAccessView* uav;
        UINT initialCount;
    };
    std::vector<PendingUAV> m_PendingUAVs;
};