# 地图编辑器（F6 Battle Map）

> 2026-11-13 从 CLAUDE.md 原样搬出（文字未改）。之后这一块的新记录直接追加到这里。


**地图作为数据保存（方案已谈、用户未选档。2026-10-05 用户问「现在的地图能不能在 F6 里编辑」「作为数据保存做不到吗」）**：现状 = 战斗地图每局由 `TerrainGenerator` 按种子生成，F6 只存摆放物的 `LevelData/<名>.json`，战斗场景不读它，所以 F6 里编辑不了战斗地图。我给了三档，等用户选：
1. **存「种子 + 生成参数」**（半天以内，我推荐先做）：生成是确定性的，存一个小 json（seed、`TerrainGenerator::Config` 全部参数、关卡号）= 存下整张图，读时重新生成。能固定 / 命名 / 指定每关用哪张图，不能手改某一处。
2. **种子 + 手动修改的增量**（几天）：另存「删这棵树 / 加这块岩 / 箱子和 Boss 门挪到哪」的列表，生成后套用；F6 里打开生成好的地图增删摆放物。通行格 / 碰撞体 / 渲染三处要同步（现有的 `BlockPropCells` 封格机制可扩展）。不能改地形形状。
3. **整张图的结果存成数据、完全手工编辑**（一两周以上）：要把生成器拆成「决定布局」和「按布局建造」两半（格子 + 高度场、碰撞体、合并网格、起伏垫子、流场台阶、草的范围都要从文件重建），再给 F6 做地形编辑工具。除非确定要做手工关卡否则不划算。

**→ 2026-10-05 晚用户定了第 3 档（「地形、碰撞箱全都想改」），未动手，排在背包手柄操作之后做。** 用户选的三项（都是推荐项）：① 起点 = 按种子生成后固化成数据、再在 F6 里手改；② 地形工具 = 按 2m 格子刷高度 + 台地 / 坡道 / 高台当可摆放的部件（不做自由雕刻）；③ 随机地图保留，每关在 `StageConfig` 指定用存好的图还是随机生成。分四步，每步做完能单独用：
1. 拆生成器 + 存读：布局存文件，战斗场景能读文件建图，验证读出来的图和生成的一样（这一步不能编辑）。
2. F6 打开地图、编辑摆放物：树、岩石、箱子、Boss 门增删移动，碰撞盒和封格自动跟着变。
3. 手动碰撞箱 / 封路区域：看不见的箱子，可选是否挡雑魚。
4. 地形工具：改格子高度、放台地 / 坡道 / 高台、改山顶和矿洞轮廓。

现状备忘：`World/LevelData.h` 的存档只有模型 / 位置 / 旋转 / 缩放（不带碰撞和通行信息），`LevelEditorScene` 只有 200m 平地、不碰生成器。

