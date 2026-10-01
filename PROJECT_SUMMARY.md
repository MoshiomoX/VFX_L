# VFX_L 项目整体总结（截至 2026-09-29）

C++ / DirectX 11 自制引擎上的 3D roguelite（幸存者类）。雑魚、投射物、经验球、范围攻击全部在 GPU 上跑，玩家和精英留在 CPU 的 ECS。
本文是面向「整体」的总结：项目走到了哪、由哪些部分组成、数字如何、还差什么。逐系统的操作细节和约束在 `CLAUDE.md`，这里不重复。

---

## 1. 规模与状态

| 项 | 值 |
|---|---|
| 代码 | 约 445 个源文件，C++ 约 6.5 万行，HLSL 约 7,700 行（不含 ImGui 等第三方） |
| 提交 | 60 个（2026-04-10 首次提交 → 2026-09-28），当前分支 `level-editor-pixel-enemies`，比 `main` 领先 19 个提交 |
| 未提交 | 84 处改动（见第 3 节末尾「9-28 未提交」） |
| 构建 | VS 18（v145）Debug / Release x64，MSBuild 命令见 `CLAUDE.md` 第 2 节 |
| 帧时间（seed 12345，关垂直同步，开局默认段） | Debug 约 4.5 ms（约 225 fps），Release 约 1.4 ms |
| GPU 雑魚上限 | `Swarm::kMaxEnemies` = 4096；压力测试 4000 只 + 2000 发弹 Release 5.7 ms |

游戏循环已经闭合：标题 → 战斗（刷怪、自动施法、经验球、升级三选一、报酬箱、背包摆放）→ 死亡「力尽きた」→ 结算。
仍是原型阶段：玩家 HP 写死 1000000（TEMP-TEST）、关卡编辑器的关卡未接进战斗、美术方向未定。

---

## 2. 架构一览

```
VFX_L/
├ Core/         Application / Game / Window（Esc 退出、Alt 菜单键屏蔽）
├ Graphics/     Renderer、Light（SceneLighting、ShadowMap、PointLightManager）、
│               Renderer/（SkyRenderer、StaticPropRenderer、GrassRenderer）、交换链
├ Shader/       Common（Lighting / Swarm / Sprite 共用 hlsli）、Swarm（40 个）、Grass、Sky、
│               Particle、PostProcess、Projectile、Skinning、UI、VFX
├ ECS/          Registry + System（RenderSystem、PhysicsSystem、RewardCrate、FeedbackVFX、Interaction…）
├ Component/    ECS 组件（Rigidbody、Backpack、Wand、Interactable、SkinnedAnim…）
├ Player/       PlayerFactory / PlayerControlSystem / PlayerAnimSystem（滑铲、上半身遮罩）
├ Camera/       FollowCamera（背后第三人称）+ BattleCamera（遮挡、震动、鼠标捕获、调参保存）
├ Collider/     CollisionSystem（AABB / 胶囊 / 凸多面体，固定物体 CSR 格子缓存）
├ Swarm/        SwarmSystem（GPU 群体：雑魚 / 弹 / 经验球 / 范围 / 特效 / 光）、ProjectileProfile、
│               SwarmVFXTable、FlowField、SwarmTypes（GPU 结构体，布局冻结）
├ Enemy/        MobSpawner（SpawnDirector、自爆兵比例）、EliteSpawner（CPU 精英）
├ Item/         ItemDatabase、ItemInfo（说明文自动生成）、ItemDataFile、Items/*.h（12 种道具）
├ UI/           HUD、GameUI、BackpackUI、LevelUpSystem、ItemSheetView、MenuList、UIDeco、ShapeSprite
├ Particle/     GPUParticleSystem、Mesh 粒子、ParticleSheets（多图集）
├ VFX_Editor/   特效编辑器（时间轴、Particle / Trail / Sprite / Light 条目）
├ World/        TerrainGenerator（随机野原：台地、坡道、高台、森林）
├ Scene/        Title、CollisionTestScene（战斗本体 + Debug 文件）、VFXEditor、ProjectileEditor、LevelEditor
├ Debug/        FrameProfiler、StressTestTools、DebugManager（F1〜F6）
├ Manager/      InputManager（Raw Input、手柄）、ResourceManager（模型缓存 + 后台预读）
└ Assets/       Data（Projectile / Area / Item / VFX json、Camera.json）、Model、Texture、Particles、Fonts、VFX
Tools/          素材生成脚本（字体、图标、UI 线稿、粒子图集、玩家模型拼装）
```

