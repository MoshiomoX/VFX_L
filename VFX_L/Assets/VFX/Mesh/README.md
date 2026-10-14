# Assets/VFX/Mesh 特效模型一览

2026-10-01 把这个文件夹的 102 个模型全部过了一遍（为魔力解放找素材时做的）。
缩略图在 `Catalog/`：`sheet_1`〜`sheet_6.jpg` 按下表编号排（每格左 = 斜视、右 = 俯视，统一缩放到同样大小，看不出原始大小），
`sheet_old_fbx61.jpg` 是下面说的 8 个老 FBX，`picks_mana_surge.jpg` 是魔力解放的候选。

## 怎么用

- 这些模型给 VFX 编辑器的 **Mesh entry**（`VFX_Editor/VFXMeshEntry`：不受光，颜色 = 贴图 × tint × intensity，可滚动噪声贴图、溶解、遮罩、双面、加法 / 半透明）和**粒子的 Mesh 渲染**（`Particle/GPUParticleMesh`，大小按最长边归一）用。
- 下拉列表从这个文件夹递归找 `.fbx / .obj / .gltf / .glb`，`Catalog/` 里的图片不会混进去。
- FBX 里记的贴图名（表里「贴图名」）对应的文件**都不在项目里**，只能当线索。实际贴图要在 Mesh entry 里另外指定（噪声图在 `Assets/VFX/Tex`）。

## 要注意的问题

1. **8 个游戏读不了**：#2 Cylinder004、#30 daoguang05、#33 daoguang08、#34 daoguang09、#36 daoguang11、#66 liuguang_08、#79 m_longjuanfeng、#80 m_longjuanfeng2。
   它们是二进制 **FBX 6.1**，游戏用的 assimp（"FBX-DOM unsupported, old format version"）和 Blender 都不支持。要用的话得转成新格式或 obj：`Tools/MeshCatalogFbx.py` 的 `convert` 能读出顶点 / 面 / UV 写成 obj（不含节点变换，转出来的 obj 还没放进项目）。
2. **6 个 obj 不在 git 里**：#0 1_uv、#22 daoguang、#24 daoguang01、#26 daoguang02、#28 daoguang03、#29 daoguang04。
   根目录 `.gitignore` 第 78 行的 `*.obj`（本来是忽略编译出的目标文件）把它们也挡掉了，换电脑会丢。要用的话需要给 `Assets` 下的 obj 加例外。
3. **大小单位很乱**：FBX 的单位有 cm（47 个）、inch（34 个，3ds Max 导出）、mm（3）、dm（1）、m（3）。游戏读模型时**不乘单位**（`Model::GetFileUnitScale` 只记录），表里的「游戏内尺寸」是用游戏同一个 assimp、同样的 import 参数（`AssimpFlags.h`）读出来、乘上节点变换后的包围盒，**就是游戏里 scale = 1 时的大小**（1 = 1 m）。同样形状的模型大小可以差 100 倍，用的时候在 Mesh entry 的 scale 里各自调。
4. **重复**：#15 = #20；#22/24/26/28/29 几乎一样；#3〜5 同一网格；#25 = #27；#30/33/34 几乎一样；#72 和 #73 只是大小不同；#75 ≈ #76；#82 = #83；#90 = #92；#95 = #96。
5. **朝向**：尺寸按游戏的 X × Y × Z 写，Y 是上。有的模型长边不在 Y（比如 #98 xulizhan_xiantiao2 躺在 Z 方向），立起来要在 Mesh entry 里转。

## 按类别

