// ============================================================
// WeaponSystem.cpp
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/WandComponent.h"
#include "Component/ManaComponent.h"
#include "Collider/CollisionSystem.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/AreaVFXPlayer.h"
#include "Audio/AudioSystem.h"
#include "VFX_Editor/EntryType.h"
#include "Item/ItemDatabase.h"
#include "ECS/View.h"
#include <algorithm>
#include <cmath>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Matrix;

namespace
{
    // 単位ベクトル from を to へ最大 maxRad だけ回す（光線が標的へ向きを変える。瞬間には向けない）
    Vector3 RotateToward(const Vector3& from, const Vector3& to, float maxRad)
    {
        const float c = (std::max)(-1.0f, (std::min)(1.0f, from.Dot(to)));
        const float angle = std::acos(c);
        if (angle <= maxRad || angle < 1e-5f) return to;
        Vector3 axis = from.Cross(to);
        if (axis.LengthSquared() < 1e-10f) axis = Vector3::Up;   // 真後ろ: 水平に回す
        axis.Normalize();
        Vector3 r = Vector3::Transform(from, DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(axis, maxRad));
        r.Normalize();
        return r;
    }
}

// ============================================================
// 1回の詠唱ぶんの発射要求を積む（分裂の扇状展開はここ）
// ============================================================
void WeaponSystem::QueueOneCast(const SpellStats& s,
    const Vector3& muzzle, const Vector3& dir, float durationMul)
{
    int count = (std::max)(1, s.projectileCount);
    auto push = [&](const Vector3& d)
        {
            CastRequest req = { s.id, s.profile, muzzle, d,
                s.speed, s.radius, s.damage, s.lifetime, s.triggerMask };
            req.areaDamageMul = s.areaDamageMul;
            req.areaDurationMul = durationMul;
            m_Requests.push_back(req);
        };

    if (count == 1 || s.spreadAngle <= 0.0f)
    {
        // 単発: そのまま積む
        push(dir);
        return;
    }

    // 分裂: spreadAngle を count 等分し、Y 軸まわりに扇状へ広げる
    //  例) count=3, spread=30 → -15°, 0°, +15°
    float step = s.spreadAngle / (float)(count - 1);
    float start = -s.spreadAngle * 0.5f;

    for (int i = 0; i < count; ++i)
    {
        float deg = start + step * (float)i;
        Matrix rot = Matrix::CreateRotationY(DirectX::XMConvertToRadians(deg));
        Vector3 d = Vector3::TransformNormal(dir, rot);
        d.Normalize();

        push(d);
    }
}

// ============================================================
// 上級魔法 1 回分（誘発）: 基本魔法の弾が消えた場所 impact に落とす。
// 分裂（projectileCount > 1）は着弾点を impact の周りの輪に並べる（扇に開くと同じ所に重なるだけなので）
// ============================================================
void WeaponSystem::QueueTriggeredCast(const SpellStats& s,
    const Vector3& impact, const Vector3& muzzle, float durationMul)
{
    constexpr float kSplitRing = 2.0f;   // 分裂した時の輪の半径（m）。爆発（半径 3）が少し重なる程度

    // 向きは Drop 型の予備（着弾点が決まっているので実際には使われない）
    Vector3 dir = impact - muzzle;
    dir.y = 0.0f;
    if (dir.LengthSquared() < 1e-6f) dir = Vector3(0, 0, 1);
    dir.Normalize();

    const int count = (std::max)(1, s.projectileCount);
    for (int i = 0; i < count; ++i)
    {
        Vector3 p = impact;
        if (count > 1)
        {
            const float a = DirectX::XM_2PI * (float)i / (float)count;
            p += Vector3(std::cos(a), 0.0f, std::sin(a)) * kSplitRing;
        }
        CastRequest req = { s.id, s.profile, p, dir,
            s.speed, s.radius, s.damage, s.lifetime, s.triggerMask };
        req.atPos = true;
        req.areaDamageMul = s.areaDamageMul;
        req.areaDurationMul = durationMul;
        m_Requests.push_back(req);
    }
}

