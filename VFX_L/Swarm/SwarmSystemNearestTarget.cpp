// ============================================================
// SwarmSystemNearestTarget.cpp
// 「本当に一番近い敵」（突風・氷のトゲのような近距離の範囲魔法の狙い）。
//   杖の狙い（SwarmCounters の aimKey = SWARM_CNT_AIM_KEY。GetAimTarget）は Boss / エリートを優先するので、すぐ横の雑魚より 20m 先の Boss を向く。
//   近距離の範囲魔法のために、同じ競争を優先度なしの距離でもう 1 本やる：
//   ・SwarmEnemyMoveCS（u4）が key = 距離 | スロットを 32B の raw buffer（Swarm::NearestTarget）の key へ InterlockedMin
//   ・SwarmAimResolveCS（u1）が key から位置と距離を書き、key を空に戻す（次の固定ステップ用）
//   ・3 枚の staging を輪転してリードバック（2〜3 フレーム遅れ。GetNearestEnemy）
//   SwarmCounters は 64B で満杯（CLAUDE.md の「既定で変えない」構造体）なので別の buffer にした
// ============================================================
#include "Swarm/SwarmSystem.h"

bool SwarmSystem::CreateNearestTargetReadback(ID3D11Device* device)
{
    // key は空、距離は 1e30（= 居ない）から始める（Swarm::NearestTarget の既定値）
    const Swarm::NearestTarget init;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(init);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA sd0 = {};
    sd0.pSysMem = &init;
    if (FAILED(device->CreateBuffer(&bd, &sd0, &m_NearestTargetBuffer))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ud.Buffer.NumElements = sizeof(init) / 4;
    if (FAILED(device->CreateUnorderedAccessView(m_NearestTargetBuffer.Get(), &ud, &m_NearestTargetUAV))) return false;

    if (!m_NearestTargetStaging.Create(device, sizeof(init))) return false;
    m_NearestTargetDist = 1.0e30f;
    return true;
}

// Flush の終わり（カウンタの写しと同じ頃）に staging へ
void SwarmSystem::CopyNearestTargetForCPU()
{
    m_NearestTargetStaging.Copy(m_Context, m_NearestTargetBuffer.Get());
}

void SwarmSystem::ReadNearestTarget()
{
    m_NearestTargetStaging.TryRead(m_Context, [&](const void* data, bool)
        {
            Swarm::NearestTarget t;
            std::memcpy(&t, data, sizeof(t));
            m_NearestTargetPos = { t.pos[0], t.pos[1], t.pos[2] };
            m_NearestTargetDist = t.dist;
            m_CloseNearCount = t.crowdCount;   // プレイヤーの 10m 以内の敵の数（スマートカメラ用）
        });
}

bool SwarmSystem::GetNearestEnemy(DirectX::SimpleMath::Vector3& pos, float& dist) const
{
    if (!(m_NearestTargetDist < 1.0e29f)) return false;
    pos = m_NearestTargetPos;
    dist = m_NearestTargetDist;
    return true;
}
