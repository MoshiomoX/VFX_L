// ============================================================
// PlayerVFXSystem.cpp
// ============================================================
#include "ECS/System/PlayerVFXSystem.h"
#include "ECS/System/SkinnedAnimSystem.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/ShieldComponent.h"
#include "Item/ItemDatabase.h"
#include "Swarm/AreaVFXPlayer.h"
#include "imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr VFXId kCastFlash = VFXId::CastFlash;
    constexpr VFXId kCastPulse = VFXId::CastPulse;
    constexpr VFXId kFootDust = VFXId::FootDust;
    constexpr VFXId kJumpDust = VFXId::JumpDust;
    constexpr VFXId kAirJumpRing = VFXId::AirJumpRing;
    constexpr VFXId kLandDust = VFXId::LandDust;
    constexpr VFXId kLandDustHeavy = VFXId::LandDustHeavy;
    constexpr VFXId kSlideDust = VFXId::SlideDust;
    constexpr VFXId kSpeedTrail = VFXId::SpeedTrail;

    // 尾の根元にする上背の骨（Shadowkin の CC 骨格 / レンジャーの UE 式骨格）
    constexpr const char* kBackBones[] = { "CC_Base_Spine02", "spine_02", "Spine2", "spine.002" };

    // 水平の向き → 度（TransformComponent::rotation.y と同じ決まり：atan2(x, z)）
    float YawOf(const Vector3& v)
    {
        return DirectX::XMConvertToDegrees(std::atan2(v.x, v.z));
    }
}

void PlayerVFXSystem::Init(AreaVFXPlayer* player, const VFXContext* ctx)
{
    m_Player = player;
    m_Ctx = ctx;
    m_Read = false;
    m_Flash = 0.0f;
    m_CastGaps.clear();
    m_Clock = 0.0f;
    m_TrailOn = false;
    m_Trail = 0;
    m_BackBone = -2;
    m_CastGlow = 0.0f;
    m_LastPulse = -1.0e9f;
}

void PlayerVFXSystem::Shutdown()
{
    UpdateTrail(false, Vector3::Zero);
}

uint32_t PlayerVFXSystem::Play(VFXId id, const Vector3& pos, float yawDeg, float duration, const Vector3& tint)
{
    if (!m_Player || !m_Ctx) return 0;
    const uint32_t h = m_Player->Play(VFXDatabase::GetFile(id), pos, duration, false, *m_Ctx, tint);
    if (h && yawDeg != 0.0f) m_Player->RotateInstance(h, yawDeg);   // json の +Z を yawDeg の向きへ
    return h;
}

