// ============================================================
// SkinnedModel.cpp
// ============================================================
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/BoneMaps.h"
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
#include <climits>

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
        sub.nodeGlobal = meshNodeGlobal;

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
            std::vector<int> weighted;   // 重みを持つ骨（bind の判定はこれだけで見る）
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

                weighted.push_back(boneIndex);
                for (unsigned int w = 0; w < aibone->mNumWeights; ++w)
                {
                    const aiVertexWeight& vw = aibone->mWeights[w];
                    if (vw.mVertexId >= sub.vertices.size()) continue;
                    sub.vertices[vw.mVertexId].AddBone((uint32_t)boneIndex, vw.mWeight);
                }
            }

            // ============================================================
            // bind 整合性チェック:
            //   offset * boneGlobalBind ≒ meshNodeGlobalBind になるはず。ずれ方で 2 通り:
            //   ・重みのある骨が全部同じだけずれる = mesh ノード自身に変換が乗っている（FBX に多い。
            //     KayKit Mage の頭）→ offset を階層から作り直す: offset = meshNodeGlobal * Invert(boneGlobalBind)
            //   ・骨ごとにずれ方が違う = ノードの既定の姿勢がスキニングした時の姿勢と違う（Reallusion CC の
            //     Shadowkin: スキニングは T ポーズ、ノードは A ポーズ + 指を曲げた姿勢）→ FBX の offset が正しい。
            //     作り直すと T ポーズの頂点を A ポーズの骨に付けることになり、前腕〜指先の腕の長さが狂って
            //     爪が長い線に伸びた（2026-10-04）
            //   重みの無い骨（末端の leaf 骨など）はスキニングに効かないので判定から外す
            // ============================================================
            const auto& bones = m_Skeleton.GetBones();
            auto globalBindOf = [&](int idx)
                {
                    Matrix g = Matrix::Identity;
                    for (int c = idx; c >= 0; c = bones[c].parentIndex)
                        g = g * bones[c].localBindTransform;   // 子 → 親の順に掛ける
                    return g;
                };
            auto maxAbs = [](const Matrix& d)
                {
                    const float* p = &d._11;
                    float md = 0.0f;
                    for (int k = 0; k < 16; ++k) md = (std::max)(md, std::fabs(p[k]));
                    return md;
                };

            float worst = 0.0f; int worstBone = -1;
            for (int b : weighted)
            {
                const float md = maxAbs(sub.boneOffsets[b] * globalBindOf(b) - meshNodeGlobal);
                if (md > worst) { worst = md; worstBone = b; }
            }
            float spread = 0.0f;   // 重みのある骨どうしの「offset * bind」の食い違い
            if (!weighted.empty())
            {
                const Matrix ref = sub.boneOffsets[weighted[0]] * globalBindOf(weighted[0]);
                for (int b : weighted)
                    spread = (std::max)(spread, maxAbs(sub.boneOffsets[b] * globalBindOf(b) - ref));
            }
            std::cout << "[bind-check] submesh=" << mi << " name=" << sub.name
                << " worstDiff=" << worst << " spread=" << spread
                << " bone=" << (worstBone >= 0 ? bones[worstBone].name : std::string("none"))
                << std::endl;

            if (worst > kOffsetRebuildThreshold)
            {
                if (spread <= (std::max)(kOffsetRebuildThreshold, worst * 0.1f))
                {
                    for (auto& [bi2, off] : sub.boneOffsets)
                        off = meshNodeGlobal * globalBindOf(bi2).Invert();
                    std::cout << "[offset-rebuild] submesh=" << mi
                        << " rebuilt " << sub.boneOffsets.size() << " offsets" << std::endl;
                }
                else
                {
                    std::cout << "[offset-keep] submesh=" << mi
                        << " bind pose differs from the node pose, keeping the file's offsets" << std::endl;
                }
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

    // 3. 材質。VS は SkinnedVS 固定、PS はテクスチャに応じて PBR / Lambert
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

// ============================================================
// 別ファイルのアニメを骨名で足す（SkinnedModel.h の説明）
// 変換は MakeLeftHanded だけ揃えれば良い（ノード・キーを変えるのはこれだけ。
// メッシュの処理は要らないので他の後処理は掛けない）
// ============================================================
static void CollectNodeBinds(const aiNode* node, std::unordered_map<std::string, Matrix>& out)
{
    out[node->mName.C_Str()] = ToSM(node->mTransformation);
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        CollectNodeBinds(node->mChildren[i], out);
}

int SkinnedModel::AddAnimationsFromFile(const std::string& filepath, const BoneMapEntry* map, int mapCount)
{
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(filepath, aiProcess_MakeLeftHanded);
    if (!scene || !scene->mRootNode)
    {
        std::cout << "[SkinnedModel] extra anims: Assimp error: " << importer.GetErrorString() << std::endl;
        return -1;
    }
    if (!scene->HasAnimations()) return 0;
    if (map && mapCount > 0)
        return RetargetAnimations(scene, filepath, map, mapCount);

    // 向こうの bind（ノード名 → 親基準の行列）
    std::unordered_map<std::string, Matrix> srcBind;
    CollectNodeBinds(scene->mRootNode, srcBind);

    // 平行移動の縮尺: hips の bind の長さの比（単位や体格が違っても腰の上下が合う）
    float tScale = 1.0f;
    {
        const int di = m_Skeleton.FindBoneIndex("hips");
        auto si = srcBind.find("hips");
        if (di >= 0 && si != srcBind.end())
        {
            const float lenSrc = si->second.Translation().Length();
            const float lenDst = m_Skeleton.GetBone(di).localBindTransform.Translation().Length();
            if (lenSrc > 1e-5f && lenDst > 1e-5f) tScale = lenDst / lenSrc;
        }
    }

    int added = 0, dropped = 0;
    for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* anim = scene->mAnimations[a];

        AnimationClip clip;
        clip.name = anim->mName.C_Str();
        // 同じ名前が既にあれば「ファイル名|名前」にする（元の方は名前のまま引け、こちらも全名で引ける）
        for (const auto& existing : m_Animations)
            if (existing.name == clip.name)
            {
                clip.name = fs::path(filepath).stem().string() + "|" + clip.name;
                break;
            }
        clip.duration = (float)anim->mDuration;
        clip.ticksPerSecond = (anim->mTicksPerSecond != 0.0) ? (float)anim->mTicksPerSecond : 25.0f;

        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* ch = anim->mChannels[c];
            const std::string node = ch->mNodeName.C_Str();
            const int bi = m_Skeleton.FindBoneIndex(node);
            if (bi < 0) { ++dropped; continue; }

            // 自分の bind と向こうの bind を S/R/T に割る（向こうに無ければ自分の物 = 差分無し）
            Vector3 dS, dT, sS, sT;
            Quaternion dR, sR;
            Matrix dst = m_Skeleton.GetBone(bi).localBindTransform;
            dst.Decompose(dS, dR, dT);
            auto sb = srcBind.find(node);
            Matrix src = (sb != srcBind.end()) ? sb->second : dst;
            src.Decompose(sS, sR, sT);
            Quaternion sRInv;
            sR.Inverse(sRInv);
            const bool sameBind = (sb == srcBind.end());

            BoneChannel bc;
            bc.nodeName = node;
            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
            {
                const auto& key = ch->mPositionKeys[k];
                const Vector3 v(key.mValue.x, key.mValue.y, key.mValue.z);
                bc.positions.push_back({ (float)key.mTime, sameBind ? v : dT + (v - sT) * tScale });
            }
            for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k)
            {
                const auto& key = ch->mRotationKeys[k];
                Quaternion q(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w);
                if (!sameBind)
                {
                    q = dR * sRInv * q;   // SimpleMath の積は「左を先に回す」= 行ベクトルの行列積と同じ順
                    q.Normalize();
                }
                bc.rotations.push_back({ (float)key.mTime, q });
            }
            for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
            {
                const auto& key = ch->mScalingKeys[k];
                Vector3 v(key.mValue.x, key.mValue.y, key.mValue.z);
                if (!sameBind)
                    v = Vector3(dS.x * v.x / (std::max)(sS.x, 1e-6f),
                                dS.y * v.y / (std::max)(sS.y, 1e-6f),
                                dS.z * v.z / (std::max)(sS.z, 1e-6f));
                bc.scales.push_back({ (float)key.mTime, v });
            }

            clip.nodeToChannel[bc.nodeName] = (int)clip.channels.size();
            clip.channels.push_back(std::move(bc));
        }
        std::cout << "[SkinnedModel]   + " << clip.name << " (" << clip.channels.size() << " channels)" << std::endl;
        m_Animations.push_back(std::move(clip));
        ++added;
    }
    std::cout << "[SkinnedModel] extra anims: " << added << " clips from " << filepath
        << " (translation scale " << tScale << ", " << dropped << " channels without a bone)" << std::endl;
    return added;
}