// ============================================================
// Update
// ※マナは書かない。CanAfford で確認して Reserve で予約するだけ。
//   引き落としと回復は ManaSystem がこの後で行う。
// ============================================================
void WeaponSystem::Update(Registry& reg, float dt, const CollisionSystem& collision)
{
    m_Requests.clear();

    // 基本魔法の弾が消えた場所（リードバックなので 2〜3 フレーム古い）。上級魔法はここでだけ撃つ
    m_TriggerEvents.clear();
    if (m_Swarm) m_Swarm->ConsumeTriggerEvents(m_TriggerEvents);
    m_TriggerEventsSeen += (uint32_t)m_TriggerEvents.size();
    // 環に入るのは地面の高さ。普段の隕石の標的（雑魚の位置 = 地面 + groundY）と同じ高さへ上げる
    const float triggerLift = m_Swarm ? m_Swarm->GetAIParams().groundY : 0.9f;

    reg.CreateView<TransformComponent, WandComponent, ManaComponent>()
        .EachFrom<WandComponent>([&](Entity e, TransformComponent& tf, WandComponent& wand, ManaComponent& mana)
            {
                // ---- 詠唱アニメ用のタイマーを進める ----
                if (wand.castAnimTimer > 0.0f)
                    wand.castAnimTimer -= dt;

                // ---- 魔力解放（2026-10-02）----
                // 詠唱: 発動間隔・上級魔法のクールダウン・連発の間の計時を castSpeed 倍で進める
                //   （始まった時にクールダウン中だった魔法もすぐ速くなり、終われば残りは普通の速さに戻る。HUD のクールダウン表示ともずれない）
                // 持続: 解放中に撃った魔法の持続する範囲（毒の池・光線）を durationMul 倍に。撃った時に決まる
                const float castSpeed = mana.CastSpeed();
                const float durationMul = mana.DurationMul();
                const float castDt = dt * castSpeed;

                Vector3 muzzle = tf.position + wand.muzzleOffset;

                // ============================================================
                // 索敵は1回だけ（全ての出力源が同じ目標を向く）
                // CPU（エリート）と GPU（雑魚）の両方から最寄りを取り、近い方を採用する。
                // GPU 側はリードバックなので 1〜2 フレーム古い。速度を持っているので
                // 弾速に応じた先読み量で補正する（下の aimFor）
                // ============================================================
                bool    hasTarget = false;
                bool    targetIsGpu = false;
                Vector3 targetPos = muzzle;
                Vector3 targetVel = Vector3::Zero;
                float   bestDist = wand.range;

                // --- CPU: エリート ---
                Entity cpuTarget = 0;
                if (collision.FindNearestEntity(muzzle, wand.range, Layer_Enemy, cpuTarget))
                {
                    Vector3 tp = reg.Get<TransformComponent>(cpuTarget).position;
                    if (reg.Has<ColliderComponent>(cpuTarget))
                        tp += reg.Get<ColliderComponent>(cpuTarget).offset;

                    const float d = (tp - muzzle).Length();
                    if (d < bestDist)
                    {
                        bestDist = d;
                        targetPos = tp;
                        hasTarget = true;
                    }
                }

                // --- GPU: 雑魚 ---
                if (m_Swarm)
                {
                    Vector3 gp, gv;
                    float   gd;
                    if (m_Swarm->GetNearestEnemy(gp, gv, gd))
                    {
                        // リードバックの距離はプレイヤー中心基準。杖先基準で測り直す
                        const float d = (gp - muzzle).Length();
                        if (d < bestDist)
                        {
                            bestDist = d;
                            targetPos = gp;
                            targetVel = gv;
                            targetIsGpu = true;
                            hasTarget = true;
                        }
                    }
                }

                // 弾速ごとの照準方向。目標が動いていれば到達時刻ぶん先を狙う
                // （1回の反復で十分。雑魚の速度は弾速の 1/5 程度）
                auto aimFor = [&](float projSpeed) -> Vector3
                    {
                        Vector3 aimPos = targetPos;
                        if (projSpeed > 0.01f)
                        {
                            const float t = (targetPos - muzzle).Length() / projSpeed;
                            aimPos += targetVel * t;
                        }
                        Vector3 dir = aimPos - muzzle;
                        if (dir.LengthSquared() < 1e-6f) return Vector3(0, 0, 1);
                        dir.Normalize();
                        return dir;
                    };

                // 可視化用は先読み量なしの素の方向
                const Vector3 aimDir = hasTarget ? aimFor(0.0f) : Vector3(0, 0, 1);

                m_AimDebug.hasTarget = hasTarget;
                m_AimDebug.targetIsGpu = targetIsGpu;
                m_AimDebug.targetPos = targetPos;
                m_AimDebug.muzzle = muzzle;
                m_AimDebug.dir = aimDir;
                m_AimDebug.range = wand.range;

                // ---- 発射の許可をモードで決める ----
                // ※pendingCasts はモードに関係なく消化する。
                //   一度始めた連発は最後まで撃ち切る（1回の詠唱 = 1 combo）。
                bool allowNewCast = true;
                bool ignoreCooldown = false;

                switch (wand.castMode)
                {
                case CastMode::Auto:
                    allowNewCast = true;
                    break;
                case CastMode::Manual:
                    allowNewCast = wand.castRequested;
                    break;
                case CastMode::DebugBurst:
                    allowNewCast = true;
                    ignoreCooldown = true;
                    break;
                }

                // プレイヤーが詠唱を止めている（Q / パッド Y）。
                // 飛行物・範囲とも新しい詠唱をしない。連発の残りは上の方針どおり撃ち切る
                if (wand.castingPaused)
                    allowNewCast = false;

                // ---- 出力源ごとに独立して処理する ----
                for (auto& s : wand.spells)
                {
                    // 水晶玉に貯蔵された基本魔法は光球が撃つ（UpdateOrbs）。貯蔵された上級魔法は下の「誘発」で光球の位置から
                    if (s.storeUnit >= 0 && !s.triggered) continue;

                    // === 連発の続き（二重詠唱の残り）===
                    if (s.pendingCasts > 0)
                    {
                        s.delayTimer -= castDt;
                        if (s.delayTimer <= 0.0f)
                        {
                            if (s.triggered)
                            {
                                // 上級魔法の連発は最初と同じ場所へ
                                if (mana.CanAfford(s.manaCost))
                                {
                                    QueueTriggeredCast(s, s.triggerPos, muzzle, durationMul);
                                    mana.Reserve(s.manaCost);
                                }
                            }
                            else if (hasTarget && mana.CanAfford(s.manaCost))
                            {
                                QueueOneCast(s, muzzle, aimFor(s.speed), durationMul);
                                mana.Reserve(s.manaCost);
                                wand.castAnimTimer = wand.castAnimDuration;
                            }
                            --s.pendingCasts;
                            s.delayTimer = s.castDelay;
                        }
                        continue;   // 連発中は新しい詠唱を始めない
                    }

                    // === 新しい詠唱 ===
                    // ※castTimer は撃てなくても減らし続ける。
                    //   標的が現れた瞬間に撃てるようにするため。
                    s.castTimer -= castDt;
                    if (s.triggered) continue;   // 上級魔法は下の「誘発」でだけ撃つ
                    if (!ignoreCooldown && s.castTimer > 0.0f) continue;
                    if (!allowNewCast) continue;
                    if (!hasTarget) continue;
                    if (!mana.CanAfford(s.manaCost)) continue;

                    QueueOneCast(s, muzzle, aimFor(s.speed), durationMul);
                    mana.Reserve(s.manaCost);
                    wand.castAnimTimer = wand.castAnimDuration;

                    // 二重詠唱: 残りの回数を pending として積んでおく
                    s.pendingCasts = (std::max)(0, s.castCount - 1);
                    s.delayTimer = s.castDelay;
                    s.castTimer = s.castInterval;
                }

                // ============================================================
                // 誘発: 基本魔法の弾が消えた場所で上級魔法を撃つ（火球・石弾 → 隕石）。
                // 上級魔法は自分のクールダウンと MP を持ち、クールダウンが明けてから最初に届いた場所で 1 回撃つ。
                // 標的は要らない（場所が決まっている）
                // ============================================================
                for (const auto& ev : m_TriggerEvents)
                {
                    // ---- bit 16〜31: 上級の範囲魔法（光線）。弾が消えた方向へ手から撃つ ----
                    for (uint32_t j = 0; j < (uint32_t)wand.areas.size() && j < 16; ++j)
                    {
                        if ((ev.tag & (1u << (16 + j))) == 0) continue;
                        auto& a = wand.areas[j];
                        if (!a.triggered) continue;
                        if (!ignoreCooldown && a.castTimer > 0.0f) continue;
                        if (!allowNewCast) continue;
                        if (!mana.CanAfford(a.manaCost)) continue;
                        // 貯蔵された光線は光球から（光球が出ていなければ撃たない）
                        Vector3 origin = muzzle;
                        uint32_t orbSerial = 0;
                        if (a.storeUnit >= 0 && !FirstOrbOf(a.storeUnit, origin, orbSerial)) continue;

                        Vector3 impact = ev.position;
                        impact.y += triggerLift;
                        if (!StartBeam(a, origin, impact, castSpeed, durationMul, orbSerial)) continue;   // チャンネルが全部埋まっている
                        mana.Reserve(a.manaCost);
                        wand.castAnimTimer = wand.castAnimDuration;
                        ++m_TriggeredCasts;
                        a.castTimer = a.castInterval;
                    }

                    for (uint32_t k = 0; k < (uint32_t)wand.spells.size() && k < 16; ++k)
                    {
                        if ((ev.tag & (1u << k)) == 0) continue;
                        auto& s = wand.spells[k];
                        if (!s.triggered || s.pendingCasts > 0) continue;   // バックパックを組み替えて添字がずれた物も弾く
                        if (!ignoreCooldown && s.castTimer > 0.0f) continue;
                        if (!allowNewCast) continue;
                        if (!mana.CanAfford(s.manaCost)) continue;
                        // 貯蔵された上級魔法は光球から（光球が出ていなければ撃たない。隕石の落点は同じ = 弾が消えた所）
                        Vector3 origin = muzzle;
                        uint32_t orbSerial = 0;
                        if (s.storeUnit >= 0 && !FirstOrbOf(s.storeUnit, origin, orbSerial)) continue;

                        Vector3 impact = ev.position;
                        impact.y += triggerLift;
                        QueueTriggeredCast(s, impact, origin, durationMul);
                        mana.Reserve(s.manaCost);
                        ++m_TriggeredCasts;

                        s.pendingCasts = (std::max)(0, s.castCount - 1);
                        s.delayTimer = s.castDelay;
                        s.castTimer = s.castInterval;
                        s.triggerPos = impact;
                    }
                }

                // ============================================================
                // 範囲攻撃（爆発・法環）
                // 判定は GPU（SwarmSystem の Area）。ここは「いつ・どこに出すか」を決めるだけ。
                // 標的の足元に出す物は標的が要る。プレイヤーの位置に出す物は標的が居なくても出る
                // ============================================================
                UpdateBeams(dt, muzzle, collision);   // 出ている光線（溜め → 判定 → 終了）

                for (auto& a : wand.areas)
                {
                    if (a.storeUnit >= 0 && !a.triggered) continue;   // 貯蔵された物は光球が撃つ（光線は誘発で光球から）
                    a.castTimer -= castDt;
                    if (a.triggered) continue;   // 光線は上の「誘発」でだけ始まる
                    if (!ignoreCooldown && a.castTimer > 0.0f) continue;
                    if (!allowNewCast) continue;
                    if (!m_Swarm) continue;
                    if (a.spawnAtTarget && !hasTarget) continue;
                    if (!mana.CanAfford(a.manaCost)) continue;

                    const bool atCaster = !a.spawnAtTarget;
                    const Vector3 center = atCaster ? tf.position : targetPos;

                    // 単発/持続・厚み・追従・硬直 はプロファイルから。
                    // 半径・持続・tick・威力 は集約済みの値（修飾ルーン込み）
                    const AreaProfile& ap = AreaProfileDB::At(a.profile);
                    Swarm::Area area = ap.MakeArea(center, atCaster);
                    area.radius = a.radius;
                    area.damage = a.damagePerTick;
                    area.timeLeft = a.duration;
                    const bool lasting = (a.profile == 0 || ap.kind == AreaProfile::Kind::Lasting);
                    if (lasting)
                    {
                        area.tickInterval = a.tickInterval;
                        area.timeLeft *= durationMul;   // 魔力解放中に出した持続する範囲は長く残る
                    }
                    m_Swarm->SpawnArea(area);
                    AudioSystem::Get().Play(ap.sound);   // CPU から出す範囲の音（GPU の命中で出る物は BattleAudio が鳴らす）

                    if (m_AreaVFX && m_AreaVFXCtx)
                    {
                        // プロファイルの VFX。無ければアイテムの vfxId（VFXDatabase のパス）
                        std::string vfx = ap.vfxFile;
                        if (vfx.empty())
                            if (const auto* adef = ItemDatabase::GetArea(a.id))
                                if (const char* path = VFXDatabase::GetPath(adef->vfxId))
                                    vfx = path;
                        const uint32_t h = m_AreaVFX->Play(vfx, center, area.timeLeft,
                            (area.flags & Swarm::kAreaFollowPlayer) != 0, *m_AreaVFXCtx);
                        if (lasting && durationMul != 1.0f)
                            m_AreaVFX->RemapTimeline(h, 0.0f, 1.0f, durationMul);   // 見た目の entry も同じだけ伸ばす
                    }

                    mana.Reserve(a.manaCost);
                    wand.castAnimTimer = wand.castAnimDuration;
                    a.castTimer = a.castInterval;
                }

                // ---- 光球（水晶玉の召喚物）：出現・回転・消滅と、貯蔵された魔法の発射 ----
                UpdateOrbs(dt, castDt, castSpeed, durationMul, tf.position, wand, mana,
                    allowNewCast, ignoreCooldown, hasTarget, targetPos, targetVel);
            });

    // ============================================================
    // 走査が終わってから GPU（SwarmSystem）へ積む。
    //   弾は「核」として GPU に積まれ、見た目はレシピから粒子が出る。CPU 側の Entity は作らない
    // ============================================================
    if (!m_Swarm) { m_Requests.clear(); return; }
    for (const auto& req : m_Requests)
    {
        // 弾そのもの（VFX・飛び方）は投射物プロファイルから。番号は集約時に決まっている。
        // 左右交互・乱数の判定は 1 発ごとに DB が持つ
        const int motion = req.profile;
        const VFXId vfx = ProjectileProfileDB::At(motion).ResolveVFX();
        const bool mirror = ProjectileProfileDB::NextMirror(motion);
        const uint32_t roll = ProjectileProfileDB::NextRoll(motion);   // 横ずれの向き（RandomAngle の時だけ乱数）

        m_Swarm->SpawnProjectile(vfx, req.muzzle, req.dir * req.speed,
            req.damage, req.radius, req.lifetime, (uint32_t)motion, mirror,
            req.triggerTag, req.atPos, req.areaDamageMul, req.areaDurationMul, roll);
        // 撃った音（分裂・二重で同じフレームに何発も出ても、cue の間隔・同時数で間引かれる）
        AudioSystem::Get().Play(ProjectileProfileDB::At(motion).castSound);
    }
}
// ============================================================
// 光線（上級の範囲魔法）
// 誘発で始まる。溜めの間はエフェクトだけ、溜めが終わった瞬間に GPU へカプセル型の範囲を 1 個出し、
// 以後は毎フレーム起点（杖先）と終点（向き × 射程を地形で切った所）を SetBeam で渡す。
// 終点は Beam entry のエフェクトにも同じ物を入れる（当たり判定と見た目が一致する）
// ============================================================
bool WeaponSystem::StartBeam(const AreaStats& a, const Vector3& muzzle, const Vector3& impact,
    float castSpeed, float durationMul, uint32_t orbSerial)
{
    // 空いているチャンネル
    uint32_t used = 0;
    for (const auto& b : m_Beams) used |= 1u << b.channel;
    uint32_t ch = 0;
    while (ch < Swarm::kMaxBeams && (used & (1u << ch))) ++ch;
    if (ch >= Swarm::kMaxBeams) return false;

    ActiveBeam b;
    b.channel = ch;
    b.dir = impact - muzzle;
    if (b.dir.LengthSquared() < 1e-6f) b.dir = Vector3(0, 0, 1);
    b.dir.Normalize();
    b.seek = impact;               // GPU がここに一番近い敵を最初の標的にする
    b.serial = ++m_BeamSerial;
    b.orbSerial = orbSerial;       // 光球から撃った光線は起点が光球に追従する（UpdateBeams）
    b.lastStart = muzzle;

    // 溜め・射程・厚み・硬直はプロファイルから。半径・持続・tick・威力は集約済みの値（修飾ルーン込み）
    const AreaProfile& ap = AreaProfileDB::At(a.profile);
    const bool hasProfile = a.profile > 0;
    const float baseCharge = hasProfile ? ap.chargeTime : 0.5f;
    castSpeed = (std::max)(castSpeed, 0.01f);
    b.charge = baseCharge / castSpeed;            // 魔力解放中は溜めも速い
    b.length = hasProfile ? ap.length : 18.0f;
    b.halfHeight = hasProfile ? ap.halfHeight : 1.2f;
    b.timeLeft = a.duration * durationMul;        // 魔力解放中に撃った光線は長く出る
    b.radius = a.radius;
    b.damage = a.damagePerTick;
    b.tickInterval = a.tickInterval;
    b.flags = Swarm::kAreaCapsule | (ch << Swarm::kAreaBeamShift);
    if (!hasProfile || ap.stun) b.flags |= Swarm::kAreaStun;
    if (hasProfile && ap.cameraShake) b.flags |= Swarm::kAreaShake;   // 光線が出た瞬間に揺らす
    if (hasProfile) b.sound = ap.sound;

    if (m_AreaVFX && m_AreaVFXCtx)
    {
        std::string vfx = hasProfile ? ap.vfxFile : std::string();
        if (vfx.empty())
            if (const auto* adef = ItemDatabase::GetArea(a.id))
                if (const char* path = VFXDatabase::GetPath(adef->vfxId))
                    vfx = path;
        b.vfxHandle = m_AreaVFX->Play(vfx, muzzle, b.charge + b.timeLeft, false, *m_AreaVFXCtx);
        // エフェクトの時間軸は「溜め（baseCharge 秒）→ 光線」で作ってある。溜めを縮め、光線を伸ばして判定と合わせる
        if (castSpeed != 1.0f || durationMul != 1.0f)
            m_AreaVFX->RemapTimeline(b.vfxHandle, baseCharge, 1.0f / castSpeed, durationMul);
        m_AreaVFX->SetInstance(b.vfxHandle, muzzle, muzzle + b.dir * b.length);
    }

    m_Beams.push_back(b);
    AudioSystem::Get().Play("beam_charge");   // 溜め（撃った瞬間の音は溜め終わりに範囲の sound）
    return true;
}

