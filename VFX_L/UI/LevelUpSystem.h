// ============================================================
// LevelUpSystem.h
// 経験値がしきい値を超えたら習得候補を抽選する。
//
// 選ばせる処理はここに置かない。
// System は「候補を作る」「選ばれたものを適用する」だけを担当し、
// 提示と入力は UI 側が持つ。
//
// 連続レベルアップは1回ずつ処理する。
// まとめて上げると三択が何度も続けて出て、選ぶ作業だけが残る。
// 余った経験値は持ち越して、選び終わってから次へ進む。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "SpellID.h"
#include "Item/ItemRarity.h"
#include "Player/LevelComponent.h"
#include <vector>

class Registry;
struct StatItemDef;
struct LevelComponent;

class LevelUpSystem
{
public:
    void Update(Registry& reg);

    // 選択を確定する。UI から呼ぶ。
    // 候補に含まれない ID を渡された場合は何もしない。
    static bool Choose(Registry& reg, Entity player, ItemID picked);

    // 誰かが選択待ちか（シーンの一時停止判定に使う）
    static bool IsAnyoneChoosing(Registry& reg);

    // レベルアップと同じ三択を出す。レベルは上がらず、経験値も減らない（報酬の箱など）。
    // 既に選択待ち・候補が無い時は false（出していない）。premium = 金色の箱（稀有度が少し出やすい）
    bool OfferChoices(Registry& reg, Entity player, bool premium = false);

    // 金貨を払って今の四択を引き直す（同じ四択の中で 10、20、30…と上がる）。
    // 今の候補はなるべく外して引く。選択待ちでない・金貨が足りない時は false
    bool Reroll(Registry& reg, Entity player);
    // 次の引き直しの値段（選択待ちでなければ 0 回目の値段）
    int RerollCost(const LevelComponent& lv) const { return rerollBaseCost + rerollStepCost * lv.rerollCount; }

    // ---- 調整値 ----
    int choiceCount = 4;        // 候補の数
    // 能力値の札（最大 HP・MP・速さ・MP 回復・ジャンプ回数・魔法威力・シールド）を四択に混ぜるか。
    // 既定は false（成長は魔法・ルーン・設置枠から）
    bool statCards = false;
    bool shieldCard = true;     // statCards = false でも「最大シールド +25」だけは出す（シールドの手に入る所はこれだけ）
    float statWeight = 0.35f;   // 能力値の札 1 枚の出やすさ（魔法・ルーン 1 枚 = 1）
    // 設置枠 1 種類の出やすさ × その枠の FrameItemDef::weight（枠は種類が多いので小さめ）
    float frameWeight = 0.2f;
    // 保底：四択を続けてこの回数出す内に設置枠が 1 枚も無ければ、その回の 1 枚を設置枠にする（0 = しない）。
    // レベルアップ・宝箱・引き直しをそれぞれ 1 回と数える
    int framePity = 3;
    // 稀有度（ItemRarity.h）：1 枚毎に先に稀有度を引き（経過時間で決まる率）、その段の中で上の種類の重みで引く
    Rarity::Odds rarityOdds;
    float runMinutes = 0.0f;                        // このプレイの経過時間（シーンが毎フレーム入れる。Rarity::RunMinutes）
    float premiumMinutes = Rarity::kPremiumMinutes; // 金色の箱の四択は時間にこれを足す
    int rerollBaseCost = 10;    // 引き直し 1 回目の金貨
    int rerollStepCost = 10;    // 同じ四択で引き直す度に上がる分
    float levelUpDelay = 1.2f;  // レベルアップしてから四択を出すまでの間（秒。その間は遊べる）

    // ---- 統計（ImGui 表示用）----
    int GetTotalLevelUps() const { return m_TotalLevelUps; }
    int GetTotalOffers() const { return m_TotalOffers; }
    // TEMP-TEST: 四択の候補だけを詰める（自動テスト rarity が稀有度の出方を数える。レベル・経験値は触らない）
    int DebugFill(LevelComponent& lv, bool premium) { return FillChoices(lv, nullptr, premium); }

private:
    void RollChoices(Registry& reg, Entity player);
    // 候補を pendingChoices に詰める（レベルアップと報酬で共通）。詰めた数を返す
    // avoid: 池から外す物（引き直しで同じ札ばかり出ないように）。外すと足りない時は外さずに引く
    // premium: 金色の箱の四択（稀有度が少し出やすい。premiumMinutes）
    int FillChoices(LevelComponent& lv, const std::vector<ItemID>* avoid = nullptr, bool premium = false);
    int m_TotalOffers = 0;   // 報酬の三択を出した回数

    // 能力値（生命・魔力の上限）をその場で適用する
    static void ApplyStat(Registry& reg, Entity player, const StatItemDef& stat);

    int m_TotalLevelUps = 0;
};