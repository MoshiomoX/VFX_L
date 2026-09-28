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
		//Skybox用
        inline constexpr const wchar_t* Sky_VS = L"Shader/SkyVS.hlsl";
        inline constexpr const wchar_t* Sky_PS = L"Shader/SkyPS.hlsl";

        // VFX 用（光を当てない）
        inline constexpr const wchar_t* VFXMesh_VS = L"Shader/VFX/VFXMeshVS.hlsl";
        inline constexpr const wchar_t* VFXMesh_PS = L"Shader/VFX/VFXMeshPS.hlsl";
        inline constexpr const wchar_t* NoiseGen_CS = L"Shader/VFX/NoiseGenCS.hlsl";
    }

    // ========================================================
    // VFX 用アセット（Mesh entry のモデル / ノイズ等の貼图）
    // Editor はフォルダを列挙して選ぶので、ここは既定値と
    // コードから直接参照する物だけ
    // ========================================================
    namespace VFX
    {
        inline constexpr const char* Slash = "Assets/VFX/Mesh/Slash.fbx";

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
        inline constexpr const char* Akai = "Assets/Model/Akai/Akai.fbx";
        // Shadowkin（骨骼付き、PBR フルセット）
        inline constexpr const char* Shadowkin =
            "Assets/Model/Shadowkin_SF/Shadowkin_Rigged.fbx";
        inline constexpr const char* SkyboxSphere = "Assets/Model/Skybox/basic_skybox_3d.fbx";

        inline constexpr const char* Jiandu_TPose =
            "Assets/Model/Jiandu/Jian_TPose.fbx";   // ①静的bind pose検証用
        inline constexpr const char* Jiandu_Idle =
            "Assets/Model/Jiandu/Idle.fbx";         // ②アニメ検証用

        inline constexpr const char* Paladin_Idle =
            "Assets/Model/testAnimModel/Idle.fbx";

        inline constexpr const char* Paladin =
            "Assets/Model/testAnimModel/PaladinWPropJNordstrom.fbx";

        inline constexpr const char* Paladin_SwordAndShieldIdle =
            "Assets/Model/testAnimModel/SwordAndShieldIdle.fbx";

        // KayKit Adventurers の Mage（CC0）。1 ファイルに 76 クリップ入り。
        // 杖・魔杖・魔道書は handslot 骨に付いた submesh で、表示切替で持ち替える
        inline constexpr const char* KayKit_Mage =
            "Assets/Model/KayKit_Mage/Mage.fbx";

        // KayKit Character Animations 1.1（CC0）の Rig_Medium。モデル無しのアニメだけのファイル。
        // 骨名が Adventurers 1.0 の Mage と同じなので、読み込み時に骨名で足す（kExtraAnims）
        namespace KayKitAnims
        {
            inline constexpr const char* MovementAdvanced =   // Crouching / Sneaking / Crawling / Dodge_* など
                "Assets/Model/KayKit_CharacterAnimations/Rig_Medium_MovementAdvanced.fbx";
        }

        // 骨付きモデルを読んだ直後に足す別ファイルのアニメ（SkinnedModel::AddAnimationsFromFile）。
        // ResourceManager::ImportModelAuto がパスの一致で引く（先読みスレッドの中でも同じ）
        struct ExtraAnimSet
        {
            const char* model;
            const char* animFile;
        };
        inline constexpr ExtraAnimSet kExtraAnims[] = {
            { KayKit_Mage, KayKitAnims::MovementAdvanced },
        };

        // KayKit Skeletons（CC0）。Minion は雑魚（1 フレーム焼いてインスタンス描画）、
        // Warrior / Mage / Rogue は精英（骨付きのまま SkinnedAnimComponent）
        inline constexpr const char* KayKit_SkeletonMinion =
            "Assets/Model/KayKit_Skeletons/Skeleton_Minion.fbx";
        inline constexpr const char* KayKit_SkeletonWarrior =
            "Assets/Model/KayKit_Skeletons/Skeleton_Warrior.fbx";
        inline constexpr const char* KayKit_SkeletonMage =
            "Assets/Model/KayKit_Skeletons/Skeleton_Mage.fbx";
        inline constexpr const char* KayKit_SkeletonRogue =
            "Assets/Model/KayKit_Skeletons/Skeleton_Rogue.fbx";

        // Kenney Blocky Characters（CC0、像素貼图の方块人）。18 体とも網格・骨・動画は同じで、
        // 貼图（Textures/texture-a..r.png）だけ違う。雑魚は L（緑肌のゾンビ）を 1 フレーム焼いて使う
        inline constexpr const char* Kenney_BlockyZombie =
            "Assets/Model/Kenney_BlockyCharacters/fbx/character-l.fbx";

        // 報酬の箱（近づいて F で三択）。Kenney Retro Fantasy の像素の木箱（0.3m 角、底が原点）
        inline constexpr const char* Kenney_RewardCrate =
            "Assets/Model/Kenney_RetroFantasy/fbx/detail-crate.fbx";

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

        // 起動時に別スレッドで先読みする骨付きモデル（ResourceManager::PreloadModelsAsync）。
        // 1 個 19MB・Debug で数秒かかるので、タイトル / 編集器の間に済ませる。
        // 戦闘で使う物だけ。Paladin（編集器の参照）と Mage/Rogue（未使用）は入れない。
        // Minion は雑魚の予備（方块人が読めない時だけ）なので外した
        inline constexpr const char* kPreload[] = {
            KayKit_Mage,
            Kenney_BlockyZombie,
            KayKit_SkeletonWarrior,
        };
    }

    // ========================================================
    // テクスチャ（wchar_t）
    // ========================================================
    namespace Tex
    {
        // KayKit Skeletons 共通の色貼图（雑魚材質の t0）
        inline constexpr const wchar_t* KayKit_SkeletonAlbedo =
            L"Assets/Model/KayKit_Skeletons/skeleton_texture.png";

        // 雑魚（Kenney Blocky の L）の像素貼图
        inline constexpr const wchar_t* Kenney_BlockyZombieAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-l.png";
        // 自爆兵（Kenney Blocky の G、赤い稲妻の機械人）。18 体とも同じメッシュ・同じ UV なので
        // 雑魚のメッシュに貼り替えるだけで良い
        inline constexpr const wchar_t* Kenney_BlockyRobotAlbedo =
            L"Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-g.png";

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

        // ---- Shadowkin / Silver 材质（金属）----
        inline constexpr const wchar_t* Silver_Albedo =
            L"Assets/Model/Shadowkin_SF/Tex/Silver_Base_color.png";
        inline constexpr const wchar_t* Silver_Normal =
            L"Assets/Model/Shadowkin_SF/Tex/Silver_Normal_OpenGL.png";   // OpenGL（G反転必要）
        inline constexpr const wchar_t* Silver_Metallic =
            L"Assets/Model/Shadowkin_SF/Tex/Silver_Metallic.png";
        inline constexpr const wchar_t* Silver_Roughness =
            L"Assets/Model/Shadowkin_SF/Tex/Silver_Roughness.png";
        inline constexpr const wchar_t* Silver_AO =
            L"Assets/Model/Shadowkin_SF/Tex/Silver_Mixed_AO.png";

        // ---- Shadowkin / Pants 材质（布）----
        inline constexpr const wchar_t* Pants_Albedo =
            L"Assets/Model/Shadowkin_SF/Tex/Pants_Base_color.png";
        inline constexpr const wchar_t* Pants_Normal =
            L"Assets/Model/Shadowkin_SF/Tex/Pants_Normal_OpenGL.png";    // OpenGL（G反転必要）
        inline constexpr const wchar_t* Pants_Metallic =
            L"Assets/Model/Shadowkin_SF/Tex/Pants_Metallic.png";
        inline constexpr const wchar_t* Pants_Roughness =
            L"Assets/Model/Shadowkin_SF/Tex/Pants_Roughness.png";
        inline constexpr const wchar_t* Pants_AO =
            L"Assets/Model/Shadowkin_SF/Tex/Pants_Mixed_AO.png";
		
        //Skyboxの一枚テクスチャ
        inline constexpr const wchar_t* SkyboxPanorama = L"Assets/Model/Skybox/Tex/sky_water_landscape.jpg";


        // ---- Jiandu / JaneDoe 材质（Diffuse のみ）----
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
    // 道具のアイコン（136px。出典と許可は Assets/Texture/UI/Icons/README.txt）
    // ========================================================
    namespace Icon
    {
        inline constexpr const wchar_t* Fireball = L"Assets/Texture/UI/Icons/Fireball.png";
        inline constexpr const wchar_t* ArcBolt = L"Assets/Texture/UI/Icons/ArcBolt.png";
        inline constexpr const wchar_t* HomingBolt = L"Assets/Texture/UI/Icons/HomingBolt.png";
        inline constexpr const wchar_t* Meteor = L"Assets/Texture/UI/Icons/Meteor.png";
        inline constexpr const wchar_t* SplitRune = L"Assets/Texture/UI/Icons/SplitRune.png";
        inline constexpr const wchar_t* DoubleCastRune = L"Assets/Texture/UI/Icons/DoubleCastRune.png";
        inline constexpr const wchar_t* Frame3x3 = L"Assets/Texture/UI/Icons/Frame3x3.png";
        inline constexpr const wchar_t* MaxHealthUp = L"Assets/Texture/UI/Icons/MaxHealthUp.png";
        inline constexpr const wchar_t* MaxManaUp = L"Assets/Texture/UI/Icons/MaxManaUp.png";
    }

    // ========================================================
    // フォント（wchar_t）
    // ========================================================
    namespace Fnt
    {
        inline constexpr const wchar_t* JP = L"Assets/Fonts/NotoSansJP.spritefont";
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
    }

    // ========================================================
    // 粒子の貼图表（ParticleSheets）。添字 = 特効 json の "sheet" 番号なので並べ替え禁止。
    // 追加は末尾へ。最大 ParticleSheets::kMaxSheets 枚
    // ========================================================
    namespace ParticleSheet
    {
        inline constexpr const char* kManifests[] = {
            "Assets/Particles/Sheets/Legacy.json",           // 0: 旧 particlesSheet.jpg（6x6）
            "Assets/Particles/Sheets/KenneyParticles.json",  // 1: Kenney Particle Pack（白・染色用）
            "Assets/Particles/Sheets/KenneySmoke.json",      // 2: Kenney Smoke Particles（色付きの煙）
            // 像素の連番（PVFX）は粒子ではなく Sprite entry で使う（Assets/VFX/SpriteSheet）
        };
    }

    // ========================================================
    // 設定ファイル（char）
    // ImGui で調整した値の保存先。実行時に読み書きする
    // ========================================================
    namespace Cfg
    {
        inline constexpr const char* HUD = "Assets/Data/HUD.json";
    }
}