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
- 已知的旧警告：C4828（CP932 残留）、ShaderPath.h 的 C4244（显示在 xutility(5094)）、HLSL 的 X4000（`SwarmIsWalkable` / `EmitMesh.hlsli` / `ParticleEmitCS` / `SwarmDebugEnemyVS` 的 `CapsulePoint`）。
- **MSB8027 / LNK4042（同名源文件）不是无害警告**：两个同名 .cpp 会编译到同一个 `x64/Debug/<名>.obj`，最后链接进去的是哪个取决于编译顺序。以前根目录混进过一个空的 `ManaSystem.cpp`，链接到它的时候蓝量既不扣也不回、`pendingSpend` 一直累加，法杖就不打了（2026-09-26 已删）。再看到这两个警告要马上查。

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
- **经验球外观**：自发光青蓝宝石（`PrimitiveBuilder::CreateBipyramid` 四角双角锥）。`SwarmOrbVS` 做浮动 / 自转 / 脉动（每槽相位不同），被吸引时长轴倒向玩家、沿速度拉伸（保体积）、变色变亮；`SwarmOrbPS` = emissive + 面的明暗 + 菲涅尔边 + 太阳高光 + 雾（b0 LightBuffer，b1 `OrbShadeCB`）。参数 `SwarmSystem::orbLook`，Swarm (GPU) 面板「Exp Orbs」。
  - `Orb.velocity` 以前一直是 0，现在由 `SwarmOrbMoveCS` 写本步的水平吸引速度（只给外观用，布局没变）。
  - 尾迹：`SwarmOrbEmitCS`（`SwarmEmitCS` + `SWARM_EMIT_ORBS`，b3 = `OrbEmitCB`）让吸引速度 > `trailMinSpeed` 的球按 `VFXId::ExpOrbTrail`（`ExpOrbTrail.json`）喷粒子，排在弹、范围之后发射（粒子池不够时先少的是它）。
  - 拾取：`FeedbackVFXSystem::OnExpPicked`，GPU 经验差分 > 0 时在玩家身上播 `ExpPickup.json`，限频 0.15 秒。
- **雑魚种类 / 自爆兵**：种类在并行 buffer `m_EnemyExtraBuffer`（`Swarm::EnemyExtra` = kind + fuse，8B）。生成请求用 `Enemy::animIndex` 带种类，SpawnEnemyCS / RecycleCS 取出后清 0（`SpawnEnemy` / `RecycleEnemy` 的 `kind` 参数）。
  - 自爆兵（`kEnemyKindBomber`）不近战：在接触 CS（第 6 段）里碰到玩家就点燃，点燃后 AI 让它原地停下；`fuseTime` 秒后爆炸，玩家在 `blastRadius` 内受 `blastDamage`（只伤玩家），自己变 DEAD（不算击杀、不掉经验），原地放 AreaData `BomberBlast`（伤害 0，只为 GPU 播 `Explosion.json`）。引信中被打死 = 正常击杀，不爆炸。
  - 参数 `Swarm::BomberCB`（新 cbuffer：ContactCS b3、雑魚 VS b5，HLSL 里 `#define SWARM_BOMBER_CB_REG` 才声明），`SwarmSystem::GetBomberParams`，`blastArea` 由 `MobSpawner::Init` 按名字查。
  - 外观：18 个 Kenney 方块人是同一网格同一 UV，自爆兵 = 雑魚网格换贴图 texture-g（红色机器人，`Res::Tex::Kenney_BlockyRobotAlbedo`）。CompactCS 按种类出存活列表（`m_KindList*`，全体 `aliveList` 只给血条用），每种各画一次、画自爆兵前换 PS t0。点燃后 VS 越闪越快（红色高亮）并以脚底为基点鼓到 1+swell 倍。
  - 刷怪：`MobSpawner` 对每个新刷/转送按 Bomber Ratio（默认 0.15）抽选；Enemies 面板「Bomber (GPU)」段调参，「Spawn 5 Bombers Nearby」在玩家 8〜12m 处出 5 个。
  - 同一时间连着几个爆炸，玩家的无敌帧只让第一次掉血（GPU 的累计伤害照算）。
  - 点燃后脚下的警告圈：`SwarmBomberRingVS/PS`（自爆兵列表 + DrawInstancedIndirect，外圈 = blastRadius，红色实心盘随引信从中心长大，预乘 alpha），样式 `SwarmSystem::bomberRing`。默认背后镜头下玩家脚边 2〜3m 的地面在画面外 / 被身体挡住，2.5m 的圈基本看不到（镜头问题，待定）。
