#include "SkinnedModel.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h> 

#include <iostream>
#include <cmath>
#include <algorithm>
using namespace DirectX::SimpleMath;

// Assimp(行優先/列ベクトル) → SimpleMath(行優先/行ベクトル) へ転置変換
// ※ skinningで使う行列は全部これを通す（既存 ConvertMatrix と同じ規則）
static Matrix ToSM(const aiMatrix4x4& m)
{
    return Matrix(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4);
}

// ノード階層を再帰走査して Skeleton にボーン枠を登録
// 全ノードを骨枠にする → channelの無い骨もbind姿勢で正しく扱える
static void BuildSkeleton(const aiNode* node, int parentIndex, Skeleton& skel)
{
    int index = skel.AddBone(node->mName.C_Str());
    Bone& b = skel.GetBone(index);
    b.parentIndex = parentIndex;
    b.localBindTransform = ToSM(node->mTransformation);

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        BuildSkeleton(node->mChildren[i], index, skel);


}

static Vector3 InterpVec(const std::vector<VectorKey>& keys, float t, const Vector3& fb)
{
    if (keys.empty())           return fb;
    if (keys.size() == 1)       return keys[0].value;
    if (t <= keys.front().time) return keys.front().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
        if (t < keys[i + 1].time)
        {
            float dt = keys[i + 1].time - keys[i].time;
            float f = (dt > 0.0f) ? std::clamp((t - keys[i].time) / dt, 0.0f, 1.0f) : 0.0f;
            return Vector3::Lerp(keys[i].value, keys[i + 1].value, f);
        }
    return keys.back().value;
}

static Quaternion InterpQuat(const std::vector<QuatKey>& keys, float t, const Quaternion& fb)
{
    if (keys.empty())           return fb;
    if (keys.size() == 1)       return keys[0].value;
    if (t <= keys.front().time) return keys.front().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i)
        if (t < keys[i + 1].time)
        {
            float dt = keys[i + 1].time - keys[i].time;
            float f = (dt > 0.0f) ? std::clamp((t - keys[i].time) / dt, 0.0f, 1.0f) : 0.0f;
            return Quaternion::Slerp(keys[i].value, keys[i + 1].value, f);
        }
    return keys.back().value;
}
// ============================================
// import済みシーンから構築（LoadModelAuto から呼ばれる）
// ============================================
bool SkinnedModel::LoadFromScene(const aiScene* scene, const std::string& directory)
{
    m_Directory = directory;

    // 1. ノード階層からスケルトン構築
    BuildSkeleton(scene->mRootNode, -1, m_Skeleton);

    // 2. 各meshの bind頂点 / index / ウェイト / offset行列
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
    {
        aiMesh* mesh = scene->mMeshes[mi];

        SubMesh sub;
        sub.name = mesh->mName.C_Str();
        sub.materialIndex = mesh->mMaterialIndex;
        sub.vertices.resize(mesh->mNumVertices);

        // 頂点（bind pose、変換は焼かない）
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            SkinnedVertex& vert = sub.vertices[v];
            vert = SkinnedVertex{}; // ゼロ初期化

            vert.position = Vector3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            if (mesh->HasNormals())
                vert.normal = Vector3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
            if (mesh->mTextureCoords[0])
            {
                vert.uv.x = mesh->mTextureCoords[0][v].x;
                vert.uv.y = mesh->mTextureCoords[0][v].y;
            }
        }

        // index
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; ++k)
                sub.indices.push_back(face.mIndices[k]);
        }

        // ボーンウェイト + offset行列
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi)
        {
            aiBone* aibone = mesh->mBones[bi];
            if (!aibone) continue;

            // ★解放済み/壊れたボーンを弾く
            //   numWが頂点数より多い or 0 は異常（解放済みボーンは 0xDDDDDDDD 等になる）
            if (aibone->mNumWeights == 0 ||
                aibone->mNumWeights > (unsigned)mesh->mNumVertices)
            {
                std::cout << "[skip-bad-bone] mesh=" << mi << " bone=" << bi
                    << " numW=" << aibone->mNumWeights << std::endl;
                continue;
            }

            // 全体スケルトンのindexを名前で引く（submesh跨ぎで共通化）
            int boneIndex = m_Skeleton.FindBoneIndex(aibone->mName.C_Str());
            if (boneIndex < 0)
                std::cout << "[warning] bone not found in skeleton: " << aibone->mName.C_Str() << std::endl;
                boneIndex = m_Skeleton.AddBone(aibone->mName.C_Str()); // 念のため

            // offset行列（mesh空間 → bone空間）
            m_Skeleton.GetBone(boneIndex).offsetMatrix = ToSM(aibone->mOffsetMatrix);

            // 影響頂点へウェイト書き込み
            for (unsigned int w = 0; w < aibone->mNumWeights; ++w)
            {
                const aiVertexWeight& vw = aibone->mWeights[w];

                // 頂点ID範囲外ガード
                if (vw.mVertexId >= sub.vertices.size())
                    continue;

                sub.vertices[vw.mVertexId].AddBone((uint32_t)boneIndex, vw.mWeight);
            }
        }
        // ウェイト正規化
        for (auto& vert : sub.vertices)
            vert.NormalizeWeights();

        m_SubMeshes.push_back(std::move(sub));
    

    }
    m_GlobalInverse = ToSM(scene->mRootNode->mTransformation).Invert();
    LoadAnimations(scene);
    //std::cout << "[SkinnedModel] OK: SubMesh=" << m_SubMeshes.size()
    //    << " Bone=" << m_Skeleton.GetBoneCount()
    //    << " (Animは B6 で)" << std::endl;

  
    return true;
}

