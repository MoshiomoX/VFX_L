// ============================================================
// CollisionTestScenePlayerDeath.cpp
// プレイヤーの死（2026-11-01。ユーザーの選択：倒れて光になる / スロー + カメラを寄せる / 周りの敵を吹き飛ばす）。
// 登場（CollisionTestScenePlayerIntro.cpp）と対になる：金の魔法陣が出て、体が金の光の粒になって消え、魔法陣が砕ける。
//   0.00  スロー開始（kSlowFrom から kSlowTo で等速へ戻す）。金の爆ぜ + 敵を吹き飛ばす + HUD を隠す
//   1.00  体の端から溶け始め、光の粒が立ち上る（kDissolveFrom〜kDissolveTo）
//   1.70  「力尽きた」の幕（kCurtainAt）
//   2.10  魔法陣が砕ける（kShatterAt）
//   3.60  リザルトへ（kToResult）
// 時刻は全部実時間（m_DeathTimer）。Update が gameplay の後に毎フレーム呼ぶ
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/DissolveComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/TransformComponent.h"
#include "Manager/ResourceManager.h"
#include "VFX_Editor/VFXId.h"
#include "VFX_Editor/VFXTextureRef.h"

namespace
{
    constexpr float kToResult = 3.6f;
    constexpr float kCurtainAt = 1.7f;      // 「力尽きた」が出始める時刻
    constexpr float kCurtainLead = 0.4f;    // GameUI::DrawGameOver は渡した秒が 0.4 を超えてから出る
    constexpr float kSlowMul = 0.25f;
    constexpr float kSlowFrom = 0.8f;       // ここまで kSlowMul、kSlowTo で等速
    constexpr float kSlowTo = 1.3f;
    constexpr float kDissolveFrom = 1.0f;
    constexpr float kDissolveTo = 2.5f;
    constexpr float kDissolveLow = 0.18f;   // Perlin の値はほぼこの範囲に入る（0〜1 のまま進めると前半は何も溶けない）
    constexpr float kDissolveHigh = 0.85f;
    constexpr float kFlashTime = 0.9f;      // 体が金色へ染まるまで
    constexpr float kFlashMax = 0.8f;
    constexpr float kShatterAt = 2.1f;      // 魔法陣が砕ける
    constexpr float kCircleSize = 2.2f;     // 足元の魔法陣の大きさ（登場と同じ）
    constexpr float kPush2At = 1.05f;       // 2 回目の吹き飛ばし（1 回目の押しが終わる頃）
    constexpr float kPushRadius1 = 12.0f;
    constexpr float kPushRadius2 = 17.0f;
    constexpr float kCamInTime = 1.3f;      // カメラを寄せ終わるまで
    constexpr float kCamDistance = 7.0f;
    constexpr float kCamPitch = 24.0f;
    constexpr float kCamHeight = 0.4f;      // 注視点（普段は胸より上。寄ると足元が画面の下へ落ちるので下げる）
    constexpr float kCamYawDrift = 22.0f;   // 寄りながら回り込む角度

    float Smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    float Mix(float a, float b, float t) { return a + (b - a) * t; }
}

// 周りの敵を吹き飛ばす：威力 0 の範囲（突風と同じ押し。向きはプレイヤーから離れる方、エリートは半分、Boss と幽霊は動かない）
void CollisionTestScene::PushEnemiesAway(float radius)
{
    Swarm::Area a;
    a.center = m_DeathFeet;
    a.radius = radius;
    a.halfHeight = 8.0f;                         // 台地の上下も
    a.damage = 0.0f;
    a.tickInterval = 1.0f;                       // 出た最初のステップで 1 回だけ
    a.timeLeft = 0.05f;
    a.flags = 15u << Swarm::kAreaBeamShift;      // 押しの強さ 1（AreaProfile と同じ 4 bit）
    m_Swarm.SpawnArea(a);
    m_Grass.AddGust(m_DeathFeet, radius);
    m_Debris.AddGust(m_DeathFeet, radius);
}

void CollisionTestScene::StartPlayerDeath()
{
    m_DeathFxStarted = true;
    if (const Vector3* pp = PlayerPos())
        m_DeathFeet = Vector3(pp->x, m_Grid.SampleHeight(pp->x, pp->z), pp->z);

    auto& cam = m_Camera.Camera();
    m_DeathCamDistance = cam.distance;
    m_DeathCamPitch = cam.GetPitch();
    m_DeathCamYaw = cam.GetYaw();
    m_DeathCamHeight = cam.height;

    m_GameUI.hideHud = true;
    m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::PlayerDeathBurst), m_DeathFeet, 3.0f, false, m_VFXContext);
    PushEnemiesAway(kPushRadius1);
    m_Water.AddRipple(m_DeathFeet, 2.0f, m_Water.GetSettings().blastStrength, true);
    cam.RaiseTrauma(0.45f);
    AudioSystem::Get().Play("cast_gust");
}

