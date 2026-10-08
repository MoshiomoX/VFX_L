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
| 帧时间（seed 12345，关垂直同步，开局默认段） | 10-03：Debug 约 5.5 ms（约 183 fps），Release 约 2.1 ms（约 474 fps，GPU 瓶颈）。9-28 时 Debug 4.5 / Release 1.4 |
| GPU 雑魚上限 | `Swarm::kMaxEnemies` = 4096；压力测试 4000 只 + 2000 发弹 Release 5.7 ms |

游戏循环已经闭合：标题 → 战斗（刷怪、自动施法、经验球、升级三选一、报酬箱、背包摆放）→ 死亡「力尽きた」→ 结算。
仍是原型阶段：玩家 HP 写死 1000000（TEMP-TEST）、关卡编辑器的关卡未接进战斗、美术方向未定。

---

## 2. 架构一览

```
VFX_L/
├ Core/         Application / Game / Window（Alt 菜单键屏蔽）
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
- **树石太多**：树 110→60、石 40→20，小石头（和矮枯树）只当装饰不挡路；10-03 再减半（树 30、石 10、灌木 80、矿洞岩 7）
- 自测 `VFXL_BATTLE_AUTOTEST=loco`（7 个方向 + 爆炸视野截图）

### 9-29：平衡第一版（暂时全部模仿 Megabonk）
- 玩家 HP 100 + 回血 10/分、拾取 5m、开局火球直接装上；XP 曲线 30 + 15n + 1.5n²（1→2 级 3 只）
- 雑魚 15/15、自爆兵 18/30、火球爆炸 10
- 难度每分钟 +12%（HP 生成时定下），刷怪 1 → 8 只/秒，上限 550（10-04 起换成按分钟的难度表，见下面的时间线）
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
- 10-01：台地上的雑魚直接从边缘落下去追崖下的玩家（不再绕坡道）：流场和 GPU 移动的崖改成单向（玩家低 1m 以上时可下），按重力下落；自测 `drop`
- 10-01：魔導光線追踪目标：GPU 锁定弾消失点的雑魚，它死了就换射程内离光线方向最近的一只，光线按 90 度/秒转过去；自测 `beamtrack`
- 10-01：Q 键从「施法暂停」改成「魔力解放」：3 秒不扣蓝、30 秒冷却，MP 条变金色 + 倒计时，玩家身上金色爆发 + 持续光环、画面金色边框；自测 `surge`
- 10-01：新基础魔法「毒」：Lob 弹道（山なりに最寄りの敵へ、途中不命中）+ 毒池（4 秒、0.5 秒 3 伤害、减速 40%，精英 / Boss 减半，GPU 并行 buffer `m_EnemySlowBuffer`）；`Assets/VFX/Mesh` 102 个特效模型一览（README + 缩略图 + `Tools/MeshCatalog*`）；修了中文贴图路径让模型加载卡死；自测 `poison`
- 10-02：被弾击退整体 ×1.5；魔力解放中詠唱 ×1.5（计时器加速，含高级魔法冷却和光线溜め）、持续型范围 ×1.5（毒池、光线，按施放时判定，GPU 每发弹带倍率 `m_ProjBoostBuffer`，光线特效时间轴跟着重映射）；新能力卡「魔法威力 +10%」（复利，乘进弹 / 爆炸 / 毒池 / 光线，背包说明同步）；用户已实机看过
- 10-02：VFX 新条目 **Liquid**（SDF 液滴 + 平滑并集的地面液面，CPU / GPU 两条路共用 `VFXLiquidPS`，GPU 范围按投掷方向溅开），毒池的水面片换成它；附代码讲解文章；用户已实机看过。用户反馈太亮 → 近景自测查明是毒池的点光源照草，已压低
- 10-02：雑魚死亡改成「方块碎裂飞散」（GPU：上帧活 / 这帧死 → 尸体环 512，部件按公式飞散、弹一次、缩小；脚下土烟 MobDeath）；自测 `death`；用户已实机看过
- 10-02：**场地 200m → 300m，分山顶 / 平原 / 矿洞三层**（用户选「有顶矿洞（不重叠）+ 对角布局」，第一步 = 露天的坑）：对角两隅 = +16m 山顶（3 条 10m 宽的长坡，滑铲 20 m/s）和 −10m 矿坑（2 条下行坡，坑底放岩石、不长草）；地面改成按格高度的阶梯网格（竖墙画成地层条纹），碰撞 = 按格分矩形的箱；内容数量 ×2.25；报酬箱 近 2 + 山顶 2 + 矿洞 2，Boss 门在矿洞最深处；刷怪在地面高度生成。流场改成后台线程算全图（Debug 13ms 不再卡主线程），并按 0.5m 原始高度判断坡道侧面的台阶（以前雑魚贴在坡侧上不去，修后 16 秒 29/30 爬上山顶）；自测 `layers`。洞顶（第二步）未做
- 10-03：用户嫌 300m 太大 → 改回 200m，高低差不变、水平全部 ×2/3（山顶 / 矿洞 33 格，坡变陡：山顶 26〜31 度、矿洞 31 度），内容数量回到原来的 200m；Debug fps 120 → 160，流场全图 6ms，爬山 10 秒 30/30
- 10-03：矿洞加顶（第二步，用户「按推荐」）：坑周一圈岩壁（挡格，只能走坡道进出）+ 5〜12m 的洞顶 + 洞口过梁（洞内 15m、洞口 5m），顶上按离边距离堆 8〜16m 的岩石成矮山，看不见的碰撞到 40m（上不去），坑壁每 12m 一支火把（洞内灯半径 10m，防止穿墙照到洞外），陨石落点在洞里时从正上方落下；自测进洞 20 秒 30/30；`VFXL_NO_GRASS` 失效已修
- 10-03：地面的格子线（9-30 起一直记为已知问题）查明是 `DebugManager` 默认开着的 y = 0 参照网格（±50m、1m 间距），和地面深度打架；战斗场景进入时关掉、离开时恢复
- 10-03：Boss 门换成石拱（遗迹包 `Arch_Round_RoundColumn`，5.6m，柱子有碰撞）+ 门洞里的粒子漩涡（`BossPortal.json`：环沿、往中心吸的火花、旋转的螺旋贴图、光团、星点、紫雾、点光源；`VFXEffect::RotateYaw` 让特效跟门的朝向转），召唤 Boss 后漩涡熄灭；用户选的「圆拱带柱 + 纯粒子漩涡」；自测 `portal`
- 10-03：外围岩石穿模（第一排岩石伸进场地 3〜4m 且没有碰撞）：用户要「稍微往后挪 + 加碰撞」→ 第一排按旋转包围盒只伸进 −0.6〜1m，伸进 0.35m 以上的加凸体碰撞（约 90 个）+ 封雑魚格子；外围墙格改成长草、场地外加 40m 地面；遗迹方柱同样处理。碰撞系统改成固定碰撞体缓存（以前每帧重拷所有凸体），加岩石碰撞后 Debug 只多约 0.1 ms；自测 `edgerock`
- 10-03：复测 fps：Debug 7.76 ms / Release 2.11 ms（GPU 瓶颈）。Debug 慢的主因是 ECS View 总扫 Transform 池（约 1500 实体），渲染每帧扫 4 遍 → `View::EachFrom<Base>` + `RenderSystem` 每帧收集一次 → Debug 5.48 ms（183 fps）；自然物减半后 5.28 ms（189 fps）
- 10-03：自然物减半（树 30、石 10、灌木 80、矿洞岩 7）；矿洞洞口加大（用户：Boss 太大出不来）：坡宽 4→8m、洞口高 5→8m、洞顶 8〜15m（洞内 18m），自测 `bossexit`
- 10-03：雑魚 / 精英 / Boss 的脚陷在地里（用户发现 Boss 的脚穿模）：部件动画按走路张腿那帧对齐脚底，站着时脚在地下 0.29m × 体型倍率 → 改按 idle 姿势对齐；顺带修了 assimp 在攻击动画里插的翻转 180 度的坏旋转键（攻击正中间整个身体沉进地里）
- 10-03：实时通し检查 soak（3 面 × 10 分钟）：修了穿过报酬箱 / Boss 门柱（封格，门柱碰撞原来只有 5mm）、伸进外围岩石（封格规则改严）、困在封闭口袋（流场反算后封掉）、墙角卡住 / 原地打转（MoveCS 墙角横移 + AI 两侧探测）、贴坑壁被抬起（洞壁格高度场）；同种子前 5 分钟 stuck 14→4、穿过物体 8→0；最后一批修改未实测（验证测试因内存不足被停）
- 10-03：镜头震动修正（用户：特效拉满时画面一直震）：以前命中火花和死亡土烟也算「范围出现」并且累加，trauma 顶满；现在只有爆炸 / 光线（范围 json `cameraShake`）会震，且改成「至少抬到」不再累加
- 10-03：**加声音**（用户选 miniaudio、音效 + BGM、CC0 素材）：`Audio/AudioSystem`（cue 数据驱动 `Sounds.json`、节流、BGM 交叉淡入淡出、4 组音量）+ `Audio/BattleAudio`（GPU 命中 / 爆炸 / 击杀按特效配方计数后播放、玩家动作、BGM 选曲）+ 各处 UI 音效；素材 Kenney / rubberduck / lentikula / NIIIEMAND + OpenGameArt 6 首 BGM，全 CC0，约 33MB；具体用哪个文件待用户试听
- 10-03：声音第 2 / 3 版：BGM 先换成激烈的战斗曲，用户听后改为「关卡 = 自然环境的主题曲，Boss 出场更激昂」→ 关卡回到 Grasslands / Negev Desert / Ruined City，最終波 Flags，Boss 换金属的 Boss Battle Theme + 咆哮 + 低频冲击音 + 0.05 秒硬切；UI 音换成木头 / 书 / 皮革的材质声；11 首曲子重编码成无缝循环（`loopStart`）；出口加限幅器（乱战混音峰值 1.55 → 输出 ≤1.0，以前 72% 的帧爆音）；新自测 `music`（不静音：逐曲 / 循环接缝 / 逐 cue / 乱战 / 切歌，WASAPI 会话电平表测实际输出）
- 10-03：敌人第一批（用户从 4 种里选）：**分裂怪**（黄色假人，HP 30，死后分成 3 只小分裂体；GPU 尸体追踪写分裂环 → CPU 回读后生成；每关主力 + 按时间增加，第 1 面 1:00 起 10% → 8:00 25%）+ **Boss 重击预警圈**（`Enemy/BossAttacks`：每 6 秒（半血后 4 秒）在玩家脚下连放 3 个红圈，1.2 秒后炸，跑动 / 跳起可躲；圈 = 贴地网格的 `SwarmWarnRingVS`，爆炸 = 场景里的 KayKit 石头从地里拱出）。自测 `split` / `bossslam`
- 10-03：**地面贴图**（用户：地面是纯色）：3dtextures.me 的风格化贴图 7 套（CC0），`TerrainSurface` + `TerrainPS`：5 层（地面 / 道路 / 崖 / 洞底 / 岩）按法线、高度或顶点指定选层，三向投影 + 法线贴图，亮度对齐原配色、保留原色斑明暗；三关各一组（草原草地 / 砂漠沙 / 遺跡石板，洞底裂石共用）。自测 `ground`
- 10-04：**基础魔法按强度换成多格形状**（用户：「小方块法术都换成各种形状，按强度决定，不用问」）：追踪弾 横 2、石弾 L 3、弧 Z 4、火球 2×2、毒 凸字 4、黄金の矢 竖 2→3（越强越大、越难摆）；影响格从固定十字改成「形状的上下左右一圈」（`ItemShape::Around4`），高级魔法的诱发规则不变。开局 3×3 框里放得下追踪弾，陨石 + 火球 + 石弾（12 格）要扩张框。顺带修了魔法书木箱里凹形道具一直不休眠（迭代 10→20 + 静止 1 秒后阻尼收敛）。自测 `shapes`
- 10-04：魔力解放冷却 30→20 秒；左上 MP 条下的状态文字改成**技能图标**（用户参考艾尔登法环 黑夜君临，选「大图标在法术栏上方」）：法术栏正上方 72px 的大圆格 + 星形魔法阵 + 「Q」小牌，可用 / 解放中（金光 + 剩余秒）/ 冷却（遮暗 + 剩余秒）/ 恢复瞬间闪光四种状态。自测 `surge` 加了 ready / flash 两张截图
- 10-04：**平衡：难度曲线 + 回蓝**（用户：1〜2 分钟太简单、6 分钟后离谱）。新自测 `curve`（bot 自动玩 8 分钟，记场上数量、击杀、实际受伤、MP）量出两个原因：① 刷怪 / HP / 伤害都直线涨，乘起来是平方级；② 玩家输出被蓝锁死（回蓝 25/秒，魔法全开要 70〜200，平均 MP 4%，拿再多魔法击杀也停在每秒 1.3 只）。改成按分钟的难度表 `Enemy/DifficultyCurve`（刷怪 2→5.2/秒，HP 10 分钟 1.75 倍，伤害只涨到 1.25 倍，面板可逐点调、存 `Difficulty.json`）+ 回蓝每级 +10%。bot 实测「100 HP 撑几秒」：1 分钟 ∞ → 20、4 分钟 2.6 → 14、6 分钟 2.0 → 17、7:30 1.5 → 4.6；6 分钟场上 552（满）→ 95
- 10-04：**金币（最小版）**（用户问要不要做土豆兄弟的金币；建议 Megabonk 式，用户选推荐）：经验球同时算金币（雑魚 1 枚）；报酬箱 12 个、要花钱开（20 起每开 ×1.3，每关重算）；4 选 1 可以付费刷新（10 起每次 +10，选完归零）；HUD 显示金币，带到下一关。自测 `gold`。关卡间商店是下一步
- 10-04：**卡通渲染**（用户选两阶柔边 + 冷色暗部、全屏描边、全部物体）：共用光照 `ShadeToon`（两阶、冷色暗部、近处硬高光 + 边缘光），所有走 `Lighting.hlsli` 的东西一起变；描边 = 深度后处理（1/z 平面预测、只描靠前一侧、乘法压暗、2px），插在不透明物之后、草之前（雑魚绘制拆成两段）；深度缓冲改成可读。Lighting 面板「Toon Shading」+「Toon Outline」，`VFXL_NO_TOON / VFXL_NO_OUTLINE` 对比
- 10-04：**粒子卡通化**：公告板粒子加每个发射器的「Toon」开关（模糊形状 + 硬边 + 1〜3 阶色 + 内侧描边，寿命减少 = 形状缩小），塞进 renderMode 空闲位；爆炸火团 / 黑烟、死亡土烟、毒雾、扬尘已开（土烟和毒雾换成圆团烟贴图）；发光类不开。受光的模型粒子本来就走卡通光照
- 10-04：卡通渲染补完：受光的模型粒子提到描边之前画（石块、箭身有线）；光线条目加卡通开关（三层硬边平涂带 + 两档流光，魔導光線已开）；毒池液面跟随场景卡通开关（两色分块、两阶光、硬高光、反光带、外缘深色线）。**Esc 不再直接退出程序**（用户要求），10-05 起 Esc = 暂停菜单的开关（原来是 P）
- 10-04：**试用用户的 PBR 人形 Shadowkin**（Reallusion CC 骨骼，和游侠的 UE 式骨骼名字不同）：新的骨骼名对应表 `BoneMaps.h` + 世界空间重定向（`SkinnedModel::RetargetAnimations`：以蒙皮时的姿势为基准，按骨骼方向对齐两边的基准姿势，腰的平移按腿长缩放），游侠的 24 段动画全部能用；顺带修了 bind 检查（「节点默认姿势 ≠ 蒙皮姿势」时不再重建 offset，以前会把爪子甩成长线）。`loco` 自测站立 / 跑 / 转身 / 施法都正常。用户看过后选「换成默认」：默认玩家改成 Shadowkin（贴图 3〜4K → 2K，94 → 25MB），`VFXL_PLAYER=ranger` 可换回
- 10-04：**斗篷物理**（用户选 Verlet 骨链）：`ClothChainSystem` 把斗篷的 8 节骨链挂到上背，按重力 + 空气阻力 + 风 + 往原形状轻拉模拟，和躯干 / 腿的胶囊碰撞（忽略左右方向，横向偏开的腿也能挡到布）、和地面碰撞；跑步后扬 30〜40°，急停落回，跳起往下拖。自测 `cape`
- 10-04：**地面起伏**（用户：「取消纯平地，改成山丘斜坡 + 小凸包」，选「缓丘 + 小凸包」）：平原和山顶顶面加梯度噪声的缓丘（高差约 4m）+ 80 个土包，台地 / 高台 / 坡道脚下用垫子压到当地平均高度后整体抬降，矿洞周围压平；地面网格改成每格 2×2 的起伏网格；新碰撞形状「高度场」（竖直推出 + 下坡吸附，镜头和光线的射线也会被丘挡住）。实测坡度中位数 3〜4°、90% 分位 10〜11°；玩家绕圈跑 0 次离地，雑魚 0 次陷地 / 浮空，3 分钟 soak 没有新问题，Debug 帧时间不变。自测 `hills`
- 10-05：起伏加大（用户：「起伏再大一点，坡也再陡一些」）：高差 4 → 6.5m，坡度中位数 3.4 → 5°、90% 分位 10.5 → 15.5°，土包更大；加坡度限制器（最陡压在 39° 以内，雑魚能走 40°）。soak 发现雑魚卡在台地角 / 坡道侧边，查出流场斜向「不切角」少查了两段（以前就有的漏洞，起伏把它放大了），补上后卡住 5 → 0
- 10-07：**シールド**（用户：「跟 Megabonk 一样」）：先扣盾再扣血，盾还有就整个一击都由盾吃掉（溢出作废，挡致命一击）；5 秒没受伤后 2 秒回满；开局 25（用户定）+ 升级能力卡「最大シールド +25」；HUD 在 HP 条下方加细蓝条（满亮水色 / 没满暗蓝，回满闪一下）；有盾时身上常驻一层浅蓝六角护罩（受击瞬间变红再褪回）、被打空时护罩碎片飞散（用特效模型里现成的护罩 / 碎罩，用户指定）+ 力场音 / 玻璃碎裂音；带到下一关。自测 `shield`
- 10-07：**トレーニング**（标题菜单新增，原 F7 实验场）：游戏内菜单（T / 手柄 RB）放靶子、刷雑魚群（4 种 + 混合、10〜300 只、可不动）、召唤 Boss、全部清除、往魔法书木箱里加魔法 / 全部加入 / 清空；开局木箱为空。自测 `training`
- 10-07：**Demo 构建**：新配置 `Demo|x64`（Release + `VFXL_DEMO`），不创建 ImGui、没有编辑器和 F 键切场景、没有控制台；判断统一走 `Core/DevUI.h`
- 10-07：**include 整理**：常用标准库头收进预编译头 `pch.h`（强制包含），139 个 .cpp 删掉 388 行重复的标准库 include，include 区统一成「自己的头 → 项目头 → 第三方 / 系统头」；脚本 `Tools/TidyIncludes.pl`
- 10-07：**Boss 招式 5 个**（用户：「接下来做 Boss 招式吧」，清单按推荐）：原来只有重撃，加了投石雨（跑开躲）、衝撃波（跳起来躲）、突進（往侧面闪）、召喚（刷雑魚 + 自爆兵）；`BossBrain` 按距离三段的权重 + 每招冷却选招，狂暴后间隔缩短、可连招；突進 / 溜め 由 `BomberCB` 的新字段让 GPU 的 Boss 直冲 / 站住；预警圈加了贴地的环带（衝撃波）；自测 `bossmoves` 各招中 / 躲都对
- 10-07：**难度节奏**（用户：「难度曲线有大问题」= 前期无聊 / 5〜8 分钟陡升 / 后期被淹没）：curve 自测量出 2〜9 分钟压力是平线、蓝量把输出卡在回蓝的一半、10:00 是断崖 → 加「湧きの波」（约 70 秒一次从一个方向的押し寄せ + 一息，画面中间案内 + 方向箭头）、精英 3 / 6 / 8 分、0〜2 分的坡放缓、回蓝 30 + 每级 16%、最终波提前 30 秒预告并缓坡（每段 ×1.25）
- 10-07：**按同类调研修正**（调研 VS / HoloCure / 20MTD / Brotato / HoT / DRG:S / Megabonk 的玩家与敌人成长）：敌人数值已和 Megabonk 一致，偏离的是玩家防御和中期敌人组成 → 受击无敌 0.6 → 0.3 秒、开局盾 25 → 0、幽灵 9.1 → 5.5 m/s、新敌人「重装兵」（HP 3 倍、慢、兽人外观，4〜9 分钟替换 8〜22% 的雑魚）
- 10-08：**魔法试算表 + 符文调平**（用户：单体 / 群体怎么平衡想不通 → 符文把强魔法放大太多）：新自测 spellbench 实测每个魔法 × 每个符文的单体 / 群体 DPS；分裂对落地范围也 ×0.6、散射第 1 发永远在正中（以前直射魔法对单体两发全空）、拡大鏡半径 1.25 + 弹伤 ×1.15 → 毒+四符文对单体魔法的群体差距 22 倍 → 10 倍
- 10-08：**新敌人两种 + 冰魔法**（用户问「还要加什么」后选了冲锋兵、盾兵、冰）：突撃兵（停下溜め、地面红色预警带 → 锁定方向直冲 14m → 喘气，冲中撞到 ×1.8 + 爆炸级击退；第 2 面主力）、盾兵（每发伤害减 5、最少 20%，毒沼 / 光线几乎打不动；身前挂特效模型的塔盾；第 3 面主力）、基本魔法アイスランス（直射，冻住命中的 1 只 1.2 秒，精英 / Boss 减半，解冻后 3 秒免疫；冻住的敌人变冰蓝 + 脚边冰刺）；`enemySlow` 改成 float4 带冻结；CompactCS 的 UAV 满了，新种类的一览另开一个 CS；自测 chargeshield
- 外围岩壁的外观换成岩山：KayKit 大岩石放大堆 3 排（8〜34m，约 400 块，实例化），碰撞仍是原来的箱子；自测 `edge`
- 追加：升级 4 选 1 + 能力卡权重 0.35；新卡「魔力回復 +20%」「跳躍回数 +1」（空中每次 ×0.75：12 → 9 → 6.75）；开局法术改成追踪弹（火球太强）；场上磁铁（`ECS/System/PickupSystem`，碰到吸全图经验球）；自测 `pickup`

---

## 4. 各系统现状

| 系统 | 状态 | 备注 |
|---|---|---|
| GPU 雑魚（AI、积分、推挤、流场） | 完成 | 空间哈希 + 3x3 分离；流场 `FlowField` 绕坡道 |
| 雑魚种类 | 自爆兵、精英、Boss（9-29）、幽灵（9-30）、分裂怪 + 分裂体（10-03）、重装兵（10-07）、突撃兵 + 盾兵（10-08） | 种类在并行 buffer，`Enemy` 布局未动；精英 / Boss 是放大染色的雑魚；Boss 招式 5 个（CPU 控制）；远程小怪是候选，未做 |
| 关卡流程 | 10 分钟 + 精英 + 最終波 + Boss 门（9-29）；Boss 招式 5 个（10-07） | `Enemy/StageDirector`；数值照 Megabonk，缺的自定。9-30 起三关（草原 → 砂漠 → 遺跡，`World/StageConfig`），打倒 Boss 后保留背包进下一关 |
| GPU 投射物 | 4 种弹道（直线 / 曲线 / 追尾 / 陨石） | profile json = 弹本身，Item = 怎么放 |
| 范围攻击 | 完成 | 火球爆炸、陨石爆炸、命中特效范围（伤害 0） |
| 经验球 | 完成 | 外观 + 尾迹 + 拾取特效 |
| 精英（CPU） | 靶子级别 | KayKit Skeleton Warrior，HP 归零燃烧消散 |
| 玩家 | 移动 / 跳 / 滑铲 / 自动施法 / 魔力解放（Q） / シールド（10-07） | Quaternius 游侠模型；Walk / Jog / Sprint，身体转向移动方向（9-29） |
| 背包 | 9x9，形状编辑器 | 道具 19 种：7 弹（9-30 加黄金の矢、石弾，10-08 加アイスランス；陨石是高级魔法，要火球 + 石弾驱动）+ 1 范围型（魔導光線，高级，追踪弾 + 弧驱动）、2 符文 + 拡大鏡 + 加速のルーン（9-30）、扩张枠、6 能力卡（9-29 加魔力回復、跳躍回数）；升级 4 选 1；10-04 起基础魔法是 2〜4 格的不同形状（按强度），影响格 = 形状的上下左右一圈；10-04 起有金币（经验球同时算金币，开箱 / 刷新选卡花钱） |
| 升级 / 报酬箱 | 完成 | 三选一共用 |
| 地形 | 随机野原 × 3 种外观，200m 三层 | 10-02 起：中间平原 + 对角的 +16m 山顶 / −10m 矿洞（10-03 加了岩石洞顶，入口是下行坡；一度 300m，10-03 改回 200m）；台地、坡道、高台三关共用；草原 = 森林 + 草地 + 岩山，砂漠 = 棕榈 / 枯树 / 仙人掌 + 岩山，遺跡 = 遗迹墙 + 石柱 + 火把 + 夜（9-30）；10-03 起地面 / 崖 / 洞有风格化贴图（`TerrainSurface`）；10-04 起平原 / 山顶顶面有起伏（缓丘 + 土包，高度场碰撞）；关卡编辑器的关卡未接入 |
| 光照 / 影子 / 雾 / 天空 | 完成 | 级联影子 + 脚底圆影；sRGB 解码 |
| UI | 幻想风格 | 结算画面、「[F] Open」、「力尽きた」幕未换 |
| 特效编辑器 | Particle / Trail / Sprite / Mesh / Light 条目 | GPU 弹道读全部粒子层 |
| 相机 | 完成 | 遮挡拉近、震动、速度演出、调参保存 |
| 性能工具 | FrameProfiler、压力 / perf 自测 | 数字见第 6 节 |
| 声音 | 第 3 版（10-03） | miniaudio；音效 41 个 cue + BGM 6 个槽位（另有 15 首 alt 候选），全 CC0（`Assets/Audio/`）；出口限幅器；关卡 = 环境主题曲、Boss = 金属曲 + 登场冲击；自测 `music` 出声测过；音效具体选哪个文件仍待用户试听 |
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
| Kenney 音效 6 包 / rubberduck / lentikula / NIIIEMAND | CC0 | 音效（`Assets/Audio/SFX/`，各文件夹 SOURCE.txt） |
| OpenGameArt BGM 21 首（nene / Juhani Junkala / Dizzy Crow / Cleyton Kauffman / Emma_MA / Alexander Ehlers / cynicmusic；11 首为无缝循环重编码过） | CC0 | BGM（`Assets/Audio/Music/SOURCE.txt`） |
| 3dtextures.me 风格化贴图 7 套（Joao Paulo） | CC0 | 地面 / 崖 / 洞的贴图（`Assets/Texture/Terrain/SOURCE.txt`） |
| miniaudio + stb_vorbis | 公有领域 / MIT-0 | 音频库（`ThirdParty/miniaudio/`） |
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
| 开局默认段（10-03，三层场地 + 装饰物约 1100 个，优化前） | 7.76 ms | 2.11 ms |
| 开局默认段（10-03，View 只扫需要的池子之后） | **5.48 ms** | 未复测（GPU 瓶颈） |
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
