#include "SkinnedModel.h"
#include "AssimpFlags.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

#include <iostream>
#include <cmath>
#include <algorithm>
using namespace DirectX::SimpleMath;

// ★offsetMatrixを階層から再計算するスイッチ
//   [bind-check] の worstDiff が大きい(>0.1)submeshがある場合に true にして再実行
static constexpr bool kRebuildOffsetsFromHierarchy =false;

// Assimp(行優先/列ベクトル) → SimpleMath(行優先/行ベクトル) へ転置変換
static Matrix ToSM(const aiMatrix4x4& m)
{
    return Matrix(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4);
}

// ノード階層を再帰走査して Skeleton にボーン枠を登録（階層は全mesh共有）
static void BuildSkeleton(const aiNode* node, int parentIndex, Skeleton& skel)
{
    int index = skel.AddBone(node->mName.C_Str());
    Bone& b = skel.GetBone(index);
    b.parentIndex = parentIndex;
    b.localBindTransform = ToSM(node->mTransformation);

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        BuildSkeleton(node->mChildren[i], index, skel);
}

// 指定mesh indexを参照しているノードのglobal bind行列を探す（再帰）
// offset整合性チェックの基準。Mixamoは通常ほぼIdentity
static bool FindMeshNodeGlobal(const aiNode* node, unsigned int meshIndex,
    const Matrix& parent, Matrix& out)
{
    Matrix global = ToSM(node->mTransformation) * parent;   // 行ベクトル規約：local * parentGlobal

    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        if (node->mMeshes[i] == meshIndex) { out = global; return true; }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        if (FindMeshNodeGlobal(node->mChildren[i], meshIndex, global, out))
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

// ============================================
// import済みシーンから構築（LoadModelAuto から呼ばれる）
// ============================================
bool SkinnedModel::LoadFromScene(const aiScene* scene, const std::string& directory)
{
    m_Directory = directory;

    // 1. ノード階層からスケルトン構築（階層・localBindは全mesh共有）
    BuildSkeleton(scene->mRootNode, -1, m_Skeleton);

    std::cout << "[SkinnedModel] Skeleton built with " << m_Skeleton.GetBoneCount()
        << " bones." << std::endl;

    // 2. 各meshの bind頂点 / index / ウェイト / offset行列(submesh毎)
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
            vert = SkinnedVertex{};

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

        // ボーンウェイト + offset行列（★offsetはこのsubmesh専用に保存）
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

            // ★offsetは全骨共有せず、submesh毎のmapに保存
            sub.boneOffsets[boneIndex] = ToSM(aibone->mOffsetMatrix);

            if (aibone->mNumWeights == 0)
                continue;
            else if (aibone->mNumWeights > (unsigned)mesh->mNumVertices)
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

        // ウェイト正規化
        for (auto& vert : sub.vertices)
            vert.NormalizeWeights();

        // ============================================
        // ★bind整合性チェック：
        //   offset * boneGlobalBind ≒ meshNodeGlobalBind のはず
        //   崩れている骨 = スパイク（手/剣/盾）の発生源
        // ============================================
        {
            Matrix meshNodeGlobal = Matrix::Identity;
            FindMeshNodeGlobal(scene->mRootNode, mi, Matrix::Identity, meshNodeGlobal);

            // ★追加：meshNodeGlobal が Identity か確認
            std::cout << "[meshNode] submesh=" << mi
                << " pos=(" << meshNodeGlobal._41 << "," << meshNodeGlobal._42 << "," << meshNodeGlobal._43 << ")"
                << " _11=" << meshNodeGlobal._11 << std::endl;
            const auto& bones = m_Skeleton.GetBones();
            auto globalBindOf = [&](int idx) {
                Matrix g = Matrix::Identity;
                for (int c = idx; c >= 0; c = bones[c].parentIndex)
                    g = g * bones[c].localBindTransform;   // 子→親へ累積
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

            // ★修復スイッチ：ファイルのoffsetを捨て、階層から再計算する
            //   offset = meshNodeGlobal * Invert(boneGlobalBind)
            //   → bind時 offset*globalBind = meshNodeGlobal が保証され、スパイクは消えるはず
            if (kRebuildOffsetsFromHierarchy)
            {
                for (auto& [bi2, off] : sub.boneOffsets)
                    off = meshNodeGlobal * globalBindOf(bi2).Invert();
                std::cout << "[offset-rebuild] submesh=" << mi
                    << " rebuilt " << sub.boneOffsets.size() << " offsets" << std::endl;
            }
        }

        std::cout << "[SkinnedModel] submesh=" << mi << " name=" << sub.name
            << " offsetCount=" << sub.boneOffsets.size() << std::endl;

        m_SubMeshes.push_back(std::move(sub));
    }

    m_GlobalInverse = ToSM(scene->mRootNode->mTransformation).Invert();
    LoadAnimations(scene);

    std::cout << "[SkinnedModel] Load finished. SubMeshes=" << m_SubMeshes.size()
        << " Bones=" << m_Skeleton.GetBoneCount() << std::endl;

    return true;
}

// ============================================
// 単体で呼ぶ用（フラグ/プロパティは AssimpFlags.h に一元化）
// ============================================
bool SkinnedModel::Load(const std::string& filepath)
{
    Assimp::Importer importer;
    const aiScene* scene = Res::ImportModelScene(importer, filepath);

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

// ============================================
// 時刻 → 各ボーンの global行列（★offsetはここで掛けない）
// ============================================
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

        // Convention A：global のみ算出（offsetはsubmesh毎に後段で掛ける）
        outGlobal[i] = (bone.parentIndex < 0) ? local : local * outGlobal[bone.parentIndex];
    }
}

// ============================================
// submesh毎の最終パレット： palette[bone] = offset(submesh固有) * global[bone]
// ============================================
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