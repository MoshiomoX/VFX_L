# 开发笔记（技术要点 + 取舍理由）

按主题追加。每条写：做了什么、为什么这么做、放弃了什么、实测数字。细节以代码注释为准，这里只记"为什么"。

---

## 2026-10-06 战斗地图边缘：从空气墙到碗形地面

**结论**：草原 / 沙漠的场地边缘 = 地面自己在边线内 11m 处升到 34m（`TerrainBuild::ApplyRimRise`），没有任何边线碰撞箱；山顶 / 矿洞整体往内缩 7 格落在碗里。

**走过的弯路（按顺序）**
1. 外围堆 400 块小岩石 + 边线空气墙（旧做法）→ 用户要"几块超大石头当山"。
2. 20 块巨石（逐轴拉伸 `Prop::stretch`）+ 按模型凸包碰撞（`Model::GetHullPoints` 50 方向极值点 → 50 面 k-DOP）+ 空气墙挪到 36m 外当兜底 → 用户：空气墙要**彻底**没有；玩家走到石头缝里掉出场地（场外地面只有外观没碰撞 → `ReliefField::margin` 40m 补上）；站在石头上因凸包和网格差而"浮空"（→ `ColliderComponent::wallOnly`：凸体只水平推，站不上去）。
3. 参考 Megabonk：边缘是地形本身的陡坡。改成碗形地面，巨石降为可选装饰。
4. 矿洞贴边那一侧露出坑底，我先补了逐格楔子 → 用户否：应当**先把碗整圈生成完，再往里放东西**。改成区域内缩，楔子删掉。

**为什么碗比石头碰撞好**
- 一张高度场同时给渲染、玩家碰撞、雑魚坡度判定、GPU 弹的格子、镜头遮挡射线用，不会出现"看得见但碰不到 / 碰得到但看不见"的缝。
- 石头方案每块要一个凸包、封格、背景墙，缝隙永远补不完。
- 代价：物理要多一条"超过 60° 的起伏按墙处理"（`PhysicsSystem` 高度场分支：竖直推出改成沿法线水平分量推、不算着地）；否则竖直推出会让玩家顺着任何坡走上去。

**保留但默认关的东西**：`boulderMountains` / `boulderCollide` / `roofBoulders`、`Hull::wallOnly`、`Prop::collide`。地图文件版本 10。

**矿洞顶平台**：洞顶 / 洞壁碰撞上沿 = 顶面（能站），一条 4 格 26° 草坡从平原上去，雑魚不走（封格记在单独 group，免得 F6 重做坡道记录时把封格丢了）。

**编辑器等价性**（`mapedit` 自测 `Verify`）踩的坑：洞壁格高度补值规则两边必须一样"只挑能走的邻格"；记录按 group 重做时，附带的封格要和部件分开记；地图里存的碰撞上沿要是实际用的值。

---

## 2026-10-06 天气系统（进行中）

**目标**：用户觉得游戏"太平"。先做天气：固定的昼 → 夕 → 夜 + 每关一种天气事件（雨 / 沙暴 / 浓雾雷电），夜和雨时经验球、血条提亮保证可读。

**做法**
- 不加新渲染路径：时间推移 = 对 `SceneLighting::Preset`（太阳 / 环境光 / 天空 / 雾）在昼 / 夕 / 夜三组关键帧之间插值，每帧 `ApplyPreset`。夕 / 夜由各关的昼预设用代码派生（`WeatherSystem::BuildTimeOfDay`），不另存三套。遗迹本来就是夜，不变。
- 天气事件 = 状态机 Idle → FadeIn → Active → FadeOut，强度 w ∈ [0,1]；w 只是乘在光照上（太阳 ×(1−0.55w)、天空 / 雾色往阴天色插值、雾距离缩短），加草的风、粒子、环境音。
- 雨 / 沙暴粒子 = 普通 VFX json（`Rain.json` / `Sandstorm.json`，Box 发射器，循环），用 `AreaVFXPlayer` 播放后每帧 `SetInstance` 到镜头前上方——复用现有特效条目，不写专门的雨系统。
- 雷 = 环境光 / 天空一帧抬高 0.14 秒 + `thunder` cue（NIIIEMAND 爆炸降调）。雨的环境音 = rubberduck `loop_rain.ogg`，给 `AudioSystem` 加一个 `SetAmbient`（单个循环音、音量外部控制）；沙暴没有风声素材，先用雨声降到 0.55 倍音高顶一下。
- 可读性：`SwarmSystem::orbLook.emissive`、`hpBar.fill` 按 max(夜, w) 乘一个倍率，Init 时记基准值、Shutdown 还原。
- 时间用 `m_RunTime`（暂停不走）决定时刻，粒子 / 淡入淡出用实时 dt（暂停时雨不该定格）。

**为什么固定昼→夜**：用户选的；光线可以当"剩余时间"的提示。随机起始时刻放弃。

**接法**：`World/WeatherSystem`，场景 `Init` 末尾 `m_Weather.Init(stageDef, stageTime, lighting, areaVfx, vfxCtx, grass, swarm)`，`UpdateGameplay` 里在 `m_AreaVFX.Update` 之前 `Update(dt, m_RunTime, camPos, camFwd)`，`Shutdown` 还原。面板「Weather」（Lighting 面板下面）：Trigger / Stop / Lightning、Time Override 滑块、各时长。

**实测（`weather` 自测，`VFXL_STAGE=2` 看沙漠）**：昼→夕是橙色低日 + 橙地平线；夜是蓝黑 + 火把 / 箱子的光明显；雨 = 白色雨丝 + 灰天 + 近雾；沙暴 = 土黄浓雾 + 飘尘团 + 远处只剩描边，夜间沙暴偏棕黑。截图在 scratchpad `weather-*.png`。

**没做 / 待定**：雨天不改玩法数值；地面变湿（顶点色压暗）先不做；雨的落地飞溅没做（发射器在镜头上空 14m，不知道地面高度）；沙暴没有专用风声；遗迹的「浓雾 + 雷」只调了雾和雷，没粒子。
