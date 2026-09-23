// ============================================================
// SkinnedAnimComponent.h
// 骨付きモデルの再生状態（純データ）。
//
//   3 層でポーズを作る:
//     base  … 全身（Idle / Run）。常に寄与 1
//     upper … 上半身だけ差し替え（施法）。upperMask の骨に weight で混ぜる
//     over  … 全身上書き（被弾 / 死亡）。weight で混ぜる
//   各層はクリップ切替時に前クリップからクロスフェードする。
//
//   誰が何を書くか:
//     PlayerAnimSystem   … 状態機を見て Play() / weight の目標を書く
//     SkinnedAnimSystem  … 時刻とフェードを進める（Update）
//     RenderSystem       … サンプリング → 蒙皮 → 描画（Render）
//
//   GPU 側（蒙皮結果バッファ）は実体ごとに要るので gpu は共有しない。
//   model（bind 頂点・骨・クリップ）は ResourceManager の共有で良い。
// ============================================================
#pragma once
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SkinnedModel;
class SkinnedModelGPU;

struct SkinnedAnimLayer
{
    int   clip = -1;            // 再生中（-1 = 無し）
    float time = 0.0f;          // 秒
    float speed = 1.0f;
    bool  loop = true;
    bool  finished = false;     // 非ループで末尾に着いた

    // クロスフェード元（fade が 1 になるまで前クリップも進める）
    int   prevClip = -1;
    float prevTime = 0.0f;
    float fade = 1.0f;          // 0 → 1
    float fadeDuration = 0.15f;

    // 層の寄与（base は使わない）。weight は targetWeight へ fadeDuration で寄る
    float weight = 0.0f;
    float targetWeight = 0.0f;

    // クリップを切り替える。同じクリップなら restart のときだけ頭出し
    void Play(int clipIndex, bool loopClip, float playSpeed = 1.0f,
        float fadeSec = 0.15f, bool restart = false)
    {
        if (clipIndex == clip && !restart) return;
        if (clip >= 0 && clipIndex != clip)
        {
            prevClip = clip;
            prevTime = time;
            fade = 0.0f;
            fadeDuration = fadeSec;
        }
        clip = clipIndex;
        time = 0.0f;
        speed = playSpeed;
        loop = loopClip;
        finished = false;
    }
};

struct SkinnedAnimComponent
{
    std::shared_ptr<SkinnedModel>    model;
    std::shared_ptr<SkinnedModelGPU> gpu;

    // モデル空間 → Entity 空間の合わせ。
    // 足元が原点のモデルを、中心が原点のカプセルに合わせる時に offset.y を下げる
    DirectX::SimpleMath::Vector3 offset = { 0.0f, 0.0f, 0.0f };
    float yawOffsetDeg = 0.0f;   // モデルの正面が +Z でない時の補正
    float scale = 1.0f;
    bool  visible = true;

    SkinnedAnimLayer base;
    SkinnedAnimLayer upper;
    SkinnedAnimLayer over;
    std::vector<float> upperMask;   // 骨毎 0/1。空なら upper 層は使わない
};
