// ============================================================
// LevelData.h
// ステージ編集シーン（LevelEditorScene）が作る「置いた物の一覧」と、その json。
//
//   Assets/Data/LevelData/<name>.json
//   {
//     "name": "forest_01",
//     "objects": [
//       { "model": "Assets/Model/KayKit_Forest/fbx/Tree_1_A_Color1.fbx",
//         "pos": [x, y, z], "rot": [x, y, z], "scale": 1.0 }
//     ]
//   }
//
//   rot は Euler 角（度）。TransformComponent / Transform と同じ規約。
//   今は見た目だけ（衝突・通行不能の情報は持たない）。
//   戦闘シーンで読む時に、モデルの包囲箱から作る想定
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <string>
#include <vector>

struct LevelObject
{
    std::string model;                                     // モデルのパス（作業ディレクトリ相対、'/' 区切り）
    DirectX::SimpleMath::Vector3 position = { 0, 0, 0 };
    DirectX::SimpleMath::Vector3 rotation = { 0, 0, 0 };   // Euler 角（度）
    float scale = 1.0f;                                    // 一様拡縮
};

struct LevelData
{
    std::string name;
    std::vector<LevelObject> objects;
};

namespace LevelIO
{
    inline constexpr const char* kDir = "Assets/Data/LevelData/";

    // kDir/<level.name>.json へ書く。名前が空・書けない時は false
    bool Save(const LevelData& level);

    // kDir/<name>.json を読む。読めない・壊れている時は false（out は触らない）
    bool Load(const std::string& name, LevelData& out);

    // 保存済みのステージ名（拡張子なし、名前順）
    std::vector<std::string> List();
}
