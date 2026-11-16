# VFX_L — 给 Claude Code 的项目说明

C++ / DirectX 11 自制引擎的 3D roguelite（幸存者类）。雑魚、投射物、经验球、范围攻击都在 GPU 上跑（`Swarm/SwarmSystem`），玩家和精英留在 CPU 的 ECS（`Registry`）。
设计依据：`GPU_GAMEPLAY_PLAN.md`（Phase 0〜4 已完成，Phase 5 未开始）。Phase 4 收尾清单的原文在 `PHASE4_CLEANUP.md`（已全部完成）。
项目整体总结（规模、时间线、各系统现状、素材许可、性能数字、待办）在 `PROJECT_SUMMARY.md`（2026-09-29 写），做完一个阶段的功能后顺手更新它的时间线和现状表。

---

## 1. 约束（全程有效）

- **日语要写成日本人看得懂的日语（2026-10-05 用户要求，注释和游戏内文案都算）**：不要把中文词直接当日语用。当天把全部 .cpp / .h 的注释按对照表替换过一遍（约 280 个文件），新写注释照这个用词：玩家 → プレイヤー、用户 → ユーザー、面板 → パネル、背包 → バックパック、鏡頭 → カメラ、精英 → エリート、升級 → レベルアップ、自測 → 自動テスト、調試 → デバッグ、貼図 → テクスチャ、回読 → リードバック、特効 → エフェクト、道具 → アイテム、影響格 / 占位格 → 影響マス / 占有マス、高級魔法 / 基礎魔法 → 上級魔法 / 基本魔法、節点 → ノード、場面 → シーン、場地 → フィールド、模型 → モデル、編集器 → エディタ、施法 → 詠唱、蒙皮 → スキニング、影図 → シャドウマップ、描線 → アウトライン、卡通 → トゥーン、胶囊 → カプセル、配方 → レシピ、流場 → フローフィールド、状態機 → ステートマシン、鉱洞 → 洞窟、分裂怪 → スプリッター、重撃 → スラム、冷却 → クールダウン、遍历 → 走査、級聯 → カスケード、穿模 → めり込み、寻路 → 経路探索、実例 → インスタンス。雑魚 / 台地 / 三択 / 置物 这类本来就是日语的词没动。只做了词的替换，句子的语法没有逐句重写；本文件（中文）里引用的旧注释用词不用改。
- **注释只简单说明功能（2026-11-10 用户：「记录时间之后写的注释都太长了，简单介绍机能为主不要这么细」）**：代码注释写「这是什么 / 做什么」，必要时加一句不明显的理由；**不写日期、用户的原话、谁决定的、改了几版 / 以前是怎样、实测数字**（这些历史记在本文件和 git 里）。同日把全部 .cpp / .h / .hlsl 里带日期的长注释按这个规则精简过一遍（只改注释，代码逐字不变；行号因此变了，本文件里旧的 `文件:行号` 引用可能对不上）。
- **编码**：C++ 注释用日语，全局 `/utf-8`。含日文 `L"..."` 字面量的文件存 UTF-8 带 BOM。
- **HLSL 也可以写日语注释（2026-11-04，用户：「把 HLSL 文件全都变成可以用日语写注释的」）**：HLSL 存 **UTF-8 无 BOM**、CRLF（以前的「纯 ASCII」规则作废）。实测 fxc（d3dcompiler_47，`D3DCompileFromFile` 同一个）：UTF-8 无 BOM 的日文注释能读、报错行号不偏（`Particle/Common/Emit*.hlsli` 等 10 个文件一直就是这样）；**带 BOM** = 第 1 行 `error X3000: Illegal character in shader file`；**Shift-JIS** = 「ソ・表・能・予」等第 2 字节是 0x5C（`\`），落在 `//` 注释行末就把下一行吞进注释（实测吞掉函数签名报语法错，最坏是静默删代码）。本机 ACP 是 932，VS 给纯 ASCII 文件加日文后默认存成 Shift-JIS → 两层保护：① 根目录 `.editorconfig` 对 `*.{hlsl,hlsli,fx,fxh}` 指定 `charset = utf-8`（无 BOM）+ `end_of_line = crlf`；② `Tools/HlslEncoding.targets`（`VFX_L.vcxproj` 在 `Microsoft.Cpp.targets` 后 Import）在 `FxCompile` 之前跑 `Tools/FixHlslEncoding.ps1`：`Shader\` 下的 .hlsl / .hlsli 有 BOM 就去掉、不是 UTF-8 但能按 CP932 读就转成 UTF-8（改了的文件出 `warning HLSLENC`，VS 里文件会被重新载入），解码后出现控制字符 / 私用区 / 半角假名（= 多半是 GBK 等）则报 `error HLSLENC` 且不改；只在某个 HLSL 比 `$(IntDir)vfxl_hlsl_encoding.stamp` 新时才跑（无变化的构建跳过）。手动检查：`powershell -NoProfile -ExecutionPolicy Bypass -File Tools/FixHlslEncoding.ps1 -Root VFX_L/Shader -Check`（不改、有问题时退出码 1）。**提交前也查（2026-11-05，git 的 pre-commit）**：`Tools/git-hooks/pre-commit`（sh，`.gitattributes` 里 `Tools/git-hooks/* text eol=lf`，CRLF 的话 sh 读不了第 1 行）对 git add 进索引的 .hlsl / .hlsli / .fx / .fxh 查 BOM（头 3 字节 `efbbbf`）和是否 UTF-8（`iconv -f UTF-8`；**11-13 起没有 `od` / `iconv` 的 sh（用户提交用的环境里报「od: command not found」、所有 HLSL 被误判成非 UTF-8）改成把索引内容写进 `.git/hlsl-check/` 再用 PowerShell 一次查完**，98 个文件约 10 秒），有问题就拒绝提交并提示跑上面的修正命令（急的话 `git commit --no-verify`）；每个克隆要执行一次 `git config core.hooksPath Tools/git-hooks`（这台机器 11-05 已设）。11-05 实测：BOM / Shift-JIS 的 HLSL 被拒、带空格的文件名也行、非着色器不查；仓库现有 162 个着色器全部通过。这个 .ps1 有日文，**必须存 UTF-8 带 BOM**（PowerShell 5.1 规则）。**同日现有的英文注释全部翻成了日语**（用户：「把现有的英文注释也翻成日」；8 个代理分批，约 4,800 行）：只改注释、**每个文件的行数和每行的代码都不变**（文档里引用的 `文件:行号` 仍然有效），全部 132 个 shader 用 Release 选项编译后的字节码和翻译前逐字节相同。留着英文的只有每个文件第 2 行的文件名、纯标识符 / 公式 / 单位的注释。顺带把几处已经和代码对不上的旧注释改成了现状（`SSAOPS` 的核旋转、`SwarmCorpseListCS` 的分列表、`SwarmEnemyAICS` 的 ChaseAISystem、`SwarmOrbMoveCS` 的吸引）。以后新写的 HLSL 注释也用日语。
- **include 的规则（2026-10-07 用户：「include 整理一下，重复的删除，用得多的合并，不然读起来太麻烦」）**：标准库的常用头（`<algorithm> <cmath> <iostream> <fstream> <filesystem> <string> <vector> <memory> <functional> <random> <chrono> <unordered_map/set> <utility> <cstdio> <cstdlib> <cstring> <cstdint> <cstddef> <cfloat> <climits> <cwchar>`）放在预编译头 `VFX_L/pch.h`，工程设置里对**所有 .cpp 强制包含**（`/FI pch.h` + `/Yu`，`pch.cpp` 是 `/Yc`），**.cpp 里不要再写这些**；`ThirdParty` 的 .cpp / .c 设成 `NotUsing`。项目自己的头不放进 pch（改一个会全部重编、也看不出依赖）。.h 文件仍然自己 include 用到的标准库（保持单独可读）。.cpp 的 include 区顺序：自己的头 → 项目头 → 空行 → 尖括号的第三方 / 系统头（`<nlohmann/json.hpp>`、`<Windows.h>` 等）。整理用的脚本 `Tools/TidyIncludes.pl`（`perl Tools/TidyIncludes.pl [--apply] 文件…`：删 pch 覆盖的标准库 include、删重复、排顺序；中间夹 `#define` / `#if` 的文件只删不排），10-07 跑过一次：139 个文件、删 388 行 + 1 处重复、28 个文件排了顺序。
  残留的 CP932 文件（编译时只出 C4828）要写日文注释前，先整体转成无 BOM 的 UTF-8。