三层分工：

- **引擎层**：DX11 渲染（HDR 4x MSAA → resolve → bloom → 色调映射 + gamma）、flip 交换链、shader 反射绑定 CB、ECS、HSM 状态机、骨骼蒙皮（GPU SkinningCS）、碰撞与物理、ImGui 调试面板。
- **GPU 游戏层**（`GPU_GAMEPLAY_PLAN.md` Phase 0〜4 完成）：每帧固定顺序的 compute 链 —— counter 清零 → 空间哈希 → 敌 AI → 敌积分 → 重叠解除 → 弹积分 → 命中（+掉经验球）→ 瞄准 → 接触 → 经验球吸引。结果只以差分 counter（击杀、受伤、经验）回到 CPU。
- **CPU 游戏层**：玩家（移动、跳、滑铲、施法）、背包聚合（BackpackLogic → BackpackAggregateSystem → WeaponSystem）、升级 / 报酬箱、精英、UI、相机。

---

## 3. 时间线（按提交）

### 4〜6 月：框架
ImGui、shader 反射、HSM + ECS、PBR、骨骼蒙皮、天空盒、调试相机、cso 路径统一。

### 5 月：特效编辑器
发射器 / 粒子系统解耦、时间轴、特效保存。这是项目名 VFX_L 的来源。

### 7〜8 月：玩家与战斗雏形
玩家 + WeaponSystem + WandComponent、公告板粒子、UI、火球（FireballManager，CPU）。

### 9 月上旬：GPU 化
升级系统 / 经验球 / HUD / UIManager 接入；敌 AI；GPU 投射物（SwarmProj）→ 敌方弹；源码按解决方案筛选器整理目录。

### 9-16 〜 9-23：编辑器与弹道
模型带贴图读取；投射物编辑器（F4）：直线 / 曲线 / 追尾三种弹道、GPU 逐粒子条带、范围攻击、3D 手柄。

### 9-24
关卡编辑器（F6，KayKit Forest 素材，存 LevelData json）；像素雑魚（Kenney Blocky，刚体部件 idle / walk / attack 动画）；雑魚血条（并行 MaxHp buffer）；点光源 + 太阳光；`CLAUDE.md` 重写成项目指南；报酬箱（F / 手柄 B，开不升级的三选一）。

### 9-25 〜 9-26：表现与 UI
- Trail entry（特效位置驱动的 GPU 条带；顺带修了 indirect args 清零填错 InstanceCount）
- UI 大改：道具说明 / tooltip 自动出数值、HUD（计时、法术栏、低 HP 红边、屏外箭头）、暂停 / 死亡 / 标题、全部日文化、修 sprite 预乘 alpha
- Mesh 粒子读模型（16 模型，Lit / Glow，朝向速度）
- 战斗相机：隐藏光标 + Raw Input 视角、Alt 单按切光标
- 粒子多图集（Kenney 两套）、帧模式、半透明混合、GPU 多层 recipe；法术特效 4 弹 2 爆炸
- Sprite entry：序列帧（PVFX 41 个）在 CPU 特效和 GPU 范围两条路径都能播
- 战斗反馈特效：升级 / 开箱 / 受伤 / 两种命中
- 战斗场景拆分：完成的功能变成部品（BattleCamera、RewardCrateSystem、FeedbackVFXSystem、SceneLighting、EliteSpawner、MobSpawner、StressTestTools），调试 UI 独立成 `CollisionTestSceneDebug.cpp`

