# VFX_L — 给 Claude Code 的项目说明

C++ / DirectX 11 自制引擎的 3D roguelite（幸存者类）。雑魚、投射物、经验球、范围攻击都在 GPU 上跑（`Swarm/SwarmSystem`），玩家和精英留在 CPU 的 ECS（`Registry`）。
设计依据：`GPU_GAMEPLAY_PLAN.md`（Phase 0〜4 已完成，Phase 5 未开始）。Phase 4 收尾清单的原文在 `PHASE4_CLEANUP.md`（已全部完成）。

---

## 1. 约束（全程有效）

- **编码**：C++ 注释用日语，全局 `/utf-8`。HLSL 纯 ASCII、无 BOM、CRLF。含日文 `L"..."` 字面量的文件存 UTF-8 带 BOM。
  残留的 CP932 文件（编译时只出 C4828）要写日文注释前，先整体转成无 BOM 的 UTF-8。
- **GPU 结构体布局**：`Enemy` / `Projectile` / `Orb` / `FrameCB` / `AICB` / `OrbCB` / `SwarmCounters` 默认不改。需要新的逐槽数据时开并行 buffer（例：`m_EnemyMaxHpBuffer`）。确实要改时先说明理由。
- **Dispatch**：每个 CS dispatch 之后必须 `UnbindSRVs` + `UnbindUAVs`；绘制用的 SRV 画完也要解绑（否则下一帧 CS 当 UAV 用时出 HAZARD）。
- **`DispatchStep` 顺序**：0 counter 清零 → 0b 空间哈希 → 1 敌 AI → 2 敌积分 → 2b 重叠解除 → 3 弹积分 → 4 命中（+经验球掉落）→ 5 瞄准 → 6 接触 → 7 经验球吸引/拾取。改顺序要先说明。
- **SwarmCounters**：`killCount` / `playerDamage` / `expTotal` 是 GPU 永久累加、CPU 取差分。任何清理都不许清 counters buffer，也不许重置 `m_LastKillCount` / `m_LastDamageTotal` / `m_LastExpTotal`。
- **工程文件**：增删源文件、着色器必须同步改 `VFX_L.vcxproj` 和 `VFX_L.vcxproj.filters`。着色器照 `SwarmEnemyVS.hlsl` 的 `FxCompile` 块写（输出到 `$(OutDir)Shader/...cso`）。
- **Esc 在 `Core/Window.cpp` 里是退出程序**，任何场景都不能拿它当快捷键。

## 2. 工作方式

- 有方案选择（外观、交互方式、数据放哪）时，先列出选项和推荐，等用户决定再动手。纯机械的步骤直接做。
- 每完成一个任务构建一次 Debug x64，通过再做下一个。不要求零警告，但不能新增警告。
- 用户在用电脑时（最近 1 分钟内有输入）不要启动游戏抢焦点，更不要发按键；锁屏时只做日志验证。

### 构建

从 PowerShell 调（Git Bash 会把 `/p:` 当路径）。VS 2022 的 MSBuild 会报 MSB8020，要用 VS 18 的：

```
& "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:\VFX_Project\VFX_L\VFX_L.sln" /p:Configuration=Debug /p:Platform=x64 /m /v:m /nowarn:C4828 "/flp:LogFile=<scratchpad>\build.log;Verbosity=minimal;Encoding=UTF-8" /noconsolelogger
```

- 构建前确认没有 `cl.exe` 在跑（用户可能在 VS 里构建，撞车会报 C1041 PDB 占用）。
- 已知的旧警告：C4828（CP932 残留）、MSB8027 / LNK4042（ManaSystem.cpp 重名）、ShaderPath.h 的 C4244。

### 自测

- 启动 `x64/Debug/VFX_L.exe`（工作目录同目录），截图看画面。**只发键盘，不发鼠标**，发键前确认前台窗口属于游戏进程。
- 程序日志走 AllocConsole，重定向拿不到；用 `AttachConsole` + `ReadConsoleOutputCharacterW` 读控制台缓冲区。

## 3. 场景（DebugManager 快捷键）

