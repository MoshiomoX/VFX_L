// ============================================================
// SwarmReadbackTypes.h
// GPU → CPU にリードバックする並び（固定エリートの様子・敵の出来事・ダメージの数字・Boss の様子・被弾の向き・光線の標的）。
// HLSL 側は RWByteAddressBuffer に手で書く：SWARM_ELITEINFO_* / SWARM_BOSSINFO_*（SwarmEnemyTypes.hlsli）、
// SWARM_PLAYERHIT_*（SwarmContactCS）、SWARM_BEAMTARGET_*（SwarmBeamTargetCS）、環は SwarmCommon.hlsli / SwarmDamageTextCS。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstddef>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    // ============================================================
    // 固定エリートの様子（GPU → CPU。SwarmEnemyKindList2CS が毎フレーム書き、staging でリードバック）
    // 先頭 16B = 数、以降 32B × kMaxEliteInfo（槽・種類・HP・最大 HP（固定小数）・位置・fuse）
    // ============================================================
    constexpr uint32_t kMaxEliteInfo = 4;
    struct EliteInfo
    {
        uint32_t slot = 0;
        uint32_t kind = 0;
        uint32_t hp = 0;
        uint32_t maxHp = 0;
        float    pos[3] = {};
        float    fuse = 0.0f;
    };
    static_assert(sizeof(EliteInfo) == 32, "SwarmEliteInfo layout mismatch");
    struct EliteInfoBlock
    {
        uint32_t count = 0;
        uint32_t _pad[3] = {};
        EliteInfo e[kMaxEliteInfo];
    };
    static_assert(sizeof(EliteInfoBlock) == 16 + 32 * kMaxEliteInfo, "SwarmEliteInfoBlock layout mismatch");
    // HLSL（SwarmEnemyKindList2CS）は RWByteAddressBuffer に手で書く。SWARM_ELITEINFO_*（SwarmEnemyTypes.hlsli）と一致させること
    static_assert(offsetof(EliteInfoBlock, count) == 0, "EliteInfoBlock: SWARM_ELITEINFO_COUNT");
    static_assert(offsetof(EliteInfoBlock, e) == 16, "EliteInfoBlock: SWARM_ELITEINFO_FIRST");
    static_assert(offsetof(EliteInfo, pos) == 16, "EliteInfo: SWARM_ELITEINFO_POS");

    // ============================================================
    // 敵の出来事の環（GPU → CPU）。先頭 16B = 今までの総数、以降 16B × kMaxEnemyEvents。
    // CPU は誘発の環と同じくリードバックして差分を取り、MobSpawner::SpawnFromEvents / HandleRuinsEvent が扱う。
    // kind の下 16 bit（kEnemyEventKindMask）= 敵の種類、上 16 bit = 出来事の中身：
    //   kEnemyKindSplitter  スプリッターの死（SwarmCorpseTrackCS）→ 分裂体を出す。上 16 bit = 0
    //   kEnemyKindArmor     呪いの鎧の 1 回目の死（SwarmCorpseTrackCS）→ 崩れて、後で蘇った鎧を湧かせる。上 16 bit = 0
    //   kEnemyKindSandMage  砂嵐の術師の詠唱（SwarmEnemyAICS）→ 砂旋を出す。上 16 bit = 0
    //   kEnemyKindNecro     死霊術師の詠唱（SwarmEnemyAICS）→ 骸骨を起こす。上 16 bit = 0
    //   kEnemyKindWarden    鎖の番人の溜めの終わり（SwarmEnemyAICS）→ 鎖を投げる。上 16 bit = 固定した向き（0〜65535 = 0〜2π）
    //   kEnemyKindGargoyle  ガーゴイル（SwarmEnemyAICS）。上 16 bit = kGargoyleEventWake / Dive / Land
    // ============================================================
    constexpr uint32_t kMaxEnemyEvents = 128;   // HLSL の SWARM_MAX_ENEMY_EVENTS と一致させること
    struct EnemyEvent
    {
        Vector3  position;    // 出来事の起きた所（敵の位置 = 地面 + centerAboveFeet。SwarmCorpseTrackCS の分は浮いている高さを引いた位置）
        uint32_t kind = 0;    // 下 16 bit = 種類、上 16 bit = 中身（上の表）
    };
    static_assert(sizeof(EnemyEvent) == 16, "SwarmEnemyEvent layout mismatch");

    // ============================================================
    // ダメージの数字（GPU → CPU）。SwarmDamageTextCS が槽毎の hp の減りをまとめて 1 件書く
    // （先頭 16B = 総数、以降 16B × kMaxDamageTexts）。CPU は敵の出来事の環と同じくリードバックし、UI/DamageNumbers が描く。
    // 同じ pass が吸血の印の付いた敵の HP の減りも吸血の累計（SwarmSystemLifesteal.cpp）へ足す
    // ============================================================
    constexpr uint32_t kMaxDamageTexts = 512;
    struct DamageText
    {
        Vector3  position;    // 頭の上
        uint32_t packed = 0;  // 下 24 bit = 量 × 10、kDtxPoison / kDtxKill
    };
    static_assert(sizeof(DamageText) == 16, "SwarmDamageText layout mismatch");
    constexpr uint32_t kDtxAmountMask = 0x00FFFFFFu;
    constexpr uint32_t kDtxPoison = 1u << 24;   // 大半が毒素の削り → 緑
    constexpr uint32_t kDtxKill = 1u << 25;     // その敵が死んだ → 大きく
    constexpr uint32_t kDtxElementShift = 26u;   // bits 26〜29 = 最後に当てた物の属性（DamageElement）
    // ダメージの属性（ProjectileProfile / AreaProfile の element。数字の色だけに使う。順番は HLSL の SwarmMotion.element の説明と同じ）
    enum class DamageElement : uint32_t { None = 0, Fire, Ice, Lightning, Arcane, Earth, Poison, Wind, Light, Shatter, Count };   // Shatter = 凍った敵を氷の範囲が砕いた（プロファイルには書かない）
    inline const char* DamageElementName(uint32_t e)
    {
        static const char* kNames[] = { "none", "fire", "ice", "lightning", "arcane", "earth", "poison", "wind", "light", "shatter" };
        return e < (uint32_t)DamageElement::Count ? kNames[e] : "?";
    }

    // ============================================================
    // Boss の様子（GPU → CPU。SwarmEnemyCompactCS が毎フレーム書き、staging でリードバック）。
    // Boss HP 条・画面外の目印・倒したかの判定に使う（リードバックなので 2〜3 フレーム古い）。
    // 空いていた 2 語で白幽霊 / 黒幽霊の数も数える（MobSpawner の幽霊の上限用）。
    // HLSL は RWByteAddressBuffer に手で書くので、オフセットは SWARM_BOSSINFO_*（SwarmEnemyTypes.hlsli）と一致させること
    // ============================================================
    struct BossInfo
    {
        uint32_t alive = 0;    // 生きている Boss の数
        uint32_t hp = 0;       // 固定小数（HpFromFixed で戻す）。複数居たら最後に書いた 1 体
        uint32_t maxHp = 0;
        uint32_t ghosts = 0;      // 生きている白幽霊の数（CompactCS が数える。MobSpawner の幽霊の上限用）
        float    pos[3] = {};
        uint32_t darkGhosts = 0;  // 生きている黒幽霊の数（同上）
    };
    static_assert(sizeof(BossInfo) == 32, "SwarmBossInfo layout mismatch");
    static_assert(offsetof(BossInfo, alive) == 0, "BossInfo: SWARM_BOSSINFO_ALIVE");
    static_assert(offsetof(BossInfo, hp) == 4, "BossInfo: SWARM_BOSSINFO_HP");
    static_assert(offsetof(BossInfo, maxHp) == 8, "BossInfo: SWARM_BOSSINFO_MAX_HP");
    static_assert(offsetof(BossInfo, ghosts) == 12, "BossInfo: SWARM_BOSSINFO_GHOSTS");
    static_assert(offsetof(BossInfo, pos) == 16, "BossInfo: SWARM_BOSSINFO_POS");
    static_assert(offsetof(BossInfo, darkGhosts) == 28, "BossInfo: SWARM_BOSSINFO_DARK_GHOSTS");

    // ============================================================
    // プレイヤーが受けた打撃の向き（GPU → CPU、ノックバック用）。
    // SwarmContactCS が殴られた / 爆発を受けた度に「敵（爆心）→ プレイヤー」の単位ベクトル × 1000 と回数を足す。
    // counters と同じく GPU 上で永久に累積し、CPU は前回値との差分を取る（消さない。読み損ねても取りこぼさない）
    // ============================================================
    struct PlayerHitInfo
    {
        int32_t  meleeX = 0, meleeZ = 0;   // 近接の向きの和（× kHitDirScale）
        uint32_t meleeCount = 0;
        int32_t  blastX = 0, blastZ = 0;   // 爆発の向きの和
        uint32_t blastCount = 0;
        int32_t  spikeX = 0, spikeZ = 0;   // トゲ玉の向きの和（爆発より遠く高く飛ぶ）
        uint32_t spikeCount = 0;
        int32_t  windX = 0, windZ = 0;     // 砂旋の向きの和（高く吹き上げる）
        uint32_t windCount = 0;
    };
    static_assert(sizeof(PlayerHitInfo) == 48, "SwarmPlayerHitInfo layout mismatch");
    // HLSL（SwarmContactCS）は RWByteAddressBuffer に手で書く。SWARM_PLAYERHIT_* と一致させること
    static_assert(offsetof(PlayerHitInfo, meleeX) == 0, "PlayerHitInfo: SWARM_PLAYERHIT_MELEE");
    static_assert(offsetof(PlayerHitInfo, meleeCount) == 8, "PlayerHitInfo: SWARM_PLAYERHIT_COUNT");
    static_assert(offsetof(PlayerHitInfo, blastX) == 12, "PlayerHitInfo: SWARM_PLAYERHIT_BLAST");
    static_assert(offsetof(PlayerHitInfo, spikeX) == 24, "PlayerHitInfo: SWARM_PLAYERHIT_SPIKE");
    static_assert(offsetof(PlayerHitInfo, windX) == 36, "PlayerHitInfo: SWARM_PLAYERHIT_WIND");
    constexpr float kHitDirScale = 1000.0f;   // = SwarmContactCS の HIT_DIR_SCALE

    struct BeamTarget
    {
        uint32_t slot = 0xFFFFFFFFu;
        uint32_t serial = 0;
        uint32_t valid = 0;
        uint32_t _pad = 0;
        float    pos[3] = {};
        float    _pad2 = 0.0f;
    };
    static_assert(sizeof(BeamTarget) == 32, "SwarmBeamTarget layout mismatch");
    // HLSL（SwarmBeamTargetCS）は RWByteAddressBuffer に手で書く。SWARM_BEAMTARGET_* と一致させること
    static_assert(offsetof(BeamTarget, slot) == 0, "BeamTarget: SWARM_BEAMTARGET_SLOT");
    static_assert(offsetof(BeamTarget, serial) == 4, "BeamTarget: SWARM_BEAMTARGET_SERIAL");
    static_assert(offsetof(BeamTarget, pos) == 16, "BeamTarget: SWARM_BEAMTARGET_POS");

    // ============================================================
    // 優先度なしの一番近い敵（SwarmEnemyMoveCS が key と混み具合を数え、SwarmAimResolveCS が位置と距離に直す。32B）
    // ============================================================
    struct NearestTarget
    {
        uint32_t key = 0xFFFFFFFFu;    // 距離 | スロット（InterlockedMin）。空 = 0xFFFFFFFF
        float    pos[3] = {};          // 一番近い敵の位置
        float    dist = 1.0e30f;       // プレイヤーからの水平距離（1e30 = 居ない）
        uint32_t crowdCounting = 0;    // このステップで数えている 10m 以内の敵の数（MoveCS が足す）
        uint32_t crowdCount = 0;       // 前のステップの数（CPU が読む。スマートカメラ用）
        uint32_t _pad = 0;
    };
    static_assert(sizeof(NearestTarget) == 32, "SwarmNearestTarget layout mismatch");
    // HLSL は RWByteAddressBuffer に手で書く。SWARM_NEAREST_*（SwarmCore.hlsli）と一致させること
    static_assert(offsetof(NearestTarget, key) == 0, "NearestTarget: SWARM_NEAREST_KEY");
    static_assert(offsetof(NearestTarget, pos) == 4, "NearestTarget: SWARM_NEAREST_POS");
    static_assert(offsetof(NearestTarget, dist) == 16, "NearestTarget: SWARM_NEAREST_DIST");
    static_assert(offsetof(NearestTarget, crowdCounting) == 20, "NearestTarget: SWARM_NEAREST_CROWD_COUNTING");
    static_assert(offsetof(NearestTarget, crowdCount) == 24, "NearestTarget: SWARM_NEAREST_CROWD");
}
