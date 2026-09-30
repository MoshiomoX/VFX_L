# 未使用代码 / 素材清单（2026-09-30）

只是清单，**什么都没删**。每项后面的 `[ ]` 留给你写决定（删 / 留 / 待定）。
「置信度」= 两个搜索代理用 grep 统计得出的把握；没有构建验证，删之前会再构建一次。

约定：「活的场景」只有 Game.cpp 里注册的 6 个（战斗、VFX 编辑器、F4、关卡编辑器、标题、结算）。
TestScene / GYScene / GPUParticleTestScene 虽然还在编译，但注册被注释掉了（Game.cpp:25-31），任何只有它们才用的东西都算死代码。

---

## A. 源码：整文件（编译着，但没有任何活代码引用）

**2026-09-30 已删（用户决定）**：CPU→GPU 的旧路径（ChaseAI、ContactDamage、ProjectileSystem / HitEvent、ProjectileVFX、Projectile 组件 + 公告板 + WeaponSystem 的 CPU 分支）、TestScene / GYScene / GPUParticleTestScene + Skybox + 其 shader、旧 GameObject 模型（Object/、Component.h、MeshRenderer、ModelRenderer）。下表里这些行保留作记录。第二轮又删了 `GPUCollider.h`、`ImGuiManager`、`ConstantBuffer.h`、示例状态机（StateMachineSystem / CounterStates / ECSComponents）、`GameTimer`（`Game::m_Timer`）——即全部「0 个 include」的头文件。

| 决定 | 文件 | 是什么 | 证据 | 置信度 |
|---|---|---|---|---|
| [ ] | `Scene/TestScene.h/.cpp` | PBR / 蒙皮测试场景 | 注册被注释；`Application.cpp:3` 无意义地 include 了它 | 高 |
| [ ] | `Scene/GYScene.h/.cpp` | 「課題用」粒子场景 | 注册被注释 | 高 |
| [ ] | `Scene/GPUParticleTestScene.h/.cpp` | 旧粒子测试场景 | .cpp 215 行全部注释掉；.h 里的方法从未定义 | 高 |
| [ ] | `Graphics/Renderer/Skybox.h/.cpp` | 旧的球形天空盒 | 只有 TestScene / GYScene 用；活的天空是 SkyRenderer | 高 |
| [ ] | `Debug/ImGuiManager.h/.cpp` | 旧 ImGui 单例 | 0 个 include；活的是 ImGuiRenderer | 高 |
| [ ] | `Enemy/ChaseAISystem.h/.cpp`、`Enemy/ChaseAIComponent.h` | CPU 雑魚追踪 AI | 0 个 include；参数早已搬到 GPU（SwarmTypes.h:263 注释） | 高 |
| [ ] | `Enemy/ContactDamageSystem.h/.cpp` | CPU 接触伤害 | 0 个 include | 高 |
| [ ] | `State/StateMachineSystem.h/.cpp`、`State/CounterStates.h/.cpp`、`State/ECSComponents.h/.cpp` | 示例用的「Counter」状态机 | 0 个 include；ECSComponents.cpp 是 0 字节。**`State/StateMachine.h` 要留**（VFXStates 用） | 高 |
| [ ] | `Object/TestCube.h/.cpp` | 立方体顶点助手 | .cpp 全注释；0 个 include | 高 |
| [ ] | `Object/GameObject.cpp` | 空文件 | 0 字节 | 高 |
| [ ] | `Graphics/ConstantBuffer.h` | `ConstantBuffer<T>` 模板 | 0 个 include | 高 |
| [ ] | `Collider/GPUCollider.h` | GPU 碰撞体结构 | 0 个 include；ParticleCommon.hlsli:177 对应的 HLSL 结构也没人用 | 高 |
| [ ] | `ECS/System/ProjectileSystem.h/.cpp`、`ECS/System/HitEvent.h` | CPU 投射物命中系统 | `CollisionTestScene.h:194` 的 `m_ProjectileSystem` 成员从未被调用 | 高 |
| [ ] | `Core/Timer/GameTimer.h/.cpp` | 倒计时器 | 只有 `Game::m_Timer` 成员，从未使用 | 高 |
| [ ] | `Object/ObjectManager.*`、`Object/GameObject.h`、`Component/Component.h`、`Component/MeshRenderer.*`、`Component/ModelRenderer.*` | 旧的 GameObject / Component 模型 | `SceneBase::CreateObject` 0 个调用者（唯一一处在死场景里被注释）；TransformComponent.h:5 写着「廃止予定」。删的话要改 SceneBase.h/.cpp | 中高 |
| [ ] | `ECS/System/ProjectileVFXSystem.*`、`Component/Projectile/ProjectileVFXComponent.h` | CPU 投射物特效 | 只有 `RegisterVFX` 被调（模板加载了但没人用）；`AttachVFX/Update` 0 个调用者 | 中高 |
| [ ] | `Component/Projectile/ProjectileComponent.h`、`ProjectileVisualComponent.h`、`Graphics/Renderer/ProjectileBillboardRenderer.*` + `WeaponSystem.cpp` 的「CPU 経路（従来）」分支 | CPU 投射物路径 | 两个 WeaponSystem 实例都 `SetSwarm` 了非空指针，这个分支跑不到；billboard 渲染器每帧对空 view 画一次；StressTestTools 也拿它当参数 | 中 |