- **战斗地形（台地 + 坡道的野原，每局随机 seed）**：`World/TerrainGenerator`。草地地面（`PrimitiveBuilder::CreateColoredGrid`，值噪声色斑）+ 外围岩壁 + 1 段台地（箱子，顶草绿侧岩石，`CreateHexahedron` 双色）+ 部分台地上的 2 段 + 坡道（凸体楔，顶面土色 = 登上台地的唯一入口）+ 高台（给滑铲的长坡 + 场地的层次：`terraceCount` 座（默认 4），高 5〜7m，**只有一面**是整面宽的 16〜20 度长坡（草色凸体楔，`SpawnRamp` 换顶色），其余三面是崖，坡朝向每座随机；一半（`terraceTier2Chance`）在一层顶面的里侧再叠二层（+3〜4.5m，坡同向，四周留 1 格边），地面 → 一层 → 二层能一路滑下来；最先摆（台地之后）；坡脚前 `terraceClear` 格（默认 5）是「麓」（占有图 `kApron`：不放树、石、台地），坡脚沿下坡方向离外围岩壁至少 12 格；一层顶面坡侧 2 格是通路（`kWay`，不放树、石））。四面坡的小丘已删（用户 2026-09-28 决定，四面坡的大丘也否掉了）+ KayKit Forest 自然物（树成林、大石头：挡路，占的格子周围一格不放别的挡路物，所以不会连成墙；碰撞层 `Layer_Prop`，镜头遮挡检测不看它；灌木只是装饰；原来的 450 个草模型 2026-09-28 删了，草改由 `GrassRenderer` 在 GPU 上生成，见下）。模型表 `Res::Mdl::Forest`。台地、坡道、高台的格子仍是 walkable，高度写进高度场；没有坡道可上的台地整块 BlockArea。seed 每局随机，`VFXL_TERRAIN_SEED` 可以固定；Terrain 面板可调全部参数并 Regenerate。地形顶点色是线性反照率（sRGB 值取 2.2 次方）。
  - 自然物的绘制：`Graphics/Renderer/StaticPropRenderer`（场景持有 `m_StaticProps`）。`TerrainGenerator` 放的外观实体带 `ModelComponent::batched`，`RenderSystem` 跳过它们；`Build` 在生成 / Regenerate 后按模型分组、算一次 world 矩阵和包围球，每帧视锥剔除后把可见的矩阵写进动态 StructuredBuffer（VS t8），每种模型每个 submesh 一次 `DrawIndexedInstanced`（`Shader/StaticPropVS.hlsl`，b1 = 这次 draw 的起始下标，PS 用模型自己的材质）。距离剔除（Terrain 面板「Dist / Radius」「Max Distance」）默认关：实例化后画多少几乎不影响帧时间，开了中距离的小物会消失。2026-09-27 实测（seed 12345、关垂直同步、开局 1 分钟内）：Debug 55 → 约 108 fps（760 个装饰物从每帧 9.6 ms 降到约 0.35 ms），Release 485 → 约 730 fps（装饰物从 0.9 ms 降到约 0.05 ms）。
  - **草地（2026-09-28）**：`Graphics/Renderer/GrassRenderer`（场景持有 `m_Grass`），GPU 生成的低多边形草叶（每片 3 个三角形、双面、不投影子，接收太阳影子和雾）。世界坐标对齐的格点（`spacing`，默认 0.25m）每格一片、格内哈希偏移，镜头动时草不会跟着滑。每帧 `Shader/Grass/GrassCullCS` 只扫镜头周围 `maxDistance`（70m）的窗口：超出距离、按距离稀疏化（`fullDensityDistance` 18m 以外逐渐降到 `farDensity` 0.2，留下的变宽；快被去掉的先缩短，不会突然消失）、不长草的格子（`TerrainGenerator::Generate` 的 `outGrassMask`：泥土坡道、外墙、登不上去的台地）、崖面（高度场 0.3m 内的坡度 > `maxSlope`）、视锥外，都去掉；剩下的 append 进 buffer → `DrawInstancedIndirect`（`GrassVS/PS`）。颜色 = 地面色斑（`TerrainGenerator::GroundColor` 烘成和高度场同分辨率的贴图，a = 长不长草）× 根部 / 尖端色。PS 的光照 CB 在 b1（b0 是 GrassCB）。
    - 风：沿风向推进的波（阵风）+ 每片的小抖动。踩倒：覆盖全图的 512² 踩踏图（R16G16F 两张轮流读写，`GrassTrampleCS`），每帧按 `trampleRecover` 衰减、在玩家脚下盖一个往外推的圆（接地时 0.7m，滑铲 1.1m），草叶按它倒下；暂停时 `Update` 不调，风和恢复都会停。地形 Regenerate 时 `Build` 重建并清空踩踏图。
    - 面板：Terrain 后面的「Grass」（密度、范围、尺寸、颜色、风、踩踏，显示可见草叶数）。用户 9-28 要求「单片大一点、稍稀、范围大」，所以默认是 0.25m 间距 / 宽 7〜12cm / 高 28〜55cm / 70m。
    - 实测（seed 12345，关垂直同步）：Debug 有草 11.8 ms / 无草 11.4 ms（换掉 450 个草模型后比原来的 12.06 ms 还快，草模型要在影子里画 3 遍）；Release 有草约 1.60 ms / 无草约 1.57 ms。
    - 已知：雑魚的脚底圆影画在地面上，草长起来后大半被草叶挡住。
  - 崖壁：雑魚不能走太陡的地方（`SwarmSlopeOk`，坡度上限 `SWARM_MAX_WALK_SLOPE` = tan40°），AI 的硬阻挡、积分、推挤都会检查；流场 `FlowField::maxStep`（相邻两格中心高度差 1.5m，斜向乘 √2）超过就不通，所以会绕到坡道。离玩家 1.5 格以内、但高度差 ≥1m 时仍然走流场，不直冲。经验球在地面高度 + `g_OrbY` 的位置平滑跟随。