- **GPU 结构体布局**：`Enemy` / `Projectile` / `Orb` / `FrameCB` / `AICB` / `OrbCB` / `SwarmCounters` 默认不改。需要新的逐槽数据时开并行 buffer（例：`m_EnemyMaxHpBuffer`）。确实要改时先说明理由。
- **ImGui 的日文字体（2026-10-18，用户：「ImGui 不能显示中文日文，有没有替代」→ 不用换库，加字体）**：`Debug/ImGuiRenderer.cpp`（同日从 CP932 转成 UTF-8）在 `ImGui_ImplWin32_Init` 之前 `AddFontFromFileTTF(Res::Fnt::ImGuiJP = Assets/Fonts/NotoSansJP-Variable.ttf, 16px)`，再把 `Res::Fnt::ImGuiSC`（Noto Sans SC）以 `MergeMode` 接在后面当后备（用户「简体中文打不开吗」：JP 没有「简」这类简体专用字）；ImGui 1.92.8 是动态图集，**不传字形范围**（用到的字才生成，先在 JP 里找、没有再找 SC）；读不到 JP 就退回默认字体。两张都是 OFL（`NotoSansJP-OFL.txt` / `NotoSansSC-OFL.txt`）、可变 TTF 9.5MB + 17.8MB（stb_truetype 用默认实例 = Regular）。perf 自测前后 NewFrame 都是 0.24 ms。Debug Info 窗第 3 行是日文 + 简体字的确认文字。
- **Dispatch**：每个 CS dispatch 之后必须 `UnbindSRVs` + `UnbindUAVs`；绘制用的 SRV 画完也要解绑（否则下一帧 CS 当 UAV 用时出 HAZARD）。D3D 调试层 Debug 构建也默认关（2026-09-28，省约 2.5 ms/帧），要看 HAZARD 等警告时设环境变量 `VFXL_D3D_DEBUG=1`（VS 里写在「调试 → 环境」）。
- **Feature Level 11_1（2026-10-22，用户问 DX12 迁移后决定只改这一行）**：以前 `D3D11CreateDevice` 传 `nullptr` 的默认列表（不含 11_1），RTX 3060 上也只拿到 11_0 → CS 的 UAV 只有 8 个（下文各处「UAV 已满 8 本（FL 11.0 上限）」的绕路都是这个原因，那些是当时的记录）。现在 `Graphics.cpp` 显式请求 {11_1, 11_0}（旧运行时 `E_INVALIDARG` 时只用 11_0 重试），控制台打 `[Info] Feature level: 11_1 (UAV slots: 64)`。**现有 shader 一个都没用 u8 以上，旧的绕路也没拆**；以后新 CS 可以用到 u63，但那个 shader 在只支持 11_0 的显卡（约 2014 年以前：GTX 600 / 700 / 750、HD 4000 以前的核显）上建不出来 → 第一次用 u8+ 时要加「启动时拿到 11_0 就弹窗说明」的检查。`Shader` 类按反射取槽数，不用改。候选：砂嵐の術師的碎块（CorpseListCS 满了才走僵尸贴图）可以直接加一个列表修掉。
- **`DispatchStep` 顺序**：0 counter 清零 → 0b 空间哈希 → 1 敌 AI → 2 敌积分 → 2b 重叠解除 → 3 弹积分 → 3b 弹的拖尾记录（`SwarmProjTrailCS`，2026-10-15）→ 4 命中（+经验球掉落）→ 4b 誘発事件（`SwarmProjEndCS`，2026-09-30）→ 4c 范围 tick / 伤害 → 5 瞄准 → 6 接触 → 7 经验球吸引/拾取。改顺序要先说明。
- **SwarmCounters**：`killCount` / `playerDamage` / `expTotal` 是 GPU 永久累加、CPU 取差分。任何清理都不许清 counters buffer，也不许重置 `m_LastKillCount` / `m_LastDamageTotal` / `m_LastExpTotal`。
- **工程文件**：增删源文件、着色器必须同步改 `VFX_L.vcxproj` 和 `VFX_L.vcxproj.filters`。着色器照 `SwarmEnemyVS.hlsl` 的 `FxCompile` 块写（输出到 `$(OutDir)Shader/...cso`）。
- **Esc 不再退出程序**（2026-10-04 用户要求删掉 `Core/Window.cpp` 里的 Esc → `PostQuitMessage`）。退出 = 窗口的 × / 标题画面「ゲームを終了」（暂停菜单可回标题）。**10-05 起 Esc = 暂停菜单的开关**（用户要求从 P 改过来，`InputMap::GetPauseToggle`；P 不再绑定，ImGui 仍会收到 Esc）。自测脚本关游戏一直是发 WM_CLOSE，不受影响。

