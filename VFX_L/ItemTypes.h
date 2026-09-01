// ============================================================
// ItemTypes.h
// 魔法ブロックのデータ構造定義。
// ここには具体的な数値を一切書かない。
//   各魔法の数値は Items/ 以下の個別ファイルに1つずつ置く。
//
// 大類型ごとに構造体を分ける理由：
//   AOE 用のフィールドを増やしても飛行物型の構造体が膨らまない。
//   新しい大類型を足す時も既存の構造体に触らずに済む。
//
// 定義順に注意：C++ は「先に定義、後で使用」なので
//   列挙型 → 修飾構造 → ItemCommon → 各大類型 Def の順に並べる。
// ============================================================
#pragma once
#include "SpellID.h"
#include "WandComponent.h"   // SpellStats を流用する
#include "AreaStats.h"
#include <SimpleMath.h>
#include <vector>

// ============================================================
// 大類型
// ============================================================
enum class ItemCategory
{
    Unknown,      // 未登録。既定値をこれにして事故に気付けるようにする
    Projectile,   // 飛行物型（攻撃）
    Function,     // 機能型（他のブロックの値を書き換える）
    Area,         // AOE 型（攻撃）
	Frame,        // 設置枠（魔法を置くための足場）
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
// 演算の種類
// 倍率でも直接加算でも同じ仕組みで表せるようにする
// ============================================================
enum class ModifyOp
{
    Add,        // 加算（+2 発、+15 度 など）
    Multiply,   // 乗算（×0.6 倍 など）
    Set,        // 上書き（特殊用途）
};

// ============================================================
// 飛行物型の修飾対象
// 新しく修飾対象を増やす時はここに1行 + ApplyModifier に case を1つ。
//   既存の構造体や呼び出し側には一切触らない。
// ============================================================
enum class SpellParam
{
    Damage,
    Speed,
    Radius,           // 投射物の当たり半径
    Lifetime,
    ProjectileCount,  // 分裂：同時発射数
    SpreadAngle,      // 散布角
    CastCount,        // 二重釈放：発射回数
    CastDelay,
    CastInterval,     // 詠唱間隔
    ManaCost,
};

// ============================================================
// AOE 型の修飾対象
// ※SpellParam とは別 enum にする。
//   飛行物用のパラメータと混ざらないので、
//   どちらかを増やしても他方に影響しない。
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
// 修飾指定（対象パラメータ + 演算 + 値）
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
// 全大類型で共通する部分
// ============================================================
struct ItemCommon
{
    ItemID       id = ItemID::Fireball;
    const char* name = "";
    ItemCategory category = ItemCategory::Projectile;

    // 形状：占位格（他のブロックと重ねられない）
    // 第1段階は単格 {{0,0}}。異形は要素を増やすだけで対応できる。
    std::vector<CellOffset> occupyCells;

    // 形状：影響格（占位には数えない。他のブロックの占位格に重なってよい）
    // ※機能型に限らず全大類型が持てる。
    //   「他を強化する攻撃ブロック」を将来作れるようにするため。
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

    // 基礎値（そのまま WandComponent.spells に入る形）
    SpellStats baseStats;

    // 見た目（当たり判定とは独立。見た目は大きく、判定は小さく）
    float       visualSize = 0.9f;
    float       visualStretch = 0.0f;      // 速度方向への引き伸ばし（0=円形）
    const char* vfxPath = nullptr;   // VFX json（不要なら nullptr）
};

// ============================================================
// 機能型
// ※飛行物向けと AOE 向けの修飾を別々に持つ。
//   同じ機能ブロックが両方に効いてもよい。
//   片方だけ書けばその大類型にだけ効く。
//   例）分裂符 = ProjectileCount +1、Damage ×0.6、SpreadAngle +15
// ============================================================
struct FunctionItemDef
{
    ItemCommon common;

    std::vector<ParamModifier> spellModifiers;   // 飛行物型（SpellStats）向け
    std::vector<AreaModifier>  areaModifiers;    // AOE 型（AreaStats）向け
};

// ============================================================
// AOE 型
// ※飛行物型と対称な構造：基礎値 + 見た目
//   基礎値は AreaStats（SpellStats とは別系統）
// ============================================================
struct AreaItemDef
{
    ItemCommon common;

    AreaStats baseStats;

    // 見た目
    float       visualScale = 1.0f;      // VFX のスケール倍率
    const char* vfxPath = nullptr;
};

struct FrameItemDef
{
    ItemCommon common;  
};
// ============================================================
// 形状のヘルパ（各魔法ファイルから使う）
// ============================================================
namespace ItemShape
{
    // 単格
    inline std::vector<CellOffset> Single()
    {
        return { { 0, 0 } };
    }

    // 十字四隣（上下左右）
    inline std::vector<CellOffset> Cross()
    {
        return { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    }

    // 八方（斜めも含む）
    inline std::vector<CellOffset> Around8()
    {
        return {
            { -1, -1 }, { -1, 0 }, { -1, 1 },
            {  0, -1 },            {  0, 1 },
            {  1, -1 }, {  1, 0 }, {  1, 1 },
        };
    }

    // 横一列（幅 w、アンカーは左端）
    inline std::vector<CellOffset> RowLine(int w)
    {
        std::vector<CellOffset> out;
        for (int c = 0; c < w; ++c) out.push_back({ 0, c });
        return out;
    }

    // 縦一列（高さ h、アンカーは上端）
    inline std::vector<CellOffset> ColLine(int h)
    {
        std::vector<CellOffset> out;
        for (int r = 0; r < h; ++r) out.push_back({ r, 0 });
        return out;
    }

    // 矩形（h × w、アンカーは左上）
    inline std::vector<CellOffset> Rect(int h, int w)
    {
        std::vector<CellOffset> out;
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c)
                out.push_back({ r, c });
        return out;
    }
}