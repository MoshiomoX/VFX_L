// ============================================================
// WeaponSystem.h
// 杖の自動発射。出力源（spells）ごとに独立して処理する。
//   分裂       = 同フレーム内で扇状に複数発（空間展開）
//   二重詠唱   = pendingCasts を残し、delayTimer 後に次を撃つ（時間展開）
// マナは杖で共有なので、出力源が多いと奪い合う。
// 弾は全部 GPU（SwarmSystem）に積む。CPU の Entity の弾は 2026-09-30 に削除した
// （GPU 化の後は到達しない経路だった）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "SpellID.h"
#include <functional>
#include <string>
#include <vector>
#include <SimpleMath.h>
#include "Swarm/SwarmTypes.h"
#include "Component/AreaStats.h"   // ActiveBeam::chainStats（連鎖の次の光線の元。2026-10-20）

class Registry;
class CollisionSystem;
struct SpellStats;
struct WandComponent;
struct ManaComponent;
class SwarmSystem;
class AreaVFXPlayer;
struct VFXContext;
class WeaponSystem
{
public:
    void Update(Registry& reg, float dt, const CollisionSystem& collision);
    void SetSwarm(SwarmSystem* swarm) { m_Swarm = swarm; }
    // 地面の高さ（x, z → y）。線で撃つ上級魔法（アイススパイク）の範囲を地面に合わせる。無ければ撃った所と着弾点の間を直線で
    std::function<float(float, float)> groundHeight;
    // 範囲攻撃の見た目の再生先。無ければ判定だけ出る
    void SetAreaVFX(AreaVFXPlayer* player, const VFXContext* ctx) { m_AreaVFX = player; m_AreaVFXCtx = ctx; }

