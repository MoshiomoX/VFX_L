// ============================================================
// BossBrain.h
// Boss の次の技を決める（規則版。2026-10-07、ユーザー「Boss の技を作る」。選び方は推奨の「距離 + 技ごとの冷却」）。
//   ・技ごとに冷却（使ってから cooldown 秒は選ばない）。HP が狂暴の線を切った後は冷却 × enragedCooldownMul
//   ・プレイヤーまでの距離で 近い（< nearDist）/ 中 / 遠い（> farDist）の 3 段に分け、段ごとの重みで抽選
//     （遠ければ突進・投石雨、近ければ重撃・衝撃波が出やすい）。minDist〜maxDist の外の技は選ばない
//   ・直前と同じ技は、他に選べる技がある限り選ばない
// 後で Jev（AI の判断。jev-boss-ai-plan）に替える時はこのクラスの Choose だけを差し替える
// ============================================================
#pragma once
#include <random>

enum class BossMove : int
{
    None = -1,
    Slam = 0,     // 重撃：プレイヤーの足元に赤い輪を 3 つ（2026-10-03 からの技）
    RockRain,     // 投石雨：プレイヤーの周りのばらばらな所に小さい輪、岩が落ちる（走って避ける）
    Shockwave,    // 衝撃波：Boss が地面を叩き、地面を這う光の帯が外へ広がる（跳んで避ける）
    Charge,       // 突進：道筋の輪が出た後、Boss が一直線に走る（横へ避ける）
    Summon,       // 召喚：Boss が止まって吠え、周りの小さな輪から雑魚・自爆兵が湧く（2026-10-07）
    // ---- 砂の守護者（砂漠の Boss）だけの技（2026-10-24。BossType::SandGuardian の時だけ enabled）----
    SandSpear,    // 砂の槍：Boss からプレイヤーへ線上に輪 → 足元から先へ石のトゲが突き出る（横へ避ける）
    Burrow,       // 潜砂奇襲：砂に潜って（撃てない）砂丘だけがプレイヤーを追う → 止まって輪 → 飛び出して叩く（輪から走って出る）
    Quicksand,    // 流沙：Boss の周りに回る砂の渦。中では遅くなり中心へ引かれる（跳んでいる間は効かない）。渦は技の後も残る
    // ---- 大サソリ（砂漠 2 面目）だけの技（2026-10-26。BossType::Scorpion の時だけ enabled）----
    Sting,        // 毒針：向きを固めて帯の予告 → 短く突っ込んで尾で刺す。当たると毒（しばらく走りが遅い）。横へ避ける
    ClawSweep,    // 鋏の薙ぎ払い：正面の扇の予告 → 鋏で薙ぐ（吹き飛ばし）。扇から出る
    // ---- 墓守の騎士（遺跡 1 面目）だけの技（2026-10-26。BossType::CryptKnight の時だけ enabled）----
    ChainPull,    // 双鎖：向きを固めて 2 本の帯の予告 → 鎖を 2 本投げ、掛かればプレイヤーを足元へ引き寄せ → すぐ重撃。帯の間 / 外へ
    // ---- 月下の狼人（草原 2 面目）だけの技（2026-10-26。BossType::Werewolf の時だけ enabled）----
    Pounce,       // 飛びかかり：しゃがむ → プレイヤーの所へ跳ぶ（着地点に輪）→ 着地で周り（輪から走って出る）
    ClawCombo,    // 爪の三連：短い帯の予告 → 短く突っ込んで斬る × 3（狂暴で 5。毎回向きを取り直す）。横へ
    Howl,         // 遠吠え：止まって吠える → 周りから雑魚が湧き、しばらく技の間が半分
    Count
};

// どの Boss か（面毎。StageDef::boss）。技の組み合わせ・召喚する敵・体の色が変わる（BossAttacks::Configure）
enum class BossType : int
{
    StoneGiant = 0,   // 草原（今までの Boss。重撃 / 投石雨 / 衝撃波 / 突進 / 召喚）
    SandGuardian,     // 砂漠「砂の守護者」：共通の 5 つ + 砂の槍 / 潜砂奇襲 / 流沙、召喚はトゲ玉・砂潜り
    Scorpion,         // 「大サソリ」（2026-10-26 ユーザーの案、まだどの面にも付けていない）：見た目 = 方块人を隠して飾りだけ
                      // （BossLooks）。技は共通の 5 つ + 潜砂（専用の技は後で）
    Stag,             // 草原 3 面目「雷鳴の雄鹿」（2026-10-26）：方块の動物の最初の 1 匹（体を隠し、腕 = 前脚・脚 = 後脚で四足の歩き）。
                      // 技はまだ共通の 5 つ（突進が主役）
    CryptKnight,      // 遺跡 1 面目「墓守の騎士」（2026-10-26）：大きな呪いの鎧。重撃 / 衝撃波 / 双鎖（引き寄せて重撃）/ 鎧の召喚、
                      // 1 回倒れると立ち上がる（HP 半分、狂暴）
    Werewolf,         // 草原 2 面目「月下の狼人」（2026-10-26、ユーザー「人形の半人半獣」）：方块人を隠して灰毛の狼人の部品を部品毎に。
                      // 飛びかかり / 爪の三連 / 遠吠え（召喚 + 間が半分）。狂暴で赤く、三連が五連
    Count
};
const char* BossTypeName(BossType t);
// 画面に出す名前（日本語。トレーニングのメニューの「ボスの種類」など。2026-10-27）
const wchar_t* BossDisplayName(BossType t);

const char* BossMoveName(BossMove m);

class BossBrain
{
public:
    struct Rule
    {
        bool  enabled = true;
        float cooldown = 6.0f;            // 秒
        float wNear = 1.0f, wMid = 1.0f, wFar = 1.0f;   // 距離の段ごとの重み（0 = その段では出さない）
        float minDist = 0.0f, maxDist = 1.0e9f;         // この外なら選ばない（m）
    };
    Rule  rules[(int)BossMove::Count];
    float nearDist = 8.0f;
    float farDist = 16.0f;
    float enragedCooldownMul = 0.7f;

    BossBrain();
    void Reset();
    void Tick(float dt);
    // dist = Boss からプレイヤーまでの水平距離。選べる技が無ければ None（冷却待ち）
    BossMove Choose(float dist, std::mt19937& rng) const;
    void OnStarted(BossMove m, bool enraged);

    float CooldownLeft(BossMove m) const { return (m == BossMove::None) ? 0.0f : m_Cooldown[(int)m]; }
    BossMove Last() const { return m_Last; }

    void DrawImGui();

private:
    float    m_Cooldown[(int)BossMove::Count] = {};
    BossMove m_Last = BossMove::None;
};
