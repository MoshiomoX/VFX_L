# Phase 4 収尾清单（Claude Code 用）

VFX_L — C++ / DirectX11 自制引擎の 3D roguelite。GPU gameplay 移行（`GPU_GAMEPLAY_PLAN.md`）の Phase 4 最終片付け。

---

## 0. 项目约束（先读，全程有效）

- C++ 注释**日语**，编译选项 `/utf-8` 全局生效。HLSL 纯 ASCII 无 BOM
- **本清单不需要改任何 `.hlsl` / `.hlsli`**。如果发现必须改，停下来报告，不要自作主张
- 不改任何 GPU 结构体布局：`Enemy` / `Projectile` / `Orb` / `FrameCB` / `AICB` / `OrbCB` / `SwarmCounters`
- 不新增 dispatch，不改 `DispatchStep` 的顺序
- 每个 CS `Dispatch` 之后必须有 `UnbindSRVs` + `UnbindUAVs`，经过时顺手核对
- `SwarmCounters` 里 `killCount / playerDamage / expTotal` 是 GPU 永久累加、CPU 取差分。**任何清理都不许清 counters buffer，也不许重置 `m_LastKillCount / m_LastDamageTotal / m_LastExpTotal`**
- 改动只限下面列出的文件
- 每完成一个任务 Build（Debug x64）一次，通过再做下一个
- 删源文件必须同步改 `.vcxproj` 和 `.vcxproj.filters`

---

## 任务 A：KillAll（Terrain Regenerate 时清 GPU 对象）

### A1. `Swarm/SwarmSystem.h`

public 区加两个声明：

```cpp
    // GPU 上の雑魚・弾・オーブを全部消す（地形の作り直し用）。
    // state を DEAD にするだけ。counter は触らない（累加値の差分が狂う）
    void KillAll();

    // 弾だけ消す（負荷テストのリセット用）
    void ClearProjectiles();
```

### A2. `Swarm/SwarmSystem.cpp`

实现，放在 `ConsumeExp` 后面：

- `KillAll`：`ClearUnorderedAccessViewUint` 清 `m_EnemyStateUAV` / `m_ProjStateUAV` / `m_OrbStateUAV`（`const UINT zero[4] = {0,0,0,0}`，写法同 `CreateBuffers` 末尾），然后 `m_PendingEnemies / m_PendingRecycles / m_PendingProjectiles` 三个队列 `clear()`
- `ClearProjectiles`：只清 `m_ProjStateUAV` + `m_PendingProjectiles.clear()`
- 不碰 `m_CounterUAV`、`m_EmitBudgetUAV`、`m_Accumulator`

### A3. `Scene/CollisionTestScene.cpp`

- Terrain 面板 `Regenerate` 按钮里，`m_Swarm.UploadTerrain(m_Grid);` **之前**加 `m_Swarm.KillAll();`
- 那段注释 `※GPU の雑魚は消せない（Phase 4 で KillAll を足す）…` 改成：「GPU 側は KillAll で全消し。counter は残るので撃破数などの累計は続く」
- Enemies 面板 `Respawn Elites` 按钮旁边加 `Kill All (GPU)` 按钮，调 `m_Swarm.KillAll()`

### A4. 验证

运行 → 等怪出来 → Terrain Regenerate → 怪、弹、球全部消失，`kills (total)` 不归零，之后 SpawnDirector 正常重新湧怪。

---

## 任务 B：摘除 CPU 版 ExpOrbSystem

### B1. 先 grep

全项目搜 `ExpOrbSystem`、`ExpOrbComponent`，列出所有引用。预期只有：
`Scene/CollisionTestScene.h/.cpp`、`Item/ExpOrbSystem.h/.cpp`、`Item/ExpOrbComponent.h`。
如果还有别的地方引用，**先报告再动**。

### B2. `Scene/CollisionTestScene.cpp`

- 删 `#include "Item/ExpOrbSystem.h"`、`#include "Item/ExpOrbComponent.h"`
- `UpdateGameplay` 里删 `// ---- 経験値オーブ ----` 那三行（含 `m_ExpOrbSystem.Update`）
- `UpdateGameplay` 里 `m_ManaSystem.Update` 之后那一大段被 `//` 注释掉的旧 CPU 投射物代码（AttachVFX 循环、`m_ProjectileSystem.Update`、命中イベント消费循环、`m_ProjectileVFXSystem.Update`）**整段删除**，替换成：

  ```cpp
    // 投射物の生成・移動・命中・撃破報酬は全部 GPU（SwarmSystem）。
    // CPU 側にはもう無い。精英の弾を CPU に戻す時はここに書く
  ```

