#include "Material.h"
#include "VertexShader.h"
#include "PixelShader.h"
#include "Texture.h"

std::shared_ptr<Texture> Material::s_DefaultTextures[Material::SlotCount];

void Material::InitDefaultTextures(ID3D11Device* device)
{
    if (s_DefaultTextures[Albedo]) return;   // 既に生成済みなら何もしない

    auto make = [&](uint8_t r, uint8_t g, uint8_t b)
        {
            auto t = std::make_shared<Texture>();
            t->CreateSolid(device, r, g, b, 255);
            return t;
        };

    // ★slotの意味に応じた安全な既定値（ここが重要）
    s_DefaultTextures[Albedo] = make(255, 255, 255);  // 白：色を変えない
    s_DefaultTextures[Normal] = make(128, 128, 255);  // 平坦法線 (0,0,1)
    s_DefaultTextures[Metallic] = make(0, 0, 0);    // 非金属
    s_DefaultTextures[Roughness] = make(255, 255, 255);  // 全ラフ（鏡面化を防ぐ）
    s_DefaultTextures[AO] = make(255, 255, 255);  // 遮蔽なし（環境光を消さない）
}
void Material::Bind(ID3D11DeviceContext* context)
{
    if (!context) return;

    if (m_VS) m_VS->Bind(context);
    if (m_PS) m_PS->Bind(context);

    if (m_PS)
    {
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            // テクスチャが無いslotはデフォルト（白/平坦法線/黒など）で埋める
            Texture* tex = m_Textures[slot]
                ? m_Textures[slot].get()
                : s_DefaultTextures[slot].get();
            m_PS->SetTexture(context, slot, tex);
        }
    }
}
