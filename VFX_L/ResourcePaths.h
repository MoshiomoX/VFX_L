// ============================================================
// ResourcePaths.h
// ゲーム資源のパス/IDを一元管理する
// 文字列の直書きを避け、変更箇所を1ファイルに集約する
//
// 使い方：
//   ResourceManager::Get().LoadTexture(Res::Tex::Rock2_Albedo);
//   ResourceManager::Get().LoadModel(Res::Mdl::Rock2);
//   shader: Res::Shd::PBR_VS など
//
// 型の使い分け（API に合わせる）：
//   LoadTexture / LoadVS / LoadPS / Compile → wchar_t（wstring）
//   LoadModel / VFX データ                  → char（string）
// ============================================================
#pragma once
#include "Graphics/Model/BoneMaps.h"   // kExtraAnims の骨名の対応表

namespace Res
{
    // ========================================================
    // ディレクトリ（ベースパス、末尾スラッシュ付き）
    // ========================================================
    namespace Dir
    {
        inline constexpr const char* Assets = "Assets/";
        inline constexpr const char* Models = "Assets/Model/";
        inline constexpr const wchar_t* Particles = L"Assets/Particles/";
        inline constexpr const char* VFXData = "Assets/Data/VFXData/";
        inline constexpr const wchar_t* ShaderDir = L"Shader/";
        inline constexpr const char* VFXMesh = "Assets/VFX/Mesh";   // VFXFileList 用（末尾スラッシュ無し）
        inline constexpr const char* VFXTex = "Assets/VFX/Tex";
    }

    // ========================================================
    // シェーダー（wchar_t）
    // ========================================================
    namespace Shd
    {
        // デフォルト（既存、簡易表示用）
        inline constexpr const wchar_t* DefaultVS = L"Shader/VS.hlsl";
        inline constexpr const wchar_t* DefaultPS = L"Shader/PS.hlsl";

        // PBR用
        inline constexpr const wchar_t* PBR_VS = L"Shader/PBR_VS.hlsl";
        inline constexpr const wchar_t* PBR_PS = L"Shader/PBR_PS.hlsl";

        // VFX 用（光を当てない）
        inline constexpr const wchar_t* VFXMesh_VS = L"Shader/VFX/VFXMeshVS.hlsl";
        inline constexpr const wchar_t* VFXMesh_PS = L"Shader/VFX/VFXMeshPS.hlsl";
        inline constexpr const wchar_t* NoiseGen_CS = L"Shader/VFX/NoiseGenCS.hlsl";
    }

    // ========================================================
    // VFX 用アセット（Mesh entry のモデル / ノイズ等のテクスチャ）
    // Editor はフォルダを列挙して選ぶので、ここは既定値と
    // コードから直接参照する物だけ
    // ========================================================
    namespace VFX
    {
        inline constexpr const char* Slash = "Assets/VFX/Mesh/Slash.fbx";
        // 盾兵が身体の前に持つ塔の盾（2026-10-08、SwarmShieldVS）
        inline constexpr const char* TowerShield = "Assets/VFX/Mesh/dun01.FBX";
        // 凍った敵の足元の氷の塊（2026-10-08、SwarmIceVS。アイスランスの弾・命中の特効も同じ物）
        inline constexpr const char* IceSpikes = "Assets/VFX/Mesh/bingci_02.FBX";