- **天空与雾**：`Graphics/Renderer/SkyRenderer`（`Shader/Sky/SkyVS/PS`，全屏渐变 + 太阳光晕，场景渲染的第一步画、不碰深度）+ `LightBuffer` 的 `fogColor/fogStart/fogEnd/fogMax`（`Lighting.hlsli` 的 `ApplyFog`，在 `ShadeLambert` / `ShadePBR` 带世界坐标的版本末尾应用）。参数在 `SceneLighting`（Lighting 面板的 Sky & Fog），雾色默认 = 地平线色；战斗场景 Shutdown 时 `SceneLighting::ClearFog` 关掉（Renderer 各场景共用）。
  - 贴图全部按 UNORM 读，sRGB 图没有解码，所以模型偏白。`LightBuffer::albedoSrgb` = 1 时 PS / PBR_PS 用 `DecodeAlbedo`（pow 2.2）；Lighting 面板「sRGB Textures」，自测用 `VFXL_SRGB`。默认关，要不要采用还没定。
- **交换链 / 帧率**：`Graphics` 用 flip 型（`FLIP_DISCARD` 3 枚，backbuffer 无 MSAA；场景的 HDR RT 仍是 4x MSAA，BeginUI 里 resolve）。支持时带 `ALLOW_TEARING`，关垂直同步时 Present 用它（可变刷新的屏不撕裂）。`PresentSettings`（vsync / fpsCap）在 Debug Info 窗口「VSync」「FPS Cap」调，默认开垂直同步、不限帧；限帧在 `EndFrame` 里 Present 前等（高精度 waitable timer + 最后 2ms 自旋，Release 限 160 实测 160.0 fps、sd 0.1 ms）。Alt+Enter 的独占全屏已关（全屏用 F11 无边框窗口）。实测（2026-09-27）：旧的 blt 交换链开垂直同步时 Debug 也不会锁到 82.5 fps，所以换 flip 型对平均 fps 没影响，意义在于撕裂模式 / 可变刷新 / 限帧。
- **光照**：`Shader/Common/Lighting.hlsli`（半球环境光 + 平行光和点光源的 GGX 高光）。点光源表是 `PointLightManager`（每帧一张，t6/t7）。
- **太阳影子（2026-09-28）**：`Graphics/Light/ShadowMap`，3 级级联（默认按离镜头 12 / 40 / 120m 分，每级 2048² D32F，Texture2DArray 一层一级）。每级 = 视锥切片的包围球（大小只由画角和距离决定）+ 中心在光源空间按 texel 对齐，所以镜头动时影子边缘不闪；投影物收进球往光源方向 `backDistance`（60m）以内的东西，光栅化不裁深度、双面、斜率偏移。
  - 接收：影子参数追加在 `LightBuffer` 雾参数之后（C++ `LightTypes.h` 和 HLSL 同步，`static_assert` 卡 offset 112 / 368），各路径本来就拷 `renderer.GetLightData()`，所以自动带上；影子图 PS **t9**、比较采样器 **s2**，整帧绑一次，场景 Render 末尾解绑。`SunShadow` 只削太阳的漫反射和高光（环境光、点光源留着），选第一个包含该点的级联，法线方向偏移 + PCF，最后一级末尾淡出。不能用单独的全局 CB：每个 PS 反射出来的 CB 在 `Bind` 时会盖掉同一槽位。
  - 投影：战斗场景 Render 开头 `m_Shadows.Render(...)` 每级回调 `RenderSystem::RenderDepth`（普通模型走 `Renderer::BeginDepthPass` → `DrawMesh` 只绑 VS、PS 置空；骨骼模型第 0 级重新蒙皮后 `SkinnedModelGPU::RenderDepth`）+ `StaticPropRenderer::RenderDepth`（按光源视锥剔除，不看近平面），然后 `Graphics::RestoreRenderTarget`。深度路径不许调 `RenderStates::Restore`（会冲掉影子的光栅状态）。
  - GPU 雑魚不进影子图，用脚底圆影代替：`SwarmBlobShadowVS/PS`（存活列表 + 血条那份 indirect 参数，四角各自贴高度场、离脚底最多 ±0.3m，预乘 alpha 压暗），参数 `SwarmSystem::blobShadow`（Swarm (GPU) 面板「Blob Shadows」）。雑魚本身照样接收地形和树的影子。
  - 面板：Lighting 后面的「Shadows」（开关、级联染色、图大小、分界、强度、PCF、各种偏移、影子阶段 CPU 耗时）。场景 Shutdown 时 `Disable`（`Renderer` 各场景共用，不关的话其他场景会采样未绑定的 t9 → 全黑）。
  - 实测（2026-09-28，seed 12345，Debug，关垂直同步）：关影子 10.9 ms / 91 fps → 开影子 12.0 ms / 83 fps。默认太阳 50 度时台地的影子短、多半落在背对镜头一侧；太阳 25 度时高台的影子很明显。
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
- **滑铲（斜坡加速）**：按住左 Ctrl / 手柄 X（`InputMap::GetSlideHeld`；手柄 X 也是调试用手动施法的键，自动施法时不冲突）。`PlayerControlSystem`：进入时（接地）平地也推到 moveSpeed × `slideBoost`（`slideBoostCooldown` 秒一次）；滑行中按脚下法线 `RigidbodyComponent::groundNormal`（`PhysicsSystem` 取最斜的地面接触）往下坡加速（水平 = `slideGravity` × cosθ sinθ，物理的重力投影也会再加一点），减 `slideFriction`，封顶 `slideMaxSpeed`，转向限 `slideTurnRate`；速度低于 `slideMinSpeed` 就站起，并且要松开再按才能再滑（`slideNeedsRelease`）；离地 0.2 秒内继续滑。跳起 / 松开后多出 moveSpeed 的惯性（`carryMomentum`）按 `momentumDecay`（空中 `momentumDecayAir`）减；普通跑步照旧每帧设成输入 × moveSpeed。滑行中朝向 = 速度方向。状态 `MoveStateID::Slide`（`PlayerStateComponent::slideActive`），动画 `Crouching`（Character Animations，见上）+ `SkinnedAnimComponent::leanDeg` 以脚底为轴后仰（`PlayerAnimSystem::slideLeanDeg`，默认 15 度，RenderSystem 里在 offset 之前转）。参数全在 `PlayerStatsComponent`，战斗场景 Player 面板「Slide」可调并显示速度 / 脚下坡度。坡道（28 度约 5〜7m）很短，实测 26 度 7m 的坡从 8 滑到 13.4 m/s；高台的长坡（seed 12345，19 度、斜着滑 25m）从 8 滑到约 18 m/s，正对坡滑 19m 到 19.7 m/s。
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
- **陨石（弹道第 4 型 `MotionMode::Drop` = 3）**：profile `Meteor.json`（原 `ExplosiveArc`）。`SwarmSpawnProjCS` 在生成时锁离玩家最近的敌人，落点 = 它当时的位置（之后不跟踪），起点 = 落点 + 往枪口方向水平 `c1.y` m + 上方 `c1.x` m（默认 16 / 9，约 60 度），`SwarmBuildDropPath` 画成直线、越落越快（s = 0.4t + 0.6t²，平均速度 = profile 的 speed），寿命自动延长到够落地。`ProjMoveCS` 的 DROP 分支不做地形检测（格子判定是 2D，飞过树和台地会误判撞墙），`HitCS` 跳过 DROP（下落途中不命中），落地时在 p3 放 hitArea（`MeteorBlast`）。落点的警告圈：`SwarmDropRingVS`（弹的全部槽位 DrawInstanced，非 DROP 的丢掉）+ 共用 `SwarmBomberRingPS`，半径 = hitArea 的半径，实心圆按 pathT 长大，样式 `SwarmSystem::dropRing`（橙黄，Swarm (GPU) 面板「Meteor Ring」）。F4 的 Mode 下拉有 Drop（Drop Height / Back Offset）。道具说明对 Drop 不显示「威力」行（弹本身不命中，只有爆炸的伤害）。
- **GPU 弹道的特效**：`SwarmVFXTable` 收特效里**全部**粒子条目（多层）+ 点光源；条目的 position 是相对弹/范围中心的偏移（`SwarmEmitCS`）；时间轴无效；不支持逐粒子条带。点光源全局 64 个先到先得，高频弹不挂灯。法术特效：Fireball / ArcBolt / HomingBolt / Meteor（弹）、Explosion（火球爆炸）/ MeteorBlast（陨石爆炸，AreaData 同名，数值照抄 Explosion）/ FireCircle（未被道具使用）。
  - 手写特效 json 时数字不能写成字符串（`"-6.0"`）：读进来是坏值，bloom 会把整屏刷白。