## B. 着色器

| 决定 | 文件 | 证据 | 置信度 |
|---|---|---|---|
| [ ] | 工程根目录的 `ParticleVS.hlsl` | 不在 vcxproj；和 `Shader/ParticleVS.hlsl` 一模一样 | 高 |
| [ ] | `Shader/ParticleVS.hlsl`、`Shader/ParticlePS.hlsl` | 不在 vcxproj；和 `Shader/Particle/` 下的副本一样 | 高 |
| [ ] | `Shader/InitDeadListCS.hlsl` | 不在 vcxproj；C++ 只加载 `Shader/Particle/InitDeadListCS.hlsl`，这份是旧版 | 高 |
| [ ] | `Shader/Particle/ParticleVS.hlsl`、`ParticlePS.hlsl` | 在 FxCompile 里，但没有 C++ 加载（渲染用的是 GPUParticleVS/PS） | 高 |
| [ ] | `Shader/SkyVS.hlsl`、`Shader/SkyPS.hlsl` | 只有死的 Skybox.cpp 加载（活的是 `Shader/Sky/`） | 高 |
| [ ] | `Shader/Skinning/SkinnedPS.hlsl` | 只有死的 TestScene 加载 | 高 |

所有 `.hlsli` 都有活的 include。

## C. 活文件里没人调用的类型 / 函数（0 个调用者，跳过了简单 getter）