        inline constexpr const wchar_t* Noise001 = L"Assets/VFX/Tex/Noise_001.png";
        inline constexpr const wchar_t* Noise002 = L"Assets/VFX/Tex/Noise_002.png";
        inline constexpr const wchar_t* Noise003 = L"Assets/VFX/Tex/Noise_003.png";
    }
    // ========================================================
    // モデル（char）
    // ========================================================
    namespace Mdl
    {
        inline constexpr const char* Rock2 = "Assets/Model/Rock-Set/Rock_2/Rock_2.fbx";
        // 必要なら .obj 版も
        inline constexpr const char* Rock2_Obj = "Assets/Model/Rock-Set/Rock_2/Rock_2.obj";
        // 巨石（Rock-Set、PBR テクスチャ付き）：外周の岩山・洞窟の上の山に数個を引き伸ばして使う（2026-10-06）
        inline constexpr const char* kBoulders[] = {
            "Assets/Model/Rock-Set/Rock_2/Rock_2.fbx",
            "Assets/Model/Rock-Set/Rock_5/Rock_5.fbx",
            "Assets/Model/Rock-Set/Rock_6/Rock_6.fbx",
            "Assets/Model/Rock-Set/Rock_7/Rock_7.fbx",
            "Assets/Model/Rock-Set/Rock_9/Rock_9.fbx",
        };
        inline constexpr const char* Akai = "Assets/Model/Akai/Akai.fbx";

        inline constexpr const char* Jiandu_TPose =
            "Assets/Model/Jiandu/Jian_TPose.fbx";   // ①静的bind pose検証用
        inline constexpr const char* Jiandu_Idle =
            "Assets/Model/Jiandu/Idle.fbx";         // ②アニメ検証用

        inline constexpr const char* Paladin_Idle =
            "Assets/Model/testAnimModel/Idle.fbx";

        inline constexpr const char* Paladin =
            "Assets/Model/testAnimModel/PaladinWPropJNordstrom.fbx";


        // KayKit Adventurers の Mage（CC0）。1 ファイルに 76 クリップ入り。
        // 杖・杖・魔道書は handslot 骨に付いた submesh で、表示切替で持ち替える
        inline constexpr const char* KayKit_Mage =
            "Assets/Model/KayKit_Mage/Mage.fbx";

        // プレイヤー（2026-09-28 から）。Quaternius の CC0 素材 4 つを Tools/BuildPlayerModel.py で 1 つの GLB に:
        // 素体の頭 + レンジャーの服 + 白い髭、アニメ 24 本（Universal Animation Library 1/2）入り。
        // 65 骨（UE 風の名前 root / pelvis / spine_01..）、メートル、glTF の -Z が正面
        inline constexpr const char* Quaternius_Ranger =
            "Assets/Model/Quaternius_Ranger/Ranger.fbx";

        // KayKit Character Animations 1.1（CC0）の Rig_Medium。モデル無しのアニメだけのファイル。
        // 骨名が Adventurers 1.0 の Mage と同じなので、読み込み時に骨名で足す（kExtraAnims）
        namespace KayKitAnims
        {
            inline constexpr const char* MovementAdvanced =   // Crouching / Sneaking / Crawling / Dodge_* など
                "Assets/Model/KayKit_CharacterAnimations/Rig_Medium_MovementAdvanced.fbx";
        }

        // 骨付きモデルを読んだ直後に足す別ファイルのアニメ（SkinnedModel::AddAnimationsFromFile）。
        // ResourceManager::ImportModelAuto がパスの一致で引く（先読みスレッドの中でも同じ）
        // map があれば骨名の違う人形から世界空間で付け替える（Graphics/Model/BoneMaps.h、2026-10-04）
        struct ExtraAnimSet
        {
            const char* model;
            const char* animFile;
            const BoneMapEntry* map = nullptr;
            int mapCount = 0;
        };

        // Reallusion CC 骨（CC_Base_*）の PBR の人形（ユーザーがUnrealの配布から落とした物）。
        // アニメは持たないので、レンジャー（UE 風の骨）の 24 段を付け替えて使う（VFXL_PLAYER=shadowkin でプレイヤーに）
        inline constexpr const char* Shadowkin = "Assets/Model/Shadowkin_SF/Shadowkin_Rigged.fbx";

        inline constexpr ExtraAnimSet kExtraAnims[] = {
            { KayKit_Mage, KayKitAnims::MovementAdvanced },
            { Shadowkin, Quaternius_Ranger, kBoneMapCCFromUE, (int)(sizeof(kBoneMapCCFromUE) / sizeof(kBoneMapCCFromUE[0])) },
        };

