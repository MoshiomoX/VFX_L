// ============================================================
// SandTornadoFx.cpp
// 砂旋の本体の見た目（Enemy/SandTornadoFx.h を参照）
// ============================================================
#include "Enemy/SandTornadoFx.h"
#include "VFX_Editor/VFXId.h"

using namespace DirectX::SimpleMath;

void SandTornadoFx::Spawn(AreaVFXPlayer& vfx, const VFXContext& ctx, const Vector3& pos, float yaw, float speed, float life)
{
    Inst t;
    t.pos = pos;
    t.dir = Vector3(std::sin(yaw), 0.0f, std::cos(yaw));
    t.speed = speed;
    t.life = life;
    // ほどけ終わるまで出しておく（止めるのは Update）
    t.handle = vfx.Play(VFXDatabase::GetFile(VFXId::SandTornado), pos, life + look.outroTime + 0.5f, false, ctx);
    if (t.handle == 0) return;
    vfx.SetPresence(t.handle, 0.0f, false);   // 最初のフレームから足元の芽の状態で
    m_Insts.push_back(t);
}

void SandTornadoFx::Update(float dt, AreaVFXPlayer& vfx, const std::function<float(float, float)>& groundHeight)
{
    for (size_t i = 0; i < m_Insts.size();)
    {
        Inst& t = m_Insts[i];
        t.age += dt;
        if (t.age >= t.life + look.outroTime)
        {
            vfx.StopInstance(t.handle);
            m_Insts[i] = m_Insts.back();
            m_Insts.pop_back();
            continue;
        }
        // GPU の砂旋は生まれた瞬間から寿命まで一定の速さで直進する（AICS）：同じ式で追う。消える間は止まりかけ
        const bool leaving = t.age >= t.life;
        float presence = 1.0f;
        if (leaving)
            presence = 1.0f - (t.age - t.life) / (std::max)(look.outroTime, 0.01f);
        else if (t.age < look.introTime)
            presence = t.age / (std::max)(look.introTime, 0.01f);
        presence = std::clamp(presence, 0.0f, 1.0f);
        t.presence = presence;
        t.pos += t.dir * (t.speed * (leaving ? 0.25f : 1.0f) * dt);
        if (groundHeight) t.pos.y = groundHeight(t.pos.x, t.pos.z);
        vfx.SetInstance(t.handle, t.pos, t.pos);
        vfx.SetPresence(t.handle, presence, leaving);
        ++i;
    }
}

void SandTornadoFx::Clear(AreaVFXPlayer& vfx)
{
    for (const Inst& t : m_Insts)
        vfx.StopInstance(t.handle);
    m_Insts.clear();
}
