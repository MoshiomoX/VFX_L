// ============================================================
// VFXId.h
// エフェクトの ID と、ID → JSON パスの対応表（エフェクトのデータベース。表は VFXDatabase.cpp）。
//
// アイテム（ItemDatabase）は VFXId だけを持ち、パスを知らない。
//   同じエフェクトを複数のアイテムが使い回せるし、
//   CPU 経路（AreaVFXPlayer）と GPU 経路（SwarmVFXTable）が
//   同じ表を引くので、二重管理にならない。
// ゲームが出すエフェクトは全部ここに登録する（コードに json のファイル名を直書きしない）。
// Assets/Data/VFXData/ の json のうち、ここに無いのはエディタの試し用（Test2 / SheetTest など）だけ。
// 一覧は F2（VFX Editor）の「Database」ボタンで見られる（押すと読み込む）。
// ============================================================
#pragma once
#include <cstdint>

enum class VFXId : uint32_t
{
    None = 0,
    Fireball,
    DeathBurn,      // 燃焼消滅（Mesh 発射 + 溶解の縁。MeshVFXSystem::StartBurn）
    Explosion,      // 爆発。弾の命中で GPU が出す範囲（hitArea）の粒子。GPU 側は登録済みの VFX しか出せない
    ArcBolt,        // アークボルト（2026-10-15 から弾ではなく、杖先から一番近い敵へ一瞬で届く稲妻 = Beam entry の稲妻 + 杖先の閃光）
    HomingBolt,     // ホーミングボルトの弾（輝石の魔術風：青い水晶 bing.FBX + 光る殻 + 途切れない HDR の帯。2026-10-15）
    Meteor,         // メテオの弾（大きな火球 + 岩）
    MeteorBlast,    // メテオの着弾（Explosion の大きい版 + 岩の破片）
    FireCircle,     // 火の輪（範囲・術者に追従）
    ArcBoltHit,     // アークボルトの命中（範囲 ArcSpark：威力 0 の見た目だけ。Sprite = electric-impact）
    HomingBoltHit,  // ホーミングボルトの命中（範囲 VoidPop：同上。Sprite = void-implosion）
    ExpOrbTrail,    // 吸い寄せられている経験値オーブの尾（SwarmOrbEmitCS。アイテムは使わない）
    GoldenArrow,    // 黄金の矢の弾（gonjian.FBX の矢が弾と一緒に飛ぶ + 金の軌跡）
    GoldenArrowHit, // 黄金の矢の命中（範囲 GoldenArrowHit：威力 0 の見た目だけ。金の火花）
    FireballHit,    // 火球の命中・消滅（範囲 FireballHit：威力 0 の見た目だけ。小さな炎の閃き。2026-09-30 に火球の爆発を廃止）
    StoneShot,      // 石弾の弾（Rock_2.fbx の岩が弾と一緒に飛ぶ + 土煙）
    StoneShotHit,   // 石弾の命中（範囲 StoneShotHit：威力 0 の見た目だけ。土煙 + 岩の欠片）
    Poison,         // 毒の弾（緑の毒液の塊 + 滴。Lob 型で山なりに飛ぶ）
    PoisonPool,     // 猛毒の沼（範囲 PoisonPool：落ちた所の飛沫 + 液面 + 泡 + 緑の光。持続 6 秒。毒霧は 2026-10-05 に削除）
    Swamp,          // 泥沼の弾（泥の塊 + 滴。Lob 型。2026-10-17）
    SwampPool,      // 泥沼（範囲 SwampPool：落ちた所の飛沫 + 泥の液面。減速だけ）
    Spore,          // 胞子の弾（黄緑の塊 + 漂う胞子。直進・遅い。2026-10-17）
    SporeCloud,     // 胞子の霧（範囲 SporeCloud：漂う胞子 + 薄い靄。ダメージは全部毒素）
    VoidVortex,     // 渦の弾（紫の小さな渦 + 光の粒の尾。旧ホーミングボルトの見た目を流用。2026-10-22）
    VoidVortexPull, // 渦（範囲 VoidVortexPull：回る紫の渦 + 中心へ吸い込まれる光 + 屈折。周りの敵を引き寄せる）
    MobDeath,       // 敵が死んだ足元の土煙（範囲 MobDeath：威力 0 の見た目だけ。部品の砕け散りと一緒。2026-10-02）
    Beam,           // 魔導光線（溜めの光球 + Beam entry の光線 + 点光源。CPU の AreaVFXPlayer が再生し、終点は WeaponSystem が毎フレーム入れる）
    IceLance,       // アイスランスの弾（bing.FBX の氷の槍が弾と一緒に飛ぶ + 冷気の粒。2026-10-08）
    IceLanceHit,    // アイスランスの命中（範囲 IceLanceHit：威力 0 の見た目だけ。氷の破片 + 冷気）
    GrassFire,      // 燃えている草の炎・煙・火の粉（GrassFireEmitCS が燃えているマスから出す。アイテムは使わない。2026-10-12）
    TornadoDust,    // 砂嵐の術師の砂旋の足元の砂埃（SwarmTornadoEmitCS が砂旋の足元から出す。アイテムは使わない。2026-10-19）

