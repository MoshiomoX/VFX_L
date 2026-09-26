// ============================================================
// VFXSpriteEntry.h
// Sprite entry：連番絵（sprite sheet、例：PVFX Foundry）を板 1 枚で再生する。
//   貼图は Assets/VFX/SpriteSheet 以下の PNG。コマ数・速さ・繰り返し・基準点は
//   横の json（SpriteSheets）から。entry 側で速さ・繰り返しを上書きできる。
//   板の向き：カメラを向く / 立てる（Y 軸だけ回る）/ 地面に寝かせる。
//   描画は VFXSpriteRenderer（CPU）。GPU の範囲（弾の命中で生まれた物）では
//   SwarmVFXTable が同じ設定を表にして SwarmSystem が描く
// ============================================================
#pragma once
#include "VFX_Editor/VFXEntry.h"
#include "VFX_Editor/SpriteSheets.h"
#include <string>

class VFXSpriteRenderer;

class VFXSpriteEntry : public VFXEntry
{
public:
    enum class Facing : int { Billboard = 0, Upright = 1, Ground = 2 };
    enum class LoopMode : int { FromSheet = 0, Once = 1, Loop = 2 };
    enum class Anchor : int { SheetPivot = 0, Center = 1, Bottom = 2 };

    EntryType GetType() const override { return EntryType::Sprite; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;
    std::unique_ptr<VFXEntry> Clone() const override;
    json ToJson() const override;
    void FromJson(const json& j) override;

    // VFXEffect::CollectAndDispatch から毎フレーム
    void Submit(VFXSpriteRenderer& renderer, const DirectX::SimpleMath::Vector3& worldOffset);

    // ---- GPU 表（SwarmVFXTable）と共通の解釈 ----
    const SpriteSheets::Info* GetSheet() const { return SpriteSheets::Get(sheetPath); }
    bool  IsLooping() const;
    float FrameTime() const;                           // 速さ込みの 1 コマの秒数
    DirectX::SimpleMath::Vector2 Pivot() const;        // Anchor 込みの基準点（0..1）
    DirectX::SimpleMath::Vector2 WorldSize() const;    // 幅・高さ（m）

    // ---- 設定 ----
    std::string sheetPath;                             // "Assets/VFX/SpriteSheet/..png"
    DirectX::SimpleMath::Vector3 offset = { 0, 0, 0 }; // effect の位置からのずらし
    float size = 2.0f;                                 // コマの高さ（m）。幅は縦横比から
    int   facing = (int)Facing::Billboard;
    float rotationDeg = 0.0f;                          // 板の面内の回転
    DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };
    int   blend = 0;                                   // 0 = 半透明 / 1 = 加算
    float speed = 1.0f;                                // 再生速度の倍率
    int   loopMode = (int)LoopMode::FromSheet;
    int   anchor = (int)Anchor::SheetPivot;
    bool  onTop = false;                               // 深度を見ずに一番手前へ（体に隠れる升級・被弾用）。GPU の範囲では無視

private:
    int CurrentFrame() const;   // 終わっていたら -1
    float m_Age = 0.0f;
};