        // KayKit Skeletons（CC0）。Minion は雑魚（1 フレーム焼いてインスタンス描画）、
        // Warrior / Mage / Rogue はエリート（骨付きのまま SkinnedAnimComponent）
        inline constexpr const char* KayKit_SkeletonMinion =
            "Assets/Model/KayKit_Skeletons/Skeleton_Minion.fbx";
        inline constexpr const char* KayKit_SkeletonWarrior =
            "Assets/Model/KayKit_Skeletons/Skeleton_Warrior.fbx";
        inline constexpr const char* KayKit_SkeletonMage =
            "Assets/Model/KayKit_Skeletons/Skeleton_Mage.fbx";
        inline constexpr const char* KayKit_SkeletonRogue =
            "Assets/Model/KayKit_Skeletons/Skeleton_Rogue.fbx";

        // Kenney Blocky Characters（CC0、ピクセルテクスチャのブロック人形）。18 体ともメッシュ・骨・動画は同じで、
        // テクスチャ（Textures/texture-a..r.png）だけ違う。雑魚は L（緑肌のゾンビ）を 1 フレーム焼いて使う
        inline constexpr const char* Kenney_BlockyZombie =
            "Assets/Model/Kenney_BlockyCharacters/fbx/character-l.fbx";

        // 報酬の箱（近づいて F で三択）。Kenney Retro Fantasy のピクセルの木箱（0.3m 角、底が原点）
        inline constexpr const char* Kenney_RewardCrate =
            "Assets/Model/Kenney_RetroFantasy/fbx/detail-crate.fbx";

        // Boss を呼ぶ門（近づいて F）。Kenney Retro Fantasy の石の門（1m 角の部品、底が原点）。
        // 2026-10-03 から使っていない（ユーザー：石の門に → Ruins_ArchGate）。比べる用に残す
        inline constexpr const char* Kenney_PortalGate =
            "Assets/Model/Kenney_RetroFantasy/fbx/wall-flat-gate.fbx";
        // Boss を呼ぶ門（2026-10-03）。Quaternius Modular Ruins の丸柱付きの丸い石の拱（CC0、cm 単位、
        // 3.09 × 0.49 × 3.53m、拱の面は XY）。間に粒子の渦（VFXData/BossPortal.json）
        inline constexpr const char* Ruins_ArchGate =
            "Assets/Model/Quaternius_ModularRuins/FBX/Arch_Round_RoundColumn.fbx";

        // 戦闘の地形に撒く自然物（KayKit Forest、CC0、cm 単位・底が原点、共通の forest_texture.png）。
        // 木と岩は通れない置物、茂みと草は見た目だけ（TerrainGenerator）
        namespace Forest
        {
            inline constexpr const char* kTrees[] = {
                "Assets/Model/KayKit_Forest/fbx/Tree_1_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_1_B_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_2_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_2_C_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_3_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_3_B_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_4_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_4_B_Color1.fbx",
            };
            inline constexpr const char* kBareTrees[] = {   // 枯れ木（たまに混ぜる）
                "Assets/Model/KayKit_Forest/fbx/Tree_Bare_1_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Tree_Bare_2_A_Color1.fbx",
            };
            inline constexpr const char* kRocks[] = {
                "Assets/Model/KayKit_Forest/fbx/Rock_1_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_D_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_H_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_2_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_2_C_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_E_Color1.fbx",
            };
            // 外周の岩山（大きい岩だけ。拡大して 3 列に積む。TerrainGenerator::Config::rockMountains）
            inline constexpr const char* kCliffRocks[] = {
                "Assets/Model/KayKit_Forest/fbx/Rock_1_J_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_K_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_L_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_M_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_N_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_O_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_P_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_1_Q_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_2_E_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_2_F_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_2_G_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_2_H_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_M_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_N_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_O_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_Q_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Rock_3_R_Color1.fbx",
            };
            inline constexpr const char* kBushes[] = {
                "Assets/Model/KayKit_Forest/fbx/Bush_1_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Bush_1_C_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Bush_2_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Bush_3_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Bush_4_A_Color1.fbx",
            };
            inline constexpr const char* kGrass[] = {
                "Assets/Model/KayKit_Forest/fbx/Grass_1_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Grass_1_C_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Grass_2_A_Color1.fbx",
                "Assets/Model/KayKit_Forest/fbx/Grass_2_C_Color1.fbx",
            };
        }

