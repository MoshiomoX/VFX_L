// ============================================================
// EliteAttacks.cpp
// 面の固定エリートの土台：出す・リードバックで追う（槽の割り当て・死の検出）・GPU への指示・HUD の条。
// 型毎の技は EliteMoves.cpp、詞缀は EliteAffixes.cpp、調整パネルは EliteAttacksPanel.cpp
// ============================================================
#include "Enemy/Elite/EliteAttacks.h"
#include "Audio/AudioSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "Swarm/SwarmSystem.h"
#include "VFX_Editor/VFXId.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kSeenWait = 3.0f;   // 出してからこれだけ見つからなければ出せなかった（GPU の池が一杯）とみなす

    // HP の割合。とどめの一撃で HP（符号なし）が 0 を下回って折り返した値は 0 とみなす
    // （Boss と同じ扱い。StageDirector の m_BossHpRatio）。最大 HP が 0 なら noMax
    float HpRatioOf(const Swarm::EliteInfo& info, float noMax)
    {
        if (info.maxHp == 0) return noMax;
        if (info.hp > info.maxHp) return 0.0f;
        return (float)info.hp / (float)info.maxHp;
    }
}

const wchar_t* EliteTypeName(EliteType t)
{
    switch (t)
    {
    case EliteType::Axe:       return L"大斧の戦鬼";
    case EliteType::Archer:    return L"森の弓手";
    case EliteType::Rider:     return L"砂塵の騎兵";
    case EliteType::FlameMage: return L"炎砂の術師";
    case EliteType::Shade:     return L"影の剣士";
    case EliteType::Librarian: return L"呪いの司書";
    default:                   return L"エリート";
    }
}

const wchar_t* EliteAffixName(int bit)
{
    static const wchar_t* kNames[kEliteAffixCount] = { L"疾風", L"鉄壁", L"雷光", L"狂暴" };
    return (bit >= 0 && bit < kEliteAffixCount) ? kNames[bit] : L"";
}

void EliteAttacks::Init(AreaVFXPlayer* vfx, const VFXContext* ctx)
{
    m_Vfx = vfx;
    m_Ctx = ctx;

    // 型毎の初期値（2026-10-25。初値、実機で調整する）
    TypeTuning& axe = tune[(int)EliteType::Axe];
    axe = {};
    axe.hpMul = 1.25f; axe.speed = 3.4f; axe.scale = 2.1f; axe.trigger = 3.2f; axe.windup = 0.85f; axe.active = 0.45f;
    axe.recover = 0.7f; axe.cooldown = 3.8f; axe.damage = 30.0f; axe.radius = 4.2f; axe.count = 1;

    TypeTuning& arc = tune[(int)EliteType::Archer];
    arc = {};
    arc.hpMul = 0.85f; arc.speed = 3.1f; arc.scale = 1.75f; arc.trigger = 19.0f; arc.windup = 1.0f; arc.recover = 0.6f;
    arc.cooldown = 3.4f; arc.damage = 16.0f; arc.radius = 22.0f; arc.width = 1.0f; arc.speedMove = 28.0f; arc.count = 3;
    arc.spreadDeg = 22.0f; arc.keepMin = 9.0f; arc.keepMax = 14.0f;

    TypeTuning& rid = tune[(int)EliteType::Rider];
    rid = {};
    rid.hpMul = 1.1f; rid.speed = 3.6f; rid.scale = 2.0f; rid.trigger = 17.0f; rid.windup = 0.9f; rid.recover = 1.3f;
    rid.cooldown = 6.0f; rid.damage = 26.0f; rid.radius = 18.0f; rid.width = 2.4f; rid.speedMove = 15.0f; rid.count = 3;
    rid.extra = 0.55f;   // 2 回目からの溜め

    TypeTuning& fla = tune[(int)EliteType::FlameMage];
    fla = {};
    fla.hpMul = 0.95f; fla.speed = 3.0f; fla.scale = 1.8f; fla.trigger = 8.0f; fla.windup = 1.1f; fla.active = 1.6f;
    fla.recover = 0.8f; fla.cooldown = 5.0f; fla.damage = 9.0f; fla.radius = 9.5f; fla.width = 30.0f; fla.speedMove = 32.0f;
    fla.keepMin = 5.5f; fla.keepMax = 7.5f;   // keepMax < trigger（立ち位置で必ず届く）

    TypeTuning& sha = tune[(int)EliteType::Shade];
    sha = {};
    sha.hpMul = 0.95f; sha.speed = 3.6f; sha.scale = 1.85f; sha.trigger = 15.0f; sha.windup = 1.0f; sha.active = 0.3f;
    sha.recover = 0.9f; sha.cooldown = 5.0f; sha.damage = 28.0f; sha.radius = 2.6f; sha.count = 1; sha.extra = 2.2f;

    TypeTuning& lib = tune[(int)EliteType::Librarian];
    lib = {};
    lib.hpMul = 0.9f; lib.speed = 2.8f; lib.scale = 1.8f; lib.trigger = 18.0f; lib.windup = 1.0f; lib.recover = 0.7f;
    lib.cooldown = 6.0f; lib.damage = 22.0f; lib.speedMove = 4.2f; lib.count = 3; lib.keepMin = 9.0f; lib.keepMax = 13.0f;
}