## 2. 工作方式

- 有方案选择（外观、交互方式、数据放哪）时，先列出选项和推荐，等用户决定再动手。纯机械的步骤直接做。
- **单个文件超过 500 行就要考虑拆（2026-10-05 用户要求）**：往一个文件里加东西、行数过 500 时，先看里面的功能能不能包装成独立的部品（类 / 系统 / 面板 / 一个自测一个文件），能包装的都单独包装、放到按类型分的现有目录里，不要继续往大文件里堆。起因是 `Scene/CollisionTestSceneDebug.cpp` 堆到 6500 行（全部调试面板 + 四十多个自测）。
- 每完成一个任务构建一次 Debug x64，通过再做下一个。不要求零警告，但不能新增警告。
- **不要自己 commit / push（2026-10-21 用户：「上传 Git 的次数太多了，别随便上传，提醒为主，我自己上传」）**：做完只提醒「可以提交了」，列出改了的文件、`Debug/` 下要 `git add -f` 的新文件、不该提交的文件（Settings.json、MapData 等），由用户自己提交。只有用户当次明确说「你提交」才动 git。
- 用户在用电脑时（最近 1 分钟内有输入）不要启动游戏抢焦点，更不要发按键；锁屏时只做日志验证。

### 构建

从 PowerShell 调（Git Bash 会把 `/p:` 当路径）。VS 2022 的 MSBuild 会报 MSB8020，要用 VS 18 的：

