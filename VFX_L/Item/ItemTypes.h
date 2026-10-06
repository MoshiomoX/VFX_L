// ============================================================
// ItemTypes.h
// アイテムの型定義とデータ構造。
// 実際の値は入れない（値は Items/ 以下の各ファイルに書く）。
//
// 修飾ルーンの考え方について:
//   AOE 型はまだ実装されていないが、構造は先に用意しておく。
//   後から丸ごと足すより、最初から並べておく方が壊れにくい。
//
// 命名の対応: C++ の型名と概念の対応表
//   共通部 → ItemCommon、種類別の値 → 各種 Def 構造体
// ============================================================
#pragma once
#include "SpellID.h"
#include "VFX_Editor/VFXId.h"          
#include <string>
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
    Stat,         // 能力値の成長（レベルアップ専用。選んだ瞬間に効いて、物としては残らない）
    Summon,       // 召喚物（2026-10-06）。影響マスに置いた魔法を貯蔵し、プレイヤーの周りの光球が代わりに撃つ。ルーンの影響は受けない
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
    CastCount,        // 二重詠唱：発射回数
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
// 修飾ルーン1件（どのパラメータ + どう演算 + 値）
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
    const char* name = "";               // 内部名（ItemData の json 名・ログ・ImGui 用）
    ItemCategory category = ItemCategory::Projectile;

    // 画面に出す名前と一言の説明（UI 用）。数値は ItemInfo が定義とプロファイルから自動で並べるので、
    // ここには役割だけ書く。空なら name を出す
    const wchar_t* displayName = L"";
    const wchar_t* description = L"";

    // 形状: 占有マス（このアイテムが物理的に占めるマス）
    // 1マスだけなら {{0,0}}。異形はここに複数マスを列挙する
    // ※Items/*.h に書くのはコードの既定。Assets/Data/ItemData/<名前>.json があれば
    //   起動時にそちらで上書きされる（投射物エディタの Item Shapes 頁で塗って保存した物）
    std::vector<CellOffset> occupyCells;

    // 形状: 影響マス（機能型がどのマスに効果を及ぼすか）
    // 機能型以外は空のままでよい
    //   飛行物型・設置枠は他を強化しないので影響マスを持たない
    std::vector<CellOffset> influenceCells;

    const wchar_t* iconPath = nullptr;

    DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };   // UI 表示色

    // 上級魔法の前提（2026-09-30）。空 = 基本魔法（自分で撃つ）。
    // 空でなければ自分では撃たず、ここに並ぶ基本魔法が「全種類」影響マスをこのブロックに届かせている時だけ有効になる。
    // 有効な時は、届いている基本魔法の弾が消えた場所（命中・寿命・壁）で撃つ（隕石ならそこへ落ちる、光線ならそこへ向けて撃つ）。
    // 飛行物型（隕石）と範囲型（光線）の両方が持てるのでここ（ItemCommon）にある。
    // 判定は BackpackLogic::GetTriggerDrivers / IsTriggerReady（集約と UI の説明で共用）
    std::vector<ItemID> triggeredBy;
};

// ============================================================
// 飛行物型
// ============================================================
struct ProjectileItemDef
{
    ItemCommon common;

    // 「どう撃つか」（集約後は WandComponent.spells に積まれる）。
    // ここで意味を持つのは projectileCount / spreadAngle / castCount / castDelay /
    // castInterval / manaCost。damage / speed / radius / lifetime は
    // 集約時に profile から写されるので、ここに書いても使われない
    SpellStats baseStats;

    // 弾そのもの（飛び方・見た目・威力・速さ・判定・寿命・命中で出す範囲）。
    // 投射物エディタで作ったプロファイルの名前（Assets/Data/ProjectileData/<名前>.json）。
    // 空 or 見つからない → 組み込みの直進（火球相当の値）
    std::string profile;
};

// ============================================================
// 機能型
// 飛行物型・AOE 型の両方に修飾ルーンを持てる。
//   飛行物用と AOE 用で意味が違うことがある
//   （同じ「分裂」でも飛行物では弾数増、AOE では範囲拡大 など）
//   例）分裂ルーン = ProjectileCount +1、Damage ×0.6、SpreadAngle +15
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

    // 投射物エディタの Area ページで作ったプロファイルの名前（Assets/Data/AreaData/<名前>.json）。
    // あれば 半径・持続・tick・威力 の基礎値と、単発/持続・見た目 をそこから取る。空 → baseStats のまま
    std::string profile;
};