// ============================================================
// 毎フレーム
// ============================================================
void PlayerVFXSystem::Update(Registry& reg, Entity player, float dt, float hpLost,
    const std::vector<WeaponSystem::CastFx>& casts)
{
    m_Clock += dt;
    if (!reg.IsValid(player) || !reg.Has<TransformComponent>(player)) return;
    const auto& tf = reg.Get<TransformComponent>(player);
    const bool hasRb = reg.Has<RigidbodyComponent>(player);
    const Vector3 vel = hasRb ? reg.Get<RigidbodyComponent>(player).velocity : Vector3::Zero;
    const bool grounded = hasRb && reg.Get<RigidbodyComponent>(player).isGrounded;
    const bool hasState = reg.Has<PlayerStateComponent>(player);
    const bool sliding = hasState && reg.Get<PlayerStateComponent>(player).slideActive;
    const bool dead = hasState && reg.Get<PlayerStateComponent>(player).IsDead();
    const int airJumps = hasState ? reg.Get<PlayerStateComponent>(player).airJumpsUsed : 0;

    // 足元（カプセルの中心から 半分の高さ + 半径 下）
    float halfH = 0.9f, radius = 0.4f;
    if (reg.Has<PlayerStatsComponent>(player))
    {
        halfH = reg.Get<PlayerStatsComponent>(player).height * 0.5f;
        radius = reg.Get<PlayerStatsComponent>(player).radius;
    }
    const Vector3 feet = tf.position - Vector3(0.0f, halfH + radius, 0.0f);
    const Vector3 hv(vel.x, 0.0f, vel.z);
    const float speed = hv.Length();
    const float moveYaw = (speed > 0.1f) ? YawOf(hv) : tf.rotation.y;

    // ---- 被弾の閃き（HP が減った = 白、シールドで受けた = 水色）----
    uint32_t shieldHits = reg.Has<ShieldComponent>(player) ? reg.Get<ShieldComponent>(player).hits : 0;
    if (enabled && hitFlash && m_Read)
    {
        if (hpLost > 0.0f) { m_Flash = 1.0f; m_FlashColor = hurtFlashColor; }
        else if (shieldHits > m_PrevShieldHits) { m_Flash = 1.0f; m_FlashColor = shieldFlashColor; }
    }
    m_PrevShieldHits = shieldHits;

    // ---- 詠唱（手元の光・魔法陣・体の光の粒）----
    if (enabled && !dead) UpdateCasts(casts, tf.position);
    m_Circles.Update(dt, feet, enabled && !dead);

    // 体の色：被弾の閃き（強い）と撃った時のほのかな光（弱い）の大きい方
    m_Flash = (std::max)(0.0f, m_Flash - dt / (std::max)(hitFlashTime, 0.01f));
    m_CastGlow = (std::max)(0.0f, m_CastGlow - dt / (std::max)(castGlowTime, 0.01f));
    if (reg.Has<SkinnedAnimComponent>(player))
    {
        auto& anim = reg.Get<SkinnedAnimComponent>(player);
        const float hit = (enabled && hitFlash) ? m_Flash * m_Flash * hitFlashStrength : 0.0f;   // 2 乗：最初だけ強く、すっと引く
        const float glow = (enabled && castPulse) ? m_CastGlow * castGlowStrength : 0.0f;
        anim.hitFlash = (std::max)(hit, glow);
        anim.hitFlashColor = (hit >= glow) ? m_FlashColor : m_CastGlowColor;
    }

    if (!enabled || dead)
    {
        UpdateTrail(false, tf.position);
        m_Read = false;
        return;
    }

    if (m_Read && jumpLand)
    {
        // 跳んだ（地面を離れた瞬間に上向き）・空中ジャンプ
        if (m_PrevGrounded && !grounded && vel.y > 2.0f)
            Play(kJumpDust, feet, 0.0f, 1.0f, dustTint);
        if (airJumps > m_PrevAirJumps)
            Play(kAirJumpRing, feet, 0.0f, 0.8f, Vector3(1.0f, 1.0f, 1.0f));
        // 着地
        if (!m_PrevGrounded && grounded && m_AirTime >= landMinAir)
            Play(-m_MinVy >= landHeavySpeed ? kLandDustHeavy : kLandDust, feet, 0.0f, 1.2f, dustTint);
    }
    if (grounded) { m_AirTime = 0.0f; m_MinVy = 0.0f; }
    else { m_AirTime += dt; m_MinVy = (std::min)(m_MinVy, vel.y); }

    // ---- 走る小石（左右の足を交互に）----
    if (footDust && grounded && !sliding && speed >= footMinSpeed)
    {
        m_FootTimer -= dt * (speed / (std::max)(footRefSpeed, 0.1f));
        if (m_FootTimer <= 0.0f)
        {
            m_FootTimer = footInterval;
            m_FootSide ^= 1;
            const float a = DirectX::XMConvertToRadians(moveYaw);
            const Vector3 right(std::cos(a), 0.0f, -std::sin(a));   // 進む向き (sin, cos) の横
            Play(kFootDust, feet + right * (m_FootSide ? 0.12f : -0.12f), moveYaw, 1.0f, dustTint);
        }
    }
    else
        m_FootTimer = (std::min)(m_FootTimer, footInterval * 0.3f);   // 走り出しの一歩をすぐ出す

    // ---- スライディングの小石と草（速いほど細かく）----
    if (slideDust && sliding && grounded && speed > 0.5f)
    {
        m_SlideTimer -= dt;
        if (m_SlideTimer <= 0.0f)
        {
            const float t = std::clamp((speed - 5.0f) / 12.0f, 0.0f, 1.0f);
            m_SlideTimer = slideIntervalSlow + (slideIntervalFast - slideIntervalSlow) * t;
            Play(kSlideDust, feet, moveYaw, 1.0f, dustTint);
        }
    }
    else
        m_SlideTimer = 0.0f;

    // ---- 速さの尾（行き過ぎを防ぐため、出す / 離す速さを分ける）。根元は上背の骨 ----
    const bool trail = speedTrail && (m_TrailOn ? speed > trailSpeedOff : speed > trailSpeedOn);
    UpdateTrail(trail, trail ? BackAnchor(reg, player, tf.position) : tf.position);

    m_PrevGrounded = grounded;
    m_PrevAirJumps = airJumps;
    m_Read = true;
}

