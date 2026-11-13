# 里程碑与验收

> 判断"做完了没有"只看这张表。每条标准都要能实测，不接受"看起来对"。

---

## 步骤 1 · 壳跑起来

### M1-1 🚧 壳可执行、可出声、可加面板

| 验收项 | 怎么验 |
|---|---|
| GUI exe 启动出窗口 | 运行，窗口出现，不闪退 |
| 播放/停止/暂停有效 | 点按钮，UI 状态跟随 |
| 新增空 VGM 面板可见 | 菜单或布局里能看到"VGM"面板，内容空白 |

**当前状态**：卡在链接。

```
ninja sequencer_gui
→ libimgui.a(imgui_impl_sdl2.cpp.obj): undefined reference to
  `SDL_IsGameController' `SDL_GameControllerOpen' `SDL_SetCursor' `SDL_ShowCursor' ...
```

原因：静态库顺序（`libimgui.a` 排在 `libSDL2.dll.a` 之前，单趟解析）+ `-static -mwindows` 与 `SDL2.dll` 混用。

修法（按优先级）：
1. `0x808/CMakeLists.txt` 的 `sequencer_gui` 链接段，调成 `sq_gui sq_midi imgui ${SDL2_LIBRARIES} ${SQ_OPENGL_LIB}`（SDL2 在引用者之后）；
2. 或加 `-Wl,--start-group ${SDL2_LIBRARIES} imgui -Wl,--end-group`；
3. 去掉 MINGW 分支的 `-static`，或改用静态 `libSDL2.a`。

---

## 步骤 2 · 导入与结构化

### M1-2 导入：VGM → 寄存器流 → JSON

| 验收项 | 怎么验 |
|---|---|
| 生成 JSON | 文件存在，`format` = `vgm-studio/song` |
| **寄存器写条数正确** | `sum(notes[].segments[].regs) + sum(orphans[].regs) + sum(globals[].regs)` == 该文件中 `0x54` 命令的条数 |
| **完整性不变量** | 上式的并集去重后 == 原始寄存器流，逐条比对无遗漏（写一个校验函数，导入时自动跑） |
| 时间戳单调 | 所有 `atSample` 在各自容器内非递减；跨容器排序后全局非递减 |
| 总长正确 | 最大 `atSample` ≤ `timeBase.totalSamples` |
| 未识别命令有账 | `meta.unhandled` 数组存在且计数可读（样例集应为空） |

**这条是全阶段最重要的验收**。条数不对就说明有写在分块时丢了。

### M1-3 显示：横向音条

| 验收项 | 怎么验 |
|---|---|
| 8 个 FM 通道各一行 | 截图里通道列表有 8 项 |
| 音条在正确采样位置 | 取 JSON 里前 3 个 `startSample`，换算成像素，与截图中音条左端位置对照，误差 ≤ 1 像素级 |
| 音条数量与 JSON 一致 | JSON 里该通道 `notes` 数 == 截图中该行音条数 |
| 先后顺序正确 | 音条从左到右的时间顺序与 JSON 一致 |
| 悬停出原始寄存器 | 截图能看到 `KC/KF/keyon` 原文 |
| 时间轴总长正确 | 时间轴右端的采样值 == `timeBase.totalSamples` |

### M1-4 回放：能出声且长度正确

| 验收项 | 怎么验 |
|---|---|
| 能出声 | 系统音量表有信号 |
| 总长正确 | 播放到结束耗时 ≈ `totalSamples / 44100` 秒（误差 < 1 秒） |
| 与参考一致 | 同一文件用 `vgm2wav` 渲染的采样数 == 我们播放消耗的采样数 |
| 循环点正确 | 有 loop 的文件播放到 `loopSample` 后回跳，`PLREVT_LOOP` 被触发 |

### M1-5 进度：播放头不撒谎

| 验收项 | 怎么验 |
|---|---|
| 位置来自播放器 | 播放中隔 2 秒截两次图，播放头像素位移 ≈ 两次截图间实际消耗的采样数 × 像素/采样比（误差 < 2%） |
| 暂停后不前进 | 暂停 → 等 3 秒 → 截图，播放头不动 |
| 跳转后位置正确 | `seek{sample:N}` → 截图，播放头位置 ≈ N 对应的像素 |

### M1-6 换尺子不改物

| 验收项 | 怎么验 |
|---|---|
| 换 pitchMapper 后 JSON 不变 | 切换前后 JSON 的 `channels[].notes[].segments` 逐字节一致 |
| 换网格后 JSON 不变 | 改 `gridUnit` / `gridBpm` 前后，同上一致 |
| 只重算 view | 切换后 `notes[].view` 有变化，`segments` 无变化 |

### M1-7 诊断可读

| 验收项 | 怎么验 |
|---|---|
| `/state` 返回完整摘要 | 含文件名、总采样、当前采样、播放状态、通道数、音符数、寄存器写数、unhandled 数 |

---

## 步骤 3 · 编辑与跨芯片输出

### M2 音色与转换基础 ✅

> 已由 `cli/` 的 `fuitool` + `opm2opll --fur` 完成（UI 侧待做）。

| 验收项 | 怎么验 |
|---|---|
| 能读 `.fui` | 用 furnace 导出一个 OPM 音色的 `.fui`，导入后界面显示的 alg/fb/TL 等参数与 furnace 里一致 |
| 能改音色并回写 JSON | 改一个 operator 的 TL，JSON 里对应的 `{reg:96, val}` 变了 |
| 音符编辑回写 | 拖动一个音条的左端 1000 采样，该音符所有 `segments` 的 `atSample` 整体移动 1000（相对距离不变） |

### M3 YM2151 → YM2413 转换 ✅

> 已由 `cli/build/opm2opll` 完成，41 首全部转换通过。
> 音高实测见 `PITCH-CONVERSION.md` §5.5（91.3% 完全一致，其余是 OPLL 的 ML 能力上限）。

| 验收项 | 怎么验 |
|---|---|
| 时域无损 | 转换后每个音符的 `startSample` / `endSample` 与源完全一致 |
| 音高映射 | 转换后音高（换算回 Hz）与源相差 < 1 个半音的占比 ≥ 95% |
| 通道映射 | 8 个通道一一对应，第 9 通道空置 |
| 音色映射可解释 | 每个源音色都能给出"匹配到 OPLL preset #N，匹配度 X，丢弃了哪些参数"的报告 |
| 有损报告完整 | 报告列出所有被丢弃/降精度的参数（见 `GOAL.md` §6 位宽表） |

### M4 输出 VGM ✅（CLI 侧）

> 已由 `opm2opll --vgm=` 完成。注意：`json2vgm` 走的是 `VgmRebuild`（寄存器直通），
> 不是 furnace dispatch —— 输出是逐条寄存器写，不需要引擎重放。

| 验收项 | 怎么验 |
|---|---|
| 生成合规 VGM | 输出文件能被 libvgm 加载、解析、播放（`CanLoadFile` 返回成功） |
| 能出声 | 用 `vgm2wav` 渲染出非静音 WAV（**仅诊断用**，平时产物不生成 wav） |
| 时域一致 | 输出的 `totalSamples` 与 target.json 一致 |
| 音符位置一致 | 输出的 key-on 采样位置与 target.json 的 `startSample` 一致 |
| 位精确对照（仅用于诊断） | 原路不编辑时，输出与原文件的命令序列 diff，差异应仅为重复写去重导致的合并 |

---

## 样例集

`vgm/Spindizzy_II_(Sharp_X68000)/`

- 41 个文件，全部 **VGM 1.51**、**单芯片 YM2151 @ 4 MHz**
- 命令流只有：`0x54`（寄存器写）+ `0x61/0x62/0x63/0x70-0x7F`（等待）+ `0x66`（结束）
- 合计约 40 分钟音频
- 部分文件有 loop 点

**第一阶段（M1-*）只做这个范围。**
**步骤 3（M2–M4）的目标芯片是 YM2413（OPLL），9 通道 2-op。**

扩到 OPN2/SSG 的条件：M1-* 全部通过 + M3 的 YM2151→YM2413 链路跑通。
