// ============================================================
// SwarmSystemShaders.cpp
// シェーダーの読み込み（LoadShaders）。
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
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

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

// ============================================================
// シェーダー読み込み
// ============================================================
bool SwarmSystem::LoadShaders(ID3D11Device* device)
{
    auto load = [&](std::shared_ptr<ComputeShader>& cs, const wchar_t* path,
        const char* name) -> bool
        {
            cs = std::make_shared<ComputeShader>();
            HRESULT hr = ShaderPath::Load(cs.get(), device, path);
            std::cout << "[SwarmSystem] " << name << ": "
                << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
            if (FAILED(hr)) { cs.reset(); return false; }
            return true;
        };

    bool ok = true;
    ok &= load(m_ClearCountersCS, L"Shader/Swarm/SwarmClearCountersCS.hlsl", "ClearCountersCS");
    ok &= load(m_SpawnProjCS, L"Shader/Swarm/SwarmSpawnProjCS.hlsl", "SpawnProjCS");
    ok &= load(m_ProjMoveCS, L"Shader/Swarm/SwarmProjMoveCS.hlsl", "ProjMoveCS");
    ok &= load(m_ProjEndCS, L"Shader/Swarm/SwarmProjEndCS.hlsl", "ProjEndCS");
    ok &= load(m_BeamTargetCS, L"Shader/Swarm/SwarmBeamTargetCS.hlsl", "BeamTargetCS");
    ok &= load(m_EmitCS, L"Shader/Swarm/SwarmEmitCS.hlsl", "EmitCS");
    ok &= load(m_SpawnEnemyCS, L"Shader/Swarm/SwarmSpawnEnemyCS.hlsl", "SpawnEnemyCS");
    ok &= load(m_EnemyAICS, L"Shader/Swarm/SwarmEnemyAICS.hlsl", "EnemyAICS");
    ok &= load(m_EnemyMoveCS, L"Shader/Swarm/SwarmEnemyMoveCS.hlsl", "EnemyMoveCS");
    ok &= load(m_RecycleCS, L"Shader/Swarm/SwarmRecycleCS.hlsl", "RecycleCS");
    ok &= load(m_HitCS, L"Shader/Swarm/SwarmHitCS.hlsl", "HitCS");
    ok &= load(m_AimResolveCS, L"Shader/Swarm/SwarmAimResolveCS.hlsl", "AimResolveCS");
    ok &= load(m_ContactCS, L"Shader/Swarm/SwarmContactCS.hlsl", "ContactCS");
    ok &= load(m_OrbCS, L"Shader/Swarm/SwarmOrbMoveCS.hlsl", "OrbMoveCS");
    ok &= load(m_SpawnAreaCS, L"Shader/Swarm/SwarmSpawnAreaCS.hlsl", "SpawnAreaCS");
    ok &= load(m_AreaTickCS, L"Shader/Swarm/SwarmAreaTickCS.hlsl", "AreaTickCS");
    ok &= load(m_AreaDamageCS, L"Shader/Swarm/SwarmAreaDamageCS.hlsl", "AreaDamageCS");
    ok &= load(m_AreaEmitCS, L"Shader/Swarm/SwarmAreaEmitCS.hlsl", "AreaEmitCS");
    load(m_FireEmitCS, L"Shader/Grass/GrassFireEmitCS.hlsl", "GrassFireEmitCS");   // 無くても草の火の粒子が出ないだけ
    load(m_OrbEmitCS, L"Shader/Swarm/SwarmOrbEmitCS.hlsl", "OrbEmitCS");   // 無くてもオーブの尾が出ないだけ
    load(m_SpriteCS, L"Shader/Swarm/SwarmSpriteCS.hlsl", "SpriteCS");   // 無くても連番画像が出ないだけ
    load(m_ProjTrailCS, L"Shader/Swarm/SwarmProjTrailCS.hlsl", "ProjTrailCS");   // 無くても弾の帯が出ないだけ
    load(m_AreaTrackCS, L"Shader/Swarm/SwarmAreaTrackCS.hlsl", "AreaTrackCS");   // 無くても液溜まりと範囲の誕生の数（揺れ・音）が出ないだけ
    load(m_CorpseTrackCS, L"Shader/Swarm/SwarmCorpseTrackCS.hlsl", "CorpseTrackCS");   // 無くても死体が砕けないだけ
    load(m_DamageTextCS, L"Shader/Swarm/SwarmDamageTextCS.hlsl", "DamageTextCS");   // 無くてもダメージの数字が出ないだけ
    load(m_CorpseListCS, L"Shader/Swarm/SwarmCorpseListCS.hlsl", "CorpseListCS");
    ok &= load(m_LightCollectCS, L"Shader/Swarm/SwarmLightCollectCS.hlsl", "LightCollectCS");
    ok &= load(m_AreaLightCollectCS, L"Shader/Swarm/SwarmAreaLightCollectCS.hlsl", "AreaLightCollectCS");
    ok &= load(m_EnemyCompactCS, L"Shader/Swarm/SwarmEnemyCompactCS.hlsl", "EnemyCompactCS");
    ok &= load(m_EnemyBinCS, L"Shader/Swarm/SwarmEnemyBinCS.hlsl", "EnemyBinCS");
    ok &= load(m_EnemyPushCS, L"Shader/Swarm/SwarmEnemyPushCS.hlsl", "EnemyPushCS");
    // ---- デバッグ描画（失敗しても gameplay には影響しない）----
    m_DebugVS = std::make_shared<VertexShader>();
    HRESULT hr = ShaderPath::Load(m_DebugVS.get(), device, L"Shader/Swarm/SwarmDebugProjVS.hlsl");
    std::cout << "[SwarmSystem] DebugProjVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DebugVS.reset();

    m_DebugPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_DebugPS.get(), device, L"Shader/Swarm/SwarmDebugPS.hlsl");
    std::cout << "[SwarmSystem] DebugPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DebugPS.reset();

    m_DebugEnemyVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_DebugEnemyVS.get(), device, L"Shader/Swarm/SwarmDebugEnemyVS.hlsl");
    std::cout << "[SwarmSystem] DebugEnemyVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DebugEnemyVS.reset();

    // ---- 雑魚の HP バー（失敗してもバーが出ないだけ）----
    m_HpBarVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_HpBarVS.get(), device, L"Shader/Swarm/SwarmEnemyHpBarVS.hlsl");
    std::cout << "[SwarmSystem] EnemyHpBarVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_HpBarVS.reset();

    m_HpBarPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_HpBarPS.get(), device, L"Shader/Swarm/SwarmEnemyHpBarPS.hlsl");
    std::cout << "[SwarmSystem] EnemyHpBarPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_HpBarPS.reset();

    // ---- 雑魚の足元の丸い影（失敗しても影が出ないだけ）----
    m_BlobShadowVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_BlobShadowVS.get(), device, L"Shader/Swarm/SwarmBlobShadowVS.hlsl");
    std::cout << "[SwarmSystem] BlobShadowVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BlobShadowVS.reset();

    m_BlobShadowPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_BlobShadowPS.get(), device, L"Shader/Swarm/SwarmBlobShadowPS.hlsl");
    std::cout << "[SwarmSystem] BlobShadowPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BlobShadowPS.reset();

    // ---- 自爆兵の警告の輪（失敗しても輪が出ないだけ）----
    m_BomberRingVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_BomberRingVS.get(), device, L"Shader/Swarm/SwarmBomberRingVS.hlsl");
    std::cout << "[SwarmSystem] BomberRingVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BomberRingVS.reset();

    m_BomberRingPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_BomberRingPS.get(), device, L"Shader/Swarm/SwarmBomberRingPS.hlsl");
    std::cout << "[SwarmSystem] BomberRingPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BomberRingPS.reset();

    // ---- 隕石の警告の輪（PS は自爆兵の輪と共用。失敗しても輪が出ないだけ）----
    m_DropRingVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_DropRingVS.get(), device, L"Shader/Swarm/SwarmDropRingVS.hlsl");
    std::cout << "[SwarmSystem] DropRingVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DropRingVS.reset();

    // ---- 地面の警告の輪（Boss のスラム。PS は自爆兵の輪と共用）----
    m_WarnRingVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_WarnRingVS.get(), device, L"Shader/Swarm/SwarmWarnRingVS.hlsl");
    std::cout << "[SwarmSystem] WarnRingVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_WarnRingVS.reset();

    // ---- 範囲の連番画像（失敗しても出ないだけ）----
    m_SpriteVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_SpriteVS.get(), device, L"Shader/Swarm/SwarmSpriteVS.hlsl");
    std::cout << "[SwarmSystem] SpriteVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_SpriteVS.reset();

    // 弾の帯。PS は粒子の帯の物をそのまま使う
    m_ProjTrailVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_ProjTrailVS.get(), device, L"Shader/Swarm/SwarmProjTrailVS.hlsl");
    std::cout << "[SwarmSystem] ProjTrailVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_ProjTrailVS.reset();
    m_ProjTrailPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_ProjTrailPS.get(), device, L"Shader/Particle/ParticleTrailPS.hlsl");
    std::cout << "[SwarmSystem] ProjTrailPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_ProjTrailPS.reset();

    m_SpritePS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_SpritePS.get(), device, L"Shader/Swarm/SwarmSpritePS.hlsl");
    std::cout << "[SwarmSystem] SpritePS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_SpritePS.reset();

    // ---- 死んだ敵の砕け散り（PS は雑魚と同じ。失敗しても砕けないだけ）----
    m_CorpseVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_CorpseVS.get(), device, L"Shader/Swarm/SwarmCorpseVS.hlsl");
    std::cout << "[SwarmSystem] CorpseVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_CorpseVS.reset();

    // ---- 液溜まり（Liquid entry。PS は CPU の経路と共用。失敗しても出ないだけ）----
    m_LiquidVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_LiquidVS.get(), device, L"Shader/Swarm/SwarmLiquidVS.hlsl");
    std::cout << "[SwarmSystem] LiquidVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_LiquidVS.reset();

    m_LiquidPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_LiquidPS.get(), device, L"Shader/VFX/VFXLiquidPS.hlsl");
    std::cout << "[SwarmSystem] LiquidPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_LiquidPS.reset();

    // テクスチャ無しの Material は Bind で既定の白を t0 に入れる。その白を用意しておく
    Material::InitDefaultTextures(device);

    // ---- 雑魚の本描画 ----
    m_EnemyVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_EnemyVS.get(), device, L"Shader/Swarm/SwarmEnemyVS.hlsl");
    std::cout << "[SwarmSystem] EnemyVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_EnemyVS.reset();

    m_EnemyPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_EnemyPS.get(), device, L"Shader/PS.hlsl");
    std::cout << "[SwarmSystem] EnemyPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_EnemyPS.reset();
    // ---- 経験値オーブの本描画 ----
    m_OrbVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_OrbVS.get(), device, L"Shader/Swarm/SwarmOrbVS.hlsl");
    std::cout << "[SwarmSystem] OrbVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_OrbVS.reset();

    m_OrbPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_OrbPS.get(), device, L"Shader/Swarm/SwarmOrbPS.hlsl");
    std::cout << "[SwarmSystem] OrbPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_OrbPS.reset();

    if (m_OrbVS && m_OrbPS)
    {
        m_OrbMaterial = std::make_shared<Material>();
        m_OrbMaterial->SetVertexShader(m_OrbVS);
        m_OrbMaterial->SetPixelShader(m_OrbPS);

        // 見た目だけの大きさ（判定は OrbCB.pickupRadius で別）。
        // 四角の双角錐：横から見ると菱形、自転で面が光ったり陰ったりする。色は VS が orbLook から入れる
        m_OrbModel = PrimitiveBuilder::CreateBipyramid(device, 0.13f, 0.17f, 0.23f, 4);
    }
    if (m_EnemyVS && m_EnemyPS)
    {
        // テクスチャ無し → Bind で既定の白が t0 に入る。色は頂点色で出す
        m_EnemyMaterial = std::make_shared<Material>();
        m_EnemyMaterial->SetVertexShader(m_EnemyVS);
        m_EnemyMaterial->SetPixelShader(m_EnemyPS);

        m_EnemyModel = BuildEnemyModel(device);
        if (!CreateEnemyDrawArgs(device))
        {
            std::cout << "[SwarmSystem] enemy draw args: FAILED (falls back to full-pool DrawInstanced)" << std::endl;
            for (auto& args : m_EnemyDrawArgs) args.clear();
            for (auto& args : m_CorpseDrawArgs) args.clear();   // 砕け散りも描かない
        }
        // 突撃兵の帯・盾兵の盾・凍結の氷（失敗しても出ないだけ）
        LoadKindAssets(device);
        // 遠くの雑魚の LOD（失敗したら従来どおり全部を部品毎に描く）
        CreateLod(device);
    }

    return ok;
}