**第 1 步已做（2026-10-05 晚，拆生成器 + 存读；第 2〜4 步未动手）**：
- **做法 = 记录 / 重放**，没有把 1700 行的 `Generate` 改写成「先出布局再建造」：生成器照旧一边用随机数决定一边建，但所有「造东西」的出口统一走 `TerrainBuild::Emitter`（`World/TerrainBuild.h/.cpp`，从 `TerrainGenerator.cpp` 分出来的「建造侧」：配色表、噪声、`ReliefField`、`Emitter`、地面网格 `SpawnGround`、高度场碰撞 `SpawnHeightField`、`GroundColor`、`BuildFromMap`）。`Emitter` 的 5 个出口 = `Box`（AABB 碰撞）/ `Hull`（凸体碰撞）/ `Visual`（合并模型里的一个六面体：8 顶点 + 顶 / 侧色 + 贴图层）/ `Prop`（摆放物：模型路径 + 位置 + yaw + 缩放）/ `Block`（封格矩形），每个出口建实体的同时往 `MapData::Map` 记一条。`TerrainGenerator::BuildFromMap` 把记录原样喂回同一个 `Emitter` → 读出来的图和生成的图走的是同一份建造代码、同样的参数。
- **`World/MapData`**：土台（每格区域 zone、洞壁 caveRing、起伏两张节点表、格子的通行 / 高度场（生成器结束时、场景后处理 `BlockUnreachablePockets` / `BlockPropCells` 之前）、草掩码）+ 记录（boxes / hulls / visuals / props / blocks / 火把）+ `Layout`（山顶 / 矿洞格、最深处、坡）+ 表头（seed、biome、relief、summitH 等）。每条记录带 `Tag { kind, group }`：`kind` = 属于什么（`kFloor / kOuterWall / kSummitRamp / kMineRamp / kCaveRoof / kTerrace / kPlateau / kSkirt / kTree / kRock / kBush / kEdgeRock / kRuinWall / kTorch / kRoofRock / kManual`），`group` = 同一个物体出的记录同号（一棵树 = 外观 + 碰撞盒 + 封格；一个台地 = 本体 + 坡道 + 二层，`Plateau::tag`）→ 第 2 步按 group 删 / 移。
- **文件**：`Assets/Data/MapData/<名>.vmap`，小端二进制（起伏 + 高度场大，一张约 2.1MB；逐字段写、不写结构体填充字节，同一张图一定是同一串字节），`kVersion` 1，读时校验魔数 / 版本 / 各数组大小 / 模型下标。**格式和位置是我按推荐定的，没问用户**（要改：`MapData.cpp` 的 `kDir` 和 `Serialize / Deserialize`）。素材缺失的模型（dglopez）读图时只跳过外观、记录保留。
- **场景**：`Scene/CollisionTestSceneTerrain.cpp`（`BuildTerrain` = `m_MapFile` 非空就读图、失败则生成；`RebuildTerrain` = 原来 Regenerate 按钮里的整套重建）。场景一直持有当前地形的记录 `m_TerrainMap`。读图时 `m_TerrainConfig.seed / biome` 改成图里的（箱子、Boss 门、磁铁、草色照旧按 seed 摆 —— 它们还不在图里，是第 2 步的事）。每关用哪张图 = `StageDef::mapFile`（`World/StageConfig.h`，nullptr = 随机生成，现在三关都是 nullptr）；`VFXL_MAP=<名>` 启动时覆盖。Terrain 面板「Map Data」：名字框 + Save Map / Load Map，显示当前是生成的还是读的、各类记录数；Regenerate = 丢掉读的图按 seed 重新生成。
- `GridWorld::Walkable() / SetData()`（整表读写）。`Generate` 多了 `outMap` 参数；草掩码 / 火把 / Layout 改成总是算、最后再分发。
- 自测 `VFXL_BATTLE_AUTOTEST=mapio`（`Debug/AutoTest/AutoTestMapIO.cpp`）：1 秒把当前（生成的）记录序列化成 A 并存 `_autotest.vmap`，记实体摘要（`m_Terrain` 每个实体的位置 / 缩放 / 碰撞形状大小层 / 有无模型的哈希，求和 = 与顺序无关）和格子哈希，`mapio look gen`；2.5 秒读回重建，序列化成 B，记 `mapio load sameBytes sameEntities sameGrid`，`mapio look load`；4.5 秒删文件、done。实测（seed 12345）：草原 2,095,862 字节 / 1205 个实体、遺跡 2,134,591 字节 / 2798 个实体，三项全是 1；前后截图地形一致。砂漠没单独跑。
- 没验证：重构前后同一 seed 生成的图是否逐位相同（随机数的调用顺序没动；摆放物碰撞盒从「中心 + 半径」改成「lo / hi」再换算，会有浮点末位的差）。`TerrainBuild.cpp` 约 530 行、`TerrainGenerator.cpp` 仍有约 2000 行（一个大函数，没再拆）。

