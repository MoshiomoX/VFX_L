#pragma once
#include <string>
#include <memory>
#include <cstdint>
#include <nlohmann/json.hpp>

class Texture;
using json = nlohmann::json;

// ============================================================
// 生成ノイズの配方。json に保存し、読み込み時に NoiseGenCS で焼く
// （size / frequency は整数で、周期的 = 継ぎ目無し）
// ============================================================
struct NoiseRecipe
{
    enum class Type : int { Perlin = 0, Worley = 1, FBM = 2 };

    Type     type = Type::Perlin;
    int      size = 256;
    int      frequency = 4;       // 1 枚に何周期入るか
    int      octaves = 4;         // FBM のみ
    float    persistence = 0.5f;  // FBM のみ
    uint32_t seed = 1;

    // ResourceManager の cache key（内容が同じなら同じ 1 枚）
    std::string CacheKey() const;

    json ToJson() const;
    void FromJson(const json& j);
    bool OnImGui();   // 変更があれば true
};

// ============================================================
// 特効が参照するテクスチャ 1 枚。
//   file  : Assets 以下のパス（既存の LoadTexture）
//   gen   : NoiseRecipe から生成
// どちらか一方。Resolve() で実体（Texture）を引く
// ============================================================
struct VFXTextureRef
{
    enum class Source : int { None = 0, File = 1, Generated = 2 };

    Source      source = Source::None;
    std::string file;
    NoiseRecipe recipe;

    std::shared_ptr<Texture> texture;   // 実行時のみ。json には出さない

    void Resolve();                     // source に応じて texture を埋める
    bool IsValid() const { return texture != nullptr; }

    json ToJson() const;
    void FromJson(const json& j);

    // label: "Main" 等。dir: File の時に列挙するフォルダ。変更があれば true
    bool OnImGui(const char* label, const char* dir);
};