    // ---- ここから CPU（AreaVFXPlayer）だけで出す物（2026-10-09 に登録。それまではコードにファイル名を直書きしていて、
    //      ユーザーに「プレイヤーのエフェクトや足元の小石がデータベースに入っていない」と言われた）。GPU の表には入れない ----
    // プレイヤーの動き（PlayerVFXSystem）
    CastFlash,      // 詠唱の手元の光 + 撃つ向きへ火花（魔法の色に染める）
    CastPulse,      // 詠唱の体の周りに昇る光の粒
    FootDust,       // 走る足元の小石
    JumpDust,       // 跳んだ足元の小石の輪
    AirJumpRing,    // 空中ジャンプの光の輪
    LandDust,       // 着地の小石
    LandDustHeavy,  // 高い所から / 速く落ちた着地
    SlideDust,      // スライディングの小石と草
    SpeedTrail,     // 速い時に背中から引く大きな帯（Trail entry）
    WaterStep,      // 水たまりの中の一歩の水しぶき（2026-10-27。波紋は WaterSurface）
    WaterLand,      // 水たまりへの着地・跳び出しの水しぶき + 白い靄
    WaterSlide,     // 水たまりの中のスライディングの水しぶき（後ろへ）
    // 魔法（CPU で出す分。判定は GPU の範囲、ここは見た目だけ）
    IceSpike,       // アイススパイクの冷気・氷の欠片・光（トゲ本体は IceSpikeField が地面に合わせて描く。2026-10-10）
    Gust,           // 突風の土煙と草屑の輪（判定は GPU の範囲、草が倒れるのは GrassRenderer::AddGust。2026-10-12）
    Twister,        // 竜巻（longjuanfeng2.FBX の柱 2 枚 + 屈折の煙団 + 巻き上がる草屑。WeaponSystem が毎フレーム竜巻の位置へ動かす。2026-10-22）
    // 戦闘の手応え（FeedbackVFXSystem）
    LevelUp,
    CrateOpen,
    Hurt,
    ExpPickup,
    ManaSurgeBurst, // 魔力解放の瞬間
    ManaSurgeAura,  // 魔力解放の間
    ShieldHit,      // シールドで受けた
    ShieldBreak,    // シールドが割れた
    // Boss（門・技。CollisionTestScene / CollisionTestSceneBoss）
    BossPortal,     // Boss の門の渦
    BossSlam,       // スラム・衝撃波の岩
    BossRockFall,   // 投石雨の落ちてくる岩
    BossRockHit,    // 投石雨の着弾
    // 天気（WeatherSystem）
    Rain,
    Sandstorm,
    // 召喚物（水晶玉の光球。SummonItemDef::orbVfx）
    SpellOrb,
    // 環境（風）
    GrassShred,     // 風源（竜巻・渦・砂旋・突風）が草地から引きちぎって巻き上げる草屑（World/GrassShreds が風源ごとに出して動かす。2026-10-23）
    // Boss「砂の守護者」の技（CollisionTestSceneBoss、2026-10-24）
    BossQuicksand,  // 流沙の渦
    BossSandMound,  // 潜砂の砂丘（ループ。Boss に付いて動く）
    BossSandBurst,  // 潜る・飛び出す時の砂の噴き上げ
    BossSandSpear,  // 砂の槍のトゲの根元の砂煙
    BossSandAura,   // 砂の守護者の体の周りに舞う砂（ループ。地上に居る間ずっと）
    BossStoneAura,  // 岩の巨人の肩から舞い落ちる葉と花粉（ループ。2026-10-25）
    BossVenom,      // 大サソリの毒針に刺されたプレイヤーの周りの毒の泡（ループ、プレイヤーに付く。2026-10-26）
    BossVenomAura,  // 大サソリの足元に漂う毒の靄と緑の光（ループ。地上に居る間ずっと。2026-10-26）
    // 遺跡の敵（2026-10-25、CPU。MobSpawner の出来事 → CollisionTestSceneRuins.cpp）
    RuinsArmorFall,     // 呪いの鎧が崩れた鎧の山 + 紫の靄（蘇るまで）
    RuinsArmorRise,     // 蘇る瞬間の紫の炎
    RuinsGargoyleWake,  // 石像が目覚めて石の欠片が剥がれる
    RuinsGargoyleDive,  // 急降下の溜めの赤い火花
    RuinsGargoyleLand,  // 着地の土煙
    RuinsNecroCast,     // 死霊術師の詠唱の緑の光
    RuinsRaiseMark,     // 骸骨が起きる所の緑の印
    RuinsSkeletonRise,  // 骸骨が土から起きる
    RuinsChainHit,      // 鎖の鉤が当たった火花
    // 面の固定エリート（2026-10-25、CPU。Enemy/EliteAttacks が出す）
    EliteSpawn,         // 現れた瞬間の暗い赤の煙と火花
    EliteAxeSpin,       // 大斧の回転斬り（輪の大きさの斬撃と土煙）
    EliteArrow,         // 森の弓手の矢（ループ。矢のモデル + 緑の尾）
    EliteArrowHit,      // 矢が当たった / 刺さった
    EliteRiderDash,     // 砂塵の騎兵の突進の砂煙（ループ）
    EliteFlameBreath,   // 炎砂の術師の炎の息（ループ。+Z へ噴く）
    EliteShadeBlink,    // 影の剣士が消える / 現れる黒い煙
    EliteShadeSlash,    // 背後からの斬撃
    EliteCurseOrb,      // 呪いの司書の暗い紫の玉（ループ）
    EliteCurseOrbBlast, // 玉が爆ぜる
    EliteThunder,       // 雷光の雷
    EliteShield,        // 鉄壁の盾（ループ）
    EliteShieldBreak,   // 盾が割れる
    EliteSwift,         // 疾風の残像の風（ループ）
    EliteBerserk,       // 狂暴になった瞬間
    EliteBerserkAura,   // 狂暴の赤い火の粉（ループ）
    EliteChest,         // 金色の宝箱の光の粒（ループ）
    LifestealAbsorb,    // 吸血のルーンで HP が戻る時、周りから赤い光が体へ吸い込まれる（2026-10-25）
    SandTornado,        // 砂嵐の術師の砂旋の本体（砂色の柱 2 枚。Enemy/SandTornadoFx が砂旋と同じ直線で動かす。2026-10-26）
    // ---- 追加はここに。VFXDatabase.cpp の表にも1行足す ----
    Count
};