**第 2 步已做（2026-10-05 晚，F6 里编辑摆放物；用户定的三项：放在 F6 里 / Unity 式 Transform 箭头 / 箱子和 Boss 门做成自带功能的预制道具。用户第一句是语音识别的「下在福禄宫甲里」，我按「放在 F6 里」理解的）**：
- **入口**：F6 顶部小窗口「Editor Mode」切「Prop Level (.json)」（原来的功能）/「Battle Map (.vmap)」。地图模式 = `Scene/MapEditMode`（`MapEditMode.cpp` 操作、`MapEditModeView.cpp` 显示、`MapEditModeUI.cpp` 窗口「Battle Map Editor」），`LevelEditorScene` 持有 `m_MapEdit`，相机、素材列表（Level Assets）、放置物的朝向 / 大小随机仍用场景原来的。切到地图模式时关 y=0 参考网格。
- **开图**：窗口里按名字 Load，或「Generate」（seed + Biome，和战斗同一个生成器，生成的实体丢掉、只留记录）。地形（地面网格 + 合并模型）用 `BuildFromMap(..., kPartVisuals | kPartGround)` 只建外观显示（`TerrainGenerator::BuildParts`，新参数 `parts`）；摆放物由编辑器自己逐个画（Debug 下约 1000 个没问题，遺跡 2500 个没测帧率）。
- **操作**：左键选（射线 × 带旋转的包围盒，被地面挡住的不选）；移动 Gizmo（原有的 `Gizmo::Translate`，带吸附）；水平拖动时高度跟着地面走（「Follow ground when moved」）；R / Shift+R 转 45°；Del 删；Ctrl+D 复制；F 聚焦；X 取消选择 / 停止放置；Ctrl+S 存。从素材列表选了以后点地面 = 新加一个（`kManual`，底贴地，足迹长边 ≥ 1.2m 的自动带碰撞）。**只有移动箭头，没有 Unity 的旋转环 / 缩放手柄**（旋转、缩放、位置数值在右侧窗口里改）。
- **碰撞和封格自动跟着变**：数据操作在 `World/MapEdit`（不依赖编辑器画面）。「单个摆放物」的物体（树 / 岩 / 灌木 / 新加的）每次移动、旋转、缩放后用 `SetPropCollision` 按生成器同样的公式重做碰撞盒（Trunk = 0.6m 方柱高 ≤ 3m / Footprint = 旋转后的包围盒 × 0.85 / None），并从碰撞盒的足迹重做封格记录，再 `RebuildWalkable`（通行表 = 全开 → 套上所有封格记录；生成的图上实测和生成器的通行表完全一致）。窗口里可以给任何单个摆放物换碰撞方式（例：给灌木加碰撞、把树改成装饰）。外围岩石 / 遗迹墙这类（带凸体或多条记录）只能整体平移（`MoveGroup`）和删除。火把的光源位置（`map.torches`）跟着 1.6m 内的火把模型一起移 / 删。选中物的碰撞画绿线、封的格子画红框。
- **预制道具（箱子 / Boss 门）**：`MapData::Placement { type (kPlaceCrate / kPlaceBossGate), pos = 底部中心, yawDeg }`，存在图里（文件版本 1 → 2，版本 1 仍可读）。窗口「Add Reward Crate」「Add Boss Gate」放在画面中央看到的地面上，之后和别的物体一样选中拖动；门只能有一个。战斗场景 `RespawnCrates` 把图里的箱子交给 `RewardCrateSystem::Spawn(..., fixed)`、门交给 `StageDirector::SpawnPortal(..., fixed)`（两个函数里把「造实体」抽成了 `spawnAt` / `placeAt`），功能（金币开箱四选一、F 召唤 Boss、门柱碰撞、漩涡特效、封格）和原来完全一样；**图里没有的种类照旧按 seed 摆**。战斗 Terrain 面板的 Save Map 会先 `BakePlacements`：图里还没有箱子 / 门时，把当前场上的位置写进图（之后可以在 F6 里挪）。磁铁仍按 seed 摆，没进图。
- **试玩**：窗口「Save and play (F1 battle)」= 存盘 + `MapData::PlayOverride()` 设成这张图 + 切到战斗场景；战斗 Terrain 面板的 Regenerate 会清掉这个覆盖。
- 自测 `VFXL_MAPEDIT_AUTOTEST=1`（`Debug/AutoTest/AutoTestMapEdit.cpp`，启动直接进 F6；不走 `BattleAutoTest`，自己往 autotest.log 写 `mapedit ...`）：seed 12345 草原 → 选一棵带碰撞的树 → 移 (6, 0, 4) → 加 2 个箱子 + 1 个门 → 存 `_edittest` 再读回。实测：`walkableMatchesBlocks 1`，移动后碰撞盒同样平移（y 跟地面 +1.30）、原格解封、新格封上，存读逐字节相同。再用 `VFXL_MAP=_edittest` 跑 `mapio`：战斗里箱子 2 个（来自图）、门的位置和图里一致、读图重建仍逐字节相同。
- **没验证**：真实鼠标操作（点选、拖 Gizmo、点地面放置）—— 自测只走了数据接口，我不发鼠标；把箱子放在不能走的地方不会被拦（放的人负责）。
- 第 1、2 步已提交 025d272（game-balance，未推送）。