        // ========================================================
        // 第 2 面「砂漠」の自然物（2026-09-30。TerrainGenerator の Biome::Desert）
        // Quaternius Ultimate Nature（CC0）+ dglopez Western Desert（非 CC0、リポジトリには入れない。
        // 無ければ読めた物だけで生成する）。全部 cm 単位の FBX
        // ========================================================
        namespace Desert
        {
            inline constexpr const char* kTrees[] = {          // 木の枠：椰子（少なめ）
                "Assets/Model/Quaternius_UltimateNature/FBX/PalmTree_1.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/PalmTree_2.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/PalmTree_3.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/PalmTree_4.fbx",
            };
            inline constexpr const char* kBareTrees[] = {      // 枯れ木（砂漠では多め）
                "Assets/Model/Quaternius_UltimateNature/FBX/CommonTree_Dead_1.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/CommonTree_Dead_3.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/CommonTree_Dead_5.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Willow_Dead_1.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Willow_Dead_3.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/dry_stump.fbx",
            };
            inline constexpr const char* kRocks[] = {
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_1.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_2.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_3.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_5.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/medium_rock.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/rounded_rock.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/eroded_rock.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/spiky_rock.fbx",
            };
            inline constexpr const char* kCliffRocks[] = {     // 外周の岩山
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_4.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_6.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Rock_7.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/big_rock.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/eroded_rock.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/plain_rock.fbx",
            };
            inline constexpr const char* kBushes[] = {         // 見た目だけ：サボテン・枯れ草・骨
                "Assets/Model/Quaternius_UltimateNature/FBX/Cactus_1.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Cactus_2.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Cactus_3.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Cactus_4.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/Cactus_5.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/CactusFlower_1.fbx",
                "Assets/Model/Quaternius_UltimateNature/FBX/CactusFlowers_3.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/cactus_1.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/ball_cactus.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/desert_bush.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/dry_bush_1.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/dry_grass_1.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/succulent.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/dry_bones_1.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/skull.fbx",
                "Assets/Model/dglopez_WesternDesert/FBX/dry_log.fbx",
            };
        }

        // ========================================================
        // 第 3 面「遺跡」（ダンジョン）の物（2026-09-30。Biome::Dungeon）。Quaternius Modular Ruins（CC0、2m 単位）
        // ========================================================
        namespace Ruins
        {
            inline constexpr const char* kWalls[] = {          // 外周の壁（2m 幅を並べて積む）
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall_Hole.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall_Overgrown.fbx",
            };
            inline constexpr const char* kWallTops[] = {       // 一番上の段（崩れた縁）
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall_Broken.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall_Half.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall.fbx",
            };
            inline constexpr const char* kColumn = "Assets/Model/Quaternius_ModularRuins/FBX/Column_Square.fbx";
            inline constexpr const char* kTorch = "Assets/Model/Quaternius_ModularRuins/FBX/Torch.fbx";
            inline constexpr const char* kTrees[] = {          // 木の枠：石柱・彫像（塞ぐ物）
                "Assets/Model/Quaternius_ModularRuins/FBX/Column_Round.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Column_Square.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Statue_Fox.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Statue_Stag.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Support_Tall.fbx",
            };
            inline constexpr const char* kBareTrees[] = {      // たまに混ぜる：枯れ木
                "Assets/Model/Quaternius_ModularRuins/FBX/DeadTree_1.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/DeadTree_2.fbx",
            };
            inline constexpr const char* kRocks[] = {          // 岩の枠：崩れた壁・本棚・樽・箱
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall_Double_Broken.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Wall_Broken.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Bookcase_Full.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Bookcase_Empty.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Column_Round_Short.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Cart.fbx",
            };
            inline constexpr const char* kBushes[] = {         // 見た目だけ：壺・蝋燭・骸骨・煉瓦・草
                "Assets/Model/Quaternius_ModularRuins/FBX/Pot1.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Pot2.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Pot3.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Pot1_Broken.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Pot2_Broken.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Candles_1.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Candles_2.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Skull.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Bricks.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Barrel.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Crate.fbx",
                "Assets/Model/Quaternius_ModularRuins/FBX/Grass.fbx",
            };
        }

