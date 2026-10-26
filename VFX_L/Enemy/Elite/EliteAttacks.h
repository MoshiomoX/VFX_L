// ============================================================
// EliteAttacks.h
// 面の固定エリート（2026-10-25、ユーザー「エリートを区別化」→ 各面 2 体、どれも「予告を避ける」型）。
// 体は GPU の雑魚（kEnemyKindEliteA / EliteB）。技の拍子・予告・当たりは CPU がここで決め、
// BomberCB::eliteCmd / eliteMove で GPU の体に「立つ・決めた向きへ走る・瞬間移動・隠れる」を伝える
// （位置・HP は SwarmSystem::GetEliteInfo のリードバック。2〜3 フレーム遅れる）。
//   草原 A「大斧の戦鬼」：近付くと足元に輪 → 回転斬り（輪の外へ走る）
//   草原 B「森の弓手」  ：距離を取り、扇に 3 本の帯 → 矢が帯に沿って飛ぶ（横へ避ける）
//   砂漠 A「砂塵の騎兵」：向きを決めて帯 → 一直線の突進を 3 回続けて（毎回狙い直す。横へ避ける）
//   砂漠 B「炎砂の術師」：扇の予告 → 炎の息（ゆっくり追ってくる扇。横へ回り込む）
//   遺跡 A「影の剣士」  ：消える → プレイヤーの背後に輪 → 現れて斬る（輪の外へ）
//   遺跡 B「呪いの司書」：暗い紫の玉を 3 つ放つ。ゆっくり追い、触れると爆ぜる（走って振り切る）
// 詞缀（3:00 は 0 個、6:00 は 1 個、8:00 は 2 個）：
//   疾風 = 歩く速さ × 1.6 + 残像 / 鉄壁 = 盾（割れるまで受けるダメージ半分。HP が shieldBreakRatio を切ると割れる）/
//   雷光 = thunderInterval 秒毎に周りへ 5 本の雷（先に地面に印）/ 狂暴 = HP 半分以下で大きく・速く・赤く、技の間隔が短く
// 倒すと死んだ所に金色の宝箱（シーンが RewardCrateSystem::SpawnFree）。経験値は × fixedEliteExpMul
// 見た目（予告の帯・扇・輪、エフェクト、音）はここが AreaVFXPlayer / AudioSystem へ直接出す。
// 被弾（無敵時間・シールド・ノックバック）はシーン（CollisionTestSceneElites.cpp）。調整パネルは EliteAttacksPanel.cpp
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include <SimpleMath.h>
#include <cstdint>
#include <random>
#include <vector>

class GridWorld;
class SwarmSystem;
class AreaVFXPlayer;
struct VFXContext;

enum class EliteType : uint8_t { Axe, Archer, Rider, FlameMage, Shade, Librarian, Count };

// 詞缀（組み合わせる）
constexpr uint32_t kEliteAffixSwift = 1u;     // 疾風
constexpr uint32_t kEliteAffixIron = 2u;      // 鉄壁
constexpr uint32_t kEliteAffixThunder = 4u;   // 雷光
constexpr uint32_t kEliteAffixBerserk = 8u;   // 狂暴
constexpr int kEliteAffixCount = 4;

class EliteAttacks
{
public:
    // 当たり（シーンが被弾・ノックバックを出す）。from = 吹き飛ばす向きの基点
    struct Hit
    {
        DirectX::SimpleMath::Vector3 from;
        float damage = 0.0f;
        int   knock = 0;   // 0 = 殴り、1 = 爆発、2 = トゲ（PlayerControlSystem::KnockKind の順）
    };
    // 地面の予告（シーンが SwarmSystem::WarnCircle へ写す）
    struct Warn
    {
        DirectX::SimpleMath::Vector3 center;
        float radius = 1.0f;
        float progress = 0.0f;   // 負 = 縁だけ
        float yaw = 0.0f;
        float shape = 0.0f;      // > 0 扇の半角、< 0 帯の幅（WarnCircle と同じ）
    };
    struct Input
    {
        float dt = 0.0f;
        DirectX::SimpleMath::Vector3 player;   // カプセルの中心
        float playerFeetY = 0.0f;
        float playerYaw = 0.0f;                // 向き（ラジアン、+Z が 0）
        float playerRadius = 0.4f;
        bool  playerAlive = true;
        float damageMul = 1.0f;                // MobSpawner の今のダメージ倍率
    };
    // HUD の HP 条
    struct Bar
    {
        wchar_t name[64] = {};
        float hp = 1.0f;
        bool  shield = false;
        bool  enraged = false;
    };
    struct Death
    {
        DirectX::SimpleMath::Vector3 pos;   // 地面の高さ
        EliteType type = EliteType::Axe;
    };

