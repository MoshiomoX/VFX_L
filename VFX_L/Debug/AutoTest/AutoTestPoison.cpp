// ============================================================
// AutoTestPoison.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=poison
// VFXL_BATTLE_AUTOTEST=poison：毒が最寄りの敵の足元へ落ちるか・池で敵が遅くなるか・池のダメージが入るか
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPoison final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

// ============================================================
// TEMP-TEST: 毒（VFXL_BATTLE_AUTOTEST=poison、2026-10-01）
// 1 秒: 湧き停止・全消し・無敵・MP 無限・入力 0、バックパックは毒だけ（3x3 の中央）。プレイヤーの +Z 17〜30m に雑魚 16 体
//   （2 列、HP 1000、3.5 m/s）、31m にエリート 1 体（HP 2000、3.4 m/s で見分ける）。プレイヤーは動かない。
//   射程 15m に入った最寄りの敵の足元へ毒が落ち、後ろから来る敵がその池を通る。
// 0.25 秒毎に DebugReadEnemies（Map で GPU を待つ。自動テスト専用）で
//   `poison t alive free slowed ratio elite hp areas`：free = プレイヤーから 3m 以上離れた雑魚、
//   slowed = その中で 水平速度 / moveSpeed < 0.8 の数、ratio = slowed の平均（40% 減速なら 0.6 前後）、
//   elite = エリートの 速度 / moveSpeed（プレイヤーの近く・死亡は -1。池の中なら 0.8 前後）、hp = 全員の HP 合計（池で減る）
// カメラはプレイヤーの後ろ 16m・俯角 40°。3.5 / 5 / 6.5 秒 `poison look <n>`（外から撮る）、10 秒 done
// ============================================================
void AutoTestPoison::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    m_PlayerControlSystem.testInput = true;
    m_PlayerControlSystem.testMove = Vector2::Zero;
    static float s_NextLog = 0.0f;
    static int s_Looks = 0;
    static bool s_PoisonClose = false;   // VFXL_POISON_CLOSE: 近くに落として液溜まりを近景で撮る
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const float gy = m_Swarm.GetAIParams().groundY;
        char envBuf[8];
        s_PoisonClose = GetEnvironmentVariableA("VFXL_POISON_CLOSE", envBuf, sizeof(envBuf)) > 0;
        if (s_PoisonClose)
        {
            // 正面 6〜9m にほとんど動かない雑魚 8 体（毒は一番近い敵の足元に落ちる）
            for (int k = 0; k < 8; ++k)
                m_Swarm.SpawnEnemy(Vector3(pp.x + ((k % 2) ? 1.2f : -1.2f), gy, pp.z + 6.0f + (float)(k / 2) * 1.0f),
                    1000.0f, 0.2f);
        }
        else
        {
            for (int k = 0; k < 16; ++k)
                m_Swarm.SpawnEnemy(Vector3(pp.x + ((k % 2) ? 0.8f : -0.8f), gy, pp.z + 17.0f + (float)(k / 2) * 1.6f),
                    1000.0f, 3.5f);
            m_Swarm.SpawnEnemy(Vector3(pp.x, gy, pp.z + 31.0f), 2000.0f, 3.4f, Swarm::kEnemyKindElite);
        }
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Poison, lo + 1, lo + 1, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.distance = s_PoisonClose ? 9.0f : 16.0f;   // 近景は普段のカメラ（8m）に近い距離
        cam.SetPitch(s_PoisonClose ? 34.0f : 40.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog(s_PoisonClose ? "poison start (close): 8 mobs standing at +Z 6-9m, looks every 0.5s from 2.5s, liquid on/off alternately"
                                  : "poison start: 16 mobs (3.5 m/s) + 1 elite (3.4 m/s) walking in from +Z 17-31m");
        s_NextLog = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog = m_AutoTime + 0.25f;
        std::vector<Swarm::Enemy> enemies;
        std::vector<uint32_t> states;
        if (m_Swarm.DebugReadEnemies(enemies, states))
        {
            int alive = 0, freeN = 0, slowed = 0;
            float ratioSum = 0.0f, elite = -1.0f, hp = 0.0f;
            for (size_t i = 0; i < enemies.size(); ++i)
            {
                if (states[i] == Swarm::kStateDead) continue;
                const Swarm::Enemy& e = enemies[i];
                ++alive;
                hp += Swarm::HpFromFixed(e.hp);
                const float dx = e.position.x - pp.x, dz = e.position.z - pp.z;
                if (dx * dx + dz * dz < 3.0f * 3.0f) continue;   // プレイヤーに詰まっている（押し合いで遅い）
                const float speed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
                const float r = speed / (std::max)(e.moveSpeed, 0.01f);
                if (std::fabs(e.moveSpeed - 3.4f) < 1e-3f) { elite = r; continue; }
                ++freeN;
                if (r < 0.8f) { ++slowed; ratioSum += r; }
            }
            char line[200];
            snprintf(line, sizeof(line), "poison t %.2f alive %d free %d slowed %d ratio %.2f elite %.2f hp %.0f areas %u",
                m_AutoTime, alive, freeN, slowed, slowed ? ratioSum / slowed : 0.0f, elite, hp,
                m_Swarm.GetCounters().aliveAreas);
            AutoTestLog(line);
        }
    }
    if (s_PoisonClose)
    {
        // 近景（VFXL_POISON_CLOSE、2026-10-02 液溜まりが明るすぎる件）: 2.5 秒から 0.5 秒毎に撮る。
        // 液溜まり（Liquid entry）を 1 枚毎に入 / 切して、他の層（泡・毒霧・点光源）と見比べる。
        // 切り替えは撮る 0.15 秒前（描画に反映されてから log を書く）
        const int k = (int)std::floor((m_AutoTime - 2.5f + 0.15f) / 0.5f);
        if (k >= 0 && k < 10)
            m_Swarm.liquids = (k % 2) == 0;
        if (s_Looks < 10 && m_AutoTime >= 2.5f + 0.5f * (float)s_Looks)
        {
            char line[48];
            snprintf(line, sizeof(line), "poison look %d liquid %s", s_Looks, m_Swarm.liquids ? "on" : "off");
            ++s_Looks;
            AutoTestLog(line);
        }
    }
    else
    {
        const float looks[] = { 3.5f, 5.0f, 6.5f };
        if (s_Looks < 3 && m_AutoTime >= looks[s_Looks])
        {
            char line[32];
            snprintf(line, sizeof(line), "poison look %d", s_Looks++);
            AutoTestLog(line);
        }
    }
    if (m_AutoTime >= 10.0f)
    {
        m_Swarm.liquids = true;
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("poison done");
        m_AutoStep = 2;
    }
}

REGISTER_BATTLE_AUTOTEST("poison", AutoTestPoison)