        // 起動時に別スレッドで先読みする骨付きモデル（ResourceManager::PreloadModelsAsync）。
        // 1 個 19MB・Debug で数秒かかるので、タイトル / エディタの間に済ませる。
        // 戦闘で使う物だけ。Paladin（エディタの参照）と Mage/Rogue（未使用）は入れない。
        // Minion は雑魚の予備（ブロック人形が読めない時だけ）なので外した
        inline constexpr const char* kPreload[] = {
            Shadowkin,   // プレイヤー（2026-10-04〜。レンジャーのアニメもこの中で読む）
            Kenney_BlockyZombie,
            KayKit_SkeletonWarrior,
        };
    }

    // ========================================================
    // テクスチャ（wchar_t）
    // ========================================================
    namespace Tex
    {
        // KayKit Skeletons 共通の色テクスチャ（雑魚材質の t0）
        inline constexpr const wchar_t* KayKit_SkeletonAlbedo =
            L"Assets/Model/KayKit_Skeletons/skeleton_texture.png";

        // 雑魚（Kenney Blocky の L）のピクセルテクスチャ
        inline constexpr const wchar_t* Kenney_BlockyZombieAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-l.png";
        // 自爆兵（Kenney Blocky の G、赤い稲妻のロボット）。18 体とも同じメッシュ・同じ UV なので
        // 雑魚のメッシュに貼り替えるだけで良い
        inline constexpr const wchar_t* Kenney_BlockyRobotAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-g.png";
        // スプリッター・分裂体（Kenney Blocky の D、黄色い衝突試験人形。2026-10-03）
        inline constexpr const wchar_t* Kenney_BlockyDummyAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-d.png";
        // 重装兵（Kenney Blocky の O、牙のある緑のオーク。2026-10-07 夜）
        inline constexpr const wchar_t* Kenney_BlockyOrcAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-o.png";
        // 突撃兵（Kenney Blocky の R、黒い忍者。2026-10-08）
        inline constexpr const wchar_t* Kenney_BlockyNinjaAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-r.png";
        // 盾兵（Kenney Blocky の J、紺の制服の警官。2026-10-08）
        inline constexpr const wchar_t* Kenney_BlockyPoliceAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-j.png";

        // 粒子
        inline constexpr const wchar_t* ParticleSheet =
            L"Assets/Particles/particlesSheet.jpg";
        inline constexpr const wchar_t* ProjectileCore =
            L"Assets/Particles/Particle.png";
        // Rock_2 の PBR テクスチャ
        inline constexpr const wchar_t* Rock2_Albedo =
            L"Assets/Model/Rock-Set/Rock_2/Rock_2_Tex/Rock_2_Base_Color.jpg";
        inline constexpr const wchar_t* Rock2_Normal =
            L"Assets/Model/Rock-Set/Rock_2/Rock_2_Tex/Rock_2_Normal.jpg";