    void Init(AreaVFXPlayer* vfx, const VFXContext* ctx);
    // 面（1..3）で A / B の個体が決まる。体格・距離を BomberCB へ入れるのは WriteGpu
    void Configure(int stage);
    // 全部消す（KillAll・面の切替）。続いているエフェクトも止める
    void Clear();

    // プレイヤーの周り ringMin〜ringMax の歩ける所に出す。hp = 今の難度の HP（型と詞缀の倍率はここで掛ける）。出せたら true
    bool Spawn(const GridWorld& grid, SwarmSystem& swarm, const DirectX::SimpleMath::Vector3& player,
        bool ranged, uint32_t affixes, float hp, float ringMin, float ringMax);
    // 決めた所（地面の上）に出す（自動テスト・パネル）
    bool SpawnAt(const GridWorld& grid, SwarmSystem& swarm, const DirectX::SimpleMath::Vector3& pos, bool ranged, uint32_t affixes, float hp);
    // 最後に出した個体の名前（「疾風・鉄壁の 大斧の戦鬼」。案内用）
    const wchar_t* LastSpawnName() const { return m_LastName; }

    void Update(const Input& in, const GridWorld& grid, const SwarmSystem& swarm);
    // この Update の結果（次の Update で消える）
    const std::vector<Hit>& Hits() const { return m_Hits; }
    const std::vector<Death>& Deaths() const { return m_Deaths; }
    const std::vector<Warn>& Warns() const { return m_Warns; }
    void GetBars(std::vector<Bar>& out) const;
    void GetPositions(std::vector<DirectX::SimpleMath::Vector3>& out) const;   // 目印（見つかっている物だけ）
    int  Alive() const { return (int)m_Elites.size(); }
    int  ActiveShots() const { return (int)(m_Orbs.size() + m_Arrows.size() + m_Strikes.size()); }   // 飛んでいる玉・矢・落ちる前の雷

    // GPU の体への指示と面毎の体格（毎フレーム。BomberCB の fixedElite* / eliteCmd / eliteMove）
    void WriteGpu(Swarm::BomberCB& cb) const;

    // Enemies パネルの「Elites」（EliteAttacksPanel.cpp）。spawn = 出す依頼（-1 = 無し、0 = A、1 = B）
    void DrawImGui(int& spawn, uint32_t& spawnAffixes);

    // ---- 自動テスト用の数え（TEMP-TEST）----
    struct Stats { int moves = 0; int strikes = 0; int orbs = 0; int arrows = 0; int shieldBreaks = 0; int enrages = 0; int teleports = 0; };
    const Stats& GetStats() const { return m_Stats; }
    // 自動テスト用：今の技を飛ばして次の Update で技を始めさせる（冷却 0）
    void DebugReady();
    // 調べる用：今の段（0 = 普段）
    int DebugPhase(int i) const { return (i >= 0 && i < (int)m_Elites.size()) ? m_Elites[i].phase : -1; }
    EliteType TypeA() const { return m_TypeA; }
    EliteType TypeB() const { return m_TypeB; }

