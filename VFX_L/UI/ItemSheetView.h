// ============================================================
// ItemSheetView.h
// ItemInfo::Sheet（道具の説明）を画面に描く部品。
//   ・Wrap        : 文を幅で折り返す（日本語は 1 文字ずつ、英単語は空白で）
//   ・DrawBody    : 説明・特性・能力値・footer を上から積む（カードと tooltip で共用）
//   ・DrawTooltip : 名前 + 本文 + 背景の箱。マウスの右下に出し、画面内に寄せる
//
// 文字は SpriteRenderer の後にまとめて描かれる。tooltip の箱が他の UI の文字より
// 上に来るように、GameUI は tooltip を別の Begin / End（覆い層）で描く。
// ============================================================
#pragma once
#include "Item/ItemInfo.h"
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <vector>

class SpriteRenderer;
class TextRenderer;
class Texture;

namespace ItemSheetView
{
    struct Style
    {
        float titleScale = 0.50f;
        float bodyScale = 0.40f;
        float smallScale = 0.36f;
        float lineGap = 2.0f;        // 行の間（px）
        float sectionGap = 8.0f;     // 説明・特性・能力値の間（px）
        float pad = 12.0f;           // 箱の内側の余白（px）

        // 画面に出る時は明るく持ち上がる（0.07 が中間の灰色に見えた）ので暗めに
        DirectX::SimpleMath::Vector4 panelColor = { 0.015f, 0.013f, 0.022f, 0.95f };
        DirectX::SimpleMath::Vector4 textColor = { 1.00f, 1.00f, 1.00f, 1.0f };
        DirectX::SimpleMath::Vector4 descColor = { 0.85f, 0.85f, 0.88f, 1.0f };
        DirectX::SimpleMath::Vector4 traitColor = { 1.00f, 0.88f, 0.55f, 1.0f };
        DirectX::SimpleMath::Vector4 labelColor = { 0.65f, 0.65f, 0.70f, 1.0f };
        DirectX::SimpleMath::Vector4 dimColor = { 0.50f, 0.50f, 0.55f, 1.0f };
        DirectX::SimpleMath::Vector4 betterColor = { 0.45f, 0.95f, 0.50f, 1.0f };
        DirectX::SimpleMath::Vector4 worseColor = { 1.00f, 0.45f, 0.40f, 1.0f };
        DirectX::SimpleMath::Vector4 footerColor = { 0.55f, 0.85f, 1.00f, 1.0f };
        DirectX::SimpleMath::Vector4 ruleColor = { 1.00f, 1.00f, 1.00f, 0.12f };

        // 全部の大きさ（文字・余白）を k 倍にした物
        Style Scaled(float k) const;
    };

    // 文を maxWidth に収まるように折り返す。行頭に「、。」が来る時は前の行に付ける
    std::vector<std::wstring> Wrap(const TextRenderer& text, const std::wstring& s,
        float maxWidth, float scale);

    // 説明（description）・特性（traits）・能力値（stats）・footer を pos から下へ積む。
    // 使った高さを返す。sprite が null なら区切り線を描かない。draw = false なら測るだけ
    float DrawBody(SpriteRenderer* sprite, const std::shared_ptr<Texture>& white,
        TextRenderer& text, const ItemInfo::Sheet& sheet,
        const DirectX::SimpleMath::Vector2& pos, float width,
        const Style& style, bool draw = true, bool withDescription = true);

    // 名前つきの箱。anchor（マウス位置）の右下に出し、画面からはみ出す時は反対側へ寄せる
    void DrawTooltip(SpriteRenderer& sprite, const std::shared_ptr<Texture>& white,
        TextRenderer& text, const ItemInfo::Sheet& sheet,
        const DirectX::SimpleMath::Vector2& anchor,
        const DirectX::SimpleMath::Vector2& screen, float width, const Style& style);
}