// 撃った弾ごとに：手元の光・魔法陣。同じ魔法は castMinGap 秒あけ、1 フレームに castMaxPerFrame 個まで。
// 体の光の粒とほのかな光は pulseGap 秒に 1 回
void PlayerVFXSystem::UpdateCasts(const std::vector<WeaponSystem::CastFx>& casts, const Vector3& center)
{
    int shown = 0;
    for (const auto& c : casts)
    {
        if (shown >= castMaxPerFrame) break;
        const int id = (int)c.id;
        auto it = std::find_if(m_CastGaps.begin(), m_CastGaps.end(), [&](const CastGap& g) { return g.id == id; });
        if (it != m_CastGaps.end() && m_Clock - it->time < castMinGap) continue;
        if (it == m_CastGaps.end()) m_CastGaps.push_back({ id, m_Clock });
        else it->time = m_Clock;

        // 魔法の色：一番明るい成分を 1 に揃える（暗い色の魔法でも光る）
        Vector3 color(1.0f, 1.0f, 1.0f);
        if (const ItemCommon* ic = ItemDatabase::GetCommon(c.id))
        {
            color = Vector3(ic->color.x, ic->color.y, ic->color.z);
            const float m = (std::max)({ color.x, color.y, color.z, 1e-3f });
            color /= m;
        }
        const Vector3 flatDir(c.dir.x, 0.0f, c.dir.z);
        if (castFlash) Play(kCastFlash, c.pos, YawOf(flatDir), 0.5f, color * castTintGain);
        if (castCircles) m_Circles.OnCast(c.pos, c.dir, color);
        if (castPulse && m_Clock - m_LastPulse >= pulseGap)
        {
            m_LastPulse = m_Clock;
            Play(kCastPulse, center, 0.0f, 1.0f, color);
            m_CastGlow = 1.0f;
            m_CastGlowColor = color * 1.4f;
        }
        ++shown;
    }
}

Vector3 PlayerVFXSystem::BackAnchor(Registry& reg, Entity player, const Vector3& center)
{
    if (!reg.Has<SkinnedAnimComponent>(player) || !reg.Has<TransformComponent>(player)) return center;
    const auto& a = reg.Get<SkinnedAnimComponent>(player);
    if (!a.model) return center;
    if (m_BackBone == -2)
    {
        m_BackBone = -1;
        for (const char* name : kBackBones)
            if ((m_BackBone = a.model->GetSkeleton().FindBoneIndex(name)) >= 0) break;
    }
    if (m_BackBone < 0) return center;
    std::vector<Matrix>& G = m_Pose;
    if (!SkinnedAnimSystem::BuildPose(a, G) || m_BackBone >= (int)G.size()) return center;
    const Matrix W = SkinnedAnimSystem::WorldMatrix(reg.Get<TransformComponent>(player), a);
    return Vector3::Transform(G[m_BackBone].Translation(), W);
}

