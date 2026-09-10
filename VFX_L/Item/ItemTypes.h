// ============================================================
// ItemTypes.h
// アイテムの型定義とデータ構造。
// 実際の値は入れない（値は Items/ 以下の各ファイルに書く）。
//
// 修飾符の考え方について:
//   AOE 型はまだ実装されていないが、構造は先に用意しておく。
//   後から丸ごと足すより、最初から並べておく方が壊れにくい。
//
// 命名の対応: C++ の型名と概念の対応表
//   共通部 → ItemCommon、種類別の値 → 各種 Def 構造体
// ============================================================
#pragma once
#include "SpellID.h"
#include "VFX_Editor/VFXId.h"          
#include "Component/WandComponent.h"   // SpellStats を借りる
#include "Component/AreaStats.h"
#include <SimpleMath.h>
#include <vector>

// ============================================================
// 種別
// ============================================================
enum class ItemCategory
{
    Unknown,      // 未登録。バグの早期発見用（本来出ないはずの値）
    Projectile,   // 飛行物型（弾）
    Function,     // 機能型（隣接する攻撃ブロックを修飾する）
    Area,         // AOE 型（範囲）
    Frame,        // 設置枠（置ける領域を広げる）
};

// ============================================================
// グリッド上の相対座標（アンカーからのオフセット）
// ============================================================
struct CellOffset
{
    int row = 0;
    int col = 0;
};

// ============================================================
// 修飾の演算子
// 機能型がパラメータをどう書き換えるかを表す
// ============================================================
enum class ModifyOp
{
    Add,        // 加算（+2 個、+15 度 など）
    Multiply,   // 乗算（×0.6 など）
    Set,        // 上書き（固定値にする）
};

// ============================================================
// 飛行物型のパラメータ種別
// 新しいパラメータを増やす時はここに1行 + ApplyModifier に case を1個。
//   増やし忘れると機能型からの修飾が静かに無視される
// ============================================================
enum class SpellParam
{
    Damage,
    Speed,
    Radius,           // 当たり判定の半径
    Lifetime,
    ProjectileCount,  // 分裂：発射数
    SpreadAngle,      // 扇の角度
    CastCount,        // 二重釈放：発射回数
    CastDelay,
    CastInterval,     // 発動間隔
    ManaCost,
};

// ============================================================
// AOE 型のパラメータ種別
// SpellParam とは別の enum にする理由:
//   飛行物と AOE では意味のあるパラメータが違うため。
//   同じ enum に混ぜると片方にしか無い値が選べてしまう
// ============================================================
enum class AreaParam
{
    Radius,
    Duration,
    TickInterval,
    DamagePerTick,
    CastInterval,
    ManaCost,
};

// ============================================================
// 修飾符1件（どのパラメータ + どう演算 + 値）
// ============================================================
struct ParamModifier
{
    SpellParam param = SpellParam::Damage;
    ModifyOp   op = ModifyOp::Add;
    float      value = 0.0f;
};

struct AreaModifier
{
    AreaParam param = AreaParam::Radius;
    ModifyOp  op = ModifyOp::Add;
    float     value = 0.0f;
};

// ============================================================
// 種類を問わず全アイテムが持つ共通部分
// ============================================================
struct ItemCommon
{
    ItemID       id = ItemID::Fireball;
    const char* name = "";
    ItemCategory category = ItemCategory::Projectile;

    // 形状: 占位格（このアイテムが物理的に占めるマス）
    // 1マスだけなら {{0,0}}。異形はここに複数マスを列挙する
    std::vector<CellOffset> occupyCells;

    // 形状: 影響格（機能型がどのマスに効果を及ぼすか）
    // 機能型以外は空のままでよい
    //   飛行物型・設置枠は他を強化しないので影響格を持たない
    std::vector<CellOffset> influenceCells;

    const wchar_t* iconPath = nullptr;

    DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };   // UI 表示色
};

// ============================================================
// 飛行物型
// ============================================================
struct ProjectileItemDef
{
    ItemCommon common;

    // 基礎値（集約後は WandComponent.spells に積まれる）
    SpellStats baseStats;

    // 見た目（当たり判定とは独立。派手に見せても判定は安っぽくしない）
    float       visualSize = 0.9f;
    float       visualStretch = 0.0f;      // 進行方向への引き伸ばし（0=円形）
    VFXId       vfxId = VFXId::None;   // VFX json（未設定なら nullptr）
};

// ============================================================
// 機能型
// 飛行物型・AOE 型の両方に修飾符を持てる。
//   飛行物用と AOE 用で意味が違うことがある
//   （同じ「分裂」でも飛行物では弾数増、AOE では範囲拡大 など）
//   例）分裂符 = ProjectileCount +1、Damage ×0.6、SpreadAngle +15
// ============================================================
struct FunctionItemDef
{
    ItemCommon common;

    std::vector<ParamModifier> spellModifiers;   // 飛行物型（SpellStats）への修飾
    std::vector<AreaModifier>  areaModifiers;    // AOE 型（AreaStats）への修飾
};

// ============================================================
// AOE 型
// 飛行物型と対になる存在: 弾を飛ばす代わりに範囲を発生させる
//   基礎値は AreaStats（SpellStats とは完全に別系統）
// ============================================================
struct AreaItemDef
{
    ItemCommon common;

    AreaStats baseStats;

    // 見た目
    float       visualScale = 1.0f;      // VFX の大きさの倍率
    VFXId       vfxId = VFXId::None;
};

struct FrameItemDef
{
    ItemCommon common;
};
// ============================================================
// 形状のプリセット（座標を手で書かなくて済むように）
// ============================================================
namespace ItemShape
{
    // 1マス
    inline std::vector<CellOffset> Single()
    {
        return { { 0, 0 } };
    }

    // 十字（上下左右）
    inline std::vector<CellOffset> Cross()
    {
        return { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    }

    // 周囲8マス（斜め込み）
    inline std::vector<CellOffset> Around8()
    {
        return {
            { -1, -1 }, { -1, 0 }, { -1, 1 },
            {  0, -1 },            {  0, 1 },
            {  1, -1 }, {  1, 0 }, {  1, 1 },
        };
    }

    // 横一列（幅 w。アンカーは左端）
    inline std::vector<CellOffset> RowLine(int w)
    {
        std::vector<CellOffset> out;
        for (int c = 0; c < w; ++c) out.push_back({ 0, c });
        return out;
    }

    // 縦一列（高さ h。アンカーは上端）
    inline std::vector<CellOffset> ColLine(int h)
    {
        std::vector<CellOffset> out;
        for (int r = 0; r < h; ++r) out.push_back({ r, 0 });
        return out;
    }

    // 矩形（h × w。アンカーは左上）
    inline std::vector<CellOffset> Rect(int h, int w)
    {
        std::vector<CellOffset> out;
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c)
                out.push_back({ r, c });
        return out;
    }
}