| 键 | 场景 | 用途 |
|---|---|---|
| F1 | Game Test（`CollisionTestScene`） | 战斗本体（结构见下方「战斗场景的结构」） |
| F2 | VFX Editor | 特效编辑 |
| F3 | Title | 标题 |
| F4 | Projectile Editor | 弹道、范围攻击编辑；第 3 页 Item Shapes 编辑道具形状 + 试放背包 |
| F5 | 重新加载当前场景 | |
| F6 | Level Editor（`LevelEditorScene`） | 摆素材、存 `Assets/Data/LevelData/<名字>.json` |

## 4. 主要系统速查

- **雑魚外观**：`SwarmSystem::BuildEnemyModel` 候选表（Kenney Blocky L 僵尸 → KayKit Minion → 胶囊）。方块人是刚体部件，用 `Model::LoadOptions` 烘姿势，`BuildEnemyPartAnim` 做 idle / walk / attack 的部件动画表，`SwarmEnemyVS` 按敌人状态选帧。
- **雑魚血条**：`SwarmEnemyHpBarVS/PS`，满格值在 `m_EnemyMaxHpBuffer`，由生成 CS 和回收 CS 写入。
- **光照**：`Shader/Common/Lighting.hlsli`（半球环境光 + 平行光和点光源的 GGX 高光）。点光源表是 `PointLightManager`（每帧一张，t6/t7）。
- **背包**：9x9（`BackpackComponent::GRID` 和 `BackpackUI::GRID_SIZE` 要一起改）。开局 3x3 枠放在 `GRID/2-1`（Rect 锚点在左上）。
- **道具形状**：`Items/*.h` 里的 `occupyCells` / `influenceCells` 是代码默认值。存在 `Assets/Data/ItemData/<道具名去掉空格>.json`（`Item/ItemDataFile`）时，`ItemDatabase::Initialize` 会用文件内容覆盖。编辑时用 `ItemDatabase::SetShape` 改，用 `GetCodeShape` 恢复成代码默认；形状变了以后用 `BackpackLogic::Refit` 重新摆放。多格方块画成连在一起的一块（`UI/ShapeSprite.h`）。
- **形状编辑器**：`Scene/ItemShapePanel`（F4 第 3 页，按键 3）。先选道具，再在格子上画这个道具的形状：左键画占位、右键画影响、Ctrl+左键设锚点。点 Save 写进这个道具的数据文件。「Item Test Bench」窗口用和正式游戏同一条链（BackpackLogic → BackpackAggregateSystem → WeaponSystem）往靶子发射。
- **战斗相机**：`Camera/FollowCamera`，背后第三人称视角：
  - 鼠标：场景每帧调 `InputManager::RequestMouseCapture`（不调了下一帧自动放开），光标隐藏并锁在窗口中央 1px，Raw Input（WM_INPUT）的位移直接转视角，不用按键。捕获中给 ImGui 设 `NoMouse`。Alt 单按切换显示光标（松开时触发，中间按了别的键就不算，`InputManager::GetAltTap`），显示时游戏不暂停。UI 打开 / 死亡 / 调试相机时强制显示光标，这时不接受 Alt。`Window.cpp` 屏蔽了 `SC_KEYMENU`（否则单按 Alt 会让游戏循环卡住）
  - 平滑跟随用 SmoothDamp，水平、垂直分开调
  - 相机偏到右肩
  - 遮挡处理：场景通过 `SetOcclusionProbe` 用 5 条射线打 `Layer_Terrain`。有东西挡住时相机立刻拉近，没挡住后慢慢退回原距离
  - 震动：调用 `AddTrauma`，抖动幅度 = trauma²。只作用在 `CameraBase::SetViewShake` → 视图矩阵上，不影响 `GetForward`，所以移动和朝向不会跟着抖
  - 触发：场景看玩家 HP 掉了多少，以及 GPU `aliveAreas` 有没有增加。参数都在「Camera」面板