        inline constexpr const wchar_t* Rock2_AO =
            L"Assets/Model/Rock-Set/Rock_2/Rock_2_Tex/Rock_2_Mixed_AO.jpg";
        inline constexpr const wchar_t* Rock2_Specular =
            L"Assets/Model/Rock-Set/Rock_2/Rock_2_Tex/Rock_2_Specular.jpg";
       
        
        inline constexpr const wchar_t* Akai_Body_Albedo =
            L"Assets/Model/Akai/AkaiTex/FemaleFitA_Body_diffuse.png";
        inline constexpr const wchar_t* Akai_Body_Normal =
            L"Assets/Model/Akai/AkaiTex/FemaleFitA_StdNM.png";
        inline constexpr const wchar_t* Akai_Clothes_Albedo =
            L"Assets/Model/Akai/AkaiTex/Erika_Archer_Clothes_diffuse.png";
        inline constexpr const wchar_t* Akai_Clothes_Normal =
            L"Assets/Model/Akai/AkaiTex/Erika_Archer_Clothes_normal.png";

        // バックパック UI 用
        inline constexpr const wchar_t* BlockSolo = L"Assets/Texture/UI/Block_solo_01.png";
        inline constexpr const wchar_t* TestBlockTex = L"Assets/Texture/UI/TestBlockTex.png";

		


        // ---- Jiandu / JaneDoe マテリアル（Diffuse のみ）----
        inline constexpr const wchar_t* JaneDoe_Body1_Albedo =
            L"Assets/Model/Jiandu/Tex/JaneDoe_Body_Map1_D.png";
        inline constexpr const wchar_t* JaneDoe_Body2_Albedo =
            L"Assets/Model/Jiandu/Tex/JaneDoe_Body_Map2_D.png";
        inline constexpr const wchar_t* JaneDoe_Face_Albedo =
            L"Assets/Model/Jiandu/Tex/JaneDoe_Face_D.png";
        inline constexpr const wchar_t* JaneDoe_Weapon_Albedo =
            L"Assets/Model/Jiandu/Tex/JaneDoe_Weapon_D.png";
    }

    // ========================================================
    // アイテムのアイコン（136px。出典と許可は Assets/Texture/UI/Icons/README.txt）
    // ========================================================
    namespace Icon
    {
        inline constexpr const wchar_t* Fireball = L"Assets/Texture/UI/Icons/Fireball.png";
        inline constexpr const wchar_t* ArcBolt = L"Assets/Texture/UI/Icons/ArcBolt.png";
        inline constexpr const wchar_t* HomingBolt = L"Assets/Texture/UI/Icons/HomingBolt.png";
        inline constexpr const wchar_t* GoldenArrow = L"Assets/Texture/UI/Icons/GoldenArrow.png";
        inline constexpr const wchar_t* Meteor = L"Assets/Texture/UI/Icons/Meteor.png";
        inline constexpr const wchar_t* StoneShot = L"Assets/Texture/UI/Icons/StoneShot.png";
        inline constexpr const wchar_t* Poison = L"Assets/Texture/UI/Icons/Poison.png";
        inline constexpr const wchar_t* IceLance = L"Assets/Texture/UI/Icons/IceLance.png";   // アイスランス（2026-10-08）
        inline constexpr const wchar_t* Beam = L"Assets/Texture/UI/Icons/Beam.png";
        inline constexpr const wchar_t* SplitRune = L"Assets/Texture/UI/Icons/SplitRune.png";
        inline constexpr const wchar_t* DoubleCastRune = L"Assets/Texture/UI/Icons/DoubleCastRune.png";
        inline constexpr const wchar_t* Magnifier = L"Assets/Texture/UI/Icons/Magnifier.png";
        inline constexpr const wchar_t* HasteRune = L"Assets/Texture/UI/Icons/HasteRune.png";
        inline constexpr const wchar_t* CrystalBall = L"Assets/Texture/UI/Icons/CrystalBall.png";   // 水晶玉（召喚物。2026-10-06）
        inline constexpr const wchar_t* Frame3x3 = L"Assets/Texture/UI/Icons/Frame3x3.png";
        inline constexpr const wchar_t* MaxHealthUp = L"Assets/Texture/UI/Icons/MaxHealthUp.png";
        inline constexpr const wchar_t* MaxManaUp = L"Assets/Texture/UI/Icons/MaxManaUp.png";
        inline constexpr const wchar_t* MoveSpeedUp = L"Assets/Texture/UI/Icons/MoveSpeedUp.png";
        inline constexpr const wchar_t* JumpPowerUp = L"Assets/Texture/UI/Icons/JumpPowerUp.png";
        inline constexpr const wchar_t* ManaRegenUp = L"Assets/Texture/UI/Icons/ManaRegenUp.png";
        inline constexpr const wchar_t* JumpCountUp = L"Assets/Texture/UI/Icons/JumpCountUp.png";
        inline constexpr const wchar_t* SpellPowerUp = L"Assets/Texture/UI/Icons/SpellPowerUp.png";
        inline constexpr const wchar_t* ShieldUp = L"Assets/Texture/UI/Icons/ShieldUp.png";   // 最大シールド（2026-10-07）
        inline constexpr const wchar_t* Magnet = L"Assets/Texture/UI/Icons/Magnet.png";   // 拾う磁石（場に落ちている物。バックパックのアイテムではない）
        inline constexpr const wchar_t* ManaSurge = L"Assets/Texture/UI/Icons/ManaSurge.png";   // 魔力解放（Q）。HUD の魔法の欄の上の大きな欄
        inline constexpr const wchar_t* Gold = L"Assets/Texture/UI/Icons/Gold.png";   // 金貨（HUD の MP バーの下）
    }

