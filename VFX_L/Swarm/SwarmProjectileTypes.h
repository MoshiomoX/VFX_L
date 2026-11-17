// ============================================================
// SwarmProjectileTypes.h
// 投射物の本体・運動表・曲線・誘発の環・生成依頼のビットの意味。
// HLSL 側は Shader/Common/SwarmProjectileTypes.hlsli（SwarmProjectile / SwarmMotion / SwarmProjPath）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    struct Projectile
    {
        Vector3  position;
        float    damage = 0.0f;
        Vector3  velocity;
        float    lifetime = 0.0f;
        float    radius = 0.25f;
        uint32_t vfxType = 0;      // index into SwarmVFXTable's recipe table
        uint32_t motion = 0;       // 運動表（Motion）の番号。生成後は bits 16〜30 に大きさの倍率も入る（GPU）
        float    pathT = 0.0f;     // 曲線上の位置 0..1。GPU が進める（< 0 = 飛ぶ前に止まっている残り秒）
    };
    static_assert(sizeof(Projectile) == 48, "SwarmProjectile layout mismatch");

    // ============================================================
    // 投射物の運動（飛び方）
    //
    // Motion   : 投射物プロファイル 1 個につき 1 行。エディタのデータがここへ入る。
    //            HLSL の SwarmMotion と同じ並び（64B）
    // ProjPath : 投射物スロットと同じ添字。今飛んでいる 3 次ベジェ。
    //            GPU が生成時（と再捕捉時）に組み立てる。CPU は中身を触らない（64B）
    //
    // 制御点は「銃口 → 標的」の座標系で、射距離に比例させて持つ。
    // 近くても遠くても同じ形の曲線になる：
    //     c.x = 銃口 → 標的 の何割の位置か
    //     c.y = 横へのずれ / 射距離（発射ごとに左右反転できる）
    //     c.z = 上へのずれ / 射距離（世界の上方向）
    // ============================================================
    constexpr uint32_t kMaxMotions = 64;

    // ============================================================
    // 弾の生成依頼に並ぶ追加情報（依頼と同じ添字。Projectile の 48B は変えない）。
    // HLSL の SwarmSpawnProjExtra と同じ並び（32B）
    //   tag      : 誘発タグ。bit k = 「この弾が消えたら、杖の k 番の上級魔法がそこで撃てる」。最上位 bit = 吸血の弾（kTagLifesteal）。
    //              タグはスロット毎の projTags に残り、SwarmProjEndCS が「このステップで消えたタグ付きの弾」を
    //              環（先頭 16B = 今までに書いた総数、以降 16B × kMaxTriggerEvents）へ書く。CPU はリードバックして差分を取る
    //   flags    : kSpawnAtPos = Drop / Lob 型の着弾点を依頼の position にする（杖の狙いを捕捉しない）。kSpawnFlip = 曲線を左右反転
    //   roll     : 曲線の横ずれの回転（0〜127 = 0〜360 度。ProjectileProfile::Mirror::RandomAngle）。
    //              SpawnProjCS が角度にして ProjPath::sideAngle（= 回転角 rad。反転は +π）へ入れる
    //   boost    : 弾が出す範囲（命中・着弾の hitArea）への倍率（PackSpawnBoost）。SwarmSpawnProjCS がスロット毎の projBoost へ写す
    //   chains   : 連鎖のルーンで跳ぶ回数（0 = 無し）。SpawnProjCS が projChain へ入れる
    //   fanIndex : 出口魔法の扇の何発目か（0 = 1 発目。脇の弾は自分の敵を選ぶ）
    // ============================================================
    constexpr uint32_t kMaxTriggerEvents = 128;
    constexpr uint32_t kSpawnAtPos = 1u;
    constexpr uint32_t kSpawnFlip = 2u;
    constexpr uint32_t kSpawnRollMask = 0x7Fu;
    constexpr uint32_t kSpawnFanMax = 15u;
    constexpr uint32_t kSpawnChainMax = 255u;
    struct SpawnProjExtra
    {
        uint32_t tag = 0;
        uint32_t flags = 0;
        uint32_t roll = 0;
        uint32_t boost = 0;
        uint32_t chains = 0;
        uint32_t fanIndex = 0;
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(SpawnProjExtra) == 32, "SwarmSpawnProjExtra layout mismatch");

    // ============================================================
    // 弾が出す範囲（命中・着弾の hitArea）への倍率
    //   bit 0〜11 = 威力の倍率、bit 12〜23 = 持続の倍率（どちらも 256 = 1.0、0 は 1.0 扱い）。
    //   範囲を出す所（HitCS / ProjMoveCS）が掛ける。
    //   威力 = 能力アップ「魔法威力」、持続 = 魔力解放中に撃った弾（持続する範囲だけ伸びる）。
    //   HLSL の SwarmBoostDamage / SwarmBoostDuration と一致させること
    // ============================================================
    inline uint32_t PackSpawnBoost(float damageMul, float durationMul)
    {
        auto q = [](float v) -> uint32_t
            {
                const float c = (v < 1.0f / 256.0f) ? 1.0f / 256.0f : (v > 4095.0f / 256.0f ? 4095.0f / 256.0f : v);
                return (uint32_t)(c * 256.0f + 0.5f);
            };
        return q(damageMul) | (q(durationMul) << 12);
    }

    // ============================================================
    // 連鎖のルーンと貫通の状態（弾スロット毎の並行バッファ projChain。HLSL の SwarmProjChain と同じ並び、16B）。
    // GPU だけが読み書きする（SpawnProjCS が生成時に書き、HitCS / ProjMoveCS が進める）
    // ============================================================
    struct ProjChain
    {
        uint32_t flags = 0;      // HLSL の SWARM_CHAIN_ACTIVE / PENDING / PIERCE
        uint32_t left = 0;       // 残りの跳躍（貫通）回数
        uint32_t lastHit = 0;    // 最後に当たった敵のスロット
        uint32_t _pad = 0;
    };
    static_assert(sizeof(ProjChain) == 16, "SwarmProjChain layout mismatch");

    struct TriggerEvent
    {
        Vector3  position;    // 弾が消えた所の地面（地形の高さ）
        uint32_t tag = 0;
    };
    static_assert(sizeof(TriggerEvent) == 16, "SwarmTriggerEvent layout mismatch");

    enum class MotionMode : uint32_t
    {
        Straight = 0,    // 直進。敵を一切見ない
        CurveOnce = 1,   // 1 回だけ捕捉して曲線で飛ぶ。標的が死んだら今の向きで直進
        Track = 2,       // 標的が死んでも、自分に一番近い敵を探して曲線を組み直す
        // 隕石: 撃った時の杖の狙いの敵の位置を着弾点に決め、空から斜めに落ちる（途中で敵に当たらない）。
        // c1 の意味が違う（m 単位）: c1.x = 着弾点からの高さ、c1.y = 銃口側へ戻した水平距離。
        // 落ちる所には警告の輪（SwarmSystem::dropRing）
        Drop = 3,
        // 投げ上げ（毒）: 撃った時の杖の狙いの敵の位置を着弾点に決め、銃口から山なりに投げる。
        // 道筋は曲線と同じ c1 / c2（c.z で持ち上げる）、振る舞いは Drop と同じ（途中で当たらず、地形も見ず、着弾点に hitArea）
        Lob = 4,
        // 周回（周回弾）: プレイヤーの周りを回り続ける。c1.x = 回る速さ（rad/秒）、c1.y = 半径 m、c1.z = 高さのずれ m。
        // 出る角度は生成依頼の roll。壁を見ない
        Orbit = 5,
    };

    struct Motion
    {
        uint32_t mode = 0;
        float    retargetRadius = 0.0f;   // Track の再捕捉半径。0 以下 = 無制限
        uint32_t hitArea = 0;             // 命中した場所に出す範囲（AreaDef の番号）。0 = 無し
        uint32_t hitAreaFlags = 0;        // kHitAreaOnExpire / kHitAreaEaseIn
        Vector3  c1 = { 0.33f, 0.0f, 0.0f };
        float    baseRadius = 0.0f;       // profile の当たり半径。これより大きく撃たれた弾は見た目も大きく（拡大鏡）。0 = 倍率 1
        Vector3  c2 = { 0.66f, 0.0f, 0.0f };
        float    freezeTime = 0.0f;       // > 0 = 当たった敵をこの秒数凍らせる（アイスランス）
        float    holdTime = 0.0f;         // 飛ぶ前に止まる秒数（kHitAreaHoldUnit 刻み、0 = 止まらない。ストーンショット）
        uint32_t element = 0;             // 弾の属性（ダメージの数字の色。DamageElement）
        uint32_t pierce = 0;              // 最初の命中の後に貫く敵の数（0〜15。黄金の矢）
        uint32_t _pad = 0;
    };
    static_assert(sizeof(Motion) == 64, "SwarmMotion layout mismatch");

    struct ProjPath
    {
        Vector3  p0;
        uint32_t target = 0xFFFFFFFFu;
        Vector3  p1;
        float    duration = 1.0f;
        Vector3  p2;
        float    sideAngle = 1.0f;  // 横ずれの回転角（rad）：0 = 右、π = 左（反転）、他 = 飛行軸の周りに回した向き（RandomAngle）
        Vector3  p3;
        float    speed = 0.0f;
    };
    static_assert(sizeof(ProjPath) == 64, "SwarmProjPath layout mismatch");
}