// 背中から大きな光の帯 1 本。出している間は毎フレーム根元を上背の骨へ入れ直す（モデルの動き・滑りの姿勢に付いて動く）
void PlayerVFXSystem::UpdateTrail(bool on, const Vector3& anchor)
{
    if (!m_Player) return;
    if (!on)
    {
        if (m_TrailOn && m_Trail) m_Player->StopInstance(m_Trail);   // 帯はその場で縮んで消える
        m_Trail = 0;
        m_TrailOn = false;
        return;
    }
    if (!m_Trail && m_Ctx) m_Trail = m_Player->Play(VFXDatabase::GetFile(kSpeedTrail), anchor, 1.0e9f, false, *m_Ctx);
    if (m_Trail) m_Player->SetInstance(m_Trail, anchor, anchor);
    m_TrailOn = true;
}

void PlayerVFXSystem::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Player Motion VFX")) return;
    ImGui::Checkbox("Enabled##PlayerVFX", &enabled);
    if (!enabled) return;
    ImGui::Checkbox("Cast flash", &castFlash); ImGui::SameLine();
    ImGui::Checkbox("Cast circles", &castCircles); ImGui::SameLine();
    ImGui::Checkbox("Cast pulse", &castPulse);
    ImGui::Checkbox("Foot pebbles", &footDust); ImGui::SameLine();
    ImGui::Checkbox("Jump / land", &jumpLand); ImGui::SameLine();
    ImGui::Checkbox("Slide pebbles", &slideDust);
    ImGui::Checkbox("Speed trail", &speedTrail); ImGui::SameLine();
    ImGui::Checkbox("Hit flash", &hitFlash);
    ImGui::DragFloat("Cast min gap (s)", &castMinGap, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Cast tint gain", &castTintGain, 0.05f, 0.2f, 4.0f);
    ImGui::DragFloat("Cast pulse gap (s)", &pulseGap, 0.01f, 0.0f, 2.0f);
    ImGui::SliderFloat("Cast glow strength", &castGlowStrength, 0.0f, 1.0f);
    if (ImGui::TreeNode("Cast circles"))
    {
        m_Circles.DrawImGui();
        ImGui::TreePop();
    }
    ImGui::DragFloat("Foot interval (s @ ref speed)", &footInterval, 0.01f, 0.08f, 1.0f);
    ImGui::DragFloat("Land min air (s)", &landMinAir, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Land heavy fall speed", &landHeavySpeed, 0.1f, 1.0f, 40.0f);
    ImGui::DragFloatRange2("Slide interval fast / slow", &slideIntervalFast, &slideIntervalSlow, 0.002f, 0.01f, 0.5f);
    ImGui::DragFloatRange2("Trail speed off / on", &trailSpeedOff, &trailSpeedOn, 0.1f, 0.0f, 40.0f);
    ImGui::DragFloat("Hit flash time (s)", &hitFlashTime, 0.01f, 0.02f, 1.0f);
    ImGui::SliderFloat("Hit flash strength", &hitFlashStrength, 0.0f, 1.0f);
    ImGui::ColorEdit3("Hurt flash color", &hurtFlashColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Shield flash color", &shieldFlashColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::ColorEdit3("Pebble tint", &dustTint.x);
    if (ImGui::Button("Test hit flash")) { m_Flash = 1.0f; m_FlashColor = hurtFlashColor; }
    ImGui::TextDisabled("Assets/Data/VFXData/CastFlash / CastPulse / FootDust / JumpDust / AirJumpRing /");
    ImGui::TextDisabled("  LandDust / LandDustHeavy / SlideDust / SpeedTrail.json");
}