    // ========================================================
    // 幻想 UI の飾り（白い線画。コードで色を掛ける。Tools/BuildUIDeco.ps1 で作る、出典は同じ目録の README）
    // ========================================================
    namespace Deco
    {
        inline constexpr const wchar_t* MagicCircleStar = L"Assets/Texture/UI/Deco/MagicCircleStar.png";
        inline constexpr const wchar_t* MagicCircleFlower = L"Assets/Texture/UI/Deco/MagicCircleFlower.png";
        inline constexpr const wchar_t* CornerKnot = L"Assets/Texture/UI/Deco/CornerKnot.png";   // 右上の角
        inline constexpr const wchar_t* DividerFleur = L"Assets/Texture/UI/Deco/DividerFleur.png";
        inline constexpr const wchar_t* DividerThin = L"Assets/Texture/UI/Deco/DividerThin.png";

        // 魔法書の木箱（Tools/BuildChestUI.ps1 がコードで描く。色付き・sRGB で読む）
        inline constexpr const wchar_t* ChestFrame = L"Assets/Texture/UI/Chest/ChestFrame.png";   // 9 分割の枠（縁 128/512）
        inline constexpr const wchar_t* ChestBack = L"Assets/Texture/UI/Chest/ChestBack.png";     // 内側の奥の板
        inline constexpr const wchar_t* ChestLock = L"Assets/Texture/UI/Chest/ChestLock.png";     // 前板から下がる錠前
    }

    // ========================================================
    // フォント（wchar_t）
    // ========================================================
    namespace Fnt
    {
        // 毛筆体 Yuji Syuku（OFL、2026-09-28 ユーザー決定）。Tools/BuildSpriteFont.ps1 で TTF から作る（32px、BC2）。
        // 旧 NotoSansJP.spritefont は比較用に残してある
        inline constexpr const wchar_t* JP = L"Assets/Fonts/YujiSyuku.spritefont";
    }