void CollisionTestScene::UpdatePlayerDeath(float dt)
{
    if (!m_DeathFxStarted) StartPlayerDeath();
    const float before = m_DeathTimer;
    m_DeathTimer += dt;
    const float t = m_DeathTimer;
    m_GameUI.SetGameOver(t - kCurtainAt + kCurtainLead);

    // ---- スロー：次のフレームの gameplay の dt を Update が縮める ----
    m_DefeatSlowLeft = (t < kSlowTo) ? 1.0f : 0.0f;
    m_DefeatSlowMul = Mix(kSlowMul, 1.0f, Smooth((t - kSlowFrom) / (kSlowTo - kSlowFrom)));

    if (before < kPush2At && t >= kPush2At) PushEnemiesAway(kPushRadius2);

    // ---- カメラ：寄りながら少し回り込む ----
    auto& cam = m_Camera.Camera();
    const float in = Smooth(t / kCamInTime);
    cam.distance = Mix(m_DeathCamDistance, kCamDistance, in);
    cam.SetPitch(Mix(m_DeathCamPitch, kCamPitch, in));
    cam.height = Mix(m_DeathCamHeight, kCamHeight, in);
    cam.SetYaw(m_DeathCamYaw + kCamYawDrift * Smooth(t / kToResult));
    cam.SnapToTarget();

    // ---- 足元の魔法陣：開いて、溶ける間は揺らぎ、砕けたら消える ----
    float circle = Smooth(t / 0.35f);
    if (t >= kDissolveFrom) circle *= 0.8f + 0.2f * std::sin(t * 34.0f);
    if (t >= kShatterAt) circle *= 1.0f - Smooth((t - kShatterAt) / 0.2f);
    m_PlayerVFX.SetIntroCircle(circle, kCircleSize);
    if (before < kShatterAt && t >= kShatterAt)
    {
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::PlayerDeathShatter), m_DeathFeet, 2.0f, false, m_VFXContext);
        AudioSystem::Get().Play("shield_break");
    }

    // ---- 体：金色へ染まり、端から溶けて光の粒になる ----
    if (m_Registry.IsValid(m_Player) && m_Registry.Has<SkinnedAnimComponent>(m_Player))
    {
        auto& anim = m_Registry.Get<SkinnedAnimComponent>(m_Player);
        anim.hitFlash = kFlashMax * Smooth(t / kFlashTime);
        anim.hitFlashColor = Vector3(1.5f, 1.12f, 0.45f);

        const Vector3 body = m_PlayerVFX.BodyAnchor(m_Registry, m_Player, m_Registry.Get<TransformComponent>(m_Player).position);
        if (before < kDissolveFrom && t >= kDissolveFrom)
        {
            DissolveComponent d;
            d.noise = ResourceManager::Get().LoadNoiseTexture(NoiseRecipe{});
            d.tiling = { 3.0f, 3.0f };
            d.edge = 0.14f;
            d.edgeColor = { 1.5f, 1.12f, 0.45f, 2.2f };
            d.duration = 1.0e6f;            // 進みはここで決める（MeshVFXSystem はスローの gameplay の dt で進める）
            d.destroyWhenDone = false;      // 実体は残す（EndRun まで使う）
            m_Registry.Add<DissolveComponent>(m_Player, d);
            m_DeathMotesFx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::PlayerDeathMotes), body, 2.0f, false, m_VFXContext);
            AudioSystem::Get().Play("mana_surge");
        }
        if (m_DeathMotesFx) m_AreaVFX.SetInstance(m_DeathMotesFx, body, body);
        if (m_Registry.Has<DissolveComponent>(m_Player))
            m_Registry.Get<DissolveComponent>(m_Player).progress = Mix(kDissolveLow, kDissolveHigh, std::clamp((t - kDissolveFrom) / (kDissolveTo - kDissolveFrom), 0.0f, 1.0f));
        if (t >= kDissolveTo) anim.visible = false;   // 影も消す
    }

    // TEMP-TEST: 自動の撮影
    if (m_AutoTest)
    {
        for (float at : { 0.12f, 0.6f, 1.2f, 1.6f, 2.0f, 2.2f, 2.6f, 3.3f })
            if (before < at && t >= at)
            {
                char line[48];
                snprintf(line, sizeof(line), "playerdeath look t%.2f", at);
                AutoTestLog(line);
            }
        if (before < kToResult - 0.1f && t >= kToResult - 0.1f) AutoTestLog("playerdeath done");
    }

    if (t >= kToResult) EndRun();
}
