// ============================================================
// HUDMinimap.h
// 右上の丸いミニマップ（2026-10-19、ユーザー「右上に小さい地図を」）。
//   見せる物 = 地形の縮図（平原 / 山頂 / 洞窟・崖・台地・塞がった所）+ プレイヤーの矢印 + 目印（箱・門・磁石・エリート・Boss。
//   画面外の矢印と同じ色）。北が上で地図は回らず、プレイヤーの矢印が向きに合わせて回る。半径 minimapRange（m）の窓。
//   ワールドは右手系なので、真上から +z を上にして見ると +x は左（2026-10-23 まで左右が逆に描いていた）。
//
// 作り：
//   地形の絵（線形の色、0.5m / texel）は World/MinimapBake が地形を建てた時に 1 回焼く（Bake）。
//   窓（kN x kN の DYNAMIC テクスチャ）は中心を窓の 1px の格子に揃えて切り出し（双線形 + 円のマスク）、
//   SpriteRenderer で 1 枚描く。SpriteRenderer にはマスクが無いので、円の形はこの窓の alpha で作る。
//   プレイヤーが格子 1 マス以上動いた時だけ作り直す。その時も前の絵をずらして使い、新しく見える行・列だけ切り出す
//   （2026-10-23、Debug の負荷試験で 1 回 0.7 ms かかっていた）。格子に足りない端数は描く位置をずらして滑らかに見せる。
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

    // style は倍率を掛けた後の物（HUD::m_S）。center = 円の中心（px）。icons = 目印の種類の絵（HUDMarkerKind の数）
    void Draw(SpriteRenderer& sprite, TextRenderer& text, ID3D11DeviceContext* context,
        const HUDStyle& style, const HUDFrameInfo& info,
        const DirectX::SimpleMath::Vector2& center, const std::shared_ptr<Texture>& white,
        const std::shared_ptr<Texture>* icons);

    // 目印 1 つ：目印の色の縁の丸 → 暗い丸 → 種類の絵（目印の色を白へ寄せた色）。size = 絵の一辺（px）。
    // 画面外の矢印（HUD::DrawMarkers）も同じ絵を使う
    static void DrawMarkerBadge(SpriteRenderer& sprite, const std::shared_ptr<Texture>& icon,
        const DirectX::SimpleMath::Vector2& center, float size, const DirectX::SimpleMath::Vector4& color);

private:
    // 窓の絵を GPU へ（格子の中心・半径が変わった時だけ作り直す）。m_SubPx = 格子に足りない端数（窓の px）
    void UpdateWindow(ID3D11DeviceContext* context, const DirectX::SimpleMath::Vector3& playerPos, float range);
    // 窓の行 j の列 [i0, i1) を地形の絵から切り出して m_Color へ
    void SampleSpan(int j, int i0, int i1);
    // 地形の絵の双線形サンプル（世界座標）。フィールドの外は透明
    void Sample(float wx, float wz, float out[4]) const;
    // 円のマスク（窓の位置だけで決まる）を 1 回だけ作る
    void BuildMask();

    static constexpr int kN = 192;     // 窓のテクスチャの一辺（px）。表示は 180 x UI 倍率 ≈ 270px なので少しぼける程度

    MinimapBake::Image m_Image;
    ID3D11Device* m_Device = nullptr;
    std::shared_ptr<Texture> m_Window;
    std::vector<uint8_t> m_Pixels;     // kN * kN * 4。GPU へ送る物（円のマスク込み）
    std::vector<uint8_t> m_Color;      // kN * kN * 4。マスク前の地形の色（ずらして使い回す）
    std::vector<uint8_t> m_Scratch;    // ずらす時の作業用
    std::vector<uint8_t> m_Mask;       // kN * kN。円のマスク 0..255
    std::vector<int> m_RowSpan;        // kN * 2。行ごとにマスクが 0 でない列 [i0, i1)
    bool m_HasWindow = false;          // m_Color が今の格子の絵か
    int m_CellX = 0, m_CellZ = 0;      // 窓の中心の格子番号（中心 = m_CellX * m_CellM）
    float m_CellM = 0.0f;              // 窓の 1px の大きさ（m）
    DirectX::SimpleMath::Vector2 m_SubPx = { 0.0f, 0.0f };
};