- **升级**：`UI/LevelUpSystem`。卡池 = `ItemDatabase::GetAllIDs()` + `GetLevelUpOnlyIDs()`（生命、法力上限卡，`ItemCategory::Stat`）。
- **施法暂停**：`WandComponent::castingPaused`（Q / 手柄 Y）。
- **UI 文字是日文**：字体 `NotoSansJP.spritefont`（汉字、假名齐全），但**没有全角英数/全角符号和 × ° →**，文案里用半角、`x`、「度」。含日文 `L"..."` 的文件存 UTF-8 带 BOM。
- **道具说明**：`ItemCommon::displayName / description`（`Items/*.h`）+ `Item/ItemInfo`（`Describe` / `DescribePlaced` 从道具定义和投射物/范围 profile 自动出数值，置于背包的物品叠符文修饰）。聚合系统用同一套 `ItemInfo::Base*Stats` + `BackpackLogic::GetInfluencers`，画面数字和实际一致。`UI/ItemSheetView` 负责折行和画卡片正文 / tooltip；tooltip 在 `GameUI` 的覆盖层（第二组 Begin/End）画。
- **HUD**：顶部中间计时（`m_RunTime`，暂停不走，结算也用它）+ 撃破、底部法术栏（冷却/MP 不足/暂停施法）、低 HP 红边、屏幕外箭头（报酬箱黄、精英红）。参数在 `HUDStyle`，ImGui 调，存 `Assets/Data/HUD.json`。
- **暂停 / 死亡 / 标题**：暂停 = P / 手柄 Back（`UILayer::Pause`，手柄 Start 是背包，Esc 是退出），菜单部件 `UI/MenuList`（暂停和标题共用）。死亡后画「力尽きた」再进结算（`kDeathToResult` 3 秒）。标题的游戏名是占位（`TitleScene.cpp` 的 `kGameTitle`）。
- **道具图标**：`Assets/Texture/UI/Icons/`（7Soul CC0 + 自画 2 个，34px 最近邻放大到 136px，出处见同目录 README），路径 `Res::Icon::*`。形状用道具色压暗做底、图标只在中心格画一次（`ShapeSprite::DrawItem`）。
- **SpriteRenderer 是预乘 alpha**（`AlphaBlend()` = ONE / INV_SRC_ALPHA）：`SpritePS` 输出 `rgb*a`，`TextRenderer::Draw` 也把颜色预乘。另外画面会把深色提亮（0.07 显示成中灰），深色底板用 0.015 左右。
- **特效 Trail entry**：`VFX_Editor/VFXTrailEntry` + `Particle/GPUParticleEffectTrail.cpp`，特效位置驱动的 GPU 条带（按距离取点、按长度渐变）。战斗火球不用（用户决定）。
- **粒子贴图（多图集）**：公告板粒子按发射器选图集 `textureIndex`（json `"sheet"`）。图集表是 `Res::ParticleSheet::kManifests`（序号就是存档里的编号，**不许重排，只能往后加**，最多 8 张），每张 = 图集 PNG + 清单 json（行列、`filter` linear/point、`premultiplied`、`frames` 名字、`groups` 命名范围），在 `Assets/Particles/Sheets/`，由 `Particle/ParticleSheets` 懒加载。0 = 旧 `particlesSheet.jpg`（6x6，直 alpha），1 = Kenney Particle Pack（白色、靠颜色染，10x8×256px），2 = Kenney Smoke（彩色烟 9x9）。像素序列帧（PVFX）不进粒子图集，走 Sprite entry。新图集一律**预乘 alpha 存 PNG**，用 `Tools/BuildParticleSheets.ps1` 从解压的素材包生成。
  - 帧：`frameMode`（`ParticleFrameMode`：0 旧式 / 1 固定 / 2 随寿命播放 / 3 出生时随机）+ `atlasIndex`（起始）+ `frameCount`。粒子端 `atlasAnimate = (first<<12)|count`。`GPUEmitter` 的 `frameCount` / `frameMode` 是原来的 `_pad2` / `_padS2`，布局没变。
  - 混合：公告板粒子整体用预乘 alpha（ONE / INV_SRC_ALPHA）画一次；加法粒子 PS 输出 alpha 0（外观等同旧的 SRC_ALPHA/ONE），半透明粒子（json `"blend": 1` → `renderMode` bit12）输出 a。**不排序**，重叠处前后可能错。
  - 编辑器：Particle entry 的 Render = Billboard 时有 Texture / Blend / Group / Frames / Pick frame（点格子选起始帧，Shift+点选结束帧）。
