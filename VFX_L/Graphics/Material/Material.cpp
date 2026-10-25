#include "Graphics/Material/Material.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"

std::shared_ptr<Texture> Material::s_DefaultTextures[Material::SlotCount];

void Material::InitDefaultTextures(ID3D11Device* device)
{
    if (s_DefaultTextures[Albedo]) return;   // 作り済み（2 回目以降は何もしない）

    auto make = [&](uint8_t r, uint8_t g, uint8_t b)
        {
            auto t = std::make_shared<Texture>();
            t->CreateSolid(device, r, g, b, 255);
            return t;
        };

    // スロットごとの既定（テクスチャを持たない材質で使う 1x1。掛けても足しても結果が変わらない値）
    s_DefaultTextures[Albedo] = make(255, 255, 255);     // 白 = 頂点色のまま
    s_DefaultTextures[Normal] = make(128, 128, 255);     // 平らな法線 (0,0,1)
    s_DefaultTextures[Metallic] = make(0, 0, 0);         // 金属でない
    s_DefaultTextures[Roughness] = make(255, 255, 255);  // 粗さ 1（ざらざら）
    s_DefaultTextures[AO] = make(255, 255, 255);         // 遮蔽無し
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
            // テクスチャの無いスロットには既定（白・平らな法線など）を張る。前の材質のテクスチャを残さない
            Texture* tex = m_Textures[slot]
                ? m_Textures[slot].get()
                : s_DefaultTextures[slot].get();
            m_PS->SetTexture(context, slot, tex);
        }
    }
}