- `UpdateGameplay` 头部「System の実行順」注释里，把 `→ 投射物 → VFX収集 → 経験値` 改成实际顺序：
  `操作 → 湧き依頼 → 衝突 → 物理 → 状態機 → 杖 → マナ結算 → レベル判定 → カメラ → GPU gameplay Flush → 粒子 Flush`
- `DrawStressPanel` 里删 `ImGui::Text("Exp Orbs : %d", m_ExpOrbSystem.GetOrbCount());`
- `DrawStressPanel` 里删整个 `if (ImGui::TreeNode("Exp Orb")) { … }` 块
- `SpawnElite` 里的 `ExpRewardComponent` **保留**（精英还是 CPU，将来用）

### B3. `Scene/CollisionTestScene.h`

删 `ExpOrbSystem m_ExpOrbSystem;` 成员及对应 include。

### B4. 删文件

`Item/ExpOrbSystem.h`、`Item/ExpOrbSystem.cpp`、`Item/ExpOrbComponent.h`。
从 `.vcxproj`（`ClCompile` / `ClInclude`）和 `.vcxproj.filters` 移除。
`Item/ExpRewardComponent.h` **不删**。

### B5. 验证

Build 通过，运行杀怪捡球，Exp 条正常涨。

---

## 任务 C：StressSpawnProjectiles 改走 GPU

背景：这个负荷测试的目的是撑满粒子池、测 EmitCS 的护栏。但它现在造的是 CPU 弹实体，`ProjectileSystem.Update` 已注释，那些球根本不动。改成往 `SwarmSystem::SpawnProjectile` 发。

### C1. `Scene/CollisionTestScene.cpp` `StressSpawnProjectiles`

整个函数体重写。保留随机方向的生成方式，每颗调：

```cpp
m_Swarm.SpawnProjectile(VFXId::Fireball, origin, dir * 8.0f, 1.0f, 0.25f, 30.0f);
```

不再 `m_Registry.Create()`，不再加 Transform / Collider / ProjectileComponent / ProjectileVisualComponent / ModelComponent，不再 `AttachVFX`。

`SwarmSystem::SpawnProjectile` 每帧上限 `kMaxSpawnProjPerFrame`，超出静默丢弃。`UpdateGameplay` 里每帧 50 的 batch 逻辑保留，但把 `50` 改成 `Swarm::kMaxSpawnProjPerFrame`（在 `Swarm/SwarmTypes.h`，先确认名字）。

### C2. `CountProjectiles()`

改成 `return (int)m_Swarm.GetCounters().aliveProjectiles;`。
函数头注释改成「GPU の存活数（回読なので 1〜2 フレーム古い）」。`const_cast` 那行删掉。

### C3. Stress 面板清理

- `Clear All` 按钮：删掉 Registry 遍历删实体的代码，改成 `m_Swarm.ClearProjectiles(); m_StressPending = 0;`
- `ApplyStressPreset` 开头遍历 `ProjectileComponent` 删实体的代码，同样改成 `m_Swarm.ClearProjectiles();`
- 删 `With Collider` / `With 3D Model` / `With VFX` 三个 Checkbox 及相关 `TextDisabled` / `TextColored` 提示（包括 `Auto Refill without VFX…` 和 `0 emitters: no VFX template…` 两条）
- 删 `ImGui::Text("Projectile VFX : %zu", m_ProjectileVFXSystem.GetActiveVFXCount());`
- 删「Alive count lives on the GPU only」「Judge by the screen…」两行 `TextDisabled`（现在有数字了）

### C4. `Scene/CollisionTestScene.h`