    // ---- デバッグ可視化用：今フレームの照準情報 ----
    struct AimDebug
    {
        bool    hasTarget = false;
        bool    targetIsGpu = false;   // true = 雑魚（GPU）、false = エリート（CPU）
        DirectX::SimpleMath::Vector3 targetPos = { 0, 0, 0 };
        DirectX::SimpleMath::Vector3 muzzle = { 0, 0, 0 };
        DirectX::SimpleMath::Vector3 dir = { 0, 0, 1 };
        float   range = 0.0f;
    };
    const AimDebug& GetAimDebug() const { return m_AimDebug; }
    // 光球（水晶玉の召喚物）の数と位置（ImGui・自動テスト・陣営の描画用）
    int GetOrbCount() const { return (int)m_Orbs.size(); }
    bool GetOrbPos(int i, DirectX::SimpleMath::Vector3& pos) const
    {
        if (i < 0 || i >= (int)m_Orbs.size()) return false;
        pos = m_Orbs[i].pos;
        return true;
    }
    uint32_t GetOrbCasts() const { return m_OrbCasts; }   // 光球が撃った回数の累計（自動テスト用）
    // 誘発の累計（ImGui・自動テスト用）: 届いた「基本魔法の弾が消えた」数 / それで撃った上級魔法の回数
    // 今フレームに撃った弾（杖口の光の演出用、PlayerVFXSystem。2026-10-09）。
    // 誘発の隕石（着弾点へ落とす物）は入らない。分裂の扇は 1 発ずつ入る
    struct CastFx
    {
        ItemID id;
        DirectX::SimpleMath::Vector3 pos;   // 撃った位置（杖口・光球）
        DirectX::SimpleMath::Vector3 dir;
    };
    const std::vector<CastFx>& GetCastFx() const { return m_CastFx; }
    // 今フレームにプレイヤーの位置に出した「吹き飛ばす」範囲（突風。2026-10-12）：草を一斉に倒す用（GrassRenderer::AddGust）
    struct GustFx
    {
        DirectX::SimpleMath::Vector3 pos;   // 風の根元（前へ吹く物は詠唱者の位置、それ以外は範囲の中心）
        float radius;
        DirectX::SimpleMath::Vector2 dir;   // 吹く向き（xz の単位ベクトル）。length 0 = 向き無し（円盤）
        float length;                       // 根元から風下の端までの m
    };
    const std::vector<GustFx>& GetGustFx() const { return m_GustFx; }
    // 今フレームに線で撃った上級魔法（SpellStats::triggerLine。2026-10-10、アイススパイクのトゲの見た目用）。
    // start = 撃った所の足元（地面）、end = 弾が消えた所の lineOvershoot 先（地面）、scale = 拡大鏡などの大きさの倍率
    struct TriggeredFx
    {
        ItemID id;
        DirectX::SimpleMath::Vector3 start;
        DirectX::SimpleMath::Vector3 end;
        float scale = 1.0f;
    };
    const std::vector<TriggeredFx>& GetTriggeredFx() const { return m_TriggeredFx; }
    uint32_t GetTriggerEventsSeen() const { return m_TriggerEventsSeen; }
    uint32_t GetTriggeredCasts() const { return m_TriggeredCasts; }
    int GetActiveBeamCount() const { return (int)m_Beams.size(); }
    // 光線が標的へ向きを回す速さ（度/秒。2026-10-01 ユーザー指定 90）
    float beamTurnRate = 90.0f;
    // 自動テスト用: i 本目の光線の向き・標的が読めているか・標的の位置。無ければ false
    bool GetBeamDebug(int i, DirectX::SimpleMath::Vector3& dir, bool& hasTarget, DirectX::SimpleMath::Vector3& target) const
    {
        if (i < 0 || i >= (int)m_Beams.size()) return false;
        dir = m_Beams[i].dir; hasTarget = m_Beams[i].hasTarget; target = m_Beams[i].targetPos;
        return true;
    }
    // ---- 竜巻（動く範囲、AreaProfile::Kind::Roam。2026-10-22）の風源と向きの回り方 ----
    struct TwisterLook
    {
        float windRadiusMul = 2.2f;   // 風の届く距離 = 範囲の半径 × これ
        float windSwirl = 14.0f;      // m/s
        float windUpdraft = 5.0f;     // m/s（落ち葉が巻き上がる）
        float windInflow = 3.0f;      // m/s
        float turnRate = 90.0f;       // 前方の標的の方へ向きを回す速さ（度/秒）
        float seekCone = 60.0f;       // 標的がこの角度（度）より前にある時だけ向きを回す（後ろの敵へは戻らない）
        float keepAway = 4.0f;        // プレイヤーからこの距離（m）以内に入ったら外へ向ける（敵をプレイヤーへ引きずらない）
        // 出る / 消える移り変わり（2026-10-22、ユーザー「出る時と消える時が急すぎる」）。
        // 出た直後 introTime 秒で柱が足元から伸び上がり、風・煙・光・進む速さも 0 から付いてくる（引き寄せと威力は最初から）。
        // 寿命が尽きたら outroTime 秒かけて細くなりながら上へほどけて止まる（この間は引き寄せも威力も出さない）
        float introTime = 0.45f;
        float outroTime = 0.7f;
    };
    TwisterLook twisterLook;
    // 自動テスト用: 出ている竜巻の数と i 個目の位置（中心 = 地面）
    int GetTwisterCount() const { return (int)m_Twisters.size(); }
    bool GetTwisterPos(int i, DirectX::SimpleMath::Vector3& pos) const
    {
        if (i < 0 || i >= (int)m_Twisters.size()) return false;
        pos = m_Twisters[i].pos;
        return true;
    }
    // 自動テスト用: i 本目の光線の射程と半径（拡大鏡で伸びた後の値）
    bool GetBeamDebugSize(int i, float& length, float& radius) const
    {
        if (i < 0 || i >= (int)m_Beams.size()) return false;
        length = m_Beams[i].length; radius = m_Beams[i].radius;
        return true;
    }

private:
    // View 走査中に GPU へ積めないので、発射要求を溜めてから積む
    struct CastRequest
    {
        ItemID  id;
        int     profile;   // ProjectileProfileDB の番号（飛び方・VFX はここから）
        DirectX::SimpleMath::Vector3 muzzle;   // 撃つ位置。atPos なら着弾点
        DirectX::SimpleMath::Vector3 dir;
        float speed, radius, damage, lifetime;
        uint32_t triggerTag = 0;   // この弾が消えたら誘発できる上級魔法（SpellStats::triggerMask）
        bool     atPos = false;    // 誘発の隕石: muzzle に落とす
        float    areaDamageMul = 1.0f;     // 命中・着弾で出す範囲の威力（SpellStats::areaDamageMul = 魔法威力）
        float    areaDurationMul = 1.0f;   // 同じく持続（魔力解放中に撃った弾。持続する範囲だけ伸びる）
        int      chainCount = 0;           // 連鎖のルーン: 当たった後に別の敵へ跳ぶ回数（SpellStats::chainCount。2026-10-16）
    };