- **Sprite entry（序列帧）**：`VFX_Editor/VFXSpriteEntry`，把一张序列帧图当一块板播放。图放在 `Assets/VFX/SpriteSheet/`（PVFX Foundry 41 个在 `PVFX/<效果名>.png + .json`，CC0，只留了 grid 版），同名 json 由 `VFX_Editor/SpriteSheets` 读（PVFX manifest：帧数/每帧时长/loop_mode/pivot；也支持简易格式 cols/rows/frames/fps/loop/pivot）。条目参数：size（一帧的高度 m）、Facing（Billboard / Upright 只绕 Y / Ground 贴地）、Anchor（素材 pivot / 中心 / 底边）、Blend（半透明/加法）、Speed、Loop（按素材/一次/循环）、Offset、Rotation、Color；「Fit Duration」把条目时长设成一次播放的长度。
  - CPU：`VFXSpriteRenderer`（`VFXContext::spriteRenderer`，VFX 编辑器 / F4 / 战斗场景各有一个，粒子之前画），dynamic structured buffer + 按贴图分组 DrawInstanced，点采样，预乘 alpha，不排序。
  - GPU 范围（弹命中生成的范围）：`SwarmVFXTable` 把 Sprite 条目转成 `SwarmSpriteDef`（80B，recipe 的 `spriteStart/spriteCount` 原是 `_pad`），用到的图拼成 Texture2DArray（每张一层，按最大尺寸补齐）；`SwarmSpriteCS` 在发射阶段后跑两次：推进年龄并回收 → 按槽位检测新范围（`areaSeen` 存上帧的 timeLeft，变大=新范围）并在 1024 个实例的环形池里启动；`SwarmSystem::RenderSprites` 画全部槽位。范围消失后动画继续播。GPU 弹道本身上的 Sprite 条目不画；CPU 生成的范围（vfxType 0）由 CPU 播。
  - 两条路径共用 `Shader/Common/SpriteQuad.hlsli`（四边形生成）。示例 `SpriteTest.json`。
- **战斗反馈特效**：升级 `LevelUp.json`（radiant-heal，跟随玩家）、开箱 `CrateOpen.json`（harvest-seal + 木屑方块）、受伤 `Hurt.json`（红色 crescent-slash，跟随玩家），由 `ECS/System/FeedbackVFXSystem` 经场景的 `m_AreaVFX` 播放；暂停时不走，所以升级/开箱要等三选一选完才看得到。玩家位置是胶囊中心（y≈0.9），相机在身后时 Q 版法师会挡住身上的特效，所以升级和受伤的 Sprite 开了「Always on top」（Sprite entry 的 `onTop`，不做深度测试，只对 CPU 路径有效）。受伤最快每 `m_HurtInterval`（1.5 秒）一次。战斗场景「Feedback VFX」面板：开关、Test 按钮、受伤间隔、Reload json。命中特效：ArcOnce → AreaData `ArcSpark`、HomingFull → `VoidPop`（伤害 0、0.1 秒、`hitAreaOnExpire` false，只为让 GPU 播 `ArcBoltHit` / `HomingBoltHit` 的 Sprite + 粒子）；`ItemInfo` 不描述伤害 0 的命中范围。
- **战斗场景的结构**：`Scene/CollisionTestScene.cpp` 只剩初始化、`UpdateGameplay` 的执行顺序、渲染、结算；ImGui 面板 / 调试绘制 / TEMP 自测在 `Scene/CollisionTestSceneDebug.cpp`（同一个类）。做完的功能拆成部品，场景持有并调用：`Camera/BattleCamera`（FollowCamera + 遮挡探针 + 鼠标捕获 / Alt + 震动触发 + Camera 面板）、`ECS/System/RewardCrateSystem`、`ECS/System/FeedbackVFXSystem`、`Graphics/Light/SceneLighting`（太阳 + 环境光 + 场景点光源 + 标记 / gizmo + Lighting 面板）、`Enemy/EliteSpawner`（精英靶子 + HP 归零的 CPU 实体燃烧消散）、`Enemy/MobSpawner`（SpawnDirector + 雑魚初始值 + Mob AI 面板）、`Debug/StressTestTools`（压力测试 + Mesh 发射测试 + Flush 耗时）。被弹的 HP 差分由场景 `TrackPlayerHpLoss` 算一次，分给相机震动和受伤特效。
- **GPU 弹道的特效**：`SwarmVFXTable` 收特效里**全部**粒子条目（多层）+ 点光源；条目的 position 是相对弹/范围中心的偏移（`SwarmEmitCS`）；时间轴无效；不支持逐粒子条带。点光源全局 64 个先到先得，高频弹不挂灯。法术特效：Fireball / ArcBolt / HomingBolt / Meteor（弹）、Explosion（火球爆炸）/ MeteorBlast（陨石爆炸，AreaData 同名，数值照抄 Explosion）/ FireCircle（未被道具使用）。
  - 手写特效 json 时数字不能写成字符串（`"-6.0"`）：读进来是坏值，bloom 会把整屏刷白。