### 9-27 〜 9-28：地图、性能、画面
- **战斗地图**：随机 seed 的野原（台地 + 坡道 + 外围岩壁 + KayKit 森林），天空渐变 + 雾，雑魚认崖壁（坡度上限 + 流场绕路）
- **自爆兵**：第一种新雑魚（接触点燃 → 停下 → 爆炸只伤玩家），种类走并行 `EnemyExtra` buffer，脚下警告圈
- **经验球外观**：青蓝宝石、被吸引时倾斜拉伸、GPU 尾迹、拾取闪光
- **陨石**：弹道第 4 型 Drop（锁最近敌人、抛物线、落点警告圈）
- **装饰物实例化**：StaticPropRenderer，760 个装饰物从 9.6 ms 降到 0.35 ms（Debug）
- **斜坡滑铲**：左 Ctrl / 手柄 X，坡上加速、跳起保留惯性
- **高台**：一面 16〜20 度长坡三面崖，可叠二层（用户否掉四面坡）
- **太阳影子**：3 级级联 2048²，参数进 LightBuffer；GPU 雑魚用脚底圆影代替
- **草地**：GPU 低多边形草叶 + 风 + 踩倒，替换 450 个草模型
- **性能**：FrameProfiler 分段计时 + 4 项修正（碰撞只维护固定物体格子、地形外观合并成 1 个模型、调试绘制默认关、D3D 调试层默认关）→ **Debug 10.8 → 4.5 ms**
- **sRGB**：模型贴图在 shader 里解码，全场景默认开
- flip 交换链 + ALLOW_TEARING + 限帧
- 删掉根目录空的 `ManaSystem.cpp`（同名 obj 互相覆盖，蓝量时好时坏的根因）

### 9-28（f830fca「Add Visuals Assets」）
- **幻想 UI**（`UI/UIDeco`）：深色面板 + 双线 + 四角绳结 + 旋转魔法阵，按道具种类金 / 青 / 银；已换升级卡片、HUD、背包、tooltip、暂停、标题
- **字体**：Yuji Syuku 毛笔体（OFL），`Tools/BuildSpriteFont.ps1` 生成 7467 字
- **图标**：game-icons.net 白色剪影（CC BY），`Tools/BuildGameIcons.ps1`
- **玩家模型**：Quaternius 游侠（白胡子老头，CC0），`Tools/BuildPlayerModel.py` 用 Blender 从 4 个免费包拼成，24 段 UAL 动画，含 `Slide_Loop`
- **拡大鏡符文**：斜角 4 格，当たり判定 x1.5 / MP x1.3，GPU 端按半径比例放大外观
- **能力卡**：移動速度 +8%、跳躍力 +8%
- **相机**：调参 Save / Load（`Camera.json`）、FOV、速度演出、前瞻、滚轮调距离；修 InputManager 滚轮读不到
- **压力测试** `VFXL_BATTLE_AUTOTEST=stress` 和 UI / magnifier 自测钩子

### 9-29：玩家反馈 1〜4（`Plan.md`，做完再进平衡）
- **特效挡视线**：镜头默认 6m / 20 度 → 8m / 28 度；`Explosion` / `MeteorBlast` 的闪光、火团、烟减量缩小，烟寿命 x0.65
- **后退 / 横移**：免费 UAL 没有这些片段；试过程序化扭腰 + 倒放后，用户决定放弃「一直朝前」→ 按镜头方向判断前 / 右 / 后 / 左，身体转向移动方向（后退就转身朝镜头跑）
- **滑步感**：Walk / Jog / Sprint 按速度切换 + 播放速率 = 实速 / 片段原速 + 切换时继承步伐相位
- **树石太多**：树 110→60、石 40→20，小石头（和矮枯树）只当装饰不挡路
- 自测 `VFXL_BATTLE_AUTOTEST=loco`（7 个方向 + 爆炸视野截图）

