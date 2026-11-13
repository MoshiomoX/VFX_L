// ============================================================
// SwarmSystemModel.cpp
// 雑魚のモデル（候補表から読む）、部品アニメの表、間接描画の引数。
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Renderer/Renderer.h"   // DissolveCB
#include "Camera/CameraBase.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Light/PointLightManager.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "World/GridWorld.h"
#include "VFX_Editor/VFXSpriteRenderer.h"   // VFXSpriteCameraCB
#include "VFX_Editor/VFXLiquidRenderer.h"   // 液溜まりの CameraCB / FrameCB（CPU の経路と同じ並び）

namespace
{
    // 雑魚モデルの倍率。KayKit はメートル（身長 ≒ 1.8）でカプセル（1.8）と同じなので 1
    constexpr float kEnemyModelScale = 1.0f;
}

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

// ============================================================
// 雑魚の部品アニメの表
// 部品（submesh）をノードで動かすモデル用。クリップ毎に 30fps でサンプルを取り、
// 「焼いた姿勢の部品 → そのフレームの部品」の差分行列を [クリップ][フレーム][部品] に並べる。
//   焼いた頂点 = v × B（B = 焼いた姿勢の全体変換）
//   そのフレーム = v × G = 焼いた頂点 × (B の逆 × G)   … 行ベクトルの約束
// ループするクリップ（待機・歩き）は終端を含めず、攻撃は 1 回きりなので終端まで取る
// ============================================================
bool SwarmSystem::BuildEnemyPartAnim(ID3D11Device* device, const char* modelPath,
    const char* bakeClip, float bakeFrac, const DirectX::SimpleMath::Matrix& rootTransform,
    size_t partCount)
{
    using namespace DirectX::SimpleMath;
    m_EnemyAnim = EnemyAnimCB{};
    m_PartAnimSRV.Reset();
    m_PartAnimBuffer.Reset();
    if (partCount == 0) return false;

    // 焼いた姿勢の部品毎の全体変換
    std::vector<std::vector<Matrix>> bake;
    if (!Model::SampleSubmeshTransforms(modelPath, bakeClip, { bakeFrac }, rootTransform, bake)
        || bake.empty() || bake[0].size() != partCount)
    {
        std::cout << "[SwarmSystem] part anim: bake pose mismatch (procedural sway)" << std::endl;
        return false;
    }
    std::vector<Matrix> bakeInv(partCount);
    for (size_t p = 0; p < partCount; ++p) bakeInv[p] = bake[0][p].Invert();
    // 砕け散りの回転の中心 = 部品のノードの原点（首・肩・股の関節。網と同じ焼いた姿勢の空間）
    for (size_t p = 0; p < 8; ++p)
    {
        const Vector3 o = (p < partCount) ? bake[0][p].Translation() : Vector3::Zero;
        m_PartPivots[p] = Vector4(o.x, o.y, o.z, 1.0f);
    }

    constexpr float kSampleFps = 30.0f;
    constexpr int   kMaxFrames = 60;
    std::vector<Matrix> table;
    for (int clip = 0; clip < 3; ++clip)
    {
        const char* name = m_AnimClips[clip];
        const bool loop = (clip != 2);

        // 長さだけ先に知りたいので 1 点だけ取る
        float length = 0.0f;
        std::vector<std::vector<Matrix>> probe;
        if (!name || !*name
            || !Model::SampleSubmeshTransforms(modelPath, name, { 0.0f }, rootTransform, probe, &length))
        {
            // 無いクリップは焼いた姿勢（差分 = 単位行列）1 フレームで埋める
            m_EnemyAnim.clipStart[clip] = (uint32_t)(table.size() / partCount);
            m_EnemyAnim.clipFrames[clip] = 1;
            m_EnemyAnim.clipLength[clip] = 1.0f;
            for (size_t p = 0; p < partCount; ++p) table.push_back(Matrix::Identity);
            std::cout << "[SwarmSystem] part anim: clip not found: " << (name ? name : "") << std::endl;
            continue;
        }

        const int frames = (std::max)(2, (std::min)(kMaxFrames, (int)std::lround(length * kSampleFps)));
        std::vector<float> fracs(frames);
        for (int f = 0; f < frames; ++f)
            fracs[f] = loop ? (float)f / (float)frames : (float)f / (float)(frames - 1);

        std::vector<std::vector<Matrix>> samples;
        if (!Model::SampleSubmeshTransforms(modelPath, name, fracs, rootTransform, samples)) return false;

        m_EnemyAnim.clipStart[clip] = (uint32_t)(table.size() / partCount);
        m_EnemyAnim.clipFrames[clip] = (uint32_t)frames;
        m_EnemyAnim.clipLength[clip] = (length > 1e-3f) ? length : 1.0f;
        for (const auto& frame : samples)
        {
            if (frame.size() != partCount) return false;
            for (size_t p = 0; p < partCount; ++p)
                table.push_back(bakeInv[p] * frame[p]);
        }
    }

    // GPU へ（読み取り専用。Matrix は行優先のまま = VS は行ベクトルとして 4 行を読む）
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(Matrix) * table.size());
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Matrix);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = table.data();
    if (FAILED(device->CreateBuffer(&bd, &sd, &m_PartAnimBuffer))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = (UINT)table.size();
    if (FAILED(device->CreateShaderResourceView(m_PartAnimBuffer.Get(), &srv, &m_PartAnimSRV))) return false;

    m_EnemyAnim.partCount = (uint32_t)partCount;
    m_EnemyAnim.enabled = 1;
    std::cout << "[SwarmSystem] part anim: " << partCount << " parts, frames idle "
        << m_EnemyAnim.clipFrames[0] << " (" << m_EnemyAnim.clipLength[0] << "s) walk "
        << m_EnemyAnim.clipFrames[1] << " (" << m_EnemyAnim.clipLength[1] << "s) attack "
        << m_EnemyAnim.clipFrames[2] << " (" << m_EnemyAnim.clipLength[2] << "s), "
        << table.size() * sizeof(Matrix) / 1024 << " KB" << std::endl;
    return true;
}