// ============================================
// 単体で呼ぶ用（importしてLoadFromSceneへ委譲）
// ※フラグは LoadModelAuto と揃える
// ============================================
bool SkinnedModel::Load(const std::string& filepath)
{
    Assimp::Importer importer;    
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_MakeLeftHanded /*|
        aiProcess_LimitBoneWeights*/);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cout << "[SkinnedModel] Assimp失敗: " << importer.GetErrorString() << std::endl;
        return false;
    }

    size_t lastSlash = filepath.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";

    return LoadFromScene(scene, dir);
}

void SkinnedModel::LoadAnimations(const aiScene* scene)
{
    if (!scene->HasAnimations()) return;

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* anim = scene->mAnimations[a];

        AnimationClip clip;
        clip.name = anim->mName.C_Str();
        clip.duration = (float)anim->mDuration;
        clip.ticksPerSecond = (anim->mTicksPerSecond != 0.0)
            ? (float)anim->mTicksPerSecond : 25.0f;

        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* ch = anim->mChannels[c];
            BoneChannel bc;
            bc.nodeName = ch->mNodeName.C_Str();

            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
            {
                auto& key = ch->mPositionKeys[k];
                bc.positions.push_back({ (float)key.mTime,
                    Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k)
            {
                auto& key = ch->mRotationKeys[k];
                // aiQuaternion(w,x,y,z) → SimpleMath(x,y,z,w)
                bc.rotations.push_back({ (float)key.mTime,
                    Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
            }
            for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
            {
                auto& key = ch->mScalingKeys[k];
                bc.scales.push_back({ (float)key.mTime,
                    Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }

            clip.nodeToChannel[bc.nodeName] = (int)clip.channels.size();
            clip.channels.push_back(std::move(bc));
        }
        m_Animations.push_back(std::move(clip));
    }


}

float SkinnedModel::GetClipDurationSec(int clipIndex) const
{
    if (clipIndex < 0 || clipIndex >= (int)m_Animations.size()) return 0.0f;
    const auto& c = m_Animations[clipIndex];
    return (c.ticksPerSecond > 0.0f) ? c.duration / c.ticksPerSecond : 0.0f;
}
void SkinnedModel::SampleAnimation(float timeSec, std::vector<Matrix>& outPalette, int clipIndex) const
{
    const int boneCount = m_Skeleton.GetBoneCount();
    outPalette.assign(boneCount, Matrix::Identity);

    if (clipIndex < 0 || clipIndex >= (int)m_Animations.size())
        return;

    const AnimationClip& clip = m_Animations[clipIndex];
    const auto& bones = m_Skeleton.GetBones();

    // ★一回だけ: 接線（boneCount/channel一致数/tps/dur）を出す
    static bool s_dumped = false;
    if (!s_dumped)
    {
        s_dumped = true;
        int matched = 0;
        for (auto& b : bones) if (clip.nodeToChannel.count(b.name)) ++matched;
        std::cout << "[anim] boneCount=" << boneCount
            << " channels=" << clip.channels.size()
            << " matched=" << matched
            << " tps=" << clip.ticksPerSecond
            << " dur=" << clip.duration << std::endl;
    }

    // 秒 → tick（ループ）
    float tick = 0.0f;
    if (clip.ticksPerSecond > 0.0f && clip.duration > 0.0f)
        tick = std::fmod(timeSec * clip.ticksPerSecond, clip.duration);

    std::vector<Matrix> global(boneCount, Matrix::Identity);

    for (int i = 0; i < boneCount; ++i)
    {
        const Bone& bone = bones[i];

        Matrix local = bone.localBindTransform;

        auto it = clip.nodeToChannel.find(bone.name);
        if (it != clip.nodeToChannel.end())
        {
            const BoneChannel& ch = clip.channels[it->second];

            // bind分解（欠けたchannel成分のフォールバック）
            Matrix bindCopy = bone.localBindTransform;
            Vector3 bS; Quaternion bR; Vector3 bT;
            bindCopy.Decompose(bS, bR, bT);

            Vector3    S = InterpVec(ch.scales, tick, bS);
            Quaternion R = InterpQuat(ch.rotations, tick, bR);
            Vector3    T = InterpVec(ch.positions, tick, bT);

            local = Matrix::CreateScale(S)
                * Matrix::CreateFromQuaternion(R)
                * Matrix::CreateTranslation(T);
        }

        // Convention A
        global[i] = (bone.parentIndex < 0) ? local : local * global[bone.parentIndex];
        outPalette[i] = bone.offsetMatrix * global[i];   // ★GIは入れない
    }

}