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
    ArcBolt,        // アークボルトの弾（電撃）
    HomingBolt,     // ホーミングボルトの弾（紫の渦）
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
    PoisonPool,     // 毒の池（範囲 PoisonPool：落ちた所の飛沫 + 泡 + 毒霧 + 緑の光。持続 4 秒）
    Swamp,          // 泥沼の弾（泥の塊 + 滴。Lob 型。2026-10-17）
    SwampPool,      // 泥沼（範囲 SwampPool：落ちた所の飛沫 + 泥の液面。減速だけ）
    Spore,          // 胞子の弾（黄緑の塊 + 漂う胞子。直進・遅い。2026-10-17）
    SporeCloud,     // 胞子の霧（範囲 SporeCloud：漂う胞子 + 薄い靄。ダメージは全部毒素）
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
    // 魔法（CPU で出す分。判定は GPU の範囲、ここは見た目だけ）
    IceSpike,       // アイススパイクの冷気・氷の欠片・光（トゲ本体は IceSpikeField が地面に合わせて描く。2026-10-10）
    Gust,           // 突風の土煙と草屑の輪（判定は GPU の範囲、草が倒れるのは GrassRenderer::AddGust。2026-10-12）
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
