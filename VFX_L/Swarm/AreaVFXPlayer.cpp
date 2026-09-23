// ============================================================
// AreaVFXPlayer.cpp
// ============================================================
#include "Swarm/AreaVFXPlayer.h"
#include "Swarm/AreaProfile.h"
#include "VFX_Editor/VFXEffect.h"
#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr size_t kMaxActive = 64;   // 溢れたら新しい物を諦める（降級）
}

AreaVFXPlayer::AreaVFXPlayer() = default;
AreaVFXPlayer::~AreaVFXPlayer() = default;

std::shared_ptr<VFXEffect> AreaVFXPlayer::GetTemplate(const std::string& vfxFile)
{
    for (auto& t : m_Templates)
        if (t.first == vfxFile) return t.second;

    // 読めなかった物も null で覚える（毎回ファイルを開き直さない）
    auto tmpl = std::make_shared<VFXEffect>();
    // ファイル名だけなら VFXData フォルダから。区切りを含むなら、そのままのパスとして読む
    const bool hasDir = vfxFile.find_first_of("/\\") != std::string::npos;
    if (!tmpl->LoadFromFile(hasDir ? vfxFile : std::string(AreaProfileDB::kVfxDir) + vfxFile))
        tmpl.reset();
    m_Templates.emplace_back(vfxFile, tmpl);
    return tmpl;
}

void AreaVFXPlayer::Play(const std::string& vfxFile, const Vector3& pos,
    float duration, bool follow, const VFXContext& ctx)
{
    if (vfxFile.empty() || m_Active.size() >= kMaxActive) return;

    auto tmpl = GetTemplate(vfxFile);
    if (!tmpl) return;

    Instance inst;
    inst.effect = std::make_unique<VFXEffect>();
    inst.effect->CloneFrom(*tmpl);
    inst.effect->InitStateMachine(ctx);
    inst.effect->SetWorldOffset(pos);
    inst.effect->Play();
    inst.timeLeft = duration;
    inst.follow = follow;
    m_Active.push_back(std::move(inst));
}

void AreaVFXPlayer::Update(float dt, const Vector3& followPos)
{
    for (auto& inst : m_Active)
    {
        if (inst.follow)
            inst.effect->SetWorldOffset(followPos);

        inst.timeLeft -= dt;
        if (!inst.stopped && inst.timeLeft <= 0.0f)
        {
            inst.effect->Stop();   // 発射を止める。出ている粒子は自然に消える
            inst.stopped = true;
        }

        inst.effect->Update(dt);
    }

    // 捨てる条件：
    //   - Stop を送って少し経った（発射はもう止まっている。出た粒子は GPU の池で勝手に消える）
    //   - loop しない特効が自分で終わった
    // ※IsFinishing を待たない。Finishing は「粒子池全体の生存数が 0」で抜ける作りなので、
    //   戦闘中（他の粒子が常に居る）は永遠に終わらず、実例が溜まり続ける
    m_Active.erase(std::remove_if(m_Active.begin(), m_Active.end(),
        [](const Instance& i)
        {
            if (i.stopped && i.timeLeft <= -0.1f) return true;
            return !i.effect->IsPlaying() && !i.effect->IsFinishing();
        }), m_Active.end());
}

void AreaVFXPlayer::StopAll()
{
    for (auto& inst : m_Active)
        inst.effect->Stop();
    m_Active.clear();
}