- 删成员：`m_StressModel`、`m_StressWithCollider`、`m_StressWithModel`、`m_StressWithVFX`、`m_StressVFXItem`
- `StressPreset` 结构体删 `withVFX / withCollider / withModel` 三个字段；`kStressPresets` 数组对应初始化项删掉；preset 的 `purpose` 文案里提到 collider / model / VFX 的改成只描述 target / batch
- `Init()` 里删 `m_StressModel = PrimitiveBuilder::CreateSphere(...)` 那行
- `m_ProjectileVFXSystem`、`m_ProjectileSystem`、`m_ProjectileRenderer` 三个成员**保留不动**（`RegisterItemVisuals` 还在用前者；后两者留给精英 / 广告牌）

### C5. 验证

Stress 面板 `+ Spawn` → Swarm 面板 `alive proj` 上升到对应数字，30 秒后回落；`Auto Refill` 开着能维持在 Target 附近；粒子面板 `Emitters` 有数。

---

## 任务 D：过期注释与文档

### D1. `Swarm/SwarmSystem.cpp`

- `DispatchEmit` 里 `※WriteBuffer の index は反射に出た cbuffer の順…` 那段注释和被注释掉的 `// m_EmitCS->WriteBuffer(m_Context, 1, …)` 删掉，换成：

  ```cpp
    // WriteBuffer の index はレジスタ番号。
    // この CS では SwarmFrameCB を b2 に逃がしてある（b0/b1 は ParticleCommon）
  ```

- `DispatchStep` 头注释改成实际顺序：
  `0 counter 清零 → 1 敵AI → 2 敵積分 → 3 弾積分 → 4 命中(+オーブ落下) → 5 照準 → 6 接触 → 7 オーブ吸引・取得`
- 第 5 段（AimResolve）上面 `// ---- 5) Hit 弹 × 怪` 改成 `// ---- 5) 照準: 最近傍の key → 位置/速度/距離 ----`
- `SpawnEnemy` 里 `Swarm::Enemy e;` 改 `Swarm::Enemy e = {};`，删掉手动赋 `velocity` / `yaw` 为 0 的两行（`= {}` 已覆盖）。`SpawnProjectile` 同样改 `Swarm::Projectile p = {};`
- `LoadShaders` 里 `Material::InitDefaultTextures(device);` 从 `if (m_EnemyVS && m_EnemyPS)` 块里提出来，放到 `// ---- 雑魚の本描画 ----` 之前单独一行
- `Render()` 里 orb 块 `m_OrbMaterial->Bind` 之后补 `m_Context->PSSetSamplers(0, 1, &samp);`（`samp` 变量提到函数开头）；`m_EnemyVS->UnbindSRVs` 挪到雑魚 draw 循环之后、orb 块之前

### D2. `Swarm/SwarmSystem.h`

- `m_OrbCS` 声明旁边确认有注释说明它是第 7 步
- `KillAll` / `ClearProjectiles` 注释见 A1

### D3. `Enemy/SpawnDirector.h`

头注释里如果有「逃亡流が封じられる / 遠くの敵は消える」类描述，改成实际行为：
「cap を超えた分は RecycleCS が rMax より遠い活きスロットを上書きする。cap は溢れ弁で、通常は届かない値にする」。措辞按现有注释风格。

### D4. `GPU_GAMEPLAY_PLAN.md`

Phase 4 全部勾上，写一行完成日期。Phase 5 项目不动。

---

## 最终验证（全部做完后）

1. Rebuild Debug x64，警告不能比改前多
2. 启动日志：`[SwarmSystem]` 下所有 CS / VS / PS 都是 `OK`
3. VS 输出窗口搜 `Forcing to NULL` / `HAZARD`，**必须一条都没有**
4. 跑 60 秒：怪追人、弹打怪、怪死掉球、球吸过来、Exp 涨、升级选项出现、HP 被怪碰掉
5. Terrain Regenerate 一次：全清、`kills` 不归零、怪重新湧
6. Stress `+ Spawn 500`：`alive proj` 涨、`Emitters` 涨、`Flush ms` 不飙
7. `git diff --stat` 只涉及本清单列出的文件

---

## 不要做的

- 不动 `Shader/` 目录
- 不动 `WeaponSystem` / `SpawnDirector.cpp` / `GPUReadback`
- 不动精英相关（`SpawnElite` / `RespawnElites` / `m_Elites` / `EliteTag`）
- 不动 `ProjectileRenderer`、`ProjectileVFXSystem`、`ProjectileSystem` 的源文件
- 不做广告牌渲染、不做 EnemyDatabase、不做 json（都是 Phase 5）