struct FrameItemDef
{
    ItemCommon common;
};

// ============================================================
// 召喚物（2026-10-06、ユーザー：「魔法を貯蔵する単位」）
//   影響マス（左右 2 マス）に置いた魔法を最大 2 つ貯蔵する。貯蔵された魔法は杖から撃てなくなり、
//   プレイヤーの周りに出る光球（最大 maxOrbs 個、orbInterval 秒毎に 1 個、各 orbLife 秒）が
//   自分の位置から、その魔法の発動間隔で撃つ。消費 MP は魔法の manaMul 倍。
//   上級魔法も貯蔵できる（光球が直接撃つ = 前提の基本魔法は要らない）。
//   貯蔵された魔法は自分の隣のルーンの修飾を受ける。召喚物そのものはルーンの影響を受けない
//   （召喚物専用の道具が後で付く予定）。光球の色は貯蔵した魔法の色の混色
// ============================================================
struct SummonItemDef
{
    ItemCommon common;
    int   maxOrbs = 3;         // 同時に出る光球の数
    float orbInterval = 2.0f;  // 光球が出る間隔（秒）
    float orbLife = 8.0f;      // 光球 1 個の持続（秒）
    float manaMul = 1.5f;      // 貯蔵された魔法の消費 MP の倍率
    float orbitRadius = 1.1f;  // プレイヤーからの距離（m）
    float orbitHeight = 1.4f;  // 足元からの高さ（m）
    float orbitSpeed = 1.2f;   // 回る速さ（rad/秒）
    const char* vfxFile = "SpellOrb.json";   // 光球の見た目（VFXData）。色は貯蔵した魔法で染める
};

// ============================================================
// 能力値の成長（レベルアップの候補専用）
//   バックパックに置く物ではないので形（occupyCells）は持たない。
//   ItemDatabase::GetAllIDs には入らず、GetLevelUpOnlyIDs からだけ出る
//   （バックパック・呪文書・デバッグの一覧に混ざらないように）
// ============================================================
enum class StatKind
{
    MaxHealth,    // HealthComponent::max
    MaxMana,      // ManaComponent::max
    MoveSpeed,    // PlayerStatsComponent::moveSpeed（歩く速さ。滑りの初速もこれの倍率）
    JumpPower,    // PlayerStatsComponent::jumpPower（跳んだ瞬間の上向きの速さ）
    ManaRegen,    // ManaComponent::regen（1 秒あたりの魔力回復）
    JumpCount,    // PlayerStatsComponent::extraJumps（空中で追加で跳べる回数。amount 回ぶん足す）
    SpellPower,   // PlayerStatsComponent::spellPower（全部の攻撃魔法のダメージの倍率。バックパックを集約し直して効く）
};

struct StatItemDef
{
    ItemCommon common;
    StatKind kind = StatKind::MaxHealth;
    // percent = false: 上限と今の値の両方に足す（取った瞬間に使える）
    // percent = true : 今の値に (1 + amount) を掛ける（0.08 = +8%。重ねると複利）
    float amount = 20.0f;
    bool  percent = false;
    const wchar_t* cardLabel = L"";    // レベルアップのカードに出す文字（形の代わり）
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

    // 形の上下左右に接するマス（形そのものは除く）。1 マスの形なら Cross と同じ。
    // 基本魔法が多マスになったので（2026-10-04）、誘発の届く範囲 = 形のどこかの上下左右、に一般化した物
    inline std::vector<CellOffset> Around4(const std::vector<CellOffset>& shape)
    {
        auto has = [](const std::vector<CellOffset>& v, int r, int c)
            {
                for (const CellOffset& o : v)
                    if (o.row == r && o.col == c) return true;
                return false;
            };
        static const int kDir[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        std::vector<CellOffset> out;
        for (const CellOffset& s : shape)
            for (const auto& d : kDir)
            {
                const int r = s.row + d[0], c = s.col + d[1];
                if (!has(shape, r, c) && !has(out, r, c)) out.push_back({ r, c });
            }
        return out;
    }

    // 斜め 4 マス（X 字。十字の Cross と重ならない）
    inline std::vector<CellOffset> Diagonal()
    {
        return { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    }

    // 左右 2 マス（横一列の両隣。十字・斜めのどちらとも一部しか重ならない）
    inline std::vector<CellOffset> Sides()
    {
        return { { 0, -1 }, { 0, 1 } };
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