namespace VFXDatabase
{
    // 何に使うか（一覧の見出し・GPU の表に入れるかを決める）
    enum class Group
    {
        Spell,      // 弾・命中・範囲（GPU。SwarmVFXTable が粒子の表にする）
        Player,     // プレイヤーの動き（CPU）
        Feedback,   // 戦闘の手応え（CPU）
        Boss,       // Boss の門・技（CPU）
        Weather,    // 天気（CPU）
        Summon,     // 召喚物（CPU）
        SpellCpu,   // 魔法の CPU 側の見た目（アイススパイク。判定は GPU）
        Enemy,      // 敵の CPU 側の見た目（遺跡の敵。2026-10-25）
        Count
    };

    // ID → JSON パス（Assets/Data/VFXData/…）。無ければ nullptr
    const char* GetPath(VFXId id);
    // ID → ファイル名だけ（AreaVFXPlayer::Play に渡す形）。無ければ ""
    const char* GetFile(VFXId id);
    Group GetGroup(VFXId id);
    const char* GroupName(Group g);
    // GPU の弾・範囲の表（SwarmVFXTable）に入れる物か
    inline bool IsGpu(VFXId id) { return GetGroup(id) == Group::Spell; }

    // 登録済みの全 ID（None を除く）
    int Count();
    VFXId At(int index);
}