    // 1回の詠唱ぶんの発射要求を積む（分裂の扇状展開もここで行う）。
    // durationMul = 撃った時の魔力解放の持続倍率（ManaComponent::DurationMul）
    void QueueOneCast(const SpellStats& s,
        const DirectX::SimpleMath::Vector3& muzzle,
        const DirectX::SimpleMath::Vector3& dir, float durationMul);
    // 上級魔法 1 回分（誘発。impact に落とす。分裂は着弾点を輪に並べる）
    void QueueTriggeredCast(const SpellStats& s,
        const DirectX::SimpleMath::Vector3& impact,
        const DirectX::SimpleMath::Vector3& muzzle, float durationMul);
    // 線で撃つ上級魔法 1 本分（triggerLine）。origin の足元 → impact の先 の線に命中の範囲を並べて出す（弾は出さない）
    void QueueTriggeredLine(const SpellStats& s,
        const DirectX::SimpleMath::Vector3& impact,
        const DirectX::SimpleMath::Vector3& origin);
    std::vector<Swarm::TriggerEvent> m_TriggerEvents;   // 今フレームにリードバックで届いた誘発
    // CPU 側で起きた誘発（アークボルトの稲妻が届いた所。2026-10-15）。次のフレームの m_TriggerEvents に足す
    std::vector<Swarm::TriggerEvent> m_CpuTriggerEvents;
    uint32_t m_TriggerEventsSeen = 0;
    uint32_t m_TriggeredCasts = 0;