// ============================================================
// 雑魚の見た目
// 骨付きモデルを歩きの 1 フレームで焼いて静的メッシュにする。
// 4096 体が同じポーズで、歩きの揺れは VS の procedural。
// 候補を上から試して、読めた物を使う:
//   1) Kenney Blocky の L（緑肌のゾンビ、ピクセルテクスチャ）… 今の雑魚
//   2) KayKit Skeleton_Minion                    … 予備
//   3) カプセル                                   … どちらも読めない時
// 位置合わせ: enemies[].position はカプセル中心（groundY = 半径 + 直線半分）
//   なので足元を -(radius + half) に置く。
// 大きさ: ファイルの単位（cm / m）に頼らず、一度そのまま焼いて測った高さを
//   targetHeight に合わせる（Kenney と KayKit で単位が違っても同じ寸法になる）
// ============================================================
std::shared_ptr<Model> SwarmSystem::BuildEnemyModel(ID3D11Device* device)
{
    using namespace DirectX::SimpleMath;
    const float footY = -(m_CachedAICB.enemyRadius + m_CachedAICB.enemyCapsuleHalf);

    struct EnemyLook
    {
        const char*    model;
        const wchar_t* albedo;
        const char*    clip;          // 焼く歩きのクリップ（無ければ idle）
        const char*    idleClip;
        const char*    attackClip;    // 近接攻撃（部品アニメの表にだけ使う）
        float          yawDeg;        // 正面を +Z に向ける回転
        float          targetHeight;  // m。0 = ファイルの寸法 × kEnemyModelScale のまま
        const char*    label;
        const wchar_t* bomberAlbedo;  // 自爆兵に貼る物（同じメッシュ・同じ UV）。null = 雑魚と同じ
        const wchar_t* splitterAlbedo;// スプリッター・分裂体に貼る物。null = 雑魚と同じ
        const wchar_t* bruteAlbedo;   // 重装兵に貼る物。null = 雑魚と同じ
        const wchar_t* chargerAlbedo; // 突撃兵に貼る物。null = 雑魚と同じ
        const wchar_t* shieldAlbedo;  // 盾兵に貼る物。null = 雑魚と同じ
        const wchar_t* burrowerAlbedo; // 砂潜りに貼る物。null = 雑魚と同じ
        const wchar_t* sandMageAlbedo; // 砂嵐の術師に貼る物。null = 雑魚と同じ
        const wchar_t* dashBomberAlbedo; // 突撃爆弾兵に貼る物。null = 雑魚と同じ
    };
    const EnemyLook looks[] =
    {
        // Kenney も KayKit と同じく -Z が正面（+Z のままだと背中を向けて歩いた）→ 180 度回す。
        // 高さはカプセル（1.8m）より少し低く
        { Res::Mdl::Kenney_BlockyZombie, Res::Tex::Kenney_BlockyZombieAlbedo,
          "walk", "idle", "attack-melee-right", 180.0f, 1.6f, "Kenney Blocky L (zombie)",
          Res::Tex::Kenney_BlockyRobotAlbedo, Res::Tex::Kenney_BlockyDummyAlbedo, Res::Tex::Kenney_BlockyOrcAlbedo,
          Res::Tex::Kenney_BlockyNinjaAlbedo, Res::Tex::Kenney_BlockyPoliceAlbedo, Res::Tex::Kenney_BlockyBurrowerAlbedo,
          Res::Tex::Kenney_BlockySandMageAlbedo, Res::Tex::Kenney_BlockyDashBomberAlbedo },
        // KayKit は Blender 出力の -Z が正面 → 180 度回す
        { Res::Mdl::KayKit_SkeletonMinion, Res::Tex::KayKit_SkeletonAlbedo,
          "Walking_A", "Idle", "", 180.0f, 0.0f, "Skeleton_Minion", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
    };

    // 焼く前の寸法 → 焼く時に掛ける変換。
    // 中心を xz の原点へ、足の裏を 0 へ → 拡縮 → 向き → カプセルの足元へ。
    // 寸法が取れない（0）時は拡縮 kEnemyModelScale、ずらし無し（Minion の従来どおり）
    auto makeXform = [&](const EnemyLook& look, const Vector3& lo, const Vector3& hi, float& outScale)
        {
            const float height = hi.y - lo.y;
            outScale = (look.targetHeight > 0.0f && height > 1e-4f)
                ? look.targetHeight / height : kEnemyModelScale;
            return Matrix::CreateTranslation(-(lo.x + hi.x) * 0.5f, -lo.y, -(lo.z + hi.z) * 0.5f)
                * Matrix::CreateScale(outScale)
                * Matrix::CreateRotationY(DirectX::XMConvertToRadians(look.yawDeg))
                * Matrix::CreateTranslation(0.0f, footY, 0.0f);
        };

    for (const EnemyLook& look : looks)
    {
        auto loaded = ResourceManager::Get().LoadModelAuto(look.model);

        // ---- 骨（skin weights）の無いモデル: 部品をノードで動かす FBX（Kenney Blocky）----
        // LoadModelAuto は Static と判定する。Model::Load に歩きの姿勢と変換を渡して焼く
        if (loaded.kind == ModelKind::Static && loaded.staticModel)
        {
            Model::LoadOptions opt;
            opt.poseClip = look.clip;
            opt.poseTimeFrac = 0.25f;   // 片足が前に出た辺り

            // 一度そのまま読んで寸法を測る（535KB の FBX なので 2 回読んでも軽い）
            auto raw = std::make_shared<Model>();
            if (!raw->Load(device, look.model, opt)) continue;
            Vector3 lo = raw->GetBoundsMin();
            Vector3 hi = raw->GetBoundsMax();
            // 足元と高さは待機の姿勢で測る。歩きの 0.25 は両脚を一番開いた所で最低点が一番高く、
            // それに合わせると待機・攻撃で足が地面に埋まる。
            // 待機に合わせると立っている時は足が地面に着き、歩きは脚が振り子のように上がる（Kenney の元の動き）
            {
                Model::LoadOptions standOpt;
                standOpt.poseClip = look.idleClip;
                standOpt.poseTimeFrac = 0.0f;
                Model stand;
                if (look.idleClip && *look.idleClip && stand.Load(device, look.model, standOpt)
                    && stand.GetBoundsMax().y - stand.GetBoundsMin().y > 1e-4f)
                {
                    lo = stand.GetBoundsMin();
                    hi = stand.GetBoundsMax();
                }
            }

            float scale = 1.0f;
            opt.rootTransform = makeXform(look, lo, hi, scale);
            auto baked = std::make_shared<Model>();
            if (!baked->Load(device, look.model, opt)) continue;

            m_EnemyMaterial->SetAlbedoTexture(ResourceManager::Get().LoadTexture(look.albedo));
            m_BomberAlbedo = look.bomberAlbedo ? ResourceManager::Get().LoadTexture(look.bomberAlbedo) : nullptr;
            m_SplitterAlbedo = look.splitterAlbedo ? ResourceManager::Get().LoadTexture(look.splitterAlbedo) : nullptr;
            m_BruteAlbedo = look.bruteAlbedo ? ResourceManager::Get().LoadTexture(look.bruteAlbedo) : nullptr;
            m_ChargerAlbedo = look.chargerAlbedo ? ResourceManager::Get().LoadTexture(look.chargerAlbedo) : nullptr;
            m_ShieldAlbedo = look.shieldAlbedo ? ResourceManager::Get().LoadTexture(look.shieldAlbedo) : nullptr;
            m_BurrowerAlbedo = look.burrowerAlbedo ? ResourceManager::Get().LoadTexture(look.burrowerAlbedo) : nullptr;
            m_SandMageAlbedo = look.sandMageAlbedo ? ResourceManager::Get().LoadTexture(look.sandMageAlbedo) : nullptr;
            m_DashBomberAlbedo = look.dashBomberAlbedo ? ResourceManager::Get().LoadTexture(look.dashBomberAlbedo) : nullptr;

            // 部品アニメの表（待機・歩き・近接攻撃）。作れなければ従来の揺れで動く
            m_AnimClips[0] = look.idleClip;
            m_AnimClips[1] = look.clip;
            m_AnimClips[2] = look.attackClip;
            BuildEnemyPartAnim(device, look.model, look.clip, opt.poseTimeFrac,
                opt.rootTransform, baked->GetSubMeshes().size());

            std::cout << "[SwarmSystem] enemy model: " << look.label << " (node pose '" << look.clip
                << "', raw height " << (hi.y - lo.y) << " -> scale " << scale
                << ", baked size " << (baked->GetBoundsMax() - baked->GetBoundsMin()).x << " x "
                << (baked->GetBoundsMax() - baked->GetBoundsMin()).y << " x "
                << (baked->GetBoundsMax() - baked->GetBoundsMin()).z << ")" << std::endl;
            return baked;
        }

        // ---- 骨付きモデル: 1 フレームを CPU スキニングして焼く（KayKit）----
        if (loaded.kind != ModelKind::Skinned || !loaded.skinnedModel) continue;

        const auto& sk = *loaded.skinnedModel;
        int clip = sk.FindClip(look.clip);
        if (clip < 0) clip = sk.FindClip(look.idleClip);
        if (clip < 0 && sk.GetClipCount() > 0) clip = 0;
        const float t = (clip >= 0) ? sk.GetClipDurationSec(clip) * 0.25f : 0.0f;   // 片足が前に出た辺り

        // 一度そのまま焼いて寸法を測る。※BakeStatic は包囲ボックスを持たないので今は 0 が返り、
        //   拡縮は kEnemyModelScale のまま（Minion は targetHeight = 0 なので元々それで良い）
        auto raw = sk.BakeStatic(device, clip, t, Matrix::Identity, {});
        if (!raw) continue;
        float scale = 1.0f;
        const Matrix xform = makeXform(look, raw->GetBoundsMin(), raw->GetBoundsMax(), scale);

        auto baked = sk.BakeStatic(device, clip, t, xform, {});
        if (!baked) continue;

        // テクスチャは雑魚材質の t0 へ（VS/PS は雑魚専用のまま。頂点色は白で焼いてある）
        m_EnemyMaterial->SetAlbedoTexture(ResourceManager::Get().LoadTexture(look.albedo));
        m_BomberAlbedo = look.bomberAlbedo ? ResourceManager::Get().LoadTexture(look.bomberAlbedo) : nullptr;
        m_SplitterAlbedo = look.splitterAlbedo ? ResourceManager::Get().LoadTexture(look.splitterAlbedo) : nullptr;
        m_BruteAlbedo = look.bruteAlbedo ? ResourceManager::Get().LoadTexture(look.bruteAlbedo) : nullptr;
        m_ChargerAlbedo = look.chargerAlbedo ? ResourceManager::Get().LoadTexture(look.chargerAlbedo) : nullptr;
        m_ShieldAlbedo = look.shieldAlbedo ? ResourceManager::Get().LoadTexture(look.shieldAlbedo) : nullptr;
        m_BurrowerAlbedo = look.burrowerAlbedo ? ResourceManager::Get().LoadTexture(look.burrowerAlbedo) : nullptr;
        m_SandMageAlbedo = look.sandMageAlbedo ? ResourceManager::Get().LoadTexture(look.sandMageAlbedo) : nullptr;
        m_DashBomberAlbedo = look.dashBomberAlbedo ? ResourceManager::Get().LoadTexture(look.dashBomberAlbedo) : nullptr;
        std::cout << "[SwarmSystem] enemy model: " << look.label << " (baked, clip "
            << (clip >= 0 ? sk.GetClipName(clip) : std::string("-"))
            << ", scale " << scale << ")" << std::endl;
        return baked;
    }

    std::cout << "[SwarmSystem] enemy model: capsule (fallback)" << std::endl;
    // 衝突体と同じ寸法（AICB.enemyRadius = 0.4 と揃える）
    return PrimitiveBuilder::CreateCapsule(device, m_CachedAICB.enemyRadius,
        m_CachedAICB.enemyCapsuleHalf * 2.0f, { 0.85f, 0.25f, 0.25f, 1.0f });
}

// ============================================================
// 雑魚描画の間接引数
//   aliveList: 活きスロット番号（CompactCS が毎フレーム append）。HP バー用
//   kindList : 同じく種類毎（雑魚 / 自爆兵）。本描画は種類毎に 1 回
//   args[k][i]: DrawIndexedInstancedIndirect の 5 uint。IndexCount は submesh 固有、
//              InstanceCount は種類 k の一覧から CopyStructureCount で毎フレーム上書き
// ============================================================
bool SwarmSystem::CreateEnemyDrawArgs(ID3D11Device* device)
{
    if (!m_EnemyModel) return false;

    // 活きスロットの一覧（append）。全種類 1 本 + 種類毎
    auto makeList = [&](ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11UnorderedAccessView>& uav,
        ComPtr<ID3D11ShaderResourceView>& srv) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = sizeof(uint32_t) * Swarm::kMaxEnemies;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sizeof(uint32_t);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;

            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
            ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = Swarm::kMaxEnemies;
            ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
            if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav))) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = Swarm::kMaxEnemies;
            return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &sd, &srv));
        };
    if (!makeList(m_AliveListBuffer, m_AliveListUAV, m_AliveListSRV)) return false;
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        if (!makeList(m_KindListBuffer[k], m_KindListUAV[k], m_KindListSRV[k])) return false;
    if (!makeList(m_SpikeBallListBuffer, m_SpikeBallListUAV, m_SpikeBallListSRV)) return false;   // トゲ玉の一覧
    // 砕け散り中の尸の一覧（見た目毎。大きさは敵と同じで足りる）
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        if (!makeList(m_CorpseListBuffer[k], m_CorpseListUAV[k], m_CorpseListSRV[k])) return false;

    // submesh 毎の間接引数（IndexCount が違う）。生きている敵と尸で別々に持つ
    auto makeDrawArgs = [&](std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>& out) -> bool
        {
            out.clear();
            for (const auto& sub : m_EnemyModel->GetSubMeshes())
            {
                if (!sub.mesh) { out.emplace_back(); continue; }

                D3D11_BUFFER_DESC desc = {};
                desc.ByteWidth = sizeof(uint32_t) * 5;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
                desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
                // IndexCountPerInstance, InstanceCount, StartIndex, BaseVertex, StartInstance
                const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
                D3D11_SUBRESOURCE_DATA sd = {};
                sd.pSysMem = init;
                Microsoft::WRL::ComPtr<ID3D11Buffer> args;
                if (FAILED(device->CreateBuffer(&desc, &sd, &args))) return false;
                out.push_back(args);
            }
            return true;
        };
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
    {
        if (!makeDrawArgs(m_EnemyDrawArgs[k])) return false;
        if (!makeDrawArgs(m_CorpseDrawArgs[k])) return false;
    }

    // HP バー: VertexCountPerInstance, InstanceCount, StartVertex, StartInstance
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * 4;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        const uint32_t init[4] = { 6, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = init;
        if (FAILED(device->CreateBuffer(&desc, &sd, &m_HpBarArgs))) return false;
        // 自爆兵の警告の輪も同じ形（InstanceCount は自爆兵の一覧から）。突撃爆弾兵の分はその一覧から
        if (FAILED(device->CreateBuffer(&desc, &sd, &m_BomberRingArgs))) return false;
        if (FAILED(device->CreateBuffer(&desc, &sd, &m_DashBomberRingArgs))) return false;
    }
    return true;
}