- **Mesh 粒子**：粒子条目 Render = Mesh，从 `Assets/VFX/Mesh` 选模型（(none) = 立方体），Lit（不透明受光）/ Glow（加法），可选朝向速度 + 前方轴，size = 模型最长边（m）。`renderMode` 打包模型号+1 / 发光 / 朝向 / 轴（`ParticleRenderMode::Pack` ↔ `ParticleCommon.hlsli`），模型表 16 个在 `Particle/GPUParticleMesh.cpp`。示例 `MeshParticleTest.json`。
- **别的文件的骨骼动画**：`SkinnedModel::AddAnimationsFromFile` 按骨骼名把另一个 FBX 的片段加到已读的骨骼模型上（旋转按两边 bind 的差在父空间换算 R = R_bind * inv(A_bind) * A，平移按 hips bind 长度比缩放，自己没有的骨骼的通道丢掉；重名的片段改叫「文件名|名字」，`FindClip` 仍先找到原来的）。`Res::Mdl::kExtraAnims`（ResourcePaths.h）列「模型 → 动画文件」，`ResourceManager::ImportModelAuto` 读完骨骼模型后按路径加（预读线程里也一样）。现在给 KayKit Mage（Adventurers 1.0）加了 KayKit Character Animations 1.1（CC0）的 `Rig_Medium_MovementAdvanced.fbx`（Crouching / Sneaking / Crawling / Dodge_* 等 13 个，`Assets/Model/KayKit_CharacterAnimations/`）：骨骼名和 Mage 一致，平移比 1。该包没有滑铲动画。
- **FBX 单位**：`Model::GetFileUnitScale()` 记录 FBX 的 UnitScaleFactor，但不乘进顶点（现有模型各自手调倍率）。KayKit Forest 和 Kenney 的 FBX 都是厘米单位。
- **素材（都是 CC0）**：`Assets/Model/KayKit_*`、`Kenney_BlockyCharacters`、`Kenney_RetroFantasy`（1m 立方的部件，编辑器默认 2 倍）。