    // ---- 調整値（型毎）----
    struct TypeTuning
    {
        float hpMul = 1.0f;       // 基準 HP（StageDirector::eliteHp × 難度）に掛ける
        float speed = 3.2f;       // 歩く速さ m/s
        float scale = 2.0f;       // 体格（BomberCB の fixedEliteAScale / BScale へ。同じ面の A / B）
        float trigger = 6.0f;     // プレイヤーがこれより近いと技を始める（m）
        float windup = 1.0f;      // 溜め（予告が育つ秒）
        float active = 0.5f;      // 技の最中の秒（息・回転）
        float recover = 0.8f;     // 技の後に立ち止まる秒
        float cooldown = 4.5f;    // 次の技まで（溜めの始めから）
        float damage = 25.0f;     // 難度の倍率 1 の時の 1 回の当たり
        float radius = 4.0f;      // 輪の半径 / 扇の長さ / 帯の長さ（m）
        float width = 1.4f;       // 帯の幅 / 扇の半角（度）
        float speedMove = 15.0f;  // 突進・矢・玉の速さ
        int   count = 3;          // 矢の本数・突進の回数・玉の数
        float spreadDeg = 0.0f;   // 矢の帯の間の角度
        float extra = 0.0f;       // 型毎（影の剣士：背後のどれだけ後ろに出るか m）
        float keepMin = 0.0f;     // B（遠距離型）が立つ距離（BomberCB の fixedEliteKeepMin〜Max）
        float keepMax = 0.0f;
    };
    TypeTuning tune[(int)EliteType::Count];

    // ---- 詞缀 ----
    float affixHpMul = 0.25f;          // 詞缀 1 個毎に HP + これ
    float shieldBreakRatio = 0.6f;     // 鉄壁：HP の割合がこれを切ったら盾が割れる（半分で受けるので実際は 0.8 倍の削りで割れる）
    float thunderInterval = 4.0f;      // 雷光：秒毎
    int   thunderCount = 5;
    float thunderWarn = 1.0f;          // 印が出てから落ちるまで
    float thunderRadius = 1.4f;
    float thunderSpread = 6.0f;        // エリートの周りこの半径に落ちる（1 本はプレイヤーの足元）
    float thunderDamage = 16.0f;
    float enrageRatio = 0.5f;          // 狂暴：HP の割合がこれを切ったら
    float enrageCooldownMul = 0.65f;   // 狂暴の間の技の間隔

    // ---- 呪いの司書の玉 ----
    float orbLife = 6.0f;
    float orbTurnDeg = 110.0f;   // 追う時の曲がり（度 / 秒）
    float orbHitRadius = 1.0f;
    float orbHeight = 1.3f;      // 地面からの高さ

private:
    struct Elite
    {
        EliteType type = EliteType::Axe;
        bool     ranged = false;
        uint32_t affixes = 0;
        wchar_t  name[64] = {};
        // GPU の槽（リードバックで見つかるまで UINT32_MAX）
        uint32_t slot = UINT32_MAX;
        bool     seen = false;
        float    wait = 0.0f;       // 出してから見つかるまで / 見えなくなってから
        int      missing = 0;       // 新しいリードバックで見当たらなかった回数
        DirectX::SimpleMath::Vector3 spawnPos;
        DirectX::SimpleMath::Vector3 pos;   // 体の位置（リードバック）
        float    hpRatio = 1.0f;
        // 技
        int      phase = 0;         // 0 = 普段（AI）、1 = 溜め、2 = 技の最中、3 = 後、4〜 = 型毎の段
        float    t = 0.0f;
        float    cd = 2.5f;         // 次の技まで
        int      count = 0;         // 残りの突進・回転の数
        DirectX::SimpleMath::Vector2 dir = { 0.0f, 1.0f };
        DirectX::SimpleMath::Vector3 target;   // 輪の中心（地面）・瞬間移動の行き先
        DirectX::SimpleMath::Vector3 dashFrom;
        float    dashLen = 0.0f;
        float    tickT = 0.0f;
        bool     hitDone = false;
        // GPU への指示
        uint32_t cmd = Swarm::kEliteCmdAI;
        uint32_t flags = 0;
        DirectX::SimpleMath::Vector2 moveDir = { 0.0f, 1.0f };
        float    moveSpeed = 0.0f;
        float    hide = 0.0f;
        float    glow = 0.0f;
        // 詞缀の様子
        bool     shieldUp = false;
        bool     enraged = false;
        float    thunderT = 0.0f;
        // 続くエフェクト（AreaVFXPlayer の番号）
        uint32_t fxShield = 0, fxSwift = 0, fxAura = 0, fxMove = 0;
        float    fxMoveYaw = 0.0f;   // fxMove を回した角度（度。RotateInstance は足し込み）
    };
    struct Orb
    {
        DirectX::SimpleMath::Vector3 pos;
        DirectX::SimpleMath::Vector2 dir;
        float life = 0.0f;
        float speed = 4.0f;
        float damage = 0.0f;
        uint32_t fx = 0;
    };
    struct Arrow
    {
        DirectX::SimpleMath::Vector3 pos;
        DirectX::SimpleMath::Vector2 dir;
        float left = 0.0f;   // 飛べる残りの距離
        float speed = 25.0f;
        float damage = 0.0f;
        uint32_t fx = 0;
    };
    struct Strike
    {
        DirectX::SimpleMath::Vector3 pos;   // 地面
        float t = 0.0f;
        float damage = 0.0f;
    };

