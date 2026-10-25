// ============================================================
// AssimpFlags.h
// SkinnedModel::Load（パスから直接読む入口）の Assimp import 設定。
// ※実際のモデル読み込みはここを通らない（2026-10-25 時点）：
//   ・ResourceManager::ImportModelAuto（LoadModelAuto と先読み。プレイヤーなど骨付きのモデルは全部これ、骨の無い物も来る）は
//     自分の flag を書いている：Triangulate | FlipUVs | CalcTangentSpace | GenNormals | MakeLeftHanded | PopulateArmatureData。
//     PopulateArmatureData は付けたまま（骨の armature 情報を埋める。外すと骨・アニメの結果が変わり得るので触らない）、
//     LimitBoneWeights は付けていない（だから AI_CONFIG_PP_LBW_MAX_WEIGHTS の設定も効いていない）。
//     1 頂点 5 本以上の骨は SkinnedVertex::AddBone が強い方の 4 本だけ残して切り、NormalizeWeights で和を 1 に戻す
//   ・Model::Load（ResourceManager::LoadModel・雑魚の姿勢を焼いたメッシュなど、静的モデル）も自分の flag（上の 5 つ。PopulateArmatureData 無し）
// ここを変えても上の 2 つには効かない
// ============================================================
#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <string>

namespace Res
{
    // SkinnedModel::Load の import フラグ
    // PopulateArmatureData は入れていない（ノード構造を変えて channel 名の一致を壊す恐れ。ImportModelAuto の方は入っている）
    // JoinIdenticalVertices は入れていない（ウェイトの付け直しの事故防止）
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