- **Mesh 粒子**：粒子条目 Render = Mesh，从 `Assets/VFX/Mesh` 选模型（(none) = 立方体），Lit（不透明受光）/ Glow（加法），可选朝向速度 + 前方轴，size = 模型最长边（m）。`renderMode` 打包模型号+1 / 发光 / 朝向 / 轴（`ParticleRenderMode::Pack` ↔ `ParticleCommon.hlsli`），模型表 16 个在 `Particle/GPUParticleMesh.cpp`。示例 `MeshParticleTest.json`。
- **FBX 单位**：`Model::GetFileUnitScale()` 记录 FBX 的 UnitScaleFactor，但不乘进顶点（现有模型各自手调倍率）。KayKit Forest 和 Kenney 的 FBX 都是厘米单位。
- **素材（都是 CC0）**：`Assets/Model/KayKit_*`、`Kenney_BlockyCharacters`、`Kenney_RetroFantasy`（1m 立方的部件，编辑器默认 2 倍）。

## 5. 仍在代码里的临时测试（TEMP-TEST）

- `Core/Game.cpp`：启动已恢复为 Title，只是 `ChangeScene(SceneType::TITLE)` 那行还留着 `// TEMP-TEST` 标记。
- `CollisionTestScene::Init`：玩家 `maxHealth = 1000000`。
- `CollisionTestScene::UpdateGameplay`：每 120 帧打一行 `[crowd]` 日志。
- `SwarmSystem::UpdateFlowField`：打 `[flow] build ms` 日志。
- 自测钩子（环境变量，不设就不生效）：`VFXL_VFX_AUTOLOAD=<VFXData 的 json 名>` 启动直接进 VFX 编辑器并播放，附加 `VFXL_VFX_MOVE`（虚拟投射物飞行）/ `VFXL_VFX_LOOP`（强制循环）/ `VFXL_VFX_CLOSE`（近景、藏参考模型）；`VFXL_PROJ_AUTOTEST=<投射物 profile 名>` 启动直接进 F4 并选中这个弹；`VFXL_BATTLE_AUTOTEST=1` 直接进战斗，5 秒给升级经验、9 秒移到最近的箱子旁、10 秒模拟按 F，反馈特效和相机状态带系统毫秒时间写进 `x64/Debug/autotest.log`（三选一要靠外部发 Enter）。代码在 `Core/Game.cpp`、`VFXEditorScene::Init`、`ProjectileEditorScene::Init`、`CollisionTestScene::UpdateAutoTest`（`Scene/CollisionTestSceneDebug.cpp`）。示例特效 `SheetTest.json`（四种图集/混合对比）。

## 6. 可交互道具（已完成）

- `Component/InteractableComponent`（kind / 半径 / 案内文字 / 浮动与光的参数）+ `ECS/System/InteractionSystem`（浮动旋转、找最近的 focus、返回被按下的实体、积点光源）。效果由场景按 `kind` 分派。
- 报酬箱：`ECS/System/RewardCrateSystem`（`Spawn` 摆放、`TryOpen` 开箱），开局在玩家周围 6〜22m 的可走格子（周围 3x3 也可走）放 4 个，用完即消失、不刷新；地形重建时重新摆。像素木箱 `Kenney_RetroFantasy/fbx/detail-crate.fbx`，缩放到 0.9m，带静态 AABB 和暖黄点光源。
- 靠近后画面下方出「[F] Open」（`GameUI::SetPrompt`），F / 手柄 B 触发 → `LevelUpSystem::OfferChoices`：和升级一样的三选一，但不升级、不扣经验；选择中不能再开（箱子保留）。
- 调参在战斗场景「Reward Crates」面板。

之后的候选（未定）：战斗场景读取关卡编辑器的关卡、流场寻路的实机验证、Phase 5（SpawnDirector GPU 化等）、**认真找 UI 素材**（用户日程：面板/按钮/边框/像素日文字体，统一 UI 风格，CC0 优先，下载前先给候选）。