    // ---- 光線（上級の範囲魔法、AreaProfile::Kind::Beam。2026-09-30）----
    // 誘発された時に始まり、溜め → 光線（GPU のカプセル型範囲 1 個 + Beam entry のエフェクト）→ 終了。
    // 起点は毎フレーム杖先、終点は向き × 射程を地形で切った所。GPU には SwarmSystem::SetBeam で毎フレーム渡す
    struct ActiveBeam
    {
        uint32_t channel = 0;        // Swarm::BeamCB の添字（同時に kMaxBeams 本まで）
        DirectX::SimpleMath::Vector3 dir = { 0, 0, 1 };
        float charge = 0.0f;         // 残りの溜め（この間は判定が無い）
        float timeLeft = 0.0f;       // 光線の残り秒
        float length = 18.0f;
        float radius = 0.6f;
        float halfHeight = 1.2f;
        float damage = 0.0f;         // 1 tick
        float tickInterval = 0.1f;
        uint32_t flags = 0;          // kAreaCapsule | channel | (stun)
        uint32_t extra = 0;          // 範囲の tickNow の上位に乗せる追加値（毒素の割合。AreaProfile::Extra。2026-10-17）
        bool spawned = false;        // GPU の範囲を出したか（溜めが終わった時に 1 回）
        uint32_t vfxHandle = 0;      // AreaVFXPlayer のインスタンス
        std::string sound;           // 溜め終わり（撃った瞬間）の音 = 範囲の sound
        // 標的（2026-10-01）: 始めに seek（弾が消えた所）に一番近い敵を GPU で捕まえ、その敵へ向きを回す。
        // 死んだら射程内で向きに一番近い敵へ乗り換える（SwarmBeamTargetCS）。回し方は beamTurnRate 度/秒
        DirectX::SimpleMath::Vector3 seek = { 0, 0, 0 };
        uint32_t serial = 0;         // SwarmSystem::SetBeamTarget の通し番号
        bool targetStarted = false;  // 開始の依頼を出した（以後は追跡）
        bool hasTarget = false;      // 最後のフレームで標的が読めた（自動テストの記録用）
        DirectX::SimpleMath::Vector3 targetPos = { 0, 0, 0 };
        uint32_t orbSerial = 0;      // 光球から撃った光線（0 = 杖から）。光球が生きている間は起点がそれに追従する
        DirectX::SimpleMath::Vector3 lastStart = { 0, 0, 0 };   // 光球が消えた後の起点（最後の位置に留まる）
        // 連鎖（連鎖のルーン × アークボルト。2026-10-20）: 稲妻が届いた敵から近くの別の敵へ、もう一本の稲妻を走らせる。
        // 流れ: 捕まえた敵の slot が読めたら GPU に「その敵の近くで別の敵」を探させ（kBeamTargetChain、chainSerial で
        // 見分ける）、光線が終わった時に答えが来ていれば届いた敵の位置から次の光線（chainLeft − 1）を始める。
        // 答えがまだなら chainWaitMax 秒だけ待つ（判定も見た目も終わった後、次の稲妻が出るまでの間）
        int chainLeft = 0;                 // あと何回跳べるか（0 = 跳ばない）
        AreaStats chainStats;              // 次の光線の元（威力・太さ・持続はルーン込み）
        float chainCastSpeed = 1.0f;
        float chainDurationMul = 1.0f;
        std::string chainHitVfx;           // 届いた所の火花（AreaProfile::hitVfxFile）
        uint32_t chainTriggerMask = 0;     // 届いた所を「弾が消えた所」として上級魔法へ
        bool fixedStart = false;           // 起点を杖先ではなく startPos に固定（跳んだ稲妻は前の敵から出る）
        DirectX::SimpleMath::Vector3 startPos = { 0, 0, 0 };
        uint32_t lockedSlot = 0xFFFFFFFFu; // 捕まえた敵の slot（跳ぶ先から除外する）
        uint32_t chainVisited[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };   // この連鎖で前に届いた敵（A → B → A と戻らないように除外）
        uint32_t chainSerial = 0;          // 跳ぶ先の問い合わせの通し番号（0 = まだ出していない）
        DirectX::SimpleMath::Vector3 chainSeek = { 0, 0, 0 };   // 問い合わせの中心 = 捕まえた敵の位置
        int chainAnswer = -1;              // -1 待ち / 0 居ない / 1 居る（chainNext）/ 2 跳んだ
        DirectX::SimpleMath::Vector3 chainNext = { 0, 0, 0 };
        float chainWait = 0.0f;            // 光線が終わってから答えを待った秒
    };
    std::vector<ActiveBeam> m_Beams;
    uint32_t m_BeamSerial = 0;
    static constexpr float kChainWaitMax = 0.25f;   // リードバックは 2〜3 フレーム遅れる。これ以上待っても来なければ諦める
    // 直前に始めた光線（m_Beams.back()）に連鎖の設定を付ける（アークボルトを撃った所で呼ぶ）
    void SetupBeamChain(const AreaStats& a, float castSpeed, float durationMul);
    // 連鎖の次の稲妻：prev が届いた敵の位置から next へ（prev.chainLeft − 1 回残して）
    void StartChainBeam(const ActiveBeam& prev, const DirectX::SimpleMath::Vector3& next);