### 9-29：平衡第一版（暂时全部模仿 Megabonk）
- 玩家 HP 100 + 回血 10/分、拾取 5m、开局火球直接装上；XP 曲线 30 + 15n + 1.5n²（1→2 级 3 只）
- 雑魚 15/15、自爆兵 18/30、火球爆炸 10
- 难度每分钟 +12%（HP 生成时定下），刷怪 1 → 8 只/秒，上限 550
- `Enemy/StageDirector`：10 分钟倒计时、3:00 / 8:00 精英（GPU 种类 2）、时间到「最終波」、Boss 门 → Boss（GPU 种类 3，回读 HP / 位置）→ 「ステージクリア」
- 自测 `balance`（跳时间看推移）、`boss`（门 → Boss → 通关）
- 新素材（用户选定）：地牢 = Quaternius Modular Ruins（CC0）；沙漠 = dglopez 西部沙漠 30 件（非 CC0，禁止再分发 → git 忽略）+ Quaternius Ultimate Nature 沙漠部分（CC0）+ Modular Ruins。顺带修了「无贴图 FBX 全白」（材质色烘进顶点色）和「贴图没写进 FBX 的 Rock-Set 全白」（按名字找同目录 / `<名>_Tex/` 的 Base_Color）；还没接进地形
- 9-30：新攻撃魔法「黄金の矢」（直线、威力 12、32 m/s）：特效里的弓箭模型 `gonjian.FBX` 做成金色 Mesh 粒子跟着 GPU 弹飞（新增「继承发射源速度」粒子选项）；自测 `arrow`
- 9-30：单体魔法出招 x0.75（每秒耗蓝不变）、新符文「加速のルーン」（左右 2 格、间隔 x0.75）；攻击魔法有了形状。用户指出「一直强化一种魔法就无敌」→ 定下**基础 / 高级魔法**：火球（取消爆炸）+ 新基础魔法石弾的影响格同时碰到陨石时，陨石在火球 / 石弾消失的地方落下（GPU 4b `SwarmProjEndCS` 报告带标签的弾消失位置 → 回读 → `WeaponSystem` 落陨石）。
- 9-30：背包右侧的魔法書改成木箱（代码生成的木框 / 内墙 / 锁扣贴图），箱内 2D 物理从「单个圆」换成按占位格的复合方块刚体（Box2D-lite 式，十字 / L 字按真实形状堆叠、静止后休眠）；道具图标放大（0.74 → 0.88）；自测 `chest`
- 9-30：**魔導光線**（用户要的「龟派气功」）：第二个高级魔法 = 追踪弾 + 弧驱动的范围型道具，从手朝弾消失的方向喷 1.2 秒的贯通光线。GPU 加胶囊型范围（`BeamCB` + 并行终点 buffer，Area 布局不变）、VFX 编辑器加 Beam entry（三层条带 + 噪声流动 + 根部伸缩）、`triggeredBy` 挪到 `ItemCommon`；自测 `beam`（伤害用光线才够得着的雑魚验证）
- 9-30：**第二、三关**（砂漠 / 遺跡）：同一战斗场景 + `World/StageConfig`（Biome 颜色表 + 自然物表 + 外围岩山 / 遗迹墙 + 火把点光源、照明 / 天空 / 雾预设、草、难度下駄）；打倒 Boss → 结算「次のステージへ」→ 保留背包 / 等级进下一关（`g_RunCarry`）；`VFXL_STAGE=n` 直接进。沙漠 / 地牢素材由此接进地形
- 9-30：清理两轮（CPU→GPU 旧路径、未注册测试场景、旧 GameObject 模型、无人 include 的头文件，共约 3,000 行；候选清单 `CLEANUP_CANDIDATES.md`）；雑魚卡墙修复（硬阻挡挪到玩家推力后、前瞻加体半径、对角格、MoveCS 沿墙滑 + 从墙里走出；自测 `stuck`）
- 9-30：最終波の幽霊（种类 4：雑魚 HP、2.6 倍速、穿墙飞过台地、半透明青白发光；时间到后 2 体/秒起每段 ×1.5；自测 `ghost`）
- 10-01：雑魚半个身子插进崖面 / 台地侧面（用户反馈 9-30 修完还会嵌墙）：移动和互推改成查体圆周（原始高度格判崖，精英 / Boss 按体格、上限 0.9m），碰到的往外退；自测 `clip`（修前崖 0.37m / 精英 0.73m → 修后全部 < 0.1m，坡道照样爬得上）
- 10-01：玩家被弾击退（雑魚 0.2 身位 ≈ 0.16m、自爆兵爆炸 0.6 身位 + 小上抛；GPU 接触 CS 记录打击方向 → 回读 → `PlayerControlSystem::ApplyKnockback`；自测 `knock` 0.17m / 0.49m）
- 外围岩壁的外观换成岩山：KayKit 大岩石放大堆 3 排（8〜34m，约 400 块，实例化），碰撞仍是原来的箱子；自测 `edge`
- 追加：升级 4 选 1 + 能力卡权重 0.35；新卡「魔力回復 +20%」「跳躍回数 +1」（空中每次 ×0.75：12 → 9 → 6.75）；开局法术改成追踪弹（火球太强）；场上磁铁（`ECS/System/PickupSystem`，碰到吸全图经验球）；自测 `pickup`