| 决定 | 位置 | 项目 |
|---|---|---|
| [ ] | 声明了没定义 | `GPUParticleSystem::CreateRenderStates`、`VFXEditorScene::SetupPBRMaterials` |
| [ ] | `Collider/CollisionMath.h` | `Segment`、`IntersectSegmentSphere`、`IntersectSegmentCapsule`、两个 `IntersectCapsuleCapsule`；`CollisionSystem::OverlapSphere` |
| [ ] | `Enemy/EnemyTags.h` | `MobTag`（注释说 SpawnDirector 用，实际 0 处） |
| [ ] | `Player/PlayerStateComponent.h` | `enum class StateLayer` |
| [ ] | `Item/ExpRewardComponent.h` | EliteSpawner 加了，但没人读（中） |
| [ ] | `ResourceManager` | `LoadVS_CSO / LoadPS_CSO / LoadCS_CSO`、`LoadMesh`、`LoadMaterial`、`LoadTextureAsync`、`IsPreloadDone`、`UnloadMesh / UnloadModel / UnloadTexture / UnloadVFXTemplate` |
| [ ] | `InputManager` | `GetKeyRelease`、`GetPadRelease`、`GetPadLeftTrigger / RightTrigger`、`SetVibration`、`IsPadConnected` |
| [ ] | `DebugManager` / `TestSpawner` / `PrimitiveBuilder` | `DrawRay`、`DrawRaycast`、`GetActiveCamera`；`SpawnDynamicSphere`、`SpawnStaticSphere`；`CreateSphere` |
| [ ] | `RenderStates` | `ApplyUI`、`Wireframe`、`DepthRead`；`ApplySkybox`、`CullFront`、`DepthLessEqual` 只有死的 Skybox 用 |
| [ ] | 渲染 / 数据类 | `TextRenderer::DrawUTF8`、`Shader::Compile`（运行时编译）、`Mesh::ModifyVertices`、`Material::SetTextureSlot`、`Model::AddMaterial`（`SetMaterial / GetMaterialCount` 只有死文件用）、`View::EachSafe`、`SparseSet::GetDense`、`CameraBase::SetUp`、`EngineTimer::TotalTime` |
| [ ] | 游戏系统 | `WeaponSystem::SetProjectileModel / GetSpawned`、`SwarmSystem::ClearAreas / GetEnemyModel`、`AreaVFXPlayer::StopInstance`（今天刚加的，光线用不到）、`VFXEditor::DrawEntryInspector` |
| [ ] | 小助手 | `GridWorld::IsAreaWalkable`（`IsWalkableAt` 只有死的 ChaseAI 用）、`ItemShape::Around8 / RowLine`、`IsSpellSource`、`ShapeSprite::DrawItem`、`GPUParticleEmitter::SetColorKeyOffset`、`StateMachine::SetOnStateChanged` |
| [ ] | `ResourcePaths.h` 0 处使用的常量 | Shd: `DefaultVS / DefaultPS / Sky_VS / Sky_PS / VFXMesh_VS / VFXMesh_PS / NoiseGen_CS`；Dir: `ShaderDir / VFXTex / Assets / Models / Particles`；VFX: `Slash / Noise001-003 / Lightning`；Mdl: `Rock2 / Rock2_Obj（文件不存在）/ Akai / Jiandu_* / Paladin / KayKit_Mage / KayKit_SkeletonMage / KayKit_SkeletonRogue / Forest::kGrass`；Tex: `Rock2_* / Akai_* / TestBlockTex / JaneDoe_*`；Icon: `Magnet`；只有死场景用的：`Shadowkin / Silver_* / Pants_* / Paladin_SwordAndShieldIdle / SkyboxSphere / SkyboxPanorama` |

## D. 枚举 / 场景

| 决定 | 项目 | 证据 |
|---|---|---|
| [ ] | `SceneType::TEST / GAME / GPU_PARTICLE_TEST / GY` | GAME 连类都没有；其余三个未注册，`RequestChangeScene` 会拒绝，任何按键 / 场景都不请求它们 |

## E. 素材（Assets 750 MB 里约 690 MB 没被运行中的游戏用到）

删素材前注意：除 dglopez 外都在 git 里，`.git` 已 492 MB、没用 LFS，删文件不会缩小历史。

### E1. 模型（整个文件夹不用，约 376 MB）

