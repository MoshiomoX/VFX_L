// ============================================================
// SwarmCBs.h
// 毎フレームの定数・生成・AI・転送・光線の定数バッファ。
// HLSL 側は Shader/Common/SwarmFrameCB.hlsli（SwarmFrameCB / SwarmAICB）、Shader/Swarm/SwarmSpawnCB.hlsli、
// SwarmRecycleCS / SwarmAreaTickCS（SwarmBeamCB）/ SwarmBeamTargetCS の cbuffer。
// ============================================================
#pragma once
#include "Swarm/SwarmCoreTypes.h"
#include "Swarm/SwarmAreaTypes.h"
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    // ============================================================
    // 毎フレームの定数（b0）
    // ============================================================
    struct FrameCB
    {
        Vector3  playerPos;
        float    playerRadius = 0.4f;

        float    step = kFixedStep;
        uint32_t maxEnemies = kMaxEnemies;
        uint32_t maxProjectiles = kMaxProjectiles;
        uint32_t maxOrbs = kMaxOrbs;

        Vector3  gridOrigin;
        float    cellSize = 2.0f;

        uint32_t gridW = 0;
        uint32_t gridD = 0;
        uint32_t seed = 0;
        uint32_t playerAlive = 1;
    };
    static_assert(sizeof(FrameCB) == 64, "SwarmFrameCB layout mismatch");

    // ============================================================
    // 生成用の定数（b1）
    // ============================================================
    struct SpawnCB
    {
        uint32_t requestCount = 0;
        uint32_t scanStart = 0;    // 毎フレーム回して探索開始位置をずらす
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(SpawnCB) == 16, "SwarmSpawnCB layout mismatch");

    // ============================================================
    // 雑魚 AI の調整値（b1）
    // 雑魚は全員同じ体格なので半径もここ（本体の 48B を崩さない）
    // ============================================================
    struct AICB
    {
        float separationRadius = 1.2f;
        float separationPower = 4.0f;
        float avoidPower = 2.0f;        // 流れ場が木を避けるので弱め（強いと木の隣の台地の角へ押し込む）
        float lookAhead = 0.35f;        // 秒。速度 × これ で先読み距離

        float centerAboveFeet = 0.9f;   // カプセルの中心から足元までの高さ（position.y = 足の高さ + これ）
        float enemyRadius = 0.4f;       // カプセルの半径
        float enemyCapsuleHalf = 0.5f;  // 直線部分の半分
        float velocityLag = 10.0f;      // 速度の追従係数。大きいほど機敏（∞ = 即時）

        float maxSpeedMul = 1.5f;       // 合成速度の上限 = moveSpeed × これ
        float turnSpeed = 12.0f;        // 旋回の上限（rad/s）
        float playerPushOut = 10.0f;    // プレイヤーに食い込んだ時に押し戻す強さ（1/s）
        float contactDamage = 15.0f;    // 接触1回のダメージ

        float attackInterval = 1.0f;    // 同じ雑魚が次に殴れるまでの秒数
        float playerCapsuleHalf = 0.5f; // プレイヤーカプセルの直線部の半分。シーンが毎フレーム入れる
        float hitStun = 0.08f;          // 被弾で止まる秒数（HitCS が animIndex = kAnimStun を立て、MoveCS が数える）
        float hitFlash = 3.0f;          // 被弾直後の頂点色の倍率。1 へ減衰。Bloom で光る
    };
    static_assert(sizeof(AICB) == 64, "SwarmAICB layout mismatch");

    // ============================================================
    // 光線（カプセル型の範囲）の起点 / 終点（b3、AreaTickCS）。CPU が毎フレーム書く。
    // start.w = 半径、end.w = 1 の間だけ生きる（0 にすると GPU 側の範囲が消える）
    // ============================================================
    struct BeamCB
    {
        DirectX::SimpleMath::Vector4 start[kMaxBeams];
        DirectX::SimpleMath::Vector4 end[kMaxBeams];
    };
    static_assert(sizeof(BeamCB) == 128, "SwarmBeamCB layout mismatch");

    // ============================================================
    // 光線の標的（b4、SwarmBeamTargetCS）。チャンネル毎:
    //   origin.xyz = 杖口、dir.xyz = 光線の向き、dir.w = 射程、seek.xyz = シーク点
    //   cmd.x = 命令（kBeamTarget*）、cmd.y = 通し番号（serial）
    //   exclude.xyz = 連鎖で除く敵のスロット（cmd 3 だけ）
    //   命令：0 = 未使用 / 1 = 開始: seek に一番近い敵を捕まえる / 2 = 追跡: 捕まえた敵が
    //   死んだら・遠くへ行ったら、射程内で光線の向きに一番近い敵へ乗り換える /
    //   3 = 連鎖（連鎖のルーン × アークボルト）:seek（届いた敵の位置）から SWARM_CHAIN_RADIUS 内で
    //   一番近い「別の」敵（exclude = この連鎖で既に届いた敵）
    // 結果は 32B × チャンネル（slot, serial, valid, pad, pos.xyz, pad）を staging でリードバック（valid = 0 なら居ない）
    // ============================================================
    enum : uint32_t { kBeamTargetIdle = 0, kBeamTargetStart = 1, kBeamTargetTrack = 2, kBeamTargetChain = 3 };
    constexpr uint32_t kBeamNoSlot = 0xFFFFFFFFu;
    struct BeamTargetCmd
    {
        uint32_t cmd = kBeamTargetIdle;
        uint32_t serial = 0;
        uint32_t _pad[2] = {};
    };
    struct BeamTargetExclude
    {
        uint32_t slot[3] = { kBeamNoSlot, kBeamNoSlot, kBeamNoSlot };
        uint32_t _pad = 0;
    };
    struct BeamTargetCB
    {
        DirectX::SimpleMath::Vector4 origin[kMaxBeams];
        DirectX::SimpleMath::Vector4 dir[kMaxBeams];
        DirectX::SimpleMath::Vector4 seek[kMaxBeams];
        BeamTargetCmd cmd[kMaxBeams];
        BeamTargetExclude exclude[kMaxBeams];
    };
    static_assert(sizeof(BeamTargetCmd) == 16 && sizeof(BeamTargetExclude) == 16, "SwarmBeamTargetCB layout mismatch");
    static_assert(sizeof(BeamTargetCB) == 320, "SwarmBeamTargetCB layout mismatch");

    // ============================================================
    // 転送回収用（b1）
    // 生成キューの [offset, offset+count) を、プレイヤーから minDist より
    // 遠い活き雑魚へ上書きする
    // ============================================================
    struct RecycleCB
    {
        uint32_t count = 0;
        uint32_t offset = 0;
        float    minDistSq = 0.0f;
        uint32_t _pad = 0;
    };
    static_assert(sizeof(RecycleCB) == 16, "SwarmRecycleCB layout mismatch");
}