---

## 4. 各系统现状

| 系统 | 状态 | 备注 |
|---|---|---|
| GPU 雑魚（AI、积分、推挤、流场） | 完成 | 空间哈希 + 3x3 分离；流场 `FlowField` 绕坡道 |
| 雑魚种类 | 自爆兵、精英、Boss（9-29） | 种类在并行 buffer，`Enemy` 布局未动；精英 / Boss 是放大染色的雑魚，Boss 没有专属攻击 |
| 关卡流程 | 10 分钟 + 精英 + 最終波 + Boss 门（9-29） | `Enemy/StageDirector`；数值照 Megabonk，缺的自定。9-30 起三关（草原 → 砂漠 → 遺跡，`World/StageConfig`），打倒 Boss 后保留背包进下一关 |
| GPU 投射物 | 4 种弹道（直线 / 曲线 / 追尾 / 陨石） | profile json = 弹本身，Item = 怎么放 |
| 范围攻击 | 完成 | 火球爆炸、陨石爆炸、命中特效范围（伤害 0） |
| 经验球 | 完成 | 外观 + 尾迹 + 拾取特效 |
| 精英（CPU） | 靶子级别 | KayKit Skeleton Warrior，HP 归零燃烧消散 |
| 玩家 | 移动 / 跳 / 滑铲 / 自动施法 / 施法暂停 | Quaternius 游侠模型；Walk / Jog / Sprint，身体转向移动方向（9-29） |
| 背包 | 9x9，形状编辑器 | 道具 18 种：6 弹（9-30 加黄金の矢、石弾；陨石是高级魔法，要火球 + 石弾驱动）+ 1 范围型（魔導光線，高级，追踪弾 + 弧驱动）、2 符文 + 拡大鏡 + 加速のルーン（9-30）、扩张枠、6 能力卡（9-29 加魔力回復、跳躍回数）；升级 4 选 1 |
| 升级 / 报酬箱 | 完成 | 三选一共用 |
| 地形 | 随机野原 × 3 种外观 | 台地、坡道、高台三关共用；草原 = 森林 + 草地 + 岩山，砂漠 = 棕榈 / 枯树 / 仙人掌 + 岩山，遺跡 = 遗迹墙 + 石柱 + 火把 + 夜（9-30）；关卡编辑器的关卡未接入 |
| 光照 / 影子 / 雾 / 天空 | 完成 | 级联影子 + 脚底圆影；sRGB 解码 |
| UI | 幻想风格 | 结算画面、「[F] Open」、「力尽きた」幕未换 |
| 特效编辑器 | Particle / Trail / Sprite / Mesh / Light 条目 | GPU 弹道读全部粒子层 |
| 相机 | 完成 | 遮挡拉近、震动、速度演出、调参保存 |
| 性能工具 | FrameProfiler、压力 / perf 自测 | 数字见第 6 节 |
| Credits 画面 | 未做 | CC BY 图标要求署名，发布前必须补 |

---

## 5. 数据与素材

**数据文件**（`Assets/Data/`）：`ProjectileData/`（ArcOnce、Fireball、HomingFull、Meteor）、`AreaData/`（Explosion、MeteorBlast、ArcSpark、VoidPop、BomberBlast、FireCircle）、`ItemData/`（道具形状覆盖）、`VFXData/`（特效 json）、`LevelData/`（关卡编辑器）、`Camera.json`。

