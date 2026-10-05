// ============================================================
// ParticleSheets.h
// 粒子のテクスチャ表。ビルボード粒子は発射器ごとに「どのテクスチャの、どの絵」を選ぶ。
//
//   テクスチャ 1 枚 = 格子に並べた絵（atlas）+ 説明 json（行列・サンプリング・名前付きの範囲）。
//   番号は Res::ParticleSheet::kManifests の並び（エフェクト json の "sheet" に保存される）。
//   GPUParticleSystem が全部を t0.. に並べて貼り、PS が番号で選ぶ
//   （SM5.0 はテクスチャの配列を動的な添字で引けないので switch）。
//
//   premultiplied = true のテクスチャは rgb に alpha を掛けた値で保存してある
//   （Tools/BuildParticleSheets.ps1）。旧 particlesSheet.jpg（0 番）だけ false
// ============================================================
#pragma once
#include <memory>
#include <string>
#include <vector>

class Texture;

namespace ParticleSheets
{
    constexpr int kMaxSheets = 8;   // GPUParticlePS の g_Sheet0..7 と一致させる

    struct Group
    {
        std::string name;
        int start = 0;
        int count = 1;
    };

    struct Sheet
    {
        std::string name;                   // 表示名（読めなければ説明 json のパス）
        std::shared_ptr<Texture> texture;   // 読めなければ null（その番号の粒子は透明になる）
        int  rows = 1;
        int  cols = 1;
        bool point = false;                 // 最近傍で読む（ピクセルアート）
        bool premultiplied = false;
        std::vector<std::string> frames;    // 格子ごとの名前（空でもよい）
        std::vector<Group> groups;          // 名前付きの範囲（連番・同じ種類の変化形）

        int CellCount() const { return rows * cols; }
    };

    // 初回だけ読む（2 回目以降は何もしない）。Count / Get も初回に自動で呼ぶ。
    // ResourceManager の初期化後であること
    void Load();

    int Count();                  // 表の長さ（読めなかった番号も含む）
    const Sheet* Get(int index);  // 範囲外は nullptr
}