| 类别 | 模型 |
|---|---|
| 环 | #0 1_uv、#13 Slash、#22 daoguang、#24 daoguang01、#26 daoguang02、#28 daoguang03、#29 daoguang04、#48 dunpai_tuowei、#58 guangqiang、#64 liuguang01、#67 liuguang_09 |
| 螺旋 | #1 Chanrao01、#7 Helix001、#8 Helix002、#66 liuguang_08、#68 liuguang_13、#74 luoxuan、#75 luoxuan4、#76 luoxuan5、#77 luoxuanxian_5、#95 xiantiao、#96 xiantiao1、#98 xulizhan_xiantiao2 |
| 龙卷 | #2 Cylinder004、#3 Cylinder005、#4 Cylinder006、#5 Cylinder007、#14 Tornado03_A、#71 longjuanfeng02、#72 longjuanfeng1、#73 longjuanfeng2、#78 m_longjuan、#79 m_longjuanfeng、#80 m_longjuanfeng2 |
| 漩涡 | #69 liuguang_smoke、#70 liuguang_smoke02、#81 mod10、#82 mod16、#83 mod17 |
| 流光 | #63 liuguang、#65 liuguang_0546 |
| 波 | #16 banyuan |
| 刀光 | #6 FX_Swordslash_0020、#15 b410_jianren_changyong、#20 daoguan、#21 daoguang、#23 daoguang01、#25 daoguang02、#27 daoguang03、#30 daoguang05、#32 daoguang07、#33 daoguang08、#34 daoguang09、#35 daoguang10、#36 daoguang11、#37 daoguang12、#38 daoguang13、#39 daoguang_l、#99 yuanhuan_08、#100 zhua_01 |
| 星芒 | #49 duoweimian、#50 duoweimian02、#51 duoweimian03 |
| 护罩 | #52 fanghuzhao2、#93 suipian01、#94 suipian02 |
| 球 | #17 basic_skybox_3d |
| 盾 | #44 dun01、#45 dunpai (2)、#46 dunpai、#47 dunpai01、#54 fangyudashi_dun |
| 地裂 | #9 M_dilie、#10 M_dilie1、#43 dimianlie |
| 地刺 | #40 dici、#41 dici01、#42 dici02 |
| 石 | #12 Rock_2、#85 shitou_02、#86 shitu03、#87 shitu04、#88 shitu05、#89 shitu06 |
| 冰 | #11 M_mod_ice001、#18 bing、#19 bingci_02、#60 ice_01 |
| 水 | #90 shuimian、#91 shuimian_02、#92 shuiwo |
| 箭 | #56 gongjian_01、#57 gonjian、#61 jian、#62 jian02 |
| 弹 | #55 fashe01_mod |
| 条 | #31 daoguang06 |
| 板 | #59 guangqiang1 |
| 平台 | #84 penquanding01 |
| 方块 | #53 fangkuai |
| 星 | #97 xing |
| 爪 | #101 zhuazi3-6 |

## 用途建议

- **魔力解放（金色，玩家身上）**：按下瞬间 = #0 1_uv 尖刺王冠从脚下冒出 / #13 Slash 地面冲击环 / #49 duoweimian 星芒一闪；解放中 = #98 xulizhan_xiantiao2 双螺旋绕身 / #64 liuguang01 脚下光带往上流 / #68 liuguang_13、#14 Tornado03_A 螺旋绕身。
- **护盾**：#52 fanghuzhao2 六边形护罩、#93/94 碎罩（护盾破裂）、#54 环绕小盾。2026-10-07 シールド的反馈已用 #52（`ShieldHit.json`，缩放 0.036）和 #93/94（`ShieldBreak.json`，缩放 0.0003）。
- **土 / 冰魔法**：#40〜42 地刺、#9/10/43 地裂、#18/19/60 冰。
- **斩击**：刀光类，#23/25/27 细月牙、#32/35 厚月牙、#6/15/21 半圆。
- 现在特效里用到的只有 #57 gonjian（黄金の矢）、#12 Rock_2（石弾）、#19 bingci_02（MeshParticleTest）、#52 fanghuzhao2 + #93 / #94 suipian（シールド，10-07）；#13 Slash、#17 天空球只在测试特效里用过。

## 全表

尺寸 = 游戏里 scale 为 1 时的 X × Y（上）× Z，单位 m（见上面第 3 条）。单位 = FBX 文件记录的单位（obj 没有单位）。