## 5. 仍在代码里的临时测试（TEMP-TEST）

- `Core/Game.cpp`：启动已恢复为 Title，只是 `ChangeScene(SceneType::TITLE)` 那行还留着 `// TEMP-TEST` 标记。
- `CollisionTestScene::Init`：玩家 `maxHealth = 1000000`。
- `CollisionTestScene::UpdateGameplay`：每 120 帧打一行 `[crowd]` 日志。
- `SwarmSystem::UpdateFlowField`：打 `[flow] build ms` 日志。
- 自测钩子（环境变量，不设就不生效）：`VFXL_VFX_AUTOLOAD=<VFXData 的 json 名>` 启动直接进 VFX 编辑器并播放，附加 `VFXL_VFX_MOVE`（虚拟投射物飞行）/ `VFXL_VFX_LOOP`（强制循环）/ `VFXL_VFX_CLOSE`（近景、藏参考模型）；`VFXL_PROJ_AUTOTEST=<投射物 profile 名>` 启动直接进 F4 并选中这个弹；`VFXL_BATTLE_AUTOTEST=1` 直接进战斗，5 秒给升级经验、9 秒移到最近的箱子旁、10 秒模拟按 F，反馈特效和相机状态带系统毫秒时间写进 `x64/Debug/autotest.log`（三选一要靠外部发 Enter）；`VFXL_BATTLE_AUTOTEST=bomber` 改跑自爆兵：1 秒停施法、停刷怪、清场、出 3 个自爆兵（应当点燃→爆炸），9 秒在玩家脚下放跟随的伤害圈、把火球放进背包并恢复施法，再出 3 个（应当被打死、不爆炸），GPU counter 和 HP 一变就记一行、每秒记一次 fps 和 MP（current / max / pendingSpend），并关掉碰撞框、法杖、格子这几项调试显示（`UpdateAutoTestBomber`）；`VFXL_BATTLE_AUTOTEST=slide` 测滑铲：2 秒后在高度图里找最长的连续下坡（1m 一步、每步下降 0.15〜0.9m、最多看 40m）把玩家放坡顶、镜头转向滑行方向（`FollowCamera::SetYaw`），用 `PlayerControlSystem::testInput/testMove/testSlide/testJump` 代替输入：跑 0.4 秒 → 滑 3 秒 → 跳，再到平地跑 0.4 秒 → 按住滑 2.6 秒，每 0.1 秒记速度 / vy / 坡度 / 接地 / 是否在滑 / 高度；然后站到全图最高的格子（一般是高台二层顶），找能一路走下去的方向，镜头 45m、从这个方向的一侧平视 1.5 秒（看台阶的侧面）、再从另一侧俯视 50 度 1.5 秒（看崖的影子落到地上）（`slide view`），再 `slide done`（`UpdateAutoTestSlide`）；`VFXL_BATTLE_AUTOTEST=perf` 测负荷：不发按键、经验每帧清零（不出三选一），4 秒后每 8 秒切一段（默认 / 藏野原装饰物 / 默认 / 关调试显示 / 两者都关 / 关调试 + 装饰物只视锥剔除 / 关调试 + 装饰物不剔除 / 默认），每段后 6 秒的平均 fps、平均/最长帧 ms、雑魚数、画了几个装饰物写进 autotest.log，最后写 `perf done`（`UpdateAutoTestPerf`，配合 `VFXL_NO_VSYNC` 用；驱动脚本关窗口）；`VFXL_NO_VSYNC` 关垂直同步、`VFXL_FPS_CAP=<数>` 限帧、`VFXL_BLT_SWAPCHAIN` 退回旧的 blt 交换链（新旧对比用）（都在 `Graphics::Initialize` / `CreateSwapChain` 读，只设初始值）；`VFXL_REF_MAGE=<片段名,片段名,...>` 直接进 VFX 编辑器，参照模型换成玩家的 Mage（实际尺寸、侧面、近景），按顺序每 3 秒换一个片段，切换时把系统毫秒时间和片段名写进 `refclip.log`（`VFXEditorScene`）；`VFXL_TERRAIN_SEED=<数字>` 固定地形；`VFXL_SRGB` 开启贴图 sRGB 解码；`VFXL_SHADOW_CASCADES` 影子按级联染色、`VFXL_NO_SHADOW` 关影子（`ShadowMap::Initialize`）；`VFXL_SUN_PITCH=<度>` 改太阳高度（`SceneLighting::Init`）；`VFXL_NO_GRASS` 关草地（`GrassRenderer::Initialize`）。bomber 自测开始时把镜头抬成俯角 50 度、距离 10m（看脚底圆影和警告圈）。代码在 `Core/Game.cpp`、`VFXEditorScene::Init`、`ProjectileEditorScene::Init`、`CollisionTestScene::UpdateAutoTest`（`Scene/CollisionTestSceneDebug.cpp`）。示例特效 `SheetTest.json`（四种图集/混合对比）。