// ============================================================
// 骨名の違う人形から世界空間で付け替えて焼く（2026-10-04。SkinnedModel.h の説明）
//   回転の積は SimpleMath の「左を先に回す」（行ベクトル）。global = local * parentGlobal
// ============================================================
namespace
{
    Vector3 SampleVecKeys(const aiVectorKey* keys, unsigned n, double t, const Vector3& fallback)
    {
        if (n == 0) return fallback;
        auto V = [](const aiVector3D& v) { return Vector3(v.x, v.y, v.z); };
        if (n == 1 || t <= keys[0].mTime) return V(keys[0].mValue);
        for (unsigned k = 0; k + 1 < n; ++k)
            if (t < keys[k + 1].mTime)
            {
                const float f = (float)((t - keys[k].mTime) / (std::max)(keys[k + 1].mTime - keys[k].mTime, 1e-9));
                return Vector3::Lerp(V(keys[k].mValue), V(keys[k + 1].mValue), f);
            }
        return V(keys[n - 1].mValue);
    }

    Quaternion SampleQuatKeys(const aiQuatKey* keys, unsigned n, double t, const Quaternion& fallback)
    {
        if (n == 0) return fallback;
        auto Q = [](const aiQuaternion& q) { return Quaternion(q.x, q.y, q.z, q.w); };
        if (n == 1 || t <= keys[0].mTime) return Q(keys[0].mValue);
        for (unsigned k = 0; k + 1 < n; ++k)
            if (t < keys[k + 1].mTime)
            {
                const float f = (float)((t - keys[k].mTime) / (std::max)(keys[k + 1].mTime - keys[k].mTime, 1e-9));
                return Quaternion::Slerp(Q(keys[k].mValue), Q(keys[k + 1].mValue), f);
            }
        return Q(keys[n - 1].mValue);
    }

