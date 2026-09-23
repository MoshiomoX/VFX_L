// ============================================================
// SkinnedModel.cpp
// ============================================================
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/MaterialLoader.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
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

// offsetMatrix を階層から作り直す閾値（[bind-check] の worstDiff がこれを超えた submesh）。
//   Blender 出力の FBX は mesh ノード自身に平行移動が乗っている事があり
//   （KayKit Mage の頭: (0,1.216,0)）、Assimp の offset がそれを含まないので
//   頭だけ 1.2m 沈む。Mixamo（Paladin）は無重み末端骨でしか差が出ず、見た目は変わらない
static constexpr float kOffsetRebuildThreshold = 0.01f;

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
        sub.nodeName = meshNodeName;

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
            << " node='" << meshNodeName << "' nodePos=("
            << meshNodeGlobal._41 << "," << meshNodeGlobal._42 << "," << meshNodeGlobal._43 << ")"
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

int SkinnedModel::FindSubMeshByNode(const std::string& nodeName) const
{
    for (int i = 0; i < (int)m_SubMeshes.size(); ++i)
        if (m_SubMeshes[i].nodeName == nodeName) return i;
    return -1;
}

int SkinnedModel::FindClip(const std::string& name) const
{
    for (int i = 0; i < (int)m_Animations.size(); ++i)
        if (m_Animations[i].name == name) return i;

    // "Rig|Idle" / "Armature|Idle" のような接頭辞付きにも当てる
    const std::string suffix = "|" + name;
    for (int i = 0; i < (int)m_Animations.size(); ++i)
    {
        const auto& n = m_Animations[i].name;
        if (n.size() > suffix.size() &&
            n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
            return i;
    }
    return -1;
}

// ============================================================
// 時刻 → 各ボーンの global 行列（offset は掛けない）
// ============================================================
void SkinnedModel::SampleAnimation(float timeSec, std::vector<Matrix>& outGlobal, int clipIndex) const
{
    if (clipIndex < 0 || clipIndex >= (int)m_Animations.size())
    {
        outGlobal.assign(m_Skeleton.GetBoneCount(), Matrix::Identity);
        return;
    }
    std::vector<BoneLocal> local;
    SampleLocal(clipIndex, timeSec, local);
    BuildGlobals(local, outGlobal);
}

// ============================================================
// 時刻 → 各ボーンの親基準 S/R/T
// 時刻はクリップ長で折り返す（ループ）。ループさせたくない側で clamp してから渡す
// ============================================================
void SkinnedModel::SampleLocal(int clipIndex, float timeSec, std::vector<BoneLocal>& outLocal) const
{
    const auto& bones = m_Skeleton.GetBones();
    const int boneCount = (int)bones.size();
    outLocal.resize(boneCount);

    const AnimationClip* clip =
        (clipIndex >= 0 && clipIndex < (int)m_Animations.size()) ? &m_Animations[clipIndex] : nullptr;

    float tick = 0.0f;
    if (clip && clip->ticksPerSecond > 0.0f && clip->duration > 0.0f)
        tick = std::fmod(timeSec * clip->ticksPerSecond, clip->duration);

    for (int i = 0; i < boneCount; ++i)
    {
        const Bone& bone = bones[i];
        BoneLocal& o = outLocal[i];

        // bind を S/R/T に割る（Decompose は非 const なのでコピー）
        Matrix bindCopy = bone.localBindTransform;
        bindCopy.Decompose(o.scale, o.rotation, o.translation);

        if (!clip) continue;
        auto it = clip->nodeToChannel.find(bone.name);
        if (it == clip->nodeToChannel.end()) continue;

        const BoneChannel& ch = clip->channels[it->second];
        o.scale       = InterpVec(ch.scales, tick, o.scale);
        o.rotation    = InterpQuat(ch.rotations, tick, o.rotation);
        o.translation = InterpVec(ch.positions, tick, o.translation);
    }
}

void SkinnedModel::BuildGlobals(const std::vector<BoneLocal>& local, std::vector<Matrix>& outGlobal) const
{
    const auto& bones = m_Skeleton.GetBones();
    const int boneCount = (int)bones.size();
    outGlobal.assign(boneCount, Matrix::Identity);
    if ((int)local.size() < boneCount) return;

    for (int i = 0; i < boneCount; ++i)
    {
        const BoneLocal& l = local[i];
        Matrix m = Matrix::CreateScale(l.scale)
            * Matrix::CreateFromQuaternion(l.rotation)
            * Matrix::CreateTranslation(l.translation);
        // global = local * parentGlobal（offset は submesh 側で掛ける）
        const int p = bones[i].parentIndex;
        outGlobal[i] = (p < 0) ? m : m * outGlobal[p];
    }
}

void SkinnedModel::BlendLocals(std::vector<BoneLocal>& a, const std::vector<BoneLocal>& b,
    float t, const std::vector<float>* weights)
{
    const size_t n = (std::min)(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
    {
        float w = t;
        if (weights && i < weights->size()) w *= (*weights)[i];
        if (w <= 0.0f) continue;
        if (w >= 1.0f) { a[i] = b[i]; continue; }

        a[i].scale       = Vector3::Lerp(a[i].scale, b[i].scale, w);
        a[i].rotation    = Quaternion::Slerp(a[i].rotation, b[i].rotation, w);
        a[i].translation = Vector3::Lerp(a[i].translation, b[i].translation, w);
    }
}

bool SkinnedModel::BuildBoneMask(const std::string& rootBone, std::vector<float>& outMask) const
{
    const auto& bones = m_Skeleton.GetBones();
    const int root = m_Skeleton.FindBoneIndex(rootBone);
    outMask.assign(bones.size(), 0.0f);
    if (root < 0) return false;

    // 親は常に子より前に並ぶ（ノード階層を深さ優先で登録している）ので 1 パスで済む
    outMask[root] = 1.0f;
    for (size_t i = 0; i < bones.size(); ++i)
    {
        const int p = bones[i].parentIndex;
        if (p >= 0 && outMask[p] > 0.0f) outMask[i] = 1.0f;
    }
    return true;
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

// ============================================================
// 1 フレームを CPU で蒙皮 → 静的 Model
// SkinningCS.hlsl と同じ結果: p = Σ w_i * (v * palette_i)（行ベクトル規約）
// ============================================================
std::shared_ptr<Model> SkinnedModel::BakeStatic(ID3D11Device* device, int clipIndex, float timeSec,
    const Matrix& xform, const std::vector<std::string>& skipNodes) const
{
    std::vector<Matrix> globals, palette;
    SampleAnimation(timeSec, globals, clipIndex);

    auto model = std::make_shared<Model>();
    int baked = 0;
    Vector3 bmin(1e9f, 1e9f, 1e9f), bmax(-1e9f, -1e9f, -1e9f);   // 焼いた結果の寸法確認用

    for (int s = 0; s < (int)m_SubMeshes.size(); ++s)
    {
        const SubMesh& sub = m_SubMeshes[s];
        if (std::find(skipNodes.begin(), skipNodes.end(), sub.nodeName) != skipNodes.end())
            continue;

        BuildSubmeshPalette(s, globals, palette);
        // ※SkinningCS の transpose() は StructuredBuffer が列優先で読む分を戻しているだけ。
        //   CPU 側は SimpleMath の行ベクトル規約（p * M）のまま掛ける。転置すると頭が足に来る

        std::vector<VERTEX_3D> verts(sub.vertices.size());
        for (size_t v = 0; v < sub.vertices.size(); ++v)
        {
            const SkinnedVertex& in = sub.vertices[v];
            Vector3 p{}, n{}, t{};
            for (int k = 0; k < MAX_BONE_INFLUENCE; ++k)
            {
                const float w = in.boneWeights[k];
                if (w <= 0.0f) continue;
                const Matrix& m = palette[in.boneIndices[k]];
                p += Vector3::Transform(in.position, m) * w;
                n += Vector3::TransformNormal(in.normal, m) * w;
                t += Vector3::TransformNormal(in.tangent, m) * w;
            }
            VERTEX_3D& o = verts[v];
            o.position = Vector3::Transform(p, xform);
            o.normal = Vector3::TransformNormal(n, xform); o.normal.Normalize();
            o.tangent = Vector3::TransformNormal(t, xform); o.tangent.Normalize();
            o.uv = in.uv;
            o.color = { 1, 1, 1, 1 };
            bmin = Vector3::Min(bmin, o.position);
            bmax = Vector3::Max(bmax, o.position);
        }

        // 巻き方向の確認: 三角形の幾何法線と頂点法線が逆なら表裏が入れ替わっている。
        // この工程の mesh（MakeLeftHanded、FlipWindingOrder 無し）は
        // (b-a)×(c-a) が内向きになるのが正常。外向きになっていたら焼き方が壊れている
        {
            int flipped = 0, total = 0;
            for (size_t t = 0; t + 2 < sub.indices.size(); t += 3)
            {
                const auto& a = verts[sub.indices[t]], & b = verts[sub.indices[t + 1]], & c = verts[sub.indices[t + 2]];
                Vector3 gn = (b.position - a.position).Cross(c.position - a.position);
                if (gn.Dot(a.normal + b.normal + c.normal) > 0.0f) ++flipped;
                ++total;
            }
            if (total > 0 && flipped * 2 > total)
                std::cout << "[SkinnedModel] bake warning: submesh " << sub.nodeName
                    << " looks inside-out (" << flipped << "/" << total << ")" << std::endl;
        }

        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, verts, sub.indices)) continue;
        model->AddSubMesh(mesh, sub.materialIndex);
        ++baked;
    }

    std::cout << "[SkinnedModel] baked static: clip=" << clipIndex << " t=" << timeSec
        << " submeshes=" << baked << "/" << m_SubMeshes.size()
        << " bbox=(" << bmin.x << "," << bmin.y << "," << bmin.z << ")-("
        << bmax.x << "," << bmax.y << "," << bmax.z << ")" << std::endl;
    return baked > 0 ? model : nullptr;
}