void EliteAttacks::Configure(int stage)
{
    switch (stage)
    {
    case 2:  m_TypeA = EliteType::Rider; m_TypeB = EliteType::FlameMage; break;
    case 3:  m_TypeA = EliteType::Shade; m_TypeB = EliteType::Librarian; break;
    default: m_TypeA = EliteType::Axe;   m_TypeB = EliteType::Archer;    break;
    }
}

void EliteAttacks::StopFx(Elite& e)
{
    if (!m_Vfx) return;
    for (uint32_t* h : { &e.fxShield, &e.fxSwift, &e.fxAura, &e.fxMove })
    {
        if (*h) m_Vfx->StopInstance(*h);
        *h = 0;
    }
}

void EliteAttacks::Clear()
{
    for (Elite& e : m_Elites) StopFx(e);
    if (m_Vfx)
    {
        for (const Orb& o : m_Orbs) if (o.fx) m_Vfx->StopInstance(o.fx);
        for (const Arrow& a : m_Arrows) if (a.fx) m_Vfx->StopInstance(a.fx);
    }
    m_Elites.clear();
    m_Orbs.clear();
    m_Arrows.clear();
    m_Strikes.clear();
    m_Hits.clear();
    m_Deaths.clear();
    m_Warns.clear();
}

uint32_t EliteAttacks::PlayFx(int id, const Vector3& pos, float duration, float yawRad, float scale)
{
    if (!m_Vfx || !m_Ctx) return 0;
    const uint32_t h = m_Vfx->Play(VFXDatabase::GetFile((VFXId)id), pos, duration, false, *m_Ctx);
    if (h && yawRad != 0.0f) m_Vfx->RotateInstance(h, DirectX::XMConvertToDegrees(yawRad));
    if (h && scale != 1.0f) m_Vfx->ScaleInstance(h, scale);
    return h;
}

Vector3 EliteAttacks::Ground(const GridWorld& grid, const Vector3& p) const
{
    return Vector3(p.x, grid.SampleHeight(p.x, p.z), p.z);
}

float EliteAttacks::Cooldown(const Elite& e) const
{
    const float cd = tune[(int)e.type].cooldown;
    return e.enraged ? cd * enrageCooldownMul : cd;
}

float EliteAttacks::Damage(const Elite& e, const Input& in, float base) const
{
    (void)e;
    return base * in.damageMul;
}

// 足元が groundPos の地面から -1.5〜reachUp m、水平に radius（+ 体の半分）以内
bool EliteAttacks::PlayerNear(const Input& in, const Vector3& groundPos, float radius, float reachUp) const
{
    if (!in.playerAlive) return false;
    const float dx = in.player.x - groundPos.x, dz = in.player.z - groundPos.z;
    const float r = radius + in.playerRadius * 0.5f;
    if (dx * dx + dz * dz > r * r) return false;
    const float feet = in.playerFeetY - groundPos.y;
    return feet > -1.5f && feet < reachUp;
}

void EliteAttacks::Finish(Elite& e)
{
    e.phase = 0;
    e.t = 0.0f;
    e.cmd = Swarm::kEliteCmdAI;
    e.moveSpeed = 0.0f;
    e.hide = 0.0f;
    e.glow = 0.0f;
    if (e.fxMove && m_Vfx) m_Vfx->StopInstance(e.fxMove);
    e.fxMove = 0;
}

// ============================================================
// 出す
// ============================================================
bool EliteAttacks::Spawn(const GridWorld& grid, SwarmSystem& swarm, const Vector3& player,
    bool ranged, uint32_t affixes, float hp, float ringMin, float ringMax)
{
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        const float ang = u01(m_Rng) * 6.2831853f;
        const float r = ringMin + (ringMax - ringMin) * u01(m_Rng);
        const Vector3 p = player + Vector3(std::cos(ang) * r, 0.0f, std::sin(ang) * r);
        int gx = 0, gz = 0;
        grid.WorldToCell(p, gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;
        return SpawnAt(grid, swarm, p, ranged, affixes, hp);
    }
    return false;
}