void WeaponSystem::UpdateBeams(float dt, const Vector3& muzzle, const CollisionSystem& collision)
{
    for (auto& b : m_Beams)
    {
        // 起点：杖先。光球から撃った物は光球（消えた後は最後の位置に留まる）
        Vector3 start = muzzle;
        if (b.orbSerial != 0)
        {
            if (FindOrb(b.orbSerial, start)) b.lastStart = start;
            else start = b.lastStart;
        }

        // 標的へ向きを回す（瞬間には向けない。beamTurnRate 度/秒まで）。
        // 標的は GPU のリードバック（2〜3 フレーム古い）。読めない間（始めの数フレーム・射程内に敵が居ない）は今の向きのまま
        Vector3 tp;
        b.hasTarget = m_Swarm && m_Swarm->GetBeamTarget(b.channel, b.serial, tp);
        if (b.hasTarget)
        {
            b.targetPos = tp;
            Vector3 want = tp - start;
            if (want.LengthSquared() > 1e-4f)
            {
                want.Normalize();
                b.dir = RotateToward(b.dir, want, DirectX::XMConvertToRadians(beamTurnRate) * dt);
            }
        }
        if (m_Swarm)
            m_Swarm->SetBeamTarget(b.channel, b.targetStarted ? Swarm::kBeamTargetTrack : Swarm::kBeamTargetStart,
                start, b.dir, b.length, b.seek, b.serial);
        b.targetStarted = true;

        // 終点：射程の先。地形に当たればそこまで
        Vector3 end = start + b.dir * b.length;
        CollisionMath::Ray ray;
        ray.origin = start;
        ray.dir = b.dir;
        ray.maxDist = b.length;
        const auto hit = collision.Raycast(ray, Layer_Terrain);
        if (hit.hit) end = hit.point;

        if (b.charge > 0.0f)
        {
            b.charge -= dt;
            if (b.charge <= 0.0f && !b.spawned && m_Swarm)
            {
                // 溜め終わり：GPU のカプセル型の範囲を 1 個。中心 / 半径 / 終点は毎ステップ BeamCB から写される
                Swarm::Area area;
                area.center = start;
                area.radius = b.radius;
                area.damage = b.damage;
                area.timeLeft = b.timeLeft;
                area.tickInterval = b.tickInterval;
                area.tickTimer = 0.0f;
                area.halfHeight = b.halfHeight;
                area.flags = b.flags;
                area.vfxType = 0;
                m_Swarm->SetBeam(b.channel, start, end, b.radius, true);
                m_Swarm->SpawnArea(area);
                b.spawned = true;
                AudioSystem::Get().Play(b.sound);
            }
        }
        else
        {
            b.timeLeft -= dt;
            if (m_Swarm) m_Swarm->SetBeam(b.channel, start, end, b.radius, b.timeLeft > 0.0f);
        }

        if (m_AreaVFX) m_AreaVFX->SetInstance(b.vfxHandle, start, end);
    }

    // 終わった光線のチャンネルは標的探しも止める
    for (const auto& b : m_Beams)
        if (b.charge <= 0.0f && b.timeLeft <= 0.0f && m_Swarm)
            m_Swarm->SetBeamTarget(b.channel, Swarm::kBeamTargetIdle, Vector3::Zero, Vector3::Zero, 0.0f, Vector3::Zero, 0);
    m_Beams.erase(std::remove_if(m_Beams.begin(), m_Beams.end(),
        [](const ActiveBeam& b) { return b.charge <= 0.0f && b.timeLeft <= 0.0f; }), m_Beams.end());
}