**第 3 步已做（2026-10-05 晚，手动碰撞箱 / 封路区域；未提交）**：
- 数据 = `MapData::Volume { tag(kManual), center, half（轴对齐）, solid, blockMobs }`，存在图里（文件版本 3，1 / 2 仍可读）。碰撞盒和封格是从它**派生**的：`MapEdit::ApplyVolume` 删掉这个 group 的 boxes / blocks 再重建 —— `solid` → 一条 `Box`（`Layer_Prop`：挡玩家，镜头遮挡射线不看，所以看不见的墙不会让镜头突然拉近）；`blockMobs` → 一条 `Block`（足迹内缩 0.15m 后覆盖到的格子，比一格小就封中心格）。战斗侧不用改：派生出的记录和别的记录一样被 `BuildFromMap` 重放。封的格子雑魚绕着走，**GPU 的弹也会在那格被挡住**（格子判定共用）。
- F6「Battle Map Editor」窗口新增一段「Invisible volumes」：`Add Collision Box`（挡玩家 + 挡雑魚）/ `Add Mob Blocker`（玩家能过、只挡雑魚），放在画面中央的地面上（默认 4×3×4m，底埋 0.3m）；「Show」开关（关掉就不画、也选不到）。全部体积画水色线框（不挡玩家的颜色暗一档），选中的是黄，派生的碰撞盒绿、封的格子红。选中后和摆放物一样用箭头拖（高度跟地面）、Ctrl+D 复制、Del 删；右侧可改 Center / Size（改高度时底不动）/ 两个开关 / `Sit On Ground`。实现上体积就是「有 Volume 记录的 group」，`MapEditMode` 的选择类型没加新的。
- 只有轴对齐的箱子，不能旋转；大小只能在窗口里改数值，没有拖拽手柄。
- 自测并进了 `VFXL_MAPEDIT_AUTOTEST`：`mapedit volume ...` 一行 = 加一个 6×3×4m → 碰撞盒 +1、封 8 格、中心格不可走 → 平移 4m 后封格跟着走（旧边缘格解封）→ 关 blockMobs 后封格 0、碰撞盒还在 → 开回来 8 格，`WalkableMatchesBlocks` 1；存读逐字节相同；`VFXL_MAP=_edittest` 跑 `mapio`：战斗里 boxes 121 → 122、blocks 312 → 313、实体 1205 → 1206。
- 没验证：真实鼠标操作；战斗里实际走过去撞这面看不见的墙 / 雑魚绕路（只看了记录和实体数）。
- 第 3 步已提交 c7d2335。

**第 4 步（地形工具）：用户 10-05 选的四项全是推荐项** = ① 改高度用「部件 + 涂层 + 起伏笔刷」（不做每格任意高度）② 部件 = 可改大小的矩形，箭头拖动（2m 吸附）、大小在窗口里改数值，坡道在窗口里选边添加 ③ 生成器已造好的台地 / 坡道 / 高台也变成可编辑 ④ 分三段：部件 → 涂山顶 / 矿洞轮廓 → 起伏笔刷。

