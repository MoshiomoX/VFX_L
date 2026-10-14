// ============================================================
// SpriteSheets.h
// 連番画像（sprite sheet）の表。Sprite entry（CPU）と GPU の範囲 sprite が使う。
//
//   PNG の横に同名の json があれば読む：
//     PVFX Foundry の grid manifest（"schema": "pvfx.export-manifest/..."）
//       コマ数 = frames の数、格子 = frames[0].sheet の幅・高さ（列数 = 画像幅 / コマ幅）、
//       1 コマの時間 = duration（numerator_ms / denominator）、loop_mode、
//       pivot（画素。コマの左上原点）
//     簡易形式 { "cols", "rows", "frames", "fps", "loop", "pivot": [x, y]（0..1）, "filter" }
//   json が無ければ 1 コマ（画像全体）
//   同じパスは 1 回だけ読む（以後は cache）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>
#include <string>

class Texture;

namespace SpriteSheets
{
    struct Info
    {
        std::string path;                     // PNG（"Assets/..."）
        std::shared_ptr<Texture> texture;
        int   texW = 0, texH = 0;
        int   cellW = 0, cellH = 0;
        int   cols = 1;
        int   frameCount = 1;
        float frameTime = 0.05f;              // 秒 / コマ
        bool  loop = false;                   // 素材が「繰り返す」前提か（entry 側で上書きできる）
        bool  point = true;                   // 最近傍で読む（ピクセルアート）
        DirectX::SimpleMath::Vector2 pivot = { 0.5f, 0.5f };   // コマ内の基準点（0..1、左上原点）

        float Duration() const { return frameTime * (float)frameCount; }

        // コマ i の UV（左上 xy と大きさ zw）
        DirectX::SimpleMath::Vector4 FrameUV(int i) const;
    };

    // 読めなければ nullptr（2 回目以降も nullptr を返す）
    const Info* Get(const std::string& pngPath);
}