| # | 文件 | 类别 | 形状 | 游戏内尺寸 X × Y × Z | 单位 | 顶点 | 备注 |
|---|---|---|---|---|---|---|---|
| 0 | `1_uv.obj` | 环 | 尖刺王冠：一圈向上的锯齿尖刺 | 289 × 105 × 289 | obj | 340 | 适合「从脚下爆发」。obj 不在 git；原始尺寸约 288，要缩到 0.01 左右 |
| 1 | `Chanrao01.FBX` | 螺旋 | 缠绕飘带：几条弯带绕成一团 | 112 × 123 × 93.8 | inch | 126 | 可绕身旋转 |
| 2 | `Cylinder004.FBX` | 龙卷 | 扭转的漏斗龙卷（和 Cylinder005〜007 同形） | 读不了 | FBX 6.1 | 331 | FBX 6.1，游戏读不了 |
| 3 | `Cylinder005.FBX` | 龙卷 | 扭转漏斗，上宽下尖 | 10.2 × 12.3 × 9.94 | inch | 331 | 005〜007 是同一网格、比例不同 |
| 4 | `Cylinder006.FBX` | 龙卷 | 扭转漏斗，更细长 | 7.37 × 15.9 × 7.20 | inch | 331 |  |
| 5 | `Cylinder007.FBX` | 龙卷 | 扭转漏斗 | 8.58 × 15.9 × 8.38 | inch | 331 |  |
| 6 | `FX_Swordslash_0020.FBX` | 刀光 | 半圆刀光弧（扁平带） | 420 × 18.5 × 247 | cm | 80 |  |
| 7 | `Helix001.FBX` | 螺旋 | 平放的两层月牙螺旋 | 128 × 67.9 × 117 | cm | 204 | 可在腰部水平旋转 |
| 8 | `Helix002.FBX` | 螺旋 | 平放的月牙螺旋，更宽 | 146 × 77.6 × 142 | cm | 204 |  |
| 9 | `M_dilie.FBX` | 地裂 | 一圈碎石片（地面裂开的环） | 418 × 31.7 × 416 | inch | 1361 | FBX 里记着石头贴图名；贴图名：T_wujian_shitou_006.png |
| 10 | `M_dilie1.FBX` | 地裂 | 立体碎石环，块更厚 | 67.5 × 7.09 × 67.8 | inch | 790 | 贴图名：T_wujian_shitou_009.png |
| 11 | `M_mod_ice001.FBX` | 冰 | 地面上的一滩冰（扁平） | 291 × 36.1 × 280 | mm | 769 |  |
| 12 | `Rock_2.fbx` | 石 | 普通岩石 | 314 × 284 × 460 | cm | 460 | 石弾的 Mesh 粒子在用；贴图名：Rock_2_Base_Color.jpg, Rock_2_Glossiness.jpg, Rock_2_Normal.jpg, Rock_2_Specular.jpg |
| 13 | `Slash.fbx` | 环 | 扁平圆环（甜甜圈片） | 3.00 × 0.10 × 3.00 | cm | 280 | 地面冲击环；测试特效 TestMesh2 用过 |
| 14 | `Tornado03_A.FBX` | 龙卷 | 几片扭转叶片绕成的小龙卷 | 37.6 × 28.9 × 35.3 | cm | 592 |  |
| 15 | `b410_jianren_changyong.FBX` | 刀光 | 半圆刀光（扁平带） | 372 × 16.0 × 184 | cm | 90 | 和 #20 daoguan 是同一个；贴图名：eye_lwy_t_win_03_single.dds |
| 16 | `banyuan.FBX` | 波 | 三片半圆柱面排成一列（剑气波） | 12.5 × 40.6 × 125 | inch | 243 | 3 个物体；贴图名：Wave1.png |
| 17 | `basic_skybox_3d.fbx` | 球 | 球体（天空盒） | 9999998 × 10000000 × 9999999 | cm | 1986 | 游戏里约 1000 万大；测试特效 TestMesh 用过（还乘了 scale 10）；缩小后可当光球 / 护罩 |
| 18 | `bing.FBX` | 冰 | 冰晶簇：几根尖冰斜着伸出 | 27.3 × 16.7 × 9.90 | dm | 154 | 贴图名：dbz_jz002_a01_2642.bmp |
| 19 | `bingci_02.FBX` | 冰 | 冰刺：一簇竖着的尖冰 | 82.2 × 102 × 61.8 | cm | 84 | MeshParticleTest 在用；贴图名：wenli_27.tga |
| 20 | `daoguan.FBX` | 刀光 | 半圆刀光（扁平带） | 372 × 16.0 × 184 | cm | 90 | 和 #15 是同一个；贴图名：eye_lwy_t_win_03_single.dds |
| 21 | `daoguang.fbx` | 刀光 | 半圆刀光，20 个顶点的平面 | 312 × 0.00 × 152 | cm | 20 | 贴图名：daoguang03.png |
| 22 | `daoguang.obj` | 环 | 甜甜圈（圆环体） | 28.6 × 5.75 × 28.6 | obj | 160 | #22/24/26/28/29 几乎一样；obj 不在 git |
| 23 | `daoguang01.FBX` | 刀光 | 细月牙 | 43.4 × 3.01 × 53.0 | inch | 175 | 贴图名：Models |
| 24 | `daoguang01.obj` | 环 | 甜甜圈，扁一些 | 28.6 × 3.69 × 28.6 | obj | 160 | obj 不在 git |
| 25 | `daoguang02.FBX` | 刀光 | 细月牙，更窄 | 17.6 × 2.01 × 65.1 | inch | 175 | 和 #27 相同；贴图名：Models |
| 26 | `daoguang02.obj` | 环 | 甜甜圈 | 28.6 × 3.50 × 28.6 | obj | 160 | obj 不在 git |
| 27 | `daoguang03.FBX` | 刀光 | 细月牙 | 18.1 × 2.01 × 65.1 | inch | 175 | 和 #25 相同 |
| 28 | `daoguang03.obj` | 环 | 甜甜圈 | 28.6 × 5.75 × 28.6 | obj | 160 | obj 不在 git |
| 29 | `daoguang04.obj` | 环 | 甜甜圈 | 28.6 × 5.75 × 28.6 | obj | 160 | obj 不在 git |
| 30 | `daoguang05.FBX` | 刀光 | 竖着的月牙刀光 | 读不了 | FBX 6.1 | 53 | FBX 6.1，游戏读不了；#30/33/34 几乎一样 |
| 31 | `daoguang06.FBX` | 条 | 一根细长平板（6 个顶点） | 22.2 × 8.22 × 199 | inch | 6 | FBX 里记着闪电贴图名 shandian；贴图名：shandian_0140.png |
| 32 | `daoguang07.FBX` | 刀光 | 有厚度的月牙壳 | 206 × 6.72 × 174 | cm | 53 | 贴图名：TX-PJ-005.png |
| 33 | `daoguang08.FBX` | 刀光 | 竖着的月牙刀光 | 读不了 | FBX 6.1 | 49 | FBX 6.1，游戏读不了 |
| 34 | `daoguang09.FBX` | 刀光 | 竖着的月牙刀光 | 读不了 | FBX 6.1 | 46 | FBX 6.1，游戏读不了 |
| 35 | `daoguang10.FBX` | 刀光 | 有厚度的月牙壳，更大 | 174 × 6.72 × 206 | inch | 53 | 贴图名：TX-PJ-005.png |
| 36 | `daoguang11.FBX` | 刀光 | 平放的半环 | 读不了 | FBX 6.1 | 39 | FBX 6.1，游戏读不了 |
| 37 | `daoguang12.FBX` | 刀光 | 3/4 圆的扇形弧（带缺口的圆盘） | 286 × 14.5 × 290 | cm | 39 |  |
| 38 | `daoguang13.FBX` | 刀光 | 竖着的窄弧（侧面看是一根线） | 14.5 × 280 × 107 | cm | 39 | 贴图名：daoguangyellow.png |
| 39 | `daoguang_l.fbx` | 刀光 | 3/4 圆的扇形弧（平面，和 #37 同形） | 286 × 0.01 × 290 | cm | 26 |  |
| 40 | `dici.fbx` | 地刺 | 一根细长石刺 | 44.5 × 221 × 41.4 | cm | 181 | 贴图名：胤七之柱山石尖石02.png |
| 41 | `dici01.FBX` | 地刺 | 三根大地刺 | 118 × 174 × 163 | inch | 220 | 贴图名：254d27d8_0.png |
| 42 | `dici02.FBX` | 地刺 | 三根弯曲的地刺 | 158 × 183 × 116 | inch | 238 | 贴图名：254d27d8_0.png |
| 43 | `dimianlie.FBX` | 地裂 | 地面开裂（中间凹下去的碎块） | 79.0 × 16.6 × 80.7 | inch | 1382 | 贴图名：{8340A31F-AF52-48B3-9D4B-8B74EA05D779}.png |
| 44 | `dun01.FBX` | 盾 | 竖长的旗帜形盾牌 | 142 × 308 × 47.9 | cm | 153 | 贴图名：m_diguoqiangqishi.tga |
| 45 | `dunpai (2).FBX` | 盾 | 几片碎掉的盾片 | 108 × 57.2 × 100 | inch | 2862 | 3 个物体；贴图名：Lance31_diff.TGA |
| 46 | `dunpai.FBX` | 盾 | 尖头盾牌 | 96.1 × 18.6 × 183 | inch | 249 | 贴图名：Em_0431.dds, Em_0431_S.dds |
| 47 | `dunpai01.FBX` | 盾 | 厚重的盾（侧面带装饰） | 34.2 × 91.1 × 63.0 | inch | 154 | 贴图名：puliangqiuzhang_d.tga |
| 48 | `dunpai_tuowei.FBX` | 环 | 三段弧片排成一圈（盾的拖尾） | 125 × 36.0 × 125 | inch | 174 |  |
| 49 | `duoweimian.FBX` | 星芒 | 多片三角叶片向四周散开（风车 / 星芒） | 239 × 113 × 225 | inch | 68 | 可做爆发时的闪光 |
| 50 | `duoweimian02.FBX` | 星芒 | 一束向一侧散开的叶片 | 68.6 × 59.9 × 93.0 | inch | 72 |  |
| 51 | `duoweimian03.FBX` | 星芒 | 十字交叉的几片板 | 19.9 × 17.6 × 18.8 | inch | 40 |  |
| 52 | `fanghuzhao2.FBX` | 护罩 | 六边形格子的半球护罩 | 60.8 × 30.1 × 60.8 | cm | 3910 | 看起来像无敌护盾；3910 顶点；贴图名：801_effect_327.jpg, Map #2 |
| 53 | `fangkuai.FBX` | 方块 | 方块阵（6×6×6） | 57.4 × 57.1 × 59.3 | inch | 864 |  |
| 54 | `fangyudashi_dun.FBX` | 盾 | 一圈环绕的小盾 | 97.2 × 41.2 × 95.8 | cm | 200 | 环绕护盾 |
| 55 | `fashe01_mod.FBX` | 弹 | 子弹 / 胶囊形（一头圆一头平） | 209 × 209 × 722 | cm | 108 |  |
| 56 | `gongjian_01.FBX` | 箭 | 箭 | 2.44 × 47.5 × 2.44 | cm | 145 | 贴图名：gongjian_01.png |
| 57 | `gonjian.FBX` | 箭 | 箭（箭头在 +Y） | 6.68 × 57.0 × 2.08 | cm | 40 | 黄金の矢在用 |
| 58 | `guangqiang.FBX` | 环 | 一圈竖立的光墙片 | 9.94 × 1.93 × 10.0 | m | 132 | 7 个物体；光柱 / 结界 |
| 59 | `guangqiang1.FBX` | 板 | 一片弯曲的竖板 | 3.82 × 1.50 × 0.59 | m | 24 |  |
| 60 | `ice_01.FBX` | 冰 | 一圈冰块（排成方形） | 22.7 × 9.15 × 31.5 | cm | 216 | 贴图名：beiou_dongxue_23_bingdeng_a01_5.dds |
| 61 | `jian.FBX` | 箭 | 箭（很小） | 1.08 × 7.96 × 1.08 | cm | 64 |  |
| 62 | `jian02.FBX` | 箭 | 箭（无尾羽） | 3.21 × 18.5 × 3.21 | inch | 32 |  |
| 63 | `liuguang.FBX` | 流光 | 一根扭曲的竖条 | 104 × 106 × 352 | cm | 165 |  |
| 64 | `liuguang01.FBX` | 环 | 竖起来的光带环（短圆筒） | 84.6 × 61.3 × 84.6 | inch | 105 | 适合脚下光柱 / 上升光环 |
| 65 | `liuguang_0546.FBX` | 流光 | 一条弯曲飘带 | 474 × 289 × 560 | cm | 110 |  |
| 66 | `liuguang_08.FBX` | 螺旋 | 平面螺旋涡 | 读不了 | FBX 6.1 | 258 | FBX 6.1，游戏读不了 |
| 67 | `liuguang_09.FBX` | 环 | 双层螺旋环带 | 362 × 141 × 349 | cm | 126 |  |
| 68 | `liuguang_13.FBX` | 螺旋 | 弹簧状的螺旋带 | 126 × 88.7 × 95.4 | inch | 428 | 可绕身上升 |
| 69 | `liuguang_smoke.FBX` | 漩涡 | 几片弯带卷成的漩涡 | 457 × 334 × 477 | cm | 208 |  |
| 70 | `liuguang_smoke02.FBX` | 漩涡 | 花瓣层叠的碗形 | 201 × 161 × 200 | cm | 288 |  |
| 71 | `longjuanfeng02.FBX` | 龙卷 | 碎带组成的高龙卷 | 347 × 635 × 323 | inch | 3167 |  |
| 72 | `longjuanfeng1.fbx` | 龙卷 | 扭转柱状龙卷 | 1022 × 1560 × 1027 | inch | 2550 | 和 #73 同形，大 15 倍 |
| 73 | `longjuanfeng2.FBX` | 龙卷 | 扭转柱状龙卷 | 67.6 × 103 × 67.9 | inch | 2550 |  |
| 74 | `luoxuan.FBX` | 螺旋 | 几条带子交叉缠成球 | 358 × 377 × 358 | cm | 140 |  |
| 75 | `luoxuan4.FBX` | 螺旋 | 平放的螺旋环带 | 89.2 × 51.3 × 88.3 | inch | 124 | 和 #76 几乎一样 |
| 76 | `luoxuan5.FBX` | 螺旋 | 平放的螺旋环带，带更宽 | 228 × 125 × 228 | cm | 124 |  |
| 77 | `luoxuanxian_5.FBX` | 螺旋 | 侧躺的螺旋带 | 572 × 650 × 281 | cm | 236 |  |
| 78 | `m_longjuan.FBX` | 龙卷 | 一层层盘上去的龙卷（带底座） | 180 × 218 × 186 | cm | 493 |  |
| 79 | `m_longjuanfeng.FBX` | 龙卷 | 盘旋上升的龙卷 | 读不了 | FBX 6.1 | 510 | FBX 6.1，游戏读不了 |
| 80 | `m_longjuanfeng2.FBX` | 龙卷 | 盘旋上升的龙卷，更粗 | 读不了 | FBX 6.1 | 255 | FBX 6.1，游戏读不了 |
| 81 | `mod10.FBX` | 漩涡 | 小漩涡（几片弧形叶） | 53.4 × 39.5 × 49.4 | cm | 144 |  |
| 82 | `mod16.FBX` | 漩涡 | 扭转的漩涡叶片 | 59.6 × 65.9 × 55.1 | cm | 144 | 和 #83 相同 |
| 83 | `mod17.FBX` | 漩涡 | 扭转的漩涡叶片 | 59.6 × 65.9 × 55.1 | cm | 144 | 和 #82 相同 |
| 84 | `penquanding01.FBX` | 平台 | 六角形浅盘平台 | 565 × 25.8 × 565 | inch | 19 | 19 个顶点 |
| 85 | `shitou_02.FBX` | 石 | 一圈石块 | 3.70 × 0.46 × 3.86 | m | 822 | 贴图名：desert_cave_wood_guardian_damaged_01.dds |
| 86 | `shitu03.FBX` | 石 | 竖立的高石柱 | 306 × 670 × 314 | cm | 273 | 贴图名：lf01_wd02_zzsl_yanshi_02.tga |
| 87 | `shitu04.FBX` | 石 | 石块 | 325 × 168 × 351 | cm | 275 | 贴图名：lf01_wd02_zzsl_yanshi_01.tga |
| 88 | `shitu05.FBX` | 石 | 石柱 | 295 × 395 × 222 | cm | 243 | 贴图名：lf01_wd02_zzsl_yanshi_03.tga |
| 89 | `shitu06.FBX` | 石 | 石块 | 337 × 239 × 324 | cm | 249 | 贴图名：lf01_wd02_zzsl_yanshi_02.tga |
| 90 | `shuimian.FBX` | 水 | 带漩涡纹的水面圆片 | 39.4 × 3.74 × 39.4 | cm | 1025 | 和 #92 相同；贴图名：clearwatertiledoriginal_cleaner.png |
| 91 | `shuimian_02.FBX` | 水 | 低面数的水面片 | 37.2 × 2.87 × 39.1 | cm | 113 | 贴图名：clearwatertiledoriginal_cleaner.png |
| 92 | `shuiwo.FBX` | 水 | 水涡 | 39.4 × 3.74 × 39.4 | cm | 1025 | 和 #90 相同；贴图名：clearwatertiledoriginal_cleaner.png |
| 93 | `suipian01.FBX` | 护罩 | 碎成六边形片的球罩 | 7595 × 4629 × 7349 | mm | 288 |  |
| 94 | `suipian02.FBX` | 护罩 | 碎成六边形片的球罩，片更多 | 7946 × 4630 × 7888 | mm | 372 |  |
| 95 | `xiantiao.FBX` | 螺旋 | 几条宽带绕成的螺旋球 | 182 × 182 × 169 | inch | 248 | 和 #96 相同 |
| 96 | `xiantiao1.FBX` | 螺旋 | 几条宽带绕成的螺旋球 | 182 × 182 × 169 | inch | 248 | 和 #95 相同 |
| 97 | `xing.FBX` | 星 | 五角星（有厚度） | 50.6 × 49.6 × 10.4 | cm | 140 | 贴图名：xingxing2.psd |
| 98 | `xulizhan_xiantiao2.FBX` | 螺旋 | 双螺旋带（文件名 = 蓄力斩线条） | 57.9 × 59.7 × 213 | cm | 224 | 游戏里也是躺着的（长边在 Z），竖起来要绕 X 转 90 度；贴图名：SmokePlume 1.png |
| 99 | `yuanhuan_08.FBX` | 刀光 | 半圆环带（竖起来的弧） | 77.8 × 40.8 × 164 | inch | 112 |  |
| 100 | `zhua_01.FBX` | 刀光 | 爪痕月牙 | 111 × 24.4 × 141 | cm | 84 |  |
| 101 | `zhuazi3-6.FBX` | 爪 | 兽爪 | 55.5 × 64.0 × 59.0 | cm | 84 | 贴图名：juxingxinmo.png |

