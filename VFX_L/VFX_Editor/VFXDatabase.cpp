// ============================================================
// VFXDatabase.cpp
// エフェクトのデータベース（VFXId → json）。
// 2026-10-09 まで VFXDatabase.h という名前で CollisionTestScene.cpp から #include されていた（中身は .cpp）。
// 同日、CPU だけで出すエフェクト 24 個（プレイヤーの動き・手応え・Boss・天気・召喚物）を登録した
// ============================================================
#include "VFX_Editor/VFXId.h"
#include "ResourcePaths.h"

namespace
{
    using VFXDatabase::Group;
    struct Entry { VFXId id; const char* path; Group group; };

    // ※VFXId の順と揃える必要は無い（線形探索）。None は登録しない。一覧（F2）はこの順に出る
    const Entry kTable[] = {
        // ---- 弾・命中・範囲（GPU）----
        { VFXId::Fireball, Res::VFX::Fireball, Group::Spell },
        { VFXId::DeathBurn, Res::VFX::DeathBurn, Group::Spell },
        { VFXId::Explosion, Res::VFX::Explosion, Group::Spell },
        { VFXId::ArcBolt, Res::VFX::ArcBolt, Group::Spell },
        { VFXId::HomingBolt, Res::VFX::HomingBolt, Group::Spell },
        { VFXId::Meteor, Res::VFX::Meteor, Group::Spell },
        { VFXId::MeteorBlast, Res::VFX::MeteorBlast, Group::Spell },
        { VFXId::FireCircle, Res::VFX::FireCircle, Group::Spell },
        { VFXId::ArcBoltHit, Res::VFX::ArcBoltHit, Group::Spell },
        { VFXId::HomingBoltHit, Res::VFX::HomingBoltHit, Group::Spell },
        { VFXId::ExpOrbTrail, Res::VFX::ExpOrbTrail, Group::Spell },
        { VFXId::GoldenArrow, Res::VFX::GoldenArrow, Group::Spell },
        { VFXId::GoldenArrowHit, Res::VFX::GoldenArrowHit, Group::Spell },
        { VFXId::FireballHit, Res::VFX::FireballHit, Group::Spell },
        { VFXId::StoneShot, Res::VFX::StoneShot, Group::Spell },
        { VFXId::StoneShotHit, Res::VFX::StoneShotHit, Group::Spell },
        { VFXId::Beam, Res::VFX::Beam, Group::Spell },
        { VFXId::Poison, Res::VFX::Poison, Group::Spell },
        { VFXId::PoisonPool, Res::VFX::PoisonPool, Group::Spell },
        { VFXId::Swamp, Res::VFX::Swamp, Group::Spell },
        { VFXId::SwampPool, Res::VFX::SwampPool, Group::Spell },
        { VFXId::Spore, Res::VFX::Spore, Group::Spell },
        { VFXId::SporeCloud, Res::VFX::SporeCloud, Group::Spell },
        { VFXId::VoidVortex, Res::VFX::VoidVortex, Group::Spell },
        { VFXId::VoidVortexPull, Res::VFX::VoidVortexPull, Group::Spell },
        { VFXId::MobDeath, Res::VFX::MobDeath, Group::Spell },
        { VFXId::IceLance, Res::VFX::IceLance, Group::Spell },
        { VFXId::IceLanceHit, Res::VFX::IceLanceHit, Group::Spell },
        { VFXId::GrassFire, Res::VFX::GrassFire, Group::Spell },
        { VFXId::TornadoDust, Res::VFX::TornadoDust, Group::Spell },

        // ---- プレイヤーの動き（CPU、PlayerVFXSystem）----
        { VFXId::CastFlash, Res::VFX::CastFlash, Group::Player },
        { VFXId::CastPulse, Res::VFX::CastPulse, Group::Player },
        { VFXId::FootDust, Res::VFX::FootDust, Group::Player },
        { VFXId::JumpDust, Res::VFX::JumpDust, Group::Player },
        { VFXId::AirJumpRing, Res::VFX::AirJumpRing, Group::Player },
        { VFXId::LandDust, Res::VFX::LandDust, Group::Player },
        { VFXId::LandDustHeavy, Res::VFX::LandDustHeavy, Group::Player },
        { VFXId::SlideDust, Res::VFX::SlideDust, Group::Player },
        { VFXId::SpeedTrail, Res::VFX::SpeedTrail, Group::Player },

        // ---- 魔法の CPU 側の見た目（判定は GPU の範囲）----
        { VFXId::IceSpike, Res::VFX::IceSpikeFrost, Group::SpellCpu },
        { VFXId::Gust, Res::VFX::Gust, Group::SpellCpu },
        { VFXId::Twister, Res::VFX::Twister, Group::SpellCpu },

        // ---- 戦闘の手応え（CPU、FeedbackVFXSystem）----
        { VFXId::LevelUp, Res::VFX::LevelUp, Group::Feedback },
        { VFXId::CrateOpen, Res::VFX::CrateOpen, Group::Feedback },
        { VFXId::Hurt, Res::VFX::Hurt, Group::Feedback },
        { VFXId::ExpPickup, Res::VFX::ExpPickup, Group::Feedback },
        { VFXId::ManaSurgeBurst, Res::VFX::ManaSurgeBurst, Group::Feedback },
        { VFXId::ManaSurgeAura, Res::VFX::ManaSurgeAura, Group::Feedback },
        { VFXId::ShieldHit, Res::VFX::ShieldHit, Group::Feedback },
        { VFXId::ShieldBreak, Res::VFX::ShieldBreak, Group::Feedback },

        // ---- Boss（CPU）----
        { VFXId::BossPortal, Res::VFX::BossPortal, Group::Boss },
        { VFXId::BossSlam, Res::VFX::BossSlam, Group::Boss },
        { VFXId::BossRockFall, Res::VFX::BossRockFall, Group::Boss },
        { VFXId::BossRockHit, Res::VFX::BossRockHit, Group::Boss },
        { VFXId::BossQuicksand, Res::VFX::BossQuicksand, Group::Boss },   // 砂の守護者（2026-10-24）
        { VFXId::BossSandMound, Res::VFX::BossSandMound, Group::Boss },
        { VFXId::BossSandBurst, Res::VFX::BossSandBurst, Group::Boss },
        { VFXId::BossSandSpear, Res::VFX::BossSandSpear, Group::Boss },
        { VFXId::BossSandAura, Res::VFX::BossSandAura, Group::Boss },
        { VFXId::BossStoneAura, Res::VFX::BossStoneAura, Group::Boss },
        { VFXId::BossVenom, Res::VFX::BossVenom, Group::Boss },
        { VFXId::BossVenomAura, Res::VFX::BossVenomAura, Group::Boss },

        // ---- 敵（CPU。遺跡の敵の出来事、2026-10-25）----
        { VFXId::RuinsArmorFall, Res::VFX::RuinsArmorFall, Group::Enemy },
        { VFXId::RuinsArmorRise, Res::VFX::RuinsArmorRise, Group::Enemy },
        { VFXId::RuinsGargoyleWake, Res::VFX::RuinsGargoyleWake, Group::Enemy },
        { VFXId::RuinsGargoyleDive, Res::VFX::RuinsGargoyleDive, Group::Enemy },
        { VFXId::RuinsGargoyleLand, Res::VFX::RuinsGargoyleLand, Group::Enemy },
        { VFXId::RuinsNecroCast, Res::VFX::RuinsNecroCast, Group::Enemy },
        { VFXId::RuinsRaiseMark, Res::VFX::RuinsRaiseMark, Group::Enemy },
        { VFXId::RuinsSkeletonRise, Res::VFX::RuinsSkeletonRise, Group::Enemy },
        { VFXId::RuinsChainHit, Res::VFX::RuinsChainHit, Group::Enemy },
        { VFXId::SandTornado, Res::VFX::SandTornado, Group::Enemy },   // 砂旋の本体（2026-10-26）
        { VFXId::EliteSpawn, Res::VFX::EliteSpawn, Group::Enemy },
        { VFXId::EliteAxeSpin, Res::VFX::EliteAxeSpin, Group::Enemy },
        { VFXId::EliteArrow, Res::VFX::EliteArrow, Group::Enemy },
        { VFXId::EliteArrowHit, Res::VFX::EliteArrowHit, Group::Enemy },
        { VFXId::EliteRiderDash, Res::VFX::EliteRiderDash, Group::Enemy },
        { VFXId::EliteFlameBreath, Res::VFX::EliteFlameBreath, Group::Enemy },
        { VFXId::EliteShadeBlink, Res::VFX::EliteShadeBlink, Group::Enemy },
        { VFXId::EliteShadeSlash, Res::VFX::EliteShadeSlash, Group::Enemy },
        { VFXId::EliteCurseOrb, Res::VFX::EliteCurseOrb, Group::Enemy },
        { VFXId::EliteCurseOrbBlast, Res::VFX::EliteCurseOrbBlast, Group::Enemy },
        { VFXId::EliteThunder, Res::VFX::EliteThunder, Group::Enemy },
        { VFXId::EliteShield, Res::VFX::EliteShield, Group::Enemy },
        { VFXId::EliteShieldBreak, Res::VFX::EliteShieldBreak, Group::Enemy },
        { VFXId::EliteSwift, Res::VFX::EliteSwift, Group::Enemy },
        { VFXId::EliteBerserk, Res::VFX::EliteBerserk, Group::Enemy },
        { VFXId::EliteBerserkAura, Res::VFX::EliteBerserkAura, Group::Enemy },
        { VFXId::EliteChest, Res::VFX::EliteChest, Group::Enemy },
        { VFXId::LifestealAbsorb, Res::VFX::LifestealAbsorb, Group::Player },

        // ---- 天気（CPU、WeatherSystem）----
        { VFXId::Rain, Res::VFX::Rain, Group::Weather },
        { VFXId::Sandstorm, Res::VFX::Sandstorm, Group::Weather },
        { VFXId::GrassShred, Res::VFX::GrassShred, Group::Weather },   // 風源の草屑（World/GrassShreds、2026-10-23）

        // ---- 召喚物（CPU）----
        { VFXId::SpellOrb, Res::VFX::SpellOrb, Group::Summon },
    };
    constexpr int kCount = (int)(sizeof(kTable) / sizeof(kTable[0]));