    // ========================================================
    // VFX データ（char）
    // ========================================================
    namespace VFX
    {
        inline constexpr const char* Fireball = "Assets/Data/VFXData/Fireball.json";
        inline constexpr const char* Lightning = "Assets/Data/VFXData/Lightning.json";
        inline constexpr const char* DeathBurn = "Assets/Data/VFXData/DeathBurn.json";   // 燃焼消滅（Mesh 発射）
        inline constexpr const char* Explosion = "Assets/Data/VFXData/Explosion.json";   // 爆発（範囲攻撃）
        inline constexpr const char* ArcBolt = "Assets/Data/VFXData/ArcBolt.json";
        inline constexpr const char* HomingBolt = "Assets/Data/VFXData/HomingBolt.json";
        inline constexpr const char* Meteor = "Assets/Data/VFXData/Meteor.json";
        inline constexpr const char* MeteorBlast = "Assets/Data/VFXData/MeteorBlast.json";   // メテオの着弾
        inline constexpr const char* FireCircle = "Assets/Data/VFXData/FireCircle.json";     // 火の輪（範囲）
        inline constexpr const char* ArcBoltHit = "Assets/Data/VFXData/ArcBoltHit.json";     // 命中（威力 0 の範囲の見た目）
        inline constexpr const char* HomingBoltHit = "Assets/Data/VFXData/HomingBoltHit.json";
        inline constexpr const char* ExpOrbTrail = "Assets/Data/VFXData/ExpOrbTrail.json";   // 吸い寄せ中の経験値オーブの尾（GPU）
        inline constexpr const char* GoldenArrow = "Assets/Data/VFXData/GoldenArrow.json";   // 黄金の矢（矢のモデルが弾と一緒に飛ぶ）
        inline constexpr const char* GoldenArrowHit = "Assets/Data/VFXData/GoldenArrowHit.json";
        inline constexpr const char* FireballHit = "Assets/Data/VFXData/FireballHit.json";
        inline constexpr const char* StoneShot = "Assets/Data/VFXData/StoneShot.json";
        inline constexpr const char* StoneShotHit = "Assets/Data/VFXData/StoneShotHit.json";
        inline constexpr const char* Poison = "Assets/Data/VFXData/Poison.json";           // 毒の弾（緑の毒液の塊 + 滴）
        inline constexpr const char* PoisonPool = "Assets/Data/VFXData/PoisonPool.json";   // 毒の池（範囲 PoisonPool：泡 + 毒霧 + 緑の光）
        inline constexpr const char* IceLance = "Assets/Data/VFXData/IceLance.json";         // アイスランスの弾（氷の槍 + 冷気の粒）
        inline constexpr const char* IceLanceHit = "Assets/Data/VFXData/IceLanceHit.json";   // アイスランスの命中（範囲 IceLanceHit：威力 0 の見た目だけ。氷の破片）
        inline constexpr const char* MobDeath = "Assets/Data/VFXData/MobDeath.json";       // 敵が死んだ足元の土煙 + 土くれ（範囲 MobDeath）
        inline constexpr const char* Beam = "Assets/Data/VFXData/Beam.json";               // 魔導光線（溜め + Beam entry の光線 + 先端の光）
    }

    // ========================================================
    // 粒子のテクスチャ表（ParticleSheets）。添字 = エフェクト json の "sheet" 番号なので並べ替え禁止。
    // 追加は末尾へ。最大 ParticleSheets::kMaxSheets 枚
    // ========================================================
    namespace ParticleSheet
    {
        inline constexpr const char* kManifests[] = {
            "Assets/Particles/Sheets/Legacy.json",           // 0: 旧 particlesSheet.jpg（6x6）
            "Assets/Particles/Sheets/KenneyParticles.json",  // 1: Kenney Particle Pack（白・染色用）
            "Assets/Particles/Sheets/KenneySmoke.json",      // 2: Kenney Smoke Particles（色付きの煙）
            // ピクセルの連番（PVFX）は粒子ではなく Sprite entry で使う（Assets/VFX/SpriteSheet）
        };
    }

    // ========================================================
    // 設定ファイル（char）
    // ImGui で調整した値の保存先。実行時に読み書きする
    // ========================================================
    namespace Cfg
    {
        inline constexpr const char* HUD = "Assets/Data/HUD.json";
        inline constexpr const char* Camera = "Assets/Data/Camera.json";   // 戦闘カメラの調整（BattleCamera）
        inline constexpr const char* Difficulty = "Assets/Data/Difficulty.json";   // 経過時間 → 難度の表（DifficultyCurve）
        inline constexpr const char* Settings = "Assets/Data/Settings.json";       // プレイヤーの設定（GameSettings：敵味方の縁取りの ON / OFF）。音量は Audio/Settings.json
    }
}