bool EliteAttacks::SpawnAt(const GridWorld& grid, SwarmSystem& swarm, const Vector3& p, bool ranged, uint32_t affixes, float hp)
{
    // 同じフレームの KillAll の後に出した物は消さない（KillAll より前の物だけ消す）
    if (swarm.GetKillAllCount() != m_KillSerial)
    {
        m_KillSerial = swarm.GetKillAllCount();
        m_IgnoreReads = 4;
        Clear();
    }
    if (m_Elites.size() >= Swarm::kMaxEliteInfo) return false;   // GPU から様子が返る数まで
    const EliteType type = ranged ? m_TypeB : m_TypeA;
    const TypeTuning& tt = tune[(int)type];
    const float groundY = swarm.GetAIParams().groundY;
    int n = 0;
    for (int b = 0; b < kEliteAffixCount; ++b) if (affixes & (1u << b)) ++n;
    const float h = hp * tt.hpMul * (1.0f + affixHpMul * (float)n);
    const Vector3 ground = Ground(grid, p);
    swarm.SpawnEnemy({ ground.x, ground.y + groundY, ground.z }, h, tt.speed,
        ranged ? Swarm::kEnemyKindEliteB : Swarm::kEnemyKindEliteA);

    Elite e;
    e.type = type;
    e.ranged = ranged;
    e.affixes = affixes;
    e.spawnPos = ground;
    e.pos = ground;
    e.shieldUp = (affixes & kEliteAffixIron) != 0;
    e.cd = 2.5f;
    // 名前：「疾風・鉄壁の 大斧の戦鬼」（詞缀が無ければ型の名前だけ）
    std::wstring name;
    for (int b = 0; b < kEliteAffixCount; ++b)
        if (affixes & (1u << b))
        {
            if (!name.empty()) name += L"・";
            name += EliteAffixName(b);
        }
    if (!name.empty()) name += L"の ";
    name += EliteTypeName(type);
    wcsncpy_s(e.name, name.c_str(), _TRUNCATE);
    wcsncpy_s(m_LastName, name.c_str(), _TRUNCATE);
    m_Elites.push_back(e);

    PlayFx((int)VFXId::EliteSpawn, ground, 2.0f);
    AudioSystem::Get().Play("elite_spawn");
    std::cout << "[Elite] spawn type " << (int)type << " affixes " << affixes << " hp " << h << std::endl;
    return true;
}

// ============================================================
// リードバックで追う。新しい値が読めた時だけ：槽の決まっている物は位置・HP を更新、
// 見当たらなければ 2 回続けて居ない時に死んだとする。まだ槽の無い物は、同じ種類で誰のでもない一番近い物を貰う
// ============================================================
void EliteAttacks::Track(const SwarmSystem& swarm, float dt)
{
    if (swarm.GetKillAllCount() != m_KillSerial)
    {
        // KillAll（トレーニングの「敵を全部消す」・地形の作り直し）：倒されたのではないので宝箱は出さない
        m_KillSerial = swarm.GetKillAllCount();
        m_IgnoreReads = 4;
        Clear();
        return;
    }
    for (Elite& e : m_Elites) e.wait += dt;
    const uint32_t serial = swarm.GetEliteInfoSerial();
    if (serial == m_LastSerial) return;
    m_LastSerial = serial;
    if (m_IgnoreReads > 0) { --m_IgnoreReads; return; }   // 消す前の古い様子（消した敵がまだ居る）
    const Swarm::EliteInfoBlock& info = swarm.GetEliteInfo();
    const uint32_t n = (std::min)(info.count, Swarm::kMaxEliteInfo);

    bool used[Swarm::kMaxEliteInfo] = {};
    for (Elite& e : m_Elites)
    {
        if (!e.seen) continue;
        bool found = false;
        for (uint32_t k = 0; k < n; ++k)
            if (info.e[k].slot == e.slot)
            {
                used[k] = true;
                found = true;
                e.pos = Vector3(info.e[k].pos[0], info.e[k].pos[1], info.e[k].pos[2]);
                e.hpRatio = HpRatioOf(info.e[k], 0.0f);
                break;
            }
        e.missing = found ? 0 : e.missing + 1;
    }
    for (Elite& e : m_Elites)
    {
        if (e.seen) continue;
        const uint32_t kind = e.ranged ? Swarm::kEnemyKindEliteB : Swarm::kEnemyKindEliteA;
        int best = -1;
        float bestD = 36.0f;   // 6m 以内
        for (uint32_t k = 0; k < n; ++k)
        {
            if (used[k] || info.e[k].kind != kind) continue;
            const float dx = info.e[k].pos[0] - e.spawnPos.x, dz = info.e[k].pos[2] - e.spawnPos.z;
            const float d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; best = (int)k; }
        }
        if (best < 0) continue;
        used[best] = true;
        e.seen = true;
        e.slot = info.e[best].slot;
        e.missing = 0;
        e.pos = Vector3(info.e[best].pos[0], info.e[best].pos[1], info.e[best].pos[2]);
        e.hpRatio = HpRatioOf(info.e[best], 1.0f);
    }

    for (size_t i = 0; i < m_Elites.size();)
    {
        Elite& e = m_Elites[i];
        const bool dead = e.seen && e.missing >= 2;
        const bool lost = !e.seen && e.wait > kSeenWait;
        if (!dead && !lost) { ++i; continue; }
        if (dead)
        {
            m_Deaths.push_back({ e.pos, e.type });
            std::cout << "[Elite] defeated type " << (int)e.type << std::endl;
        }
        else
            std::cout << "[Elite] never appeared (type " << (int)e.type << ")" << std::endl;
        StopFx(e);
        m_Elites.erase(m_Elites.begin() + i);
    }
}

