#pragma once
#include <d3d11.h>
#include <memory>
#include <vector>
#include <string>

struct aiScene;
class Material;
class VertexShader;

// ============================================================
// assimp の scene からマテリアル一式を組む。静的 / 骨付き共用。
//   - 各スロットを候補順に探す（埋め込み / 外部の両方）
//   - 法線 or 金属/粗さがあれば PBR、無ければ Lambert
//   - vsOverride を渡すと VS はそれ固定（骨付きは SkinnedVS）
// ============================================================
namespace MaterialLoader
{
    std::vector<std::shared_ptr<Material>> LoadFromScene(
        ID3D11Device* device, const aiScene* scene,
        const std::string& directory, const std::string& modelName,
        std::shared_ptr<VertexShader> vsOverride = nullptr);
}