| 决定 | 文件夹 | 大小 | 证据 |
|---|---|---|---|
| [ ] | `Assets/Model/Akai` | 117 MB | 常量 0 处使用 |
| [ ] | `Assets/Model/Shadowkin_SF` | 97 MB | 只有死的 TestScene 用 |
| [ ] | `Assets/Model/field` | 48 MB | 0 处 |
| [ ] | `Assets/Model/Jiandu` | 40 MB | 常量 0 处使用 |
| [ ] | `Assets/Model/Rock-Set` | 19 MB | 只有 `VFXL_ASSET_SET=rocks` 自测用；石弾用的是 `VFX/Mesh/Rock_2.fbx` 那份 |
| [ ] | `Assets/Model/house` | 19 MB | 只有死场景里一行注释 |
| [ ] | `Assets/Model/KayKit_Mage` | 20 MB | 旧玩家模型，CLAUDE.md 说留作回退（只在 PlayerFactory.h:57 的注释里） |
| [ ] | `Assets/Model/plane` | 15 MB | 0 处 |
| [ ] | `Assets/Model/KayKit_CharacterAnimations` | 2 MB | 只通过 `kExtraAnims` 挂在 KayKit_Mage 上，Mage 不加载就不用 |
| [ ] | `Assets/Model/Skybox` | 2 MB | 只有死场景用 |
| [ ] | `Assets/Model/spot` | 1.5 MB | 0 处 |

### E2. 模型（部分不用）

| 决定 | 位置 | 不用的部分 |
|---|---|---|
| [ ] | `testAnimModel` | `Idle.fbx` 在用（VFX 编辑器参照模型）；`PaladinWPropJNordstrom.fbx`（0 处）和 `SwordAndShieldIdle.fbx`（只有死场景）约 18 MB |
| [ ] | `KayKit_Skeletons` | Warrior 在用（精英）；Minion 是雑魚的后备；`Skeleton_Mage.fbx`、`Skeleton_Rogue.fbx` 0 处（约 43 MB，ResourcePaths.h:326 自己写着「未使用」）；`sample_contents.png` 1.5 MB |
| [ ] | `Kenney_BlockyCharacters` | 只用 `character-l.fbx` + `texture-l / texture-g`；其余 17 个 fbx（8.7 MB）+ 16 张贴图 |
| [ ] | `Kenney_RetroFantasy` | 代码只用 crate 和 gate 2 个；其余 103 个只出现在关卡编辑器列表（3 MB，低优先） |
| [ ] | `KayKit_Forest` | 43 / 105 在用；其余 62 个只在关卡编辑器列表（1.7 MB，低优先） |
| [ ] | `Quaternius_ModularRuins` | 30 / 92 在用（含今天的遗迹墙）；其余 62 个只有 `assets` 自测用（7.1 MB） |
| [ ] | `Quaternius_UltimateNature` | 23 / 31 在用；不用的 8 个 310 KB |
| [ ] | `dglopez_WesternDesert`（本机、不在 git） | 16 / 30 在用；14 个 fbx + 整个 `GLB/` 不用 |
| [ ] | 各素材包的 `sample*.png` 预览图 | 共 3.1 MB，0 处 |

### E3. 贴图 / 字体 / 粒子

| 决定 | 位置 | 证据 |
|---|---|---|
| [ ] | `Assets/Texture/Noise/` 整个文件夹（509 KB） | 代码、着色器、json、脚本、文档都 0 处 |
| [ ] | `Assets/Texture/UI/TestBlockTex.png` | 常量 0 处 |
| [ ] | `Assets/Texture/UI/Icons/Magnet.png` | 磁铁是代码生成的模型，图标没人用（BuildGameIcons.ps1 还在生成它；中） |
| [ ] | `Assets/Fonts/NotoSansJP.spritefont`（46 MB） | 旧字体，CLAUDE.md 说留着对比 |
| [ ] | `Assets/Fonts/font_jp.spritefont`（25 MB） | 0 处代码引用；**vcxproj:1677 / filters:1523 有条目，删文件要一起删** |
| [ ] | `Assets/Fonts/BebasNeue-Regular.ttf`、`RobotoMono-Regular.ttf` | 0 处 |
| [ ] | `Assets/Particles/` 散图：`9.png`、`Particle100.png`、`circle_05.png`、`particlesSheetPerso.png`、`smoke.png`、`smokePerso.png`、`smokePerso2.jpg`（956 KB） | 0 处（图集 json 里的 circle_05 是图集内的帧名，不是这个文件） |