```
& "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:\VFX_Project\VFX_L\VFX_L.sln" /p:Configuration=Debug /p:Platform=x64 /m /v:m /nowarn:C4828 "/flp:LogFile=<scratchpad>\build.log;Verbosity=minimal;Encoding=UTF-8" /noconsolelogger
```

- 构建前确认没有 `cl.exe` 在跑（用户可能在 VS 里构建，撞车会报 C1041 PDB 占用）。
- **VS 里 F5 跑 Debug 比直接开 exe 慢好几倍（2026-10-20，用户报「掉到 60 帧」）**：调试器挂着时 Windows 启用调试堆，每次 `new / delete` 都走堆校验，这个工程 Debug 下 `vector / unordered_map` 密集，帧时间翻倍以上，再被垂直同步锁到 60。本文件里记的 Debug 帧时间（5.48 ms 等）全是脚本直接启动 exe 量的。已在本机 `VFX_L.vcxproj.user`（不进 git）的 Debug|x64 调试环境里加了 `_NO_DEBUG_HEAP=1`；换电脑要重加。要看真实帧数用 Ctrl+F5 或直接开 exe。同日顺带查过：`Shader::SetSRV / SetUAV` 找不到名字会**每帧**往控制台打一行「[Warning] SRV not found」（10-17〜10-20 的 `enemySlow` 就是），控制台一行零点几 ms，看到控制台在滚就先查它；全部 132 个绑定名已核对都存在于某个 .cso 里。
- **多核编译 /MP（2026-10-16，用户：「编译慢有没有什么解决方法」）**：以前工程没开 `MultiProcessorCompilation`，258 个 .cpp 在一个项目里**逐个串行编译**（MSBuild 的 `/m` 只并行项目，不并行文件），16 核只用 1 个。实测摸一下 `Item/ItemTypes.h`（106 个 .cpp 重编）：无 /MP 356 秒 → 有 /MP 50 秒。现在 vcxproj 全部 5 个配置的 ClCompile 都写了 `<MultiProcessorCompilation>true</MultiProcessorCompilation>`，命令行和 VS 里都生效。剩下的慢源头（没动）：① `Item/ItemTypes.h`（39 处直接 include，传递到 106 个 .cpp）、`Debug/AutoTest/AutoTestCommon.h`（61 个自测）、`ResourcePaths.h`（55）、`Manager/ResourceManager.h`（51）这些头一改就是半个工程重编；② Debug 的 PDB 560MB（默认 /Zi，PDB 写入要串行化，换 /Z7 能再快一点但 PDB 更大）；③ 104 个 HLSL 每个 FxCompile 单独起进程。
- 已知的旧警告：C4828（CP932 残留）、ShaderPath.h 的 C4244（显示在 xutility(5094)）、HLSL 的 X4000（`SwarmIsWalkable` / `EmitMesh.hlsli` / `ParticleEmitCS` / `SwarmDebugEnemyVS` 的 `CapsulePoint`；10-25 全量重编时还出了这些以前就有的：`Lighting.hlsli` 的 `ToonRim / ShadeLambert / ShadePBR`、`SwarmAreaPush`、`ProbeBlocked`、`PathHitsGround`、`ParticleDistortLevel`；11-04 全量重编时还有 `MiasmaStepCS` 的 `SolidAt / SamplePrev`、`ParticleEmitCS` 的 `InThisPass`，代码没变 = 以前就有）。增量构建只报重编了的 shader，所以「以前有没有」要和 HEAD 的函数体比，不能只看上一次的日志。
- **MSB8027 / LNK4042（同名源文件）不是无害警告**：两个同名 .cpp 会编译到同一个 `x64/Debug/<名>.obj`，最后链接进去的是哪个取决于编译顺序。以前根目录混进过一个空的 `ManaSystem.cpp`，链接到它的时候蓝量既不扣也不回、`pendingSpend` 一直累加，法杖就不打了（2026-09-26 已删）。再看到这两个警告要马上查。
- **Demo 构建（2026-10-07，用户：「做一个 DemoBuild，排除所有 ImGui，编辑器之类全部只在 Debug 里」）**：配置 **`Demo|x64`**（VS 的配置下拉里能选；命令行把上面的 `Configuration=Debug` 换成 `Demo`），= Release 的全部设置（工程里约 300 处按配置写的着色器输出等，用 `Tools/AddDemoConfig.pl` 从 Release|x64 复制生成）+ 预处理宏 `VFXL_DEMO` + 构建后把 `C:\assimp\5.2.5\bin\assimp-vc143-mtd.dll` 复制到输出。输出 `x64/Demo/`（`Assets` 照样是目录链接，拷到别的电脑要另外打包，还没做）。代码侧统一看 `Core/DevUI.h`：`DevUI::kEnabled`（Demo = false）→ 不创建 ImGui（`DebugManager`）、不注册 F2 / F4 / F6 编辑器场景、F 键不切场景、不弹控制台（`main.cpp`）、不读自测的环境变量（`Game.cpp` / 战斗场景）、标题右下的「F1: Game…」不显示；战斗场景的 `DrawDebugUI` 在没有 ImGui 时直接返回。游戏逻辑里要问「ImGui 是否占用鼠标 / 键盘」时用 `DevUI::WantMouse / WantKeyboard / WantTextInput`（没有 ImGui 时返回 false），**不要直接写 `ImGui::GetIO()`**（Demo 里没有 context 会崩）。ImGui 的源码还链接在 Demo 里，只是从不初始化 / 调用。Demo 第一次全量构建约 10 分钟以上（Release 级优化）。**10-07 用户报「demo 不能运行」**：从 VS 选 Demo 按 F5 时，`VFX_L.vcxproj.user`（本机文件，不进 git）只给 Debug / Release 设了调试工作目录 = 输出目录，Demo 在源码目录下启动、找不到 `Shader/*.cso` → 弹「起動失敗」框。修法：`main.cpp` 启动时先 `SetCurrentDirectory` 到 exe 所在的目录（所有配置都这样，从哪里启动都行），`.user` 也补了 Demo。