    const Entry* Find(VFXId id)
    {
        for (const auto& e : kTable)
            if (e.id == id) return &e;
        return nullptr;
    }
}

namespace VFXDatabase
{
    const char* GetPath(VFXId id)
    {
        const Entry* e = Find(id);
        return e ? e->path : nullptr;
    }

    const char* GetFile(VFXId id)
    {
        const char* path = GetPath(id);
        if (!path) return "";
        const char* slash = std::strrchr(path, '/');
        return slash ? slash + 1 : path;
    }

    Group GetGroup(VFXId id)
    {
        const Entry* e = Find(id);
        return e ? e->group : Group::Count;
    }

    const char* GroupName(Group g)
    {
        switch (g)
        {
        case Group::Spell:    return "Spells / hits / areas (GPU)";
        case Group::Player:   return "Player motion (CPU)";
        case Group::Feedback: return "Combat feedback (CPU)";
        case Group::Boss:     return "Boss (CPU)";
        case Group::Weather:  return "Weather (CPU)";
        case Group::Summon:   return "Summons (CPU)";
        case Group::SpellCpu: return "Spells, CPU side (CPU)";
        case Group::Enemy:    return "Enemies (CPU)";
        default:              return "?";
        }
    }

    int Count() { return kCount; }
    VFXId At(int index)
    {
        return (index >= 0 && index < kCount) ? kTable[index].id : VFXId::None;
    }
}