    Quaternion RotationOf(Matrix m)
    {
        Vector3 s, t;
        Quaternion r;
        m.Decompose(s, r, t);
        r.Normalize();
        return r;
    }

    Quaternion Inv(const Quaternion& q)
    {
        Quaternion r;
        q.Inverse(r);
        return r;
    }

    // 左（left）と上（up）の 2 本から正規直交の基底（行 = x 左, y 上, z 前）
    Matrix FrameOf(Vector3 left, Vector3 up)
    {
        left.Normalize();
        up = up - left * up.Dot(left);
        up.Normalize();
        Vector3 fwd = left.Cross(up);
        return Matrix(left.x, left.y, left.z, 0, up.x, up.y, up.z, 0, fwd.x, fwd.y, fwd.z, 0, 0, 0, 0, 1);
    }
}

int SkinnedModel::RetargetAnimations(const aiScene* scene, const std::string& filepath,
    const BoneMapEntry* map, int mapCount)
{
    // ---- 向こうのノード（深さ優先 = 親が先）----
    struct SrcNode { std::string name; int parent; Matrix bind; };
    std::vector<SrcNode> src;
    std::unordered_map<std::string, int> srcIndex;
    {
        std::vector<std::pair<const aiNode*, int>> stack = { { scene->mRootNode, -1 } };
        while (!stack.empty())
        {
            auto [node, parent] = stack.back();
            stack.pop_back();
            const int idx = (int)src.size();
            src.push_back({ node->mName.C_Str(), parent, ToSM(node->mTransformation) });
            srcIndex[src.back().name] = idx;
            for (int c = (int)node->mNumChildren - 1; c >= 0; --c)
                stack.push_back({ node->mChildren[c], idx });
        }
    }
    const int ns = (int)src.size();
    std::vector<Matrix> sBindG(ns);
    for (int i = 0; i < ns; ++i)
        sBindG[i] = (src[i].parent >= 0) ? src[i].bind * sBindG[src[i].parent] : src[i].bind;

    // ---- 自分の骨（配列は親が先）。基準 = スキニングした時の姿勢 ----
    //   offset のある骨は Invert(offset) * mesh ノード、無い骨はノードの親基準の変換で親に付ける。
    //   ノードの既定の姿勢は飾りのポーズの事がある（Shadowkin: 片膝を曲げてつま先を伸ばした浮遊ポーズ、
    //   マントも後ろへなびいている）。それを基準にするとアニメ全体にその癖が乗る
    const auto& bones = m_Skeleton.GetBones();
    const int nb = (int)bones.size();
    std::vector<Matrix> tBindG(nb);
    {
        std::vector<char> fromOffset(nb, 0);
        for (const auto& sub : m_SubMeshes)
            for (const auto& [b, off] : sub.boneOffsets)
                if (b >= 0 && b < nb && !fromOffset[b])
                {
                    tBindG[b] = off.Invert() * sub.nodeGlobal;
                    fromOffset[b] = 1;
                }
        for (int i = 0; i < nb; ++i)
            if (!fromOffset[i])
            {
                const int p = bones[i].parentIndex;
                tBindG[i] = (p >= 0) ? bones[i].localBindTransform * tBindG[p] : bones[i].localBindTransform;
            }
    }
    std::vector<Quaternion> tBindLocalR(nb), tBindGR(nb);
    std::vector<Vector3> tBindLocalT(nb), tBindLocalS(nb);
    for (int i = 0; i < nb; ++i)
    {
        const int p = bones[i].parentIndex;
        Matrix local = (p >= 0) ? tBindG[i] * tBindG[p].Invert() : tBindG[i];
        local.Decompose(tBindLocalS[i], tBindLocalR[i], tBindLocalT[i]);
        tBindLocalR[i].Normalize();
        tBindGR[i] = RotationOf(tBindG[i]);
    }

    // ---- 対応（自分の骨 → 向こうのノード）----
    std::vector<int> toSrc(nb, -1);
    int mapped = 0;
    auto pairOf = [&](const char* srcName, int& ti, int& si)
    {
        ti = si = -1;
        for (int k = 0; k < mapCount; ++k)
            if (std::string(map[k].source) == srcName)
            {
                ti = m_Skeleton.FindBoneIndex(map[k].target);
                auto it = srcIndex.find(map[k].source);
                si = (it != srcIndex.end()) ? it->second : -1;
                return ti >= 0 && si >= 0;
            }
        return false;
    };
    for (int k = 0; k < mapCount; ++k)
    {
        const int ti = m_Skeleton.FindBoneIndex(map[k].target);
        auto it = srcIndex.find(map[k].source);
        if (ti < 0 || it == srcIndex.end()) continue;
        toSrc[ti] = it->second;
        ++mapped;
    }
    if (mapped < 8)
    {
        std::cout << "[SkinnedModel] retarget: only " << mapped << " bones matched, skipped " << filepath << std::endl;
        return 0;
    }

    // ---- 向き合わせ（向こうのモデル空間 → 自分のモデル空間）：左右の上腕と腰 → 頭 ----
    Quaternion align;   // 単位
    {
        int tl, sl, tr, sr, th, sh, tp, sp;
        if (pairOf("upperarm_l", tl, sl) && pairOf("upperarm_r", tr, sr) && pairOf("Head", th, sh) && pairOf("pelvis", tp, sp))
        {
            const Matrix fs = FrameOf(sBindG[sl].Translation() - sBindG[sr].Translation(),
                                      sBindG[sh].Translation() - sBindG[sp].Translation());
            const Matrix ft = FrameOf(tBindG[tl].Translation() - tBindG[tr].Translation(),
                                      tBindG[th].Translation() - tBindG[tp].Translation());
            align = Quaternion::CreateFromRotationMatrix(fs.Transpose() * ft);   // 向こう → 基底の座標 → 自分
            align.Normalize();
        }
    }
    const Quaternion alignInv = Inv(align);

    // ---- 腰（map の先頭）：平行移動も。縮尺 = 腰の高さ（腰 → 左足首）の比 ----
    const int hipT = m_Skeleton.FindBoneIndex(map[0].target);
    const int hipS = srcIndex.count(map[0].source) ? srcIndex[map[0].source] : -1;
    float hipScale = 1.0f;
    {
        int tf, sf;
        if (hipT >= 0 && hipS >= 0 && pairOf("foot_l", tf, sf))
        {
            const float lt = (tBindG[hipT].Translation() - tBindG[tf].Translation()).Length();
            const float ls = (sBindG[hipS].Translation() - sBindG[sf].Translation()).Length();
            if (lt > 1e-6f && ls > 1e-6f) hipScale = lt / ls;
        }
    }
    Matrix hipParentInv = Matrix::Identity;
    if (hipT >= 0 && bones[hipT].parentIndex >= 0)
        hipParentInv = tBindG[bones[hipT].parentIndex].Invert();   // 腰より上の骨は動かない前提（root 等）

    // ---- 基準の姿勢の違いを直す（T ポーズ ↔ A ポーズ、つま先、指の開き）----
    //   骨ごとに「骨 → 向きの子」の世界の向きが向こうの基準（向き合わせ後）と揃う最小の回転 corr。
    //   向きの子 = 子孫のうち表で最初に出てくる骨（BoneMaps.h）。子の無い末端は親の corr を使う。
    //   世界の回転を骨ごとに直接決めるので、親を直しても子の向きはずれない
    std::vector<Quaternion> corr(nb, Quaternion::Identity);
    {
        std::vector<int> mapOrder(nb, INT_MAX);   // 表の何行目か
        for (int k = 0; k < mapCount; ++k)
        {
            const int ti = m_Skeleton.FindBoneIndex(map[k].target);
            if (ti >= 0 && toSrc[ti] >= 0 && mapOrder[ti] == INT_MAX) mapOrder[ti] = k;
        }
        std::vector<int> mappedAnc(nb, -1), dirChild(nb, -1);
        for (int i = 0; i < nb; ++i)
        {
            const int p = bones[i].parentIndex;
            mappedAnc[i] = (p < 0) ? -1 : (toSrc[p] >= 0 ? p : mappedAnc[p]);
            if (toSrc[i] < 0 || mappedAnc[i] < 0) continue;
            int& c = dirChild[mappedAnc[i]];
            if (c < 0 || mapOrder[i] < mapOrder[c]) c = i;
        }
        // 最小の回転（a を b に向ける。どちらも正規化済み）
        auto fromTo = [](const Vector3& a, const Vector3& b)
        {
            const float d = a.Dot(b);
            if (d < -0.9999f)
            {
                Vector3 axis = a.Cross(Vector3::UnitX);
                if (axis.LengthSquared() < 1e-6f) axis = a.Cross(Vector3::UnitY);
                axis.Normalize();
                return Quaternion(axis.x, axis.y, axis.z, 0.0f);
            }
            const Vector3 axis = a.Cross(b);
            Quaternion q(axis.x, axis.y, axis.z, 1.0f + d);
            q.Normalize();
            return q;
        };
        float worstDeg = 0.0f; int worstBone = -1;
        for (int i = 0; i < nb; ++i)
        {
            if (toSrc[i] < 0) continue;
            const int c = dirChild[i];
            if (c < 0)
            {
                corr[i] = (mappedAnc[i] >= 0) ? corr[mappedAnc[i]] : Quaternion::Identity;
                continue;
            }
            Vector3 dt = tBindG[c].Translation() - tBindG[i].Translation();
            Vector3 ds = Vector3::Transform(sBindG[toSrc[c]].Translation() - sBindG[toSrc[i]].Translation(), align);
            if (dt.Length() < 1e-4f * (std::max)(1.0f, tBindG[i].Translation().Length()) || ds.Length() < 1e-6f)
            {
                corr[i] = (mappedAnc[i] >= 0) ? corr[mappedAnc[i]] : Quaternion::Identity;   // 同じ位置の骨（腰と Waist 等）
                continue;
            }
            dt.Normalize();
            ds.Normalize();
            corr[i] = fromTo(dt, ds);
            const float deg = std::acos(std::clamp(dt.Dot(ds), -1.0f, 1.0f)) * 57.2958f;
            if (deg > worstDeg) { worstDeg = deg; worstBone = i; }
        }
        std::cout << "[SkinnedModel] retarget: rest pose fixed up to " << worstDeg << " deg ("
            << (worstBone >= 0 ? bones[worstBone].name : std::string("-")) << ")" << std::endl;
    }

    int added = 0;
    std::vector<const aiNodeAnim*> chOf(ns);
    std::vector<Matrix> sG(ns);
    std::vector<Quaternion> tGR(nb);
    for (unsigned a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* anim = scene->mAnimations[a];
        std::fill(chOf.begin(), chOf.end(), nullptr);
        for (unsigned c = 0; c < anim->mNumChannels; ++c)
        {
            auto it = srcIndex.find(anim->mChannels[c]->mNodeName.C_Str());
            if (it != srcIndex.end()) chOf[it->second] = anim->mChannels[c];
        }

        AnimationClip clip;
        clip.name = anim->mName.C_Str();
        for (const auto& existing : m_Animations)
            if (existing.name == clip.name)
            {
                clip.name = fs::path(filepath).stem().string() + "|" + clip.name;
                break;
            }
        clip.ticksPerSecond = (anim->mTicksPerSecond != 0.0) ? (float)anim->mTicksPerSecond : 25.0f;
        clip.duration = (float)anim->mDuration;

        // 全部の骨に channel を作る（ノードの既定の姿勢ではなくスキニングした時の姿勢を基準にするため）。
        // 付け替える骨は回転を毎フレーム、それ以外はスキニングした時の親基準の回転を 1 キー。
        // 平行移動と拡縮はスキニングした時の物を 1 キー（腰の平行移動だけ毎フレーム）
        std::vector<int> chOfBone(nb, -1);
        for (int i = 0; i < nb; ++i)
        {
            chOfBone[i] = (int)clip.channels.size();
            BoneChannel bc;
            bc.nodeName = bones[i].name;
            if (toSrc[i] < 0)
                bc.rotations.push_back({ 0.0f, tBindLocalR[i] });
            if (i != hipT)
                bc.positions.push_back({ 0.0f, tBindLocalT[i] });
            bc.scales.push_back({ 0.0f, tBindLocalS[i] });
            clip.nodeToChannel[bc.nodeName] = chOfBone[i];
            clip.channels.push_back(std::move(bc));
        }

        const double step = (double)clip.ticksPerSecond / 30.0;
        const int samples = (int)std::floor(clip.duration / step) + 1;
        for (int k = 0; k <= samples; ++k)
        {
            const double t = (std::min)((double)k * step, (double)clip.duration);

            // 向こうの世界の行列
            for (int i = 0; i < ns; ++i)
            {
                Matrix local = src[i].bind;
                if (const aiNodeAnim* ch = chOf[i])
                {
                    Vector3 bs, bt;
                    Quaternion br;
                    Matrix bc = src[i].bind;
                    bc.Decompose(bs, br, bt);
                    const Vector3 s = SampleVecKeys(ch->mScalingKeys, ch->mNumScalingKeys, t, bs);
                    const Quaternion r = SampleQuatKeys(ch->mRotationKeys, ch->mNumRotationKeys, t, br);
                    const Vector3 p = SampleVecKeys(ch->mPositionKeys, ch->mNumPositionKeys, t, bt);
                    local = Matrix::CreateScale(s) * Matrix::CreateFromQuaternion(r) * Matrix::CreateTranslation(p);
                }
                sG[i] = (src[i].parent >= 0) ? local * sG[src[i].parent] : local;
            }

            // 自分の世界の回転を親から順に。付け替える骨 = 基準の世界の回転（向きを直した物）に
            // 向こうの基準からの差分（向きを合わせて）を足す
            for (int i = 0; i < nb; ++i)
            {
                const int p = bones[i].parentIndex;
                const Quaternion parentR = (p >= 0) ? tGR[p] : Quaternion::Identity;
                const int s = toSrc[i];
                if (s < 0)
                {
                    tGR[i] = tBindLocalR[i] * parentR;
                    continue;
                }
                Quaternion dS = Inv(RotationOf(sBindG[s])) * RotationOf(sG[s]);   // 向こうの基準からの差分（世界）
                Quaternion dT = alignInv * dS * align;                               // 自分の空間へ
                tGR[i] = tBindGR[i] * corr[i] * dT;
                tGR[i].Normalize();
                Quaternion localR = tGR[i] * Inv(parentR);
                localR.Normalize();
                clip.channels[chOfBone[i]].rotations.push_back({ (float)t, localR });
            }

            // 腰の平行移動（世界の差分を向きを合わせて縮め、親基準へ）
            if (hipT >= 0 && hipS >= 0 && chOfBone[hipT] >= 0)
            {
                const Vector3 d = sG[hipS].Translation() - sBindG[hipS].Translation();
                const Vector3 world = tBindG[hipT].Translation() + Vector3::Transform(d, align) * hipScale;
                clip.channels[chOfBone[hipT]].positions.push_back({ (float)t, Vector3::Transform(world, hipParentInv) });
            }
            if (t >= clip.duration) break;
        }

        std::cout << "[SkinnedModel]   ~ " << clip.name << " (retargeted " << mapped << " bones)" << std::endl;
        m_Animations.push_back(std::move(clip));
        ++added;
    }
    std::cout << "[SkinnedModel] retarget: " << added << " clips from " << filepath << " (" << mapped
        << " bones mapped, hip scale " << hipScale << ")" << std::endl;
    return added;
}

Matrix SkinnedModel::GetSkinBindGlobal(int boneIndex) const
{
    const auto& bones = m_Skeleton.GetBones();
    if (boneIndex < 0 || boneIndex >= (int)bones.size()) return Matrix::Identity;
    for (const auto& sub : m_SubMeshes)
    {
        auto it = sub.boneOffsets.find(boneIndex);
        if (it != sub.boneOffsets.end()) return it->second.Invert() * sub.nodeGlobal;
    }
    const int p = bones[boneIndex].parentIndex;
    return (p >= 0) ? bones[boneIndex].localBindTransform * GetSkinBindGlobal(p) : bones[boneIndex].localBindTransform;
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
// 1 フレームを CPU でスキニング → 静的 Model
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