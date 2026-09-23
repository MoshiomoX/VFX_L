// ============================================================
// SkinnedModel.cpp
// ============================================================
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/MaterialLoader.h"
#include "Manager/ResourceManager.h"
#include "AssimpFlags.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;
using namespace DirectX::SimpleMath;

// offsetMatrix を階層から作り直すか。
//   [bind-check] の worstDiff が大きい（>0.1）submesh がある時だけ true にして試す
static constexpr float kOffsetRebuildThreshold = FLT_MAX;

// Assimp（列優先 / 右手）→ SimpleMath（行優先 / 左手）: 転置
static Matrix ToSM(const aiMatrix4x4& m)
{
    return Matrix(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4);
}

// ノード階層をそのまま Skeleton にする（mesh を持つノードも含む）
static void BuildSkeleton(const aiNode* node, int parentIndex, Skeleton& skel)
{
    int index = skel.AddBone(node->mName.C_Str());
    Bone& b = skel.GetBone(index);
    b.parentIndex = parentIndex;
    b.localBindTransform = ToSM(node->mTransformation);

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        BuildSkeleton(node->mChildren[i], index, skel);
}

static bool FindMeshNode(const aiNode* node, unsigned int meshIndex,
    const Matrix& parent, Matrix& outGlobal, std::string& outName)
{
    Matrix global = ToSM(node->mTransformation) * parent;   // 行優先: local * parentGlobal

    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        if (node->mMeshes[i] == meshIndex)
        {
            outGlobal = global;
            outName = node->mName.C_Str();
            return true;
        }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        if (FindMeshNode(node->mChildren[i], meshIndex, global, outGlobal, outName))
            return true;

    return false;
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

// ============================================================
// import 済みの scene から組み立てる（LoadModelAuto もここに来る）
// ============================================================
bool SkinnedModel::LoadFromScene(ID3D11Device* device, const aiScene* scene,
    const std::string& directory, const std::string& modelName)
{
    m_Directory = directory;

    // 1. 骨格（ノード階層をそのまま。localBind はノード変換）
    BuildSkeleton(scene->mRootNode, -1, m_Skeleton);
    std::cout << "[SkinnedModel] Skeleton built with " << m_Skeleton.GetBoneCount()
        << " bones." << std::endl;

    // 2. mesh 毎に bind 頂点 / index / 重み / offset（submesh 単位）
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
    {
        aiMesh* mesh = scene->mMeshes[mi];

        SubMesh sub;
        sub.name = mesh->mName.C_Str();
        sub.materialIndex = (int)mesh->mMaterialIndex;
        sub.vertices.resize(mesh->mNumVertices);

        // この mesh を持つノード（global bind と名前）
        Matrix meshNodeGlobal = Matrix::Identity;
        std::string meshNodeName;
        FindMeshNode(scene->mRootNode, mi, Matrix::Identity, meshNodeGlobal, meshNodeName);

        // ---- 頂点（bind pose。ノード変換は焼かない）----
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            SkinnedVertex& vert = sub.vertices[v];
            vert = SkinnedVertex{};

            vert.position = Vector3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            if (mesh->HasNormals())
                vert.normal = Vector3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
            if (mesh->HasTangentsAndBitangents())
                vert.tangent = Vector3(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
            if (mesh->mTextureCoords[0])
            {
                vert.uv.x = mesh->mTextureCoords[0][v].x;
                vert.uv.y = mesh->mTextureCoords[0][v].y;
            }
        }

        // ---- index ----
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; ++k)
                sub.indices.push_back(face.mIndices[k]);
        }

        if (mesh->mNumBones == 0)
        {
            // ============================================================
            // 骨無しの剛体パーツ（武器・飾り・骨に直接ぶら下がった mesh）
            // 頂点はノードのローカル空間にある。ノード自身を「骨」にして
            // 全頂点を重み 1 で結ぶ。offset は Identity（ローカル → そのまま）。
            // palette = Identity * global[node] = ノードの動きに追従する
            // ============================================================
            int nodeBone = m_Skeleton.FindBoneIndex(meshNodeName);
            if (nodeBone < 0) nodeBone = 0;   // 見つからない事は無いはずだが保険

            for (auto& vert : sub.vertices)
                vert.AddBone((uint32_t)nodeBone, 1.0f);
            sub.boneOffsets[nodeBone] = Matrix::Identity;

            std::cout << "[SkinnedModel] submesh=" << mi << " name=" << sub.name
                << " rigid -> node '" << meshNodeName << "'" << std::endl;
        }
        else
        {
            // ---- 骨の重み + offset（offset は submesh 毎の map に入れる）----
            for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi)
            {
                aiBone* aibone = mesh->mBones[bi];
                if (!aibone) continue;

                std::string boneName = aibone->mName.C_Str();
                int boneIndex = m_Skeleton.FindBoneIndex(boneName);
                if (boneIndex < 0)
                {
                    boneIndex = m_Skeleton.AddBone(boneName);
                    std::cout << "[add-missing-bone] " << boneName << std::endl;
                }

                sub.boneOffsets[boneIndex] = ToSM(aibone->mOffsetMatrix);

                if (aibone->mNumWeights == 0)
                    continue;
                if (aibone->mNumWeights > (unsigned)mesh->mNumVertices)
                {
                    std::cout << "[skip-bad-bone] mesh=" << mi << " bone=" << bi
                        << " numW=" << aibone->mNumWeights
                        << " name=" << boneName << std::endl;
                    continue;
                }

                for (unsigned int w = 0; w < aibone->mNumWeights; ++w)
                {
                    const aiVertexWeight& vw = aibone->mWeights[w];
                    if (vw.mVertexId >= sub.vertices.size()) continue;
                    sub.vertices[vw.mVertexId].AddBone((uint32_t)boneIndex, vw.mWeight);
                }
            }

            // ============================================================
            // bind 整合性チェック:
            //   offset * boneGlobalBind ≒ meshNodeGlobalBind になるはず。
            //   ずれていれば mesh ノード自身に変換が乗っている（FBX に多い）ので
            //   offset を階層から作り直す: offset = meshNodeGlobal * Invert(boneGlobalBind)
            // ============================================================
            const auto& bones = m_Skeleton.GetBones();
            auto globalBindOf = [&](int idx)
                {
                    Matrix g = Matrix::Identity;
                    for (int c = idx; c >= 0; c = bones[c].parentIndex)
                        g = g * bones[c].localBindTransform;   // 子 → 親の順に掛ける
                    return g;
                };

            float worst = 0.0f; int worstBone = -1;
            for (auto& [bi2, off] : sub.boneOffsets)
            {
                Matrix d = off * globalBindOf(bi2) - meshNodeGlobal;
                const float* p = &d._11;
                float md = 0.0f;
                for (int k = 0; k < 16; ++k) md = (std::max)(md, std::fabs(p[k]));
                if (md > worst) { worst = md; worstBone = bi2; }
            }
            std::cout << "[bind-check] submesh=" << mi << " name=" << sub.name
                << " worstDiff=" << worst
                << " bone=" << (worstBone >= 0 ? bones[worstBone].name : std::string("none"))
                << std::endl;

            if (worst > kOffsetRebuildThreshold)
            {
                for (auto& [bi2, off] : sub.boneOffsets)
                    off = meshNodeGlobal * globalBindOf(bi2).Invert();
                std::cout << "[offset-rebuild] submesh=" << mi
                    << " rebuilt " << sub.boneOffsets.size() << " offsets" << std::endl;
            }
        }

        // 重みの正規化（骨無しパーツは 1.0 が入っているのでそのまま）
        for (auto& vert : sub.vertices)
            vert.NormalizeWeights();

        std::cout << "[SkinnedModel] submesh=" << mi << " name=" << sub.name
            << " offsetCount=" << sub.boneOffsets.size() << std::endl;

        m_SubMeshes.push_back(std::move(sub));
    }

    m_GlobalInverse = ToSM(scene->mRootNode->mTransformation).Invert();

    // 3. 材質。VS は SkinnedVS 固定、PS は貼图に応じて PBR / Lambert
    auto skinnedVS = ResourceManager::Get().LoadVS(L"SkinnedVS", L"Shader/Skinning/SkinnedVS.hlsl");
    m_Materials = MaterialLoader::LoadFromScene(device, scene, directory, modelName, skinnedVS);

    // 4. アニメ
    LoadAnimations(scene);

    std::cout << "[SkinnedModel] Load finished. SubMeshes=" << m_SubMeshes.size()
        << " Bones=" << m_Skeleton.GetBoneCount()
        << " Materials=" << m_Materials.size()
        << " Clips=" << m_Animations.size() << std::endl;
    return true;
}
// ============================================================
// ファイルから（import は AssimpFlags.h の共通設定）
// ============================================================
bool SkinnedModel::Load(ID3D11Device* device, const std::string& filepath)
{
    Assimp::Importer importer;
    const aiScene* scene = Res::ImportModelScene(importer, filepath);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cout << "[SkinnedModel] Assimp error: " << importer.GetErrorString() << std::endl;
        return false;
    }

    const size_t lastSlash = filepath.find_last_of("/\\");
    const std::string dir = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash + 1) : "";
    const std::string name = fs::path(filepath).stem().string();

    return LoadFromScene(device, scene, dir, name);
}

