// ============================================================
// WeaponSystemTwister.cpp
// 竜巻（上級の範囲魔法、AreaProfile::Kind::Roam。2026-10-22。ユーザー：渦を下級魔法にして「新しい上級の竜巻」、
//   前提は渦 + 突風、動きは「敵の方へ進んで巻き込む」）。
//   誘発された所に出て、プレイヤーに一番近い敵の方へ向きを回しながら roamSpeed m/s で進む。
//   GPU の範囲は中心を動かせないので、kTwisterPulse 秒ごとに今の位置へ「その間だけ生きる」範囲を出し直す
//   （引き寄せ = AreaProfile::pull・減速は毎回、威力は tickInterval ごとの回にだけ乗せる。数字が出過ぎないように）。
//   見た目は CPU の VFX（AreaVFXPlayer のインスタンス）を毎フレーム竜巻の位置へ動かし、
//   風源（SwarmSystem::AddFrameWindSource）も毎フレーム出し直す：草が渦になってなびき、落ち葉が巻き上がる。
//   出る / 消える時は急に出入りしないよう（2026-10-22 ユーザー指摘）、出た直後 introTime 秒で足元から伸び上がり、
//   寿命が尽きたら outroTime 秒かけてほどける（AreaVFXPlayer::SetPresence。ほどけている間は判定を出さない）
// ============================================================
#include "ECS/System/WeaponSystem.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/AreaVFXPlayer.h"
#include "Audio/AudioSystem.h"

using DirectX::SimpleMath::Vector3;

void WeaponSystem::StartTwister(const AreaStats& a, const Vector3& at, const Vector3& playerPos, float durationMul)
{
    if (!m_Swarm || a.profile <= 0) return;
    const AreaProfile& ap = AreaProfileDB::At(a.profile);

    ActiveTwister t;
    t.id = a.id;
    t.profile = a.profile;
    t.pos = at;
    t.pos.y = groundHeight ? groundHeight(at.x, at.z) : at.y - m_Swarm->GetAIParams().groundY;
    t.speed = ap.roamSpeed;
    t.life = a.duration * durationMul;   // 魔力解放中に誘発した竜巻は長く残る
    t.radius = a.radius;                 // 拡大鏡で広がる
    t.damage = a.damagePerTick;          // 魔法威力込み
    t.tickInterval = (std::max)(a.tickInterval, kTwisterPulse);
    t.lifesteal = a.lifesteal > 0.0f;
    // 最初の向き = プレイヤー → 出た所（寄ってくる群れを外へ突っ切る）。重なっている時は +Z
    Vector3 out = t.pos - playerPos;
    out.y = 0.0f;
    if (out.LengthSquared() > 0.04f) { out.Normalize(); t.dir = out; }
    if (m_AreaVFX && m_AreaVFXCtx && !ap.vfxFile.empty())
    {
        // 寿命 + 消える分。止めるのは UpdateTwisters（ほどけ終わった時）。ここの長さは止め忘れた時の保険
        t.vfxHandle = m_AreaVFX->Play(ap.vfxFile, t.pos, t.life + twisterLook.outroTime + 0.5f, false, *m_AreaVFXCtx);
        // 拡大鏡で広がった分、柱も煙も同じ倍率に（2026-10-22。判定と風源は t.radius で既に広がっている）
        if (t.vfxHandle != 0 && ap.radius > 0.0f)
            m_AreaVFX->ScaleInstance(t.vfxHandle, t.radius / ap.radius);
        if (t.vfxHandle != 0)
            m_AreaVFX->SetPresence(t.vfxHandle, 0.0f, false);   // 最初のフレームから足元の芽の状態で
    }
    AudioSystem::Get().Play(ap.sound);
    m_Twisters.push_back(t);
}