### E4. VFX 数据与素材

| 决定 | 位置 | 证据 |
|---|---|---|
| [ ] | `VFXData/Debug.json`、`GY.json`、`Hanabi.json`、`Test2.json`、`Lightning.json`、`TestMesh.json`、`TestMesh2.json`、`TrailTest.json`、`EffectTrailTest.json` | 代码、数据、文档 0 处（只能在编辑器手动打开） |
| [ ] | `VFXData/SheetTest.json`、`SpriteTest.json`、`MeshParticleTest.json` | 没有游戏引用，但 CLAUDE.md 里当示例写着 |
| [ ] | `VFXData/FireCircle.json` + `AreaData/FireCircle.json` | 在 VFXDatabase 里占一个 GPU 配方槽，但没有道具用（火の輪道具从未做） |
| [ ] | `AreaData/Explosion.json` | 没有道具 / hitArea 按名字引用（火球 9-30 取消爆炸后只剩 MeteorBlast 那份数值）；只在 F4 / Area Test 菜单里出现 |
| [ ] | `Assets/VFX/Tex/`（108 MB） | 没有任何活特效用；`Noise_001/004/006/019`（23 MB）只有孤儿 TestMesh 用，其余 22 张（85 MB）0 处。编辑器的贴图下拉里能选到 |
| [ ] | `Assets/VFX/Mesh/`（96 个，4.4 MB） | 活的只有 `gonjian.FBX`（黄金の矢）和 `Rock_2.fbx`（石弾）；`bingci_02.FBX` 只有示例用；`Slash.fbx`、`basic_skybox_3d.fbx` 只有孤儿 TestMesh 用；`gongjian_01.FBX`、`jian02.FBX` 只有 `arrows` 自测用；其余 89 个 0 处 |
| [ ] | `Assets/VFX/SpriteSheet/PVFX/`（41 个） | 活的 5 个 + 示例 3 个；其余 33 个不用（1.4 MB，CC0 整包，删的价值低） |

### E5. 工具脚本

六个脚本的产物都还在用，没有过时的。小问题：`BuildGameIcons.ps1` 还在生成没人用的 `Magnet.png`；`BuildParticleSheets.ps1` 头部写的 `Sheets/README.md` 实际是 `README.txt`。

## F. 注释里标了「旧 / 互換用 / 未使用」但还活着的（不建议删，列出来供参考）

- `VFXEffect.h:44` 「互換用」的 `IsPlaying / IsFinishing`（VFXEditor 在用）
- `GameUI.cpp:503`、`DragContext.h:20` 「デバッグ用の旧経路」ImGui 调色板拖拽
- `TransformComponent.h:14` `scale` 字段「未使用」（TerrainGenerator 其实用了）
- `GPUParticle.h:172` / `GPUParticleSystem.cpp:743` `deadCount` 「未使用」
- `Graphics.cpp:120` `VFXL_BLT_SWAPCHAIN` 旧交换链、`SceneLighting.cpp:38` `VFXL_SRGB=0` 旧画面 —— 对比用的开关
- TEMP-TEST：`CollisionTestSceneDebug.cpp` 里 19 个 `UpdateAutoTest*`（约 1,700 行）、各处环境变量钩子。这些是自测在用的，不算死代码；要不要瘦身另议。

---

## 如果按「高置信度」一次删干净，要顺手改的地方

- `Core/Game.cpp:3-5`、`Core/Application.cpp:3` 的 include
- `CollisionTestScene.h:194` 的 `m_ProjectileSystem`、`Game.h:21` 的 `m_Timer`
- `VFX_L.vcxproj` / `.filters` 里对应的 ClCompile / ClInclude / FxCompile / None 条目
- `ResourcePaths.h` 里对应的常量
- `SceneType` 枚举的 4 个值和 `SceneTypeName`