    // EliteMoves.cpp（型毎の技）
    void UpdateMove(Elite& e, const Input& in, const GridWorld& grid);
    void MoveAxe(Elite& e, const Input& in, const GridWorld& grid);
    void MoveArcher(Elite& e, const Input& in, const GridWorld& grid);
    void MoveRider(Elite& e, const Input& in, const GridWorld& grid);
    void MoveFlame(Elite& e, const Input& in, const GridWorld& grid);
    void MoveShade(Elite& e, const Input& in, const GridWorld& grid);
    void MoveLibrarian(Elite& e, const Input& in, const GridWorld& grid);
    void UpdateOrbs(const Input& in, const GridWorld& grid);
    void UpdateArrows(const Input& in, const GridWorld& grid);
    // EliteAffixes.cpp
    void UpdateAffixes(Elite& e, const Input& in, const GridWorld& grid);
    void UpdateStrikes(const Input& in, const GridWorld& grid);
    void StopFx(Elite& e);

    void Track(const SwarmSystem& swarm, float dt);
    void Finish(Elite& e);                          // 技を終えて普段へ
    float Cooldown(const Elite& e) const;
    float Damage(const Elite& e, const Input& in, float base) const;
    bool  PlayerNear(const Input& in, const DirectX::SimpleMath::Vector3& groundPos, float radius, float reachUp) const;
    DirectX::SimpleMath::Vector3 Ground(const GridWorld& grid, const DirectX::SimpleMath::Vector3& p) const;
    uint32_t PlayFx(int id, const DirectX::SimpleMath::Vector3& pos, float duration, float yawRad = 0.0f, float scale = 1.0f);
    void AddHit(const DirectX::SimpleMath::Vector3& from, float damage, int knock) { m_Hits.push_back({ from, damage, knock }); }

    AreaVFXPlayer*    m_Vfx = nullptr;
    const VFXContext* m_Ctx = nullptr;
    EliteType m_TypeA = EliteType::Axe;
    EliteType m_TypeB = EliteType::Archer;
    std::vector<Elite>  m_Elites;
    std::vector<Orb>    m_Orbs;
    std::vector<Arrow>  m_Arrows;
    std::vector<Strike> m_Strikes;
    std::vector<Hit>    m_Hits;
    std::vector<Death>  m_Deaths;
    std::vector<Warn>   m_Warns;
    uint32_t m_LastSerial = 0;
    uint32_t m_KillSerial = 0;   // SwarmSystem::GetKillAllCount を最後に見た値（変わったら全部消された）
    int      m_IgnoreReads = 0;  // 全部消された後、古いリードバック（staging の輪の分）を捨てる回数
    wchar_t  m_LastName[64] = {};
    std::mt19937 m_Rng{ 2510u };
    Stats m_Stats;
};

// 型の名前（日本語）と詞缀の名前
const wchar_t* EliteTypeName(EliteType t);
const wchar_t* EliteAffixName(int bit);   // 0..3
