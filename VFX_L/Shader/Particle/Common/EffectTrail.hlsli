// ============================================================
// EffectTrail.hlsli
// アンカー点（エフェクトの Trail エントリ: エフェクト位置 + オフセット）で動く
// 帯トレイル。全部 GPU で行う。パーティクルごとのトレイル
// （ParticleTrail.hlsli）は各パーティクル自身の動きを追うが、こちらは
// エフェクトを追うので、止まったパーティクルの芯でも帯が残る。
//
// トレイルのインスタンスはそれぞれ確定した点 EFFECT_TRAIL_POINTS 個のリングと
// 状態レコード 1 つを持つ:
//     effectTrailPoints[trailIndex * EFFECT_TRAIL_POINTS + slot]
//     effectTrailStates[trailIndex]
//
// 毎フレーム、インスタンスごとにアンカーが 1 つ届く（EffectTrailCS）:
//   - 生きている先頭はアンカーを追う。headTime = 実際に最後に動いた時刻。
//     止まった先頭は他の点と同じように古くなる
//   - 先頭が最新の点から minDistance 離れたら点を確定する
//     （距離ベース。Unity の TrailRenderer と同じ）
//   - 点はそれぞれ <style.lifetime> 秒生きる。末尾はちょうど
//     age == lifetime で切るので、帯はなめらかに縮む
//   - command NONE = 切り離し: 新しい点は足さず、縮んで消える
//
// ストリップの並び（EffectTrailVS）、インスタンス 1 つ = トレイル 1 本:
//   pair 0                : 生きている先頭
//   pair 1 .. validCount  : リングの点、新しい順
//   pair validCount + 1   : 末尾（切った点、または一番古い点）
//   末尾より後の pair は末尾に潰す（縮退三角形）
//
// GPUParticle.h の EffectTrail*GPU / kEffectTrailPoints と一致させること
// ============================================================
#ifndef EFFECT_TRAIL_HLSLI
#define EFFECT_TRAIL_HLSLI

#define EFFECT_TRAIL_POINTS 64u
#define EFFECT_TRAIL_STRIP_PAIRS (EFFECT_TRAIL_POINTS + 2u)

#define EFFECT_TRAIL_CMD_NONE  0u // 切り離し: 薄れ続け、新しい点は足さない
#define EFFECT_TRAIL_CMD_MOVE  1u // このフレームの先頭は position にある
#define EFFECT_TRAIL_CMD_RESET 2u // position から新しいトレイルを始める

// 毎フレーム全インスタンス分を書く（CPU からアップロード）。32 バイト
struct EffectTrailAnchor
{
    float3 position;
    float minDistance;
    uint styleSlot; // スタイル番号 + 1、0 = 未使用のインスタンス
    uint command;
    uint2 _pad;
};

// 20 バイト
struct EffectTrailPoint
{
    float3 position;
    float time; // 確定した時の時計の値
    float distance; // トレイルの始点からの経路長（Tile UV）
};

// GPU 専用。64 バイト
struct EffectTrailState
{
    float3 headPos;
    float headTime;
    float headDist;
    uint ringHead; // 最新の確定点
    uint ringCount; // リングに残っている確定点の数
    uint validCount; // 末尾より前にあるリングの点（このフレーム）
    float3 tailPos;
    float tailTime;
    float tailDist;
    float3 _pad;
};

uint EffectTrailSlot(uint ringHead, uint newestFirst)
{
    return (ringHead + EFFECT_TRAIL_POINTS - newestFirst) % EFFECT_TRAIL_POINTS;
}

#endif