void EliteAttacks::Update(const Input& in, const GridWorld& grid, const SwarmSystem& swarm)
{
    m_Hits.clear();
    m_Deaths.clear();
    m_Warns.clear();
    Track(swarm, in.dt);
    for (Elite& e : m_Elites)
    {
        if (!e.seen) continue;
        UpdateAffixes(e, in, grid);
        UpdateMove(e, in, grid);
    }
    UpdateOrbs(in, grid);
    UpdateArrows(in, grid);
    UpdateStrikes(in, grid);
}

void EliteAttacks::DebugReady()
{
    for (Elite& e : m_Elites)
        if (e.phase == 0) e.cd = 0.0f;
}

// ============================================================
// GPU へ：面の体格と、見つかっている個体への指示（最大 kMaxEliteCmd 体）
// ============================================================
void EliteAttacks::WriteGpu(Swarm::BomberCB& cb) const
{
    cb.fixedEliteAScale = tune[(int)m_TypeA].scale;
    cb.fixedEliteBScale = tune[(int)m_TypeB].scale;
    cb.fixedEliteKeepMin = tune[(int)m_TypeB].keepMin;
    cb.fixedEliteKeepMax = tune[(int)m_TypeB].keepMax;
    memset(cb.eliteCmd, 0, sizeof(cb.eliteCmd));
    memset(cb.eliteMove, 0, sizeof(cb.eliteMove));
    uint32_t k = 0;
    for (const Elite& e : m_Elites)
    {
        if (!e.seen || k >= Swarm::kMaxEliteCmd) continue;
        cb.eliteCmd[k][0] = e.slot + 1u;
        cb.eliteCmd[k][1] = e.flags;
        cb.eliteCmd[k][2] = e.cmd;
        cb.eliteCmd[k][3] = (uint32_t)(std::clamp(e.glow, 0.0f, 1.0f) * 1000.0f);
        const bool tp = (e.cmd == Swarm::kEliteCmdTeleport);
        cb.eliteMove[k][0] = tp ? e.target.x : e.moveDir.x;
        cb.eliteMove[k][1] = tp ? e.target.z : e.moveDir.y;
        cb.eliteMove[k][2] = e.moveSpeed;
        cb.eliteMove[k][3] = e.hide;
        ++k;
    }
}

void EliteAttacks::GetBars(std::vector<Bar>& out) const
{
    out.clear();
    for (const Elite& e : m_Elites)
    {
        if (!e.seen) continue;
        Bar b;
        wcsncpy_s(b.name, e.name, _TRUNCATE);
        b.hp = e.hpRatio;
        b.shield = e.shieldUp;
        b.enraged = e.enraged;
        out.push_back(b);
    }
}

void EliteAttacks::GetPositions(std::vector<Vector3>& out) const
{
    out.clear();
    for (const Elite& e : m_Elites)
        if (e.seen && e.hide < 0.5f) out.push_back(e.pos);
}
