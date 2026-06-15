// ============================================================
// AssimpFlags.h
// Assimp import 設定の一元管理
// LoadModelAuto / SkinnedModel::Load の両方がここを使う
// ※2箇所に別々に書くと必ずズレるので、編集はこのファイルだけ
// ============================================================
#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <string>

namespace Res
{
    // モデル共通 import フラグ
    // PopulateArmatureData は削除（ノード構造を変えchannel名一致を壊す恐れ）
    // JoinIdenticalVertices は削除（ウェイト再マップ事故防止）
    inline constexpr unsigned int kModelImportFlags =
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_MakeLeftHanded |
        aiProcess_LimitBoneWeights;

    // importer プロパティ込みの共通入口（flags + LBW_MAX + PreservePivots）
    inline const aiScene* ImportModelScene(Assimp::Importer& importer,
        const std::string& filepath)
    {
        importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        return importer.ReadFile(filepath, kModelImportFlags);
    }
}