**第 4 步第 1 段（部件）已做（2026-10-05 夜，未提交）**：
- **数据（文件版本 4，旧版仍可读但不能编辑部件）**：`MapData::BlockPart`（箱：格子矩形、bottom / top / base、顶侧色、`raise` = 顶面能走（否则封格）、`onGround`）、`RampPart`（楔：矩形、下坡方向 side、base / top、`grassy`、`owner` = 附在同 group 第几个箱上 + `offset`，−1 = 山顶 / 矿洞的坡）、`Pad`（压平的垫子：区域、节点范围、过渡宽度、高度 L，带 Tag）、`rawPlain / rawSummit`（垫子压平**之前**的起伏）、`padMargin / reliefMaxSlopeDeg / rampSlopeDeg`。文件约 2.1MB → 3.4MB。
- **生成器**：台地 / 高台 / 坡的建造改成先填部件再走 `TerrainBuild::EmitBlockPart / EmitRampPart`（`TerrainGenerator.cpp` 里的 `addBlock / addRamp`）；垫子的合成、矿洞周围压平、最后的坡度限制搬到共享函数 `ComputeMineWeights / ComposeRelief / LimitReliefSlopes`（`World/TerrainParts.cpp`，从 `TerrainBuild.cpp` 拆出来的：格子矩形 / 高度场辅助、起伏合成、部件建造）。`Emitter` 多了「只记录不建实体」的构造（`Emitter(MapData::Map*)`）。`emit.Begin` 挪到 `padRect` 之前（垫子要带同一个 Tag）。同一 seed 的生成结果和重构前记录数一致（草原 boxes 121 / hulls 122 / visuals 466，遺跡 210 / 33 / 466）。
- **`World/MapTerrainEdit`**（纯数据操作）：`Rederive` = 从原始起伏 + 垫子重算起伏 → 高度场（起伏面 → 部件抬高 → 洞壁格按生成器同样的找法填最近的非壁格高度）→ 通行 → 草掩码；`RegenRecords`（按部件重做该 group 的碰撞 / 外观 / 封格记录）、`RegenPads`（箱周围 1 格、土坡 + 降落口、草坡 + 5 格坡脚）、`Refresh`（先把 group 的脚下高度重新对到「去掉自己垫子后的地面平均」→ 附着的坡按箱重算矩形和高度 → 记录 → 垫子 → Rederive）、`MoveGroup(dx, dz 格)`、`DeleteGroup`、`AddPlateau`、`AddRamp`、`GroupAt`、`Verify`。
- **等价性（`Verify`，自测每次都跑）**：对刚生成的图重新推导 → 起伏逐位相同、通行相同、草掩码相同、部件的碰撞 / 外观记录相同（不计顺序）。高度场有 247 / 160000 个高度格不同：19 格是一排坡脚（两个部件的垫子重叠，实际地面比坡脚高约 0.5m，生成器写进高度场的是坡脚旧值 = 比看到的地面低；重新推导的值和地面一致，算是修正），其余是洞壁（不可走）格上厘米级的差（生成器是在最后一次坡度限制之前填的）。**只有在 F6 里动过地形部件的图才会带上这些差别**（不动部件就不会 Rederive，读回逐字节相同）。
- **F6**：窗口新增「Terrain parts」：`Add Plateau`（6×6 格、3m、一条土坡道）/ `Add Terrace`（8×8 格、6m、整面宽 18° 草坡），放在画面中央的地面。点台地 / 高台的任何位置（顶面或坡）= 选中整个部件（山顶 / 矿洞的坡不可选，属于第 2 段）。箭头拖 = 按 2m 格移动，脚下高度自动对到新位置的地面、原位置的地面恢复原来的起伏；右侧属性：每个箱的大小（格）、高度、`Walkable top`，二层的格子位置；每条坡的朝向、沿边偏移、宽、长（显示坡度，> 35° 红字提示雑魚上不去）、草坡开关、删除；`Add Ramp`（选边）。Del 删整个部件。数据每次改动立刻重算，地面网格的重建等松手后再做（`m_ViewDirty`）。属性 UI 在 `Scene/MapEditModeTerrainUI.cpp`。
- 自测（并进 `VFXL_MAPEDIT_AUTOTEST`）：`mapedit rederive ...`（上面的等价性）、`mapedit part move`（生成的台地移 4 格：顶 4.27 → 4.17、脚下 0.76 → 0.66 跟地面，原位置高度场回到地面）、`part add`（中央加 6×6 3m 台地 + 坡道：顶面高度场 = top，坡 26.6°，坡中点高度 = 一半）、`part delete`（删除后高度场回到加之前的值）、`mapedit look parts` 截图（新台地 + 坡道 + 周围压平的地面 + 黄色选中线）；存读逐字节相同；`VFXL_MAP=_edittest` 的 `mapio`：战斗里 boxes / hulls / 实体数和编辑后的图一致，草原 / 遺跡重新生成 + 存读也都通过。
- **没做 / 没验证**：真实鼠标操作；战斗里实际走上新台地、雑魚走新坡道（只核对了高度场和记录）；移动 / 新加部件**不检查重叠**（压到树、别的台地、洞口上都不拦，放的人负责；站在被移走的台地上的树会留在原处悬空或被埋）；二层只能在窗口里改格子位置，不会自动跟着本体缩放；没有撤销。
- 第 1 段已提交 8023c30。

**第 4 步第 2 段（涂山顶 / 矿洞轮廓 + 山顶坡 / 矿洞坡）已做（2026-10-05 夜，未提交）**：
- **生成器再拆一块**：`World/TerrainZones.cpp`（`TerrainBuild` 命名空间）= 由「区域的形状」决定的东西：`MaskRects`、`EmitFloorBoxes`（平原 / 坑底 / 山顶的崖壁碰撞箱）、`ComputeCaveRing`（洞壁 = 坑的 8 邻域、洞口除外）、`EmitCaveRoof`（洞壁封格 + 壁 / 顶 / 过梁的碰撞 + 洞顶外观）、`EmitRoofRocks`（洞顶堆的岩石，随机数的取法和顺序与原来相同）、`EmitCaveTorches`、`ComputeLayoutCells`（山顶 / 矿洞可走格、最深处）。生成器改成调这些函数；**重构前后同一 seed 的地图序列化哈希完全相同**（草原、遺跡都对过；`mapio gen hash ...`，注意哈希随文件版本变，只能在同一版本内比）。
- **文件版本 5**：`hasZoneParams` + 地面箱顶面高度（`floorPlainTop / floorSummitTop`）、`roofBottomY / roofCollTop / roofRockMin / roofRockMax`、`caveTorchSpacing`、`rimRock / rimSink`、`roofRockModels`、`torchModel`。版本 4 的图读得进来、能改部件，但不能涂区域（要重新 Generate）。
- **`World/MapZoneEdit.cpp`**（仍在 `MapTerrainEdit` 命名空间）：`PaintZone(cx, cz, 半径, 区域)` 只改 `map.zone`（外圈墙格不涂）；`RegenZones` = 删掉 `kFloor / kCaveRoof` 的记录重做 → 洞口 / 降落口由矿洞坡道部件推出（洞口 = 坡道高端外侧一列）→ 重算洞壁 → 重放洞顶岩石和洞内火把（岩石的随机数用图的 seed，**涂过以后原有洞顶岩石的排列也会换一套**）→ 山顶 / 矿洞格子表、最深处（= Boss 门按 seed 摆时的候选）、坡道表 → `Rederive`。`AddZoneRamp(summit?, x, z, side, 宽, 长)`：山顶坡 = 草坡、高端在山顶边、坡脚按地面重新对高度并压平（坡 + 前方 8 格 + 山顶侧 2 格）；矿洞坡 = 土坡、在坑内、−mineD → 0。`MapTerrainEdit::Refresh / DeleteGroup` 遇到矿洞坡会自动走 `RegenZones`。
- **等价性**：`Verify` 多了 `zonesSame`（对刚生成的图重做地面箱 / 洞壁 / 洞顶记录、洞壁格、格子表、最深处、坡道表，全部和生成器相同；岩石 / 火把因为随机数不同不比）= 1。
- **F6**：窗口新增「Zones」：`Paint` 下拉（Off / Plain / Summit / Mine）+ 笔刷半径；选了区域后按住左键在地面上拖 = 涂（画面上只有笔刷的圆圈，松手后才重建崖壁、洞壁、洞顶、岩石、火把和地面网格）；涂刷模式开着时点击不再选物体。`Add Summit Ramp` / `Add Mine Ramp` 放在画面中央（默认朝 +X 下坡、宽 4 格，长度按 28° / 31° 算）；山顶坡 / 矿洞坡现在可以点选、箭头按格拖、Del 删，右侧改格子位置、朝向、宽、长（显示坡度）。**坡道要自己对齐到区域边缘**（山顶坡的高端贴山顶边；矿洞坡放在坑里、高端贴坑边），没有自动吸附。
- 自测（`VFXL_MAPEDIT_AUTOTEST`）：`mapedit zone paint`（在中央西侧涂山顶、西北涂矿洞，各半径 3 格：共 74 格；高度场 = 16.08 / −10.00；洞壁格 89 → 121、洞顶岩石 297 → 319、火把灯 19 → 21、山顶 / 矿洞格子表各 +37）、`zone mineRamp`（坑里加坡道后，高端外那一格从洞壁变成洞口：ring 1 → 0、可走 0 → 1；坡中点 −4.99m）、`mapedit look zones` 截图；存读逐字节相同；战斗读编辑后的图通过（实体 1269 = 1269，箱子 / 门照旧）。
- **没做 / 没验证**：真实鼠标涂刷；战斗里实际走进新涂的矿洞 / 爬新山顶（只核对了高度场、封格和记录）；涂区域**不处理上面已有的东西**（树、台地、箱子留在原处，可能悬空或被埋；压平垫子也不跟着改）；涂出很小的矿洞时坡道放不下合适的长度（自测里那条是 45°）；涂掉全部矿洞后洞顶参数还在但没有洞；没有撤销。
- **卡顿（2026-10-05 夜，用户：「调试的时候这么卡，按理不该卡」）**：实测 F6 地图模式空闲时 Debug 4.9〜5.7 ms/帧，不卡；卡的是每次改地形（拖台地一格、改一个数、涂完松手）都整图重算：`MoveGroup` 960 ms、`Rederive` 704 ms（Debug）。原因是我写的三处热点：① `ComposeRelief` 每个节点把全部垫子过一遍（16 万节点 × 76 个）→ 改成按垫子遍历它影响到的节点（每个节点的累加顺序仍是垫子顺序）；② `LimitSlope` 整图最多扫 64 遍 → 第 2 遍起只查「上一遍或本遍动过的节点」相关的相邻对（查的顺序不变）；③ 热循环里用 `vector` 下标（Debug 有逐元素检查）→ 裸指针；另外「给部件重新对地面高度」从整图合成改成只合成脚下那块。**结果逐位不变**（遺跡的生成哈希 `903d8476dc51441e` 前后一致、`Verify` 全 1、编辑后的图字节数相同）。现在 `MoveGroup` 约 100 ms、`Rederive` 约 80 ms（合成 46 / 写高度场 30）、`RegenZones` 约 88 ms；松手后重建地面网格 `RebuildView` 约 250 ms 没动（一次操作只发生一次）。计时在自测的 `mapedit perf ...` 行（`MapTerrainEdit::detail::LastPerf` 是 TEMP-TEST）。Release 没测。
- 第 2 段 + 卡顿优化已提交 83682b1。