## 6. 可交互道具（已完成）

- `Component/InteractableComponent`（kind / 半径 / 案内文字 / 浮动与光的参数）+ `ECS/System/InteractionSystem`（浮动旋转、找最近的 focus、返回被按下的实体、积点光源）。效果由场景按 `kind` 分派。
- 报酬箱：`ECS/System/RewardCrateSystem`（`Spawn` 摆放、`TryOpen` 开箱），开局在玩家周围 6〜22m 的可走格子（周围 3x3 也可走、且高度相同）放 4 个，用完即消失、不刷新；地形重建时重新摆。像素木箱 `Kenney_RetroFantasy/fbx/detail-crate.fbx`，缩放到 0.9m，带静态 AABB 和暖黄点光源。
- 靠近后画面下方出「[F] Open」（`GameUI::SetPrompt`），F / 手柄 B 触发 → `LevelUpSystem::OfferChoices`：和升级一样的三选一，但不升级、不扣经验；选择中不能再开（箱子保留）。
- 调参在战斗场景「Reward Crates」面板。

之后的候选（未定）：战斗场景读取关卡编辑器的关卡、流场寻路的实机验证、Phase 5（SpawnDirector GPU 化等）、**认真找 UI 素材**（用户日程：面板/按钮/边框/像素日文字体，统一 UI 风格，CC0 优先，下载前先给候选）。