void WeaponSystem::UpdateTwisters(float dt, const Vector3& playerPos, bool hasTarget, const Vector3& targetPos)
{
    if (m_Twisters.empty()) return;
    const float lift = m_Swarm ? m_Swarm->GetAIParams().groundY : 0.9f;   // 範囲の中心は敵の体の高さ（地面 + groundY）

    for (size_t i = 0; i < m_Twisters.size();)
    {
        ActiveTwister& t = m_Twisters[i];

        // ---- 出る / 消える移り変わり（2026-10-22）：presence 0〜1 を柱・煙・光・風・進む速さに掛ける。
        //      寿命が尽きたら outroTime 秒かけてほどけ、その間は引き寄せも威力も出さない ----
        const bool leaving = t.age >= t.life;
        if (leaving)
        {
            t.outro = (std::max)(t.outro, 0.0f) + dt;
            if (t.outro >= twisterLook.outroTime)
            {
                if (m_AreaVFX && t.vfxHandle != 0) m_AreaVFX->StopInstance(t.vfxHandle);
                m_Twisters.erase(m_Twisters.begin() + (ptrdiff_t)i);
                continue;
            }
        }
        const float presence = leaving
            ? 1.0f - t.outro / (std::max)(twisterLook.outroTime, 1e-3f)
            : (twisterLook.introTime > 0.0f ? (std::min)(t.age / twisterLook.introTime, 1.0f) : 1.0f);
        const float presenceSmooth = presence * presence * (3.0f - 2.0f * presence);

        // ---- 向き（2026-10-22 第 1 版はプレイヤーに一番近い敵を追わせたら、群れごとプレイヤーへ引きずってきた）：
        //      基本はまっすぐ外へ。プレイヤーに一番近い敵が前方 seekCone 度以内に居ればその方へ turnRate 度/秒で曲がる
        //      （後ろの敵へは戻らない。もう標的の上 = 1m 以内なら今の向きのまま）。
        //      プレイヤーから keepAway m 以内に入ったら、プレイヤーから離れる向きへ曲げる ----
        Vector3 want = t.dir;
        bool steer = false;
        Vector3 fromPlayer = t.pos - playerPos;
        fromPlayer.y = 0.0f;
        const float pd = fromPlayer.Length();
        if (pd < twisterLook.keepAway && pd > 1e-3f)
        {
            want = fromPlayer / pd;
            steer = true;
        }
        else if (hasTarget)
        {
            Vector3 to = targetPos - t.pos;
            to.y = 0.0f;
            const float d = to.Length();
            if (d > 1.0f)
            {
                to /= d;
                if (to.Dot(t.dir) > std::cos(DirectX::XMConvertToRadians(twisterLook.seekCone)))
                {
                    want = to;
                    steer = true;
                }
            }
        }
        if (steer)
        {
            const float cur = std::atan2(t.dir.x, t.dir.z), goal = std::atan2(want.x, want.z);
            float diff = goal - cur;
            while (diff > DirectX::XM_PI) diff -= DirectX::XM_2PI;
            while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
            const float step = DirectX::XMConvertToRadians(twisterLook.turnRate) * dt;
            const float ang = cur + std::clamp(diff, -step, step);
            t.dir = Vector3(std::sin(ang), 0.0f, std::cos(ang));
        }
        // 出た直後は動き出しながら、消える時は止まりかけながら（0.25 倍まで）
        t.pos += t.dir * (t.speed * (0.25f + 0.75f * presenceSmooth) * dt);
        if (groundHeight) t.pos.y = groundHeight(t.pos.x, t.pos.z);
        t.age += dt;

        // ---- 判定：今の位置に短い範囲（引き寄せ・減速は毎回、威力は tickInterval ごと）。ほどけている間は出さない ----
        t.pulseTimer -= dt;
        t.damageTimer -= dt;
        if (!leaving && t.pulseTimer <= 0.0f && m_Swarm)
        {
            t.pulseTimer += kTwisterPulse;
            const AreaProfile& ap = AreaProfileDB::At(t.profile);
            Swarm::Area area = ap.MakeArea(t.pos + Vector3(0.0f, lift, 0.0f), false);
            area.radius = t.radius;
            if (t.lifesteal) area.tickNow |= Swarm::kAreaExtraLifesteal;   // 吸血のルーン（2026-10-25）
            area.damage = 0.0f;
            if (t.damageTimer <= 0.0f)
            {
                area.damage = t.damage;
                t.damageTimer += t.tickInterval;
            }
            // 出た瞬間に 1 回だけ tick して（引き寄せの 1 回の長さ = kTwisterPulse）、次を出す前に消える
            area.tickInterval = kTwisterPulse;
            area.timeLeft = kTwisterPulse * 0.9f;
            m_Swarm->SpawnArea(area);
        }

        // ---- 見た目と風源を付けて動かす ----
        if (m_AreaVFX && t.vfxHandle != 0)
        {
            m_AreaVFX->SetInstance(t.vfxHandle, t.pos, t.pos);
            m_AreaVFX->SetPresence(t.vfxHandle, presence, leaving);
        }
        if (m_Swarm)
        {
            Swarm::WindSource w = {};
            w.pos[0] = t.pos.x; w.pos[1] = t.pos.y; w.pos[2] = t.pos.z;
            w.radius = t.radius * twisterLook.windRadiusMul;
            w.swirl = twisterLook.windSwirl;
            w.updraft = twisterLook.windUpdraft;
            w.inflow = twisterLook.windInflow;
            w.fade = presenceSmooth;
            m_Swarm->AddFrameWindSource(w);
        }
        ++i;
    }
}