**第 4 步第 3 段（起伏笔刷）已做（2026-10-05 夜，未提交）= 地图编辑器四步全部做完**：
- **`World/MapReliefEdit.cpp`**（`MapTerrainEdit` 命名空间）：`BrushRelief(x, z, 半径 m, 模式, 量, 目标高度)` 改的是**垫子压平之前的原始起伏** `rawPlain / rawSummit`（笔刷中心那格是平原就改平原、是山顶就改山顶，矿洞底不动），形状 (1−(d/r)²)²，原始起伏夹在 −9〜+12m。四种模式：Raise / Lower（量 = 中心变化 m）、Smooth（往周围 3×3 平均靠，量 0〜1）、Flatten（往目标高度靠）。`FinishBrush` = `Rederive`（垫子、30° 坡度限制照旧叠上去 → 部件脚下的平地不会被破坏、不会出现雑魚上不去的陡坡）；如果地面被压到地面碰撞箱顶面以下，就把 `floorPlainTop / floorSummitTop` 降下去并 `RegenZones(map, false)` 重做地面箱（否则会踩到看不见的台阶）。
- **F6**：`Scene/MapEditModeTools.cpp`（区域涂刷的逻辑也搬到这里：`UpdateTools` / `DrawToolsUI`）。窗口「Hills」：`Sculpt` 下拉（Off / Raise / Lower / Smooth / Flatten）+ 半径（2〜30m）+ 强度；按住左键 = 每帧按「强度 × dt」作用一次（Raise / Lower 是 m/秒；Flatten 的目标 = 按下那一刻脚下的高度），画面上只有两圈笔刷圆，松手后才重算和重建地面。区域涂刷和起伏笔刷是互斥的两个工具，任一开着时点击不选物体。
- 自测（`VFXL_MAPEDIT_AUTOTEST` 的 `mapedit brush` 行 + `mapedit look hills`）：中央附近半径 10m：压低 8m → 实际 −3.5m（被坡度限制摊缓，沿线最大坡度 30.0°），地面箱顶面 −2.98 → −4.72（= 新的最低点 −4.62 再低 0.1）；Flatten 到 1m → 0.88；Raise 3m → 2.81（最大坡度 29.7°）；25m 外的点不变；截图里能看到圆丘。存读、战斗读图、遺跡生成哈希（`903d8476dc51441e`）都没变。
- **没做 / 没验证**：真实鼠标涂刷的手感（强度 × dt 的速度合不合适）；拖动时没有实时预览；站在上面的树 / 箱子不跟着升降；没有撤销。
- **起伏也变成「部件 + 整体参数」（2026-10-05 深夜，用户：「把地形的小山丘起伏也变成我们的模式」，语音输入有错字，问了以后选的是「两个都要」= 整体参数可调 + 单个山丘当部件；未提交）**：
  - 原始起伏拆成三层：**噪声缓丘**（`MapData::ReliefParams`：丘高 / 丘大小 / 细波高 / 细波大小 / 山顶倍率，存的是乘过 Biome 倍率后的值）+ **山丘部件**（`MapData::Hill { tag(kHill), x, z, radius, height }`，高度 × (1−(d/r)²)²，负值 = 洼地；生成器撒的 70 个土包就是它）→ 28° 坡度限制 → + **笔刷增量**（`sculptPlain / sculptSummit`，笔刷现在同时写原始起伏和这张增量表，改参数 / 山丘后重算时加回去）。文件版本 6（v5 的图不能编辑山丘，要重新 Generate）；用过笔刷的图多一张约 0.65MB 的增量表。
  - 生成器的 `BuildRelief` 只决定参数和土包位置，实际写起伏走共享的 `TerrainBuild::BuildReliefNoise / AddHillsAndLimit`（`TerrainParts.cpp`）。`Verify` 的 `rawSame` = 用参数 + 山丘重算的原始起伏和生成器的逐位相同；重构前后自测里依赖起伏的读数（树的位置、台地脚下高度等）到小数点后两位都没变。
  - `World/MapHillEdit.cpp`：`RebuildRaw`（噪声层有缓存，只有参数变了才重算）、`AddHill / DeleteHill / FindHill / HillAt`、`ReseatAll`（起伏变了以后把所有台地 / 高台 / 山顶坡的脚下高度重新对到地面；`Refresh(map, group, derive = false)` 逐个处理、最后只 Rederive 一次）。
  - F6「Relief (whole map) and hill parts」：六个滑块（丘高、丘大小、细波高、细波大小、山顶倍率、最大坡度）；`Add Hill`（半径 6m、高 2m）/ `Remove All Hills` / `Show`。山丘画成贴地的圆圈（隆起绿、洼地蓝、选中黄），**点圆圈里面的空地 = 选中**（台地、摆放物优先；Show 关掉就选不到），箭头水平拖动，右侧改位置 / 半径 / 高度（显示最陡处的大概坡度）、Ctrl+D 复制、Del 删。改动时数据立刻重算，「台地重新对地面 + 重建地面网格」等松手后做（`m_ReseatDirty` / `m_ViewDirty`）。
  - 自测 `mapedit hill ...`：自动找一块空地加半径 9m 高 3m 的山丘 → +2.97m；移 12m → 原处回到原高度、新处 +2.72m；丘高参数减半 → 全图平原起伏幅度 8.05 → 5.75m、台地脚下跟着变，改回去后幅度回到 8.00m。耗时（Debug）：移动山丘一步 106 ms、改整体参数 201 ms、松手后的 `ReseatAll` 约 500 ms。
  - 已知：`ReseatAll` 不是严格可逆的（各部件的垫子互相影响，参数改过去再改回来，台地脚下高度会差几厘米到 0.3m）；树 / 箱子不跟着地面升降；改整体参数会让全图的摆放物都对不上地面（只适合在摆东西之前调）。
- **整个地图编辑器的共同遗留**：所有鼠标操作（点选、拖箭头、放置、涂刷）都只测了数据接口，用户 10-05 夜试过后说「感觉是对的」（指整体，没逐项确认）；不检查部件 / 区域 / 摆放物之间的重叠；没有撤销；磁铁仍按 seed 摆。`MapTerrainEdit::detail::LastPerf` 和自测里的 `mapedit perf` 是 TEMP-TEST。