## 重新生成

脚本在 `Tools/`（需要 Blender，当时用的是 Steam 版 5.2：`C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe`）：

1. `blender -b --factory-startup -P Tools/MeshCatalogRender.py -- <Mesh 文件夹> <输出文件夹>`：每个模型渲染斜视 / 俯视两张图，写 `stats.tsv`（顶点、Blender 米尺寸、UV、贴图名）。
2. `blender -b --factory-startup -P Tools/MeshCatalogFbx.py -- units <Mesh 文件夹> <输出 tsv>`：读每个 FBX 的版本和 UnitScaleFactor（表里的「单位」）。
3. `powershell -File Tools/MeshCatalogGameBounds.ps1 -MeshDir <Mesh 文件夹> -Out <输出 tsv>`：用游戏的 assimp dll（`x64/Release`）按游戏的 import 参数读，量游戏里的包围盒（表里的「游戏内尺寸」）。
4. `blender -b --factory-startup -P Tools/MeshCatalogFbx.py -- convert <Mesh 文件夹> <输出文件夹> 名字:编号 ...`：读 FBX 6.1，转成 obj，再用第 1 步给它们出图。
5. `powershell -File Tools/MeshCatalogSheets.ps1 -Dir <输出文件夹>`：拼成带文件名的总表 PNG（`Catalog/` 里的是转成 JPG 质量 85 的版本）。

表里的「类别 / 形状 / 备注」是看图手写的，加了新模型要手动补一行。