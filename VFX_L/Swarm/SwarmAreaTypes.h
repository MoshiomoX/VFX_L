// ============================================================
// SwarmAreaTypes.h
// 範囲攻撃（爆発・法環・光線）の本体と雛形、flags / extra のビットの意味。
// HLSL 側は Shader/Common/SwarmAreaTypes.hlsli（SwarmArea / SwarmAreaDef）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    // ============================================================
    // 範囲攻撃（爆発・法環）
    //
    // Area    : 生きている範囲 1 個（64B）。HLSL の SwarmArea と同じ並び。
    //           単発と持続は同じ物：爆発 = 1 回だけ tick して、粒子が出終わるまで残る範囲
    // AreaDef : 雛形（48B）。GPU が自分で範囲を出す時（弾の命中）に引く表。0 番 = 無し
    //
    // 形は円盤：XZ の距離 <= radius かつ 高さの差 <= halfHeight（+ 雑魚のカプセル）
    // ============================================================
    constexpr uint32_t kAreaFollowPlayer = 1u;   // 中心がプレイヤーに付いて動く
    constexpr uint32_t kAreaStun = 2u;           // tick で被弾硬直 + 閃光を入れる
    constexpr uint32_t kAreaCapsule = 4u;        // カプセル（光線）：中心 → 終点の線分の周り。起点 / 終点 / 半径は毎ステップ BeamCB から
    constexpr uint32_t kAreaShake = 8u;          // 出た時にカメラを揺らす（爆発だけ。火花・土煙・毒の池は揺らさない）
    // 凍結（アイススパイク）:(flags >> 4) & 0xF = q、tick で当たった敵を q × 0.25 秒凍らせる
    // （アイスランスの命中と同じ凍結の要求。エリート・Boss は半分、解けた直後は凍らない、は AICS が決める）
    constexpr uint32_t kAreaFreezeShift = 4u;
    constexpr float kAreaFreezeStep = 0.25f;     // HLSL の SWARM_AREA_FREEZE_STEP と同じ
    // 生まれた範囲を GPU の VFX レシピ（Area::vfxType）毎に数える数（SwarmAreaTrackCS、音を鳴らす用）。
    // レシピ番号がこれ以上の物は最後の枠にまとめる
    constexpr uint32_t kAreaBirthKinds = 127u;
    // 減速（毒の池）:(flags >> 12) & 0xF = q、tick の度に中の敵を q / 15 だけ遅くする（エリート・Boss は半分）
    constexpr uint32_t kAreaSlowShift = 12u;
    constexpr uint32_t kMaxBeams = 4;            // 同時に出せる光線（HLSL の SWARM_MAX_BEAMS と同じ）
    // Motion::hitAreaFlags。HLSL の SWARM_HITAREA_ON_EXPIRE / SWARM_HITAREA_EASE_IN（SwarmProjectileTypes.hlsli）と一致させること
    constexpr uint32_t kHitAreaOnExpire = 1u;    // 寿命切れ・壁に当たった時も出す
    constexpr uint32_t kHitAreaEaseIn = 1u << 16; // 加速（曲線の進みを 2 乗）
    // Motion::holdTime の刻みと上限（飛ぶ前に止まる秒数）
    constexpr float    kHitAreaHoldUnit = 0.02f;
    constexpr uint32_t kHitAreaHoldMaxSteps = 255u;
    // 範囲の追加値 Area::extra / AreaDef::extra（AreaProfile::Extra）。HLSL の SWARM_AREA_EXTRA_*（SwarmAreaTypes.hlsli）と一致させること
    constexpr uint32_t kAreaExtraToxinShift = 0u;   // (extra >> 0) & 0xF = q、1 tick の威力の q / 15 を毒素として敵に溜め、1 秒かけて削る
    constexpr uint32_t kAreaExtraSplashOnly = 1u << 4;   // 最初の tick だけダメージ（泥沼：落ちた時の飛沫。残る沼は減速だけ）
    // 吸血（吸血のルーン）：この範囲が当てた敵に吸血の印を付ける（SwarmDamageTextCS が HP の減りを吸血の累計へ）
    constexpr uint32_t kAreaExtraLifesteal = 1u << 5;
    // 回る剣（魔剣の輪・回旋斬）：同じステップの剣の範囲は一番強い 1 つだけ当て、当たった敵はガード秒（bits 16〜23 × 0.01 秒）の間
    // 次の剣の範囲を受けない（1 回の通過で 1 回。HLSL の SWARM_AREA_EXTRA_BLADE）
    constexpr uint32_t kAreaExtraBlade = 1u << 6;
    constexpr uint32_t kAreaExtraElementShift = 8u;    // bits 8〜11 = 範囲の属性（ダメージの数字の色）
    constexpr uint32_t kAreaExtraGuardShift = 16u;
    // 弾の生成依頼の tag（= SpellStats::triggerMask）の最上位 bit：吸血の弾（誘発の環には報告しない。triggerMask は bit 30 まで）
    constexpr uint32_t kTagLifesteal = 1u << 31;
    // 吸血の割合（ルーンを重ねても増えない。GPU には印だけ渡すので 1 つの値。LifestealHeal と ItemDatabase が使う）
    constexpr float kLifestealRatio = 0.03f;
    // 吹き飛ばし（突風）/ 引き寄せ（渦）の強さ Area::pushQ / pullQ の上限（q / 15）
    constexpr uint32_t kAreaForceMaxQ = 15u;

    struct Area
    {
        Vector3  center;
        float    radius = 3.0f;
        float    damage = 0.0f;          // 1 tick あたり
        float    timeLeft = 0.0f;
        float    tickInterval = 0.25f;
        float    tickTimer = 0.0f;       // 0 = 出た最初のステップで tick する
        float    halfHeight = 1.5f;
        uint32_t flags = 0;
        uint32_t vfxType = 0;            // GPU 側で粒子を出すレシピ。0 = 出さない（CPU が VFX を再生する）
        uint32_t tickState = 0;          // GPU（AreaTickCS）が書く tick 状態。出す時は 0
        uint32_t extra = 0;              // 追加値（kAreaExtra*）
        uint32_t beamChannel = 0;        // kAreaCapsule の範囲だけ：光線のチャンネル（BeamCB の添字）
        uint32_t pushQ = 0;              // 吹き飛ばしの強さ 0..15（突風）
        uint32_t pullQ = 0;              // 中心への引き寄せの強さ 0..15（渦）
    };
    static_assert(sizeof(Area) == 64, "SwarmArea layout mismatch");

    struct AreaDef
    {
        float    radius = 3.0f;
        float    halfHeight = 1.5f;
        float    damage = 0.0f;
        float    duration = 0.3f;
        float    tickInterval = 1.0e9f;
        uint32_t flags = 0;
        uint32_t vfxType = 0;
        uint32_t extra = 0;              // 出た範囲の Area::extra へ（kAreaExtra*）
        uint32_t pushQ = 0;              // 出た範囲の Area::pushQ へ
        uint32_t pullQ = 0;              // 出た範囲の Area::pullQ へ
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(AreaDef) == 48, "SwarmAreaDef layout mismatch");
}
