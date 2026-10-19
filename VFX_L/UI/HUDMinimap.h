// ============================================================
// HUDMinimap.h
// 右上の丸いミニマップ（2026-10-19、ユーザー「右上に小さい地図を」）。
//   見せる物 = 地形の縮図（平原 / 山頂 / 洞窟・崖・台地・塞がった所）+ プレイヤーの矢印 + 目印（箱・門・磁石・エリート・Boss。
//   画面外の矢印と同じ色）。北が上で地図は回らず、プレイヤーの矢印が向きに合わせて回る。半径 minimapRange（m）の窓。
//
// 作り：
//   地形の絵（線形の色、0.5m / texel）は World/MinimapBake が地形を建てた時に 1 回焼く（Bake）。
//   毎フレーム、プレイヤーを中心にした窓を CPU で切り出し（双線形 + 円のマスク）、小さな DYNAMIC テクスチャへ書いて
//   SpriteRenderer で 1 枚描く。SpriteRenderer にはマスクが無いので、円の形はこの窓の alpha で作る。
//   プレイヤーが動いていない間は窓を作り直さない。
// 描く順：暗い円の地 → 窓 → 目印 → プレイヤー → 古金の輪 → 「N」
// ============================================================
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <SimpleMath.h>
#include "World/MinimapBake.h"

class SpriteRenderer;
class TextRenderer;
class Texture;
class GridWorld;
namespace MapData { struct Map; }
struct HUDStyle;
struct HUDFrameInfo;

class HUDMinimap
{
public:
    // 地形を建てた後に呼ぶ（生成でも地図の読み込みでも）。grid は後処理まで終わった物
    void Bake(ID3D11Device* device, const GridWorld& grid, const MapData::Map& map, int biome, uint32_t seed);
    void Clear();
    bool HasMap() const { return m_Image.Valid(); }

    // style は倍率を掛けた後の物（HUD::m_S）。center = 円の中心（px）
    void Draw(SpriteRenderer& sprite, TextRenderer& text, ID3D11DeviceContext* context,
        const HUDStyle& style, const HUDFrameInfo& info,
        const DirectX::SimpleMath::Vector2& center, const std::shared_ptr<Texture>& white);

private:
    // 窓の絵を作り直して GPU へ（プレイヤーの位置・半径が変わった時だけ）
    void UpdateWindow(ID3D11DeviceContext* context, const DirectX::SimpleMath::Vector3& playerPos, float range);
    // 地形の絵の双線形サンプル（世界座標）。フィールドの外は透明
    void Sample(float wx, float wz, float out[4]) const;

    static constexpr int kN = 192;     // 窓のテクスチャの一辺（px）。表示は 180 x UI 倍率 ≈ 270px なので少しぼける程度

    MinimapBake::Image m_Image;
    ID3D11Device* m_Device = nullptr;
    std::shared_ptr<Texture> m_Window;
    std::vector<uint8_t> m_Pixels;     // kN * kN * 4
    DirectX::SimpleMath::Vector3 m_LastPos = { 1.0e9f, 0.0f, 0.0f };
    float m_LastRange = -1.0f;
};