    // ---- 竜巻（上級の範囲魔法、AreaProfile::Kind::Roam。2026-10-22。WeaponSystemTwister.cpp）----
    // 誘発された所（渦の弾が消えた所・突風の吹いた所）に出て、プレイヤーから離れる向きに roamSpeed で進み、
    // 前方にいる敵の方へは少しずつ曲がる（寄ってくる群れを突っ切り、巻き込んだ敵をプレイヤーから引き離す）。
    // GPU の範囲は動かせないので、kTwisterPulse 秒ごとに今の位置へ短い範囲（その間だけ生きる）を出し直す：
    // 引き寄せ・減速は毎回、威力は tickInterval ごと（数字が出過ぎないように）。見た目（CPU の VFX）と風源は毎フレーム付いて動く
    struct ActiveTwister
    {
        ItemID id = ItemID::Unknown;
        int profile = 0;               // AreaProfileDB の番号
        DirectX::SimpleMath::Vector3 pos = { 0, 0, 0 };   // 地面の上の中心
        DirectX::SimpleMath::Vector3 dir = { 0, 0, 1 };   // 進む向き（xz）
        float speed = 3.0f;
        float age = 0.0f;
        float life = 4.0f;
        float radius = 2.8f;
        float damage = 0.0f;           // 1 回（tickInterval ごと）
        float tickInterval = 0.5f;
        float pulseTimer = 0.0f;       // 次の短い範囲まで
        bool  lifesteal = false;       // 吸血のルーン（2026-10-25）
        float damageTimer = 0.0f;      // 次に威力を乗せるまで
        float outro = -1.0f;           // < 0 = まだ消え始めていない、≥ 0 = 寿命が尽きて消え始めてからの秒
        uint32_t vfxHandle = 0;
    };
    std::vector<ActiveTwister> m_Twisters;
    static constexpr float kTwisterPulse = 0.25f;   // 短い範囲を出し直す間隔（= 引き寄せの 1 回の長さ）
    // 誘発で竜巻を出す（at = 地面の高さ + 標的の高さ、playerPos = 進む向きの基準）。durationMul = 魔力解放の持続倍率
    void StartTwister(const AreaStats& a, const DirectX::SimpleMath::Vector3& at,
        const DirectX::SimpleMath::Vector3& playerPos, float durationMul);
    // 進む・短い範囲を出す・見た目と風源を付けて動かす・終わったら止める
    void UpdateTwisters(float dt, const DirectX::SimpleMath::Vector3& playerPos,
        bool hasTarget, const DirectX::SimpleMath::Vector3& targetPos);