### 自测

- 启动 `x64/Debug/VFX_L.exe`（工作目录同目录），截图看画面。**只发键盘，不发鼠标**，发键前确认前台窗口属于游戏进程。
- 程序日志走 AllocConsole，重定向拿不到；用 `AttachConsole` + `ReadConsoleOutputCharacterW` 读控制台缓冲区。
- **自截图 `VFXL_SELF_SHOT=<文件夹>`（2026-10-23，TEMP-TEST）**：战斗自测每写一行 `... look <名>`，这一帧的画面（UI 画完、Present 之前的后缓冲）由 `Graphics::RequestScreenshot` → DirectXTK `SaveWICTextureToFile` 存成 `<文件夹>\<那一行的空格换成 _>.png`。外部脚本截屏要求游戏窗口在前台（不然会截到用户的桌面，所以会跳过），用户在用电脑时一张都拍不到；自截图不受影响。`windtoys` 的 `VFXL_WINDTOYS_SHOT=1` = 给资料拍图：HP / MP 保持正常上限并每帧补满 + 无敌（HUD 不出 100000）、不开蒲公英、镜头 13m / 20°。10-23 第一次跑沙漠时启动 0 秒就以退出码 3（abort）结束过一次，没有转储、重跑没复现，原因没查。

- **动画书き出し `VFXL_CAPTURE=<输出路径（不带扩展名）>`（2026-10-23，作品介绍视频用，TEMP-TEST）**：`Core/VideoCapture`。设了以后：窗口 = `VFXL_CAPTURE_W/H`（默认 1920×1080）、每帧固定 1/60 秒（Application 的 dt，与实际帧率无关 = 重场面也不掉帧；`SetTimeScale` = 只让游戏慢、钟和音不慢）、垂直同步关；**音不开设备**（`AudioSystem::Initialize(offline)` = miniaudio `noDevice`，每帧 `ReadOffline` 读 800 个采样 → `<路径>.wav`，限幅器照样走，和画面逐帧对齐），BGM 音量 0（`VFXL_CAPTURE_MUSIC=1` 才录）；`SetRecording(true)` 期间每帧在 Present 前把 backbuffer 拷到 staging 读回、管道给 ffmpeg（`VFXL_FFMPEG`，libx264 crf 12 BT.709）→ `<路径>.mp4`，`Finish()` 关闭。实时钟（`UIDeco::Clock`、光线 / 液面的 time）书き出し中改用 `VideoCapture::Time()`。不在锁屏 / 前台与否上出问题（不需要窗口在前台）。Debug 1080p 约 40 fps 录制（4,000 体时约 25 fps）。**镜头**：`VFXL_BATTLE_AUTOTEST=video_<名>`（`Debug/AutoTest/VideoShot.h/.cpp` = 基类：Setup / Tick / 镜头关键帧（时刻・yaw・pitch・距离・注视高度，smoothstep 插值）/ `CameraTarget`（新 `BattleCamera::SetTargetOverride`）/ recStart〜recEnd 自动开关录制并 `PostQuitMessage`；HP / MP 保持普段上限每帧补满 + 无敌、经验条固定不出四择、FPS 表示和雑魚血条关、`GameUI::hideAll` 可整个不画 UI；`VideoShotsCombat.cpp`（open / combo / beam / swarm / shatter / ending）、`VideoShotsWorld.cpp`（gust / twister / cliff / flow）、`VideoShotsRead.cpp`（outline / boss / damage / backpack））。10-24 加了 `video_fire`（メテオ炸开点燃草、风下延烧；延烧当时默认关，只在这一条打开；11-01 起默认开）/ `video_wind`（天气风 4.2 m/s：草浪・落叶・斗篷）/ `video_flatten`（低镜头：爆炸和光线压倒草）。**编辑器的录制**：书き出し中 ImGui 不开 multi-viewport（面板画进游戏画面而不是另开 OS 窗口），并用 `x64/Debug/imgui_capture.ini`（不碰普段的 imgui.ini）；`VFXL_CAPTURE_AUTO=<始>,<终>` = 场景不操作录制时按启动后的秒数自动开关并退出（F2 `VFXL_VFX_AUTOLOAD` / F4 `VFXL_PROJ_AUTOTEST` 用）。不设自测、只设 `VFXL_CAPTURE` = 录**标题画面**（`UpdateTitle`：从黑场淡入起 `VFXL_TITLE_SECONDS` 秒、`TitleScreenUI` 不画菜单改画一句话 + 作品说明）。用户的 Settings.json 里 `damageNumbers` 是 false，video_damage 只在那一条里临时打开。剪辑脚本在 `C:\VFX_Project\RunePack_video\`（仓库外；`RunShot.ps1` 录一条、`Sheet.ps1` 抽帧拼图、`Plan.ps1` 片段表 + ASS 字幕、`MakeVideo.ps1` 裁切 → xfade 拼接（按帧数算偏移，按小数取整会让 xfade 提前截断）→ 音效按位置 adelay + amix → BGM 混音 + loudnorm -14 LUFS → 烧字幕 → H.264）。

## 3. 场景（DebugManager 快捷键）

**F8 = 流向小地图（2026-10-28，用户：「能不能动态生成风和敌人走向的流向图，按开关在游戏里显示」→ 选了「只做显示」+「敌人两种都要」，并纠正「不是画在地图上，是像小地图那样显示」）**：`Debug/FlowDebugView`（场景 `m_FlowDebug`，在 `DrawDebugUI` 里每帧画，F10 面板关着也出、暂停中也出；Debug 构建才有 = ImGui 前面的 draw list）。F8 轮流切：关 → 风 → 寻路流场 → 敌群实际移动。画面左边中间一个 380px（按画面高 900 换算）的正方形窗，以玩家为中心 ±30m、**北朝上、+x 在左（和右上角小地图一致）**，白三角 = 玩家朝向。**底色 = 青 → 白 → 红 的热度图**（用户同日：「根据风的强度、场地的高低来加入青红差」）：风模式 = 风速 0 → `windHeatMax` 8 m/s，其他模式 = 窗里可走格的高度（低 → 高，按窗内最低 / 最高自动拉开）；堵的格暗灰、水深蓝（高度模式），窗下面一条色标 + 两端数值；底色 × `heatDim` 0.62，箭头一律白色加黑边。风 = `WindState::Velocity` + `LocalWind::Air`（风源回读晚 2〜3 帧），每格一个箭头、长度 = 风速；寻路 = `SwarmSystem::GetFlowField().Directions()`（目标格画圈）；敌群 = `DebugReadEnemies` 每 0.25 秒读一次（会等 GPU，只在这个模式下）→ 每只怪一个黄点 + 每 2m 格平均速度的箭头（白 = 1 只 → 黄 = 6 只以上）。自测 `VFXL_BATTLE_AUTOTEST=flowview`（`Debug/AutoTest/AutoTestFlowView.cpp`，**从没进过 git、10-26 清理自测时已删**：周围放 40 只怪、右 6m 一个竜巻风源，依次 `look path / crowd / wind`）。**第一版画在地面上**（`DebugManager` 的线）被用户否了。

**调试线的绘制顺序（2026-10-28，用户要求改）**：以前 `DebugManager::Render` 在 `m_Game.Render()` **之前**画线（`Application.cpp`），战斗场景的天空、地面一画就把碰撞框 / 法杖 / 格子这些调试线盖掉了（只剩被草挡得断断续续的灰线）。现在 `DebugManager::Render()` 只定相机、积参照网格，真正画线是新的 `DebugManager::RenderLines()`：战斗场景在 3D 画完（粒子、扭曲之后）、结算照片之后、UI 之前调（先 `RestoreRenderTarget` 把场景 RT + 深度绑回来）；没自己调的场景（F2 / F4 / F6 编辑器）由 `Application` 在 `m_Game.Render()` 之后补画（画过就清空，第二次不做事）。`DebugLineRenderer::Render` 自己设状态：Opaque、**深度只读**（被地形 / 怪挡住的部分会藏起来，但不写深度）、CullNone。`flowview` 自测最后在玩家周围画一个 8m 黄框 + 红竖线 `look lines` 验证：线清楚可见、被前面的怪挡住的部分藏起来。F6 的 `mapedit` 自测照常通过（编辑器的参照网格走补画那条路）。**同日的回归（用户：「为什么结算界面会有 Grid 显示出来」）**：y = 0 的参照格子（`DebugManager::m_ShowGrid`，编辑器用）战斗场景进来时关、`Shutdown` 时恢复成开，所以结算 / 商店（2D 画面，不是 `CollisionTestScene`）里格子是开着的；以前线在场景之前画、被结算的背景盖住看不见，改成场景之后补画以后就浮在结算画面上了。现在 `DebugManager::Render` 只在 F2 / F4 / F6 三个编辑器场景积参照格子（Debug Info 的「Show Grid」在别的场景不起作用）。结算 / 商店截图确认没有格子（结算 = `VFXL_SELF_SHOT` 时出现 4 秒后存 `result_screen.png`，商店 = `ShopScene::TestLog` 的 `shop look …` 行也自截图，都是 TEMP-TEST）。

**调试面板默认全部隐藏（2026-10-16，用户：「ImGui 太多了找不到东西，全给我关了」）**：`DebugManager::m_ShowPanels` 默认 false → `Debug Info` 和战斗场景的全部面板（`CollisionTestScene::DrawDebugUI` 开头返回，含 Game Test / Spell FX / Spell Lab 窗和太阳标记 / 光源 gizmo）都不画，**F10** 切换显示；F2 / F4 / F6 编辑器自己的窗不受影响。自测用代码改的调试开关不受影响。


| 键 | 场景 | 用途 |
|---|---|---|
| F1 | Game Test（`CollisionTestScene`） | 战斗本体（结构见下方「战斗场景的结构」） |
| F2 | VFX Editor | 特效编辑 |
| F3 | Title（`TitleScene` = 战斗场景的标题设定） | 标题（10-09 起背景是游戏内实景，见 docs/ui.md「标题 / 结算画面」） |
| F4 | Projectile Editor | 弹道、范围攻击编辑；第 3 页 Item Shapes 编辑道具形状 + 试放背包 |
| F5 | 重新加载当前场景 | |
| F6 | Level Editor（`LevelEditorScene`） | 摆素材、存 `Assets/Data/LevelData/<名字>.json`；「Battle Map」模式编辑战斗地图 `Assets/Data/MapData/<名>.vmap`（见 docs/map-editor.md） |

## 4. 系统的详细记录（`docs/`）

各系统的实现说明、参数、实测数字和修改经过都在 `docs/` 下（2026-11-13 从本文件原样搬出，本文件以前 775KB，太长）。
**改某个系统之前先读对应的文件；做完功能把记录追加到对应文件**（没有合适的就新建一个，并在下表加一行）。本文件只放约束、工作方式、构建 / 自测方法和快捷键，保持 30KB 左右。

| 文件 | 内容 |
|---|---|
| `docs/enemies.md` | 雑魚（GPU 群体）：外观、死亡碎裂、血条、经验球、各种类（自爆兵・分裂怪・突撃兵・盾兵・沙漠 / 遗迹的敌人・精英） |
| `docs/terrain.md` | 战斗地形：段々台地、水面、起伏、地面贴图、草、落叶、风滚草、碗形边缘、雑魚卡墙 / 爬墙 |
| `docs/world-env.md` | 风格子（战神式）、天气、草的延烧 + 冰、砂漠的绿洲 / 沙面状态场、遗迹瘴气 / 沙尘、草原的风で揺れる木（程序生成） |
| `docs/engine-rendering.md` | 交换链、FrameProfiler / 压力测试、CPU 碰撞、光照 / Forward+、卡通、SSAO、PBR、亮度、影子 |
| `docs/backpack-items.md` | 背包、设置枠、道具形状、形状编辑器、报酬箱、金币、商店、背包的手柄操作 |
| `docs/player-camera.md` | 战斗相机、滑铲、玩家登场 / 死亡、魔力解放、盾、击退、动作特效、智能相机、主角的违和感 |
| `docs/ui.md` | 伤害数字、经验条、升级缓冲、UI 文案 / 倍率 / 字体、说明卡、幻想 UI、FPS、小地图、暂停 / 标题 / 结算、新手引导、图标、手柄 / 键盘图标 |
| `docs/balance.md` | 升级、Megabonk 式平衡、难度表 / 湧きの波 / 面毎难度、发动间隔、稀有度、schoolbench / enemystats、敌人比例 |
| `docs/spells.md` | Noita 试做、出口魔法、基础 / 高级魔法、水晶玉、毒系、渦、竜巻、冰、連鎖 / 吸血、回る剣、吸血コウモリ、拡大鏡、陨石、曲线 |
| `docs/bosses-stages.md` | 三关、选关（地图 × Tier）、9 只 Boss（外观・招式・狂暴 / 击倒演出）、飞行 Boss 的高度 |
| `docs/vfx-audio.md` | 声音、Liquid entry、特效数据库、Trail / 粒子图集 / Sprite / Mesh 粒子、战斗反馈特效、GPU 弹道的特效 |
| `docs/models-assets.md` | 别的文件的骨骼动画 / 重定向、玩家模型（Shadowkin）、斗篷、FBX 单位、删掉的模型、素材许可 |
| `docs/code-history.md` | 11-05 的小修、CPU 路径清理、战斗场景 / SwarmSystem 的拆分、全代码检查、Spell Lab、11-02 晚间任务 |
| `docs/autotests.md` | 仍在代码里的 TEMP-TEST、删掉的自测、全部自测钩子（环境变量）的说明 |
| `docs/map-editor.md` | 地图作为数据保存、F6 Battle Map 编辑器四步 |
| `docs/plans-todo.md` | 雑魚蒙皮 / LOD 的计划、晚上的任务、砂漠的 bug、选关崩溃、待办顺序、之后的候选 |