**素材许可**（全部可商用）：

| 来源 | 许可 | 用途 |
|---|---|---|
| Kenney Blocky Characters / Retro Fantasy / Particle Pack / Smoke | CC0 | 雑魚、自爆兵、木箱、粒子图集 |
| KayKit Forest / Skeletons / Character Animations | CC0 | 森林、精英、备用动画 |
| Quaternius UBC / Modular Outfits / UAL 1+2 | CC0 | 玩家模型与动画 |
| PVFX Foundry 序列帧 | CC0 | Sprite entry |
| UI 线稿原图 | CC0 / 公有领域 | `Assets/Texture/UI/Deco/`（出处见同目录 README） |
| Yuji Syuku 字体 | OFL | UI 文字 |
| game-icons.net | **CC BY 3.0** | 道具图标（每个作者写进 README，需 Credits 画面） |

**生成脚本**（`Tools/`）：`BuildSpriteFont.ps1`、`BuildGameIcons.ps1`、`BuildUIDeco.ps1`、`BuildParticleSheets.ps1`、`BuildPlayerModel.py`（Blender 后台）。素材都不手改，改表重跑。

---

## 6. 性能数字（seed 12345，关垂直同步）

| 场景 | Debug | Release |
|---|---|---|
| 开局默认段（9-28 优化前） | 10.8 ms | 1.49 ms |
| 开局默认段（优化后） | **4.5 ms** | **1.40 ms** |
| 4000 只雑魚 | 5.2 ms | 3.14 ms |
| 4000 只 + 2000 发弹 | 6.0 ms | 5.69 ms |
| 太阳影子开销 | +1.1 ms | 未单测 |
| 草地开销 | +0.4 ms | +0.03 ms |

Debug 剩下的大头：影子 1.0、resolve + bloom + ImGui 0.75、ECS 模型 0.56、调试面板 0.31。Release 全程 GPU 瓶颈（CPU < 0.5 ms），4000 只时雑魚绘制 1.5 ms 是最大项。

---

## 7. 已知问题与待办

**已知问题**
- 第一次刷出雑魚时有一次 13〜20 ms 卡顿，原因未查。
- 4000 只时血条铺满画面；2000 发弹时火焰 + bloom 刷白。
- 自爆兵警告圈在默认背后镜头下基本看不到（镜头问题，待定）。
- 雑魚脚底圆影被草叶挡住大半。
- `CreateSphere` / `CreateCapsule` 绕序疑似反了（未验证）。
- Paladin FBX 子网格名字错位（资产问题，用户决定不处理）。
- `Debug/` 目录被 `.gitignore` 的 `[Dd]ebug/` 挡住，新文件要 `git add -f`；`Gizmo.cpp/h` 一直没被跟踪。

**TEMP-TEST 残留**：`[crowd]` / `[flow]` 日志、`Game.cpp` 的 TEMP-TEST 标记、各环境变量自测钩子（见 `CLAUDE.md` 第 5 节）。

**候选（未定）**
- （9-28 的改动已在 f830fca 提交）
- 战斗场景读取关卡编辑器的关卡
- Phase 5（SpawnDirector GPU 化、VAT 雑魚动画等）
- Credits 画面
- 结算 / 提示 / 死亡幕换成幻想 UI
- 美术方向（卡通渲染等）定了以后替换临时模型
- 玩家施法特效方案

---

## 8. 文档索引

| 文件 | 内容 |
|---|---|
| `CLAUDE.md` | 给 Claude Code 的工作说明：约束、构建、场景、各系统细节、自测钩子 |
| `PROJECT_SUMMARY.md` | 本文，整体总结 |
| `GPU_GAMEPLAY_PLAN.md` | GPU 游戏管线移行计划（Phase 0〜4 完成，5 未开始） |
| `PHASE4_CLEANUP.md` | Phase 4 收尾清单（已全部完成） |
| `MESH_PARTICLE_PLAN.md` | Mesh 粒子实施计划（2026-09-18 定稿，已实现） |
| `Assets/Texture/UI/Icons/README.txt`、`Deco/README` | 图标作者署名、UI 线稿出处 |