    // ---- 光球（水晶玉の召喚物、2026-10-06。WeaponSystemOrbs.cpp）----
    // WandComponent::orbs[unit] の設定で出る。貯蔵された魔法（SpellStats / AreaStats の storeUnit == unit）を
    // 光球ごとの計時で、光球の位置から撃つ。見た目は AreaVFXPlayer のインスタンス（貯蔵した魔法の色で染める）
    struct Orb
    {
        int unit = 0;                // 今の wand.orbs の添字（SyncOrbUnits が unitId / unitDup から毎フレーム引き直す）
        ItemID unitId = ItemID::Unknown;   // どの水晶玉の光球か（バックパックを組み替えても同じ水晶玉を指す）
        int unitDup = 0;
        uint32_t serial = 0;
        float age = 0.0f;
        float life = 8.0f;
        float phase = 0.0f;          // 出た時の向き（rad）。出る位置を決めるだけ（回らない）
        DirectX::SimpleMath::Vector3 pos = { 0, 0, 0 };
        uint32_t vfxHandle = 0;
        // 貯蔵された魔法ごとの計時。魔法は道具 ID と同じ道具の何個目か（SpellStats / AreaStats::dupIndex）で見分ける
        // （wand.spells / areas の添字は組み替えでずれるので使わない）
        struct Timer { ItemID id = ItemID::Unknown; int dup = 0; float castTimer = 0.0f; int pendingCasts = 0; float delayTimer = 0.0f; };
        struct AreaTimer { ItemID id = ItemID::Unknown; int dup = 0; float timer = 0.0f; };
        std::vector<Timer> spellTimers;
        std::vector<AreaTimer> areaTimers;
    };
    std::vector<Orb> m_Orbs;
    uint32_t m_OrbSerial = 0;
    uint32_t m_OrbCasts = 0;
    // 光球と水晶玉（wand.orbs）の対応を引き直す。水晶玉が無くなった / 何も貯蔵していない光球は消えるようにする
    void SyncOrbUnits(const WandComponent& wand);
    // 光球の出現・移動・消滅と、貯蔵された魔法の発射（Update の走査の中、範囲攻撃の後）。
    // allowPending = false なら連発の残りも撃たずに捨てる（死んだ時）
    void UpdateOrbs(float dt, float castDt, float castSpeed, float durationMul,
        const DirectX::SimpleMath::Vector3& playerPos, WandComponent& wand, ManaComponent& mana,
        bool allowNewCast, bool allowPending, bool ignoreCooldown, bool hasTarget,
        const DirectX::SimpleMath::Vector3& targetPos, const DirectX::SimpleMath::Vector3& targetVel);
    // 誘発されない範囲魔法を 1 回撃つ（杖と光球で共通：アークボルトの稲妻、突風などの範囲）。
    // origin = 撃つ所（杖先 / 光球）、caster = 足元に出す範囲の中心・前へ吹く根元（プレイヤー / 光球の真下）、
    // followCaster = プレイヤーに付いて動く範囲を許す（光球は false）、orbSerial = 光球の通し番号（杖は 0）。
    // 撃てたら true（MP の予約・クールダウンは呼ぶ側）
    bool CastArea(const AreaStats& a, const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& caster, bool followCaster,
        bool hasTarget, const DirectX::SimpleMath::Vector3& targetPos,
        float castSpeed, float durationMul, uint32_t orbSerial);
    // 光球の位置（生きていれば true）
    bool FindOrb(uint32_t serial, DirectX::SimpleMath::Vector3& pos) const;
    // 水晶玉 unit の生きている光球の先頭（貯蔵された上級魔法を誘発で撃つ時の起点）。無ければ false
    bool FirstOrbOf(int unit, DirectX::SimpleMath::Vector3& pos, uint32_t& serial) const;
    // 誘発で光線を始める（チャンネルが空いていなければ false = 撃たない）
    // castSpeed / durationMul = 撃った時の魔力解放（溜めを castSpeed 倍速く、光線を durationMul 倍長く。エフェクトの時間軸も合わせる）
    // orbSerial = 光球から撃つ時はその通し番号（起点が光球に追従する）
    // lengthOverride > 0 = 射程をこの長さまでに縮める（アークボルト：標的の体で止める。2026-10-15）
    bool StartBeam(const AreaStats& a, const DirectX::SimpleMath::Vector3& muzzle,
        const DirectX::SimpleMath::Vector3& impact, float castSpeed = 1.0f, float durationMul = 1.0f,
        uint32_t orbSerial = 0, float lengthOverride = 0.0f);
    // 溜め・判定の出現・起点 / 終点の更新・終了
    void UpdateBeams(float dt, const DirectX::SimpleMath::Vector3& muzzle, const CollisionSystem& collision);

    SwarmSystem* m_Swarm = nullptr;
    AreaVFXPlayer* m_AreaVFX = nullptr;
    const VFXContext* m_AreaVFXCtx = nullptr;
    std::vector<CastRequest> m_Requests;
    std::vector<CastFx> m_CastFx;   // 今フレームに撃った弾（GetCastFx）
    std::vector<GustFx> m_GustFx;   // 今フレームの吹き飛ばす範囲（GetGustFx）
    std::vector<TriggeredFx> m_TriggeredFx;   // 今フレームに撃った上級魔法（GetTriggeredFx）
    AimDebug m_AimDebug;   // 可視化用
};