// ============================================================
// アニメクリップ
// ============================================================
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
                const auto& key = ch->mPositionKeys[k];
                bc.positions.push_back({ (float)key.mTime,
                    Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k)
            {
                const auto& key = ch->mRotationKeys[k];
                bc.rotations.push_back({ (float)key.mTime,
                    Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
            }
            for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
            {
                const auto& key = ch->mScalingKeys[k];
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

// ============================================================
// 時刻 → 各ボーンの global 行列（offset は掛けない）
// ============================================================
void SkinnedModel::SampleAnimation(float timeSec, std::vector<Matrix>& outGlobal, int clipIndex) const
{
    const int boneCount = m_Skeleton.GetBoneCount();
    outGlobal.assign(boneCount, Matrix::Identity);

    if (clipIndex < 0 || clipIndex >= (int)m_Animations.size())
        return;

    const AnimationClip& clip = m_Animations[clipIndex];
    const auto& bones = m_Skeleton.GetBones();

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

    float tick = 0.0f;
    if (clip.ticksPerSecond > 0.0f && clip.duration > 0.0f)
        tick = std::fmod(timeSec * clip.ticksPerSecond, clip.duration);

    for (int i = 0; i < boneCount; ++i)
    {
        const Bone& bone = bones[i];
        Matrix local = bone.localBindTransform;

        auto it = clip.nodeToChannel.find(bone.name);
        if (it != clip.nodeToChannel.end())
        {
            const BoneChannel& ch = clip.channels[it->second];

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

        // global = local * parentGlobal（offset は submesh 側で掛ける）
        outGlobal[i] = (bone.parentIndex < 0) ? local : local * outGlobal[bone.parentIndex];
    }
}

// ============================================================
// submesh 毎のパレット: palette[bone] = offset(submesh 固有) * global[bone]
// ============================================================
void SkinnedModel::BuildSubmeshPalette(int submeshIndex,
    const std::vector<Matrix>& global,
    std::vector<Matrix>& outPalette) const
{
    const int boneCount = m_Skeleton.GetBoneCount();
    outPalette.assign(boneCount, Matrix::Identity);

    if (submeshIndex < 0 || submeshIndex >= (int)m_SubMeshes.size())
        return;

    const SubMesh& sub = m_SubMeshes[submeshIndex];
    for (const auto& [boneIdx, offset] : sub.boneOffsets)
    {
        if (boneIdx >= 0 && boneIdx < boneCount)
            outPalette[boneIdx] = offset * global[boneIdx];
    }
}