# 硬约束与禁止事项

> 评审任何改动时对着这张表核。违反任何一条即为设计缺陷，不是"实现选择"。

---

## 数据层约束

### D1 · 时间单位只有采样数

整个软件的时间轴都是采样数（int64，44100 Hz）。
数据文件里**不出现** tick / 行 / 拍 / 帧 / BPM。

推论：任何"第 N 行""第 N 小节""第 N 拍"都是 UI 层的派生量，不得落盘。

### D2 · 不记录 BPM

VGM 是日志格式，里面没有 BPM。BPM 只允许作为 UI 的**参考假设值**出现，且必须标注"假设"。
`IGridMapper` 可以返回 BPM，但它的返回值不得写回 JSON。

### D3 · 原值是唯一真值

唯一真值 = `(chip, port, reg, val, atSample)` 五元组。
音高、音名、音量、网格位置全是视图层推测，必须带 `exact` / `confidence` / 溯源信息，且必须能随时丢弃重算。

### D4 · 按音符事件分块，但不许丢写

寄存器流切成"key-on 到 key-off"的块。不属于任何块的写进 `orphans[]`。

**不变量**（每次导入必须校验）：
```
notes[].segments[] ∪ orphans[] ∪ globals[]  ==  完整原始寄存器流
```
任何一条原始写都不能丢。

### D5 · 换尺子不改物

换音高换算器、换网格、换主题、换可视化方案 —— **JSON 数据一字节不动**。
若某功能需要改数据才能实现，说明它是编辑功能（步骤 3），不是显示功能。

---

## 引擎分工约束

### E1 · 解析只用 libvgm

不自己写 VGM 解析器。libvgm 的命令表 `_CMD_INFO[0x100]`（`player/vgmplayer_cmdhandler.cpp:20`）已覆盖全部命令族。

### E2 · 仿真只用 libvgm

不自己写芯片仿真。libvgm 有 60+ 核。

### E3 · 输出只用 furnace dispatch

不自己拼 VGM 寄存器字节。理由：会漏掉时序约束（见下方 F1）。

### E4 · 播放位置只来自播放器

`VGMPlayer::GetCurPos(PLAYPOS_SAMPLE)`。不用墙钟累加，不用帧计数近似。

### E5 · 转换只发生在 JSON 语义层

不做寄存器层翻译（A 芯片寄存器 → B 芯片寄存器）。必须走"JSON 语义 → 目标芯片 driver"。

---

## 禁止事项

| # | 禁止 | 原因 |
|---|---|---|
| **F1** | 自己拼 VGM 寄存器写序列 | 会漏掉 5 类时序约束：① addr→data 两端口配对（`arcade.cpp:69-77` 的 `addrOrVal` 状态机）② 同一地址多次写靠 bit7 区分（`0x19` AM/PM、`0x0f` noise bit7）③ 必须插延时（hard reset 需 127 周期）④ 必须发静音序列（`softReset` 把 RR 写 0xF）⑤ `rWrite` vs `immWrite` 的去重语义差异 |
| **F2** | 手工 hexdump / Python 脚本去核对 VGM 头部偏移、统计命令 | 那是 libvgm 的职责。读库，不读数据文件 |
| **F3** | 时间量化（对齐到行/拍/帧/BPM 网格） | 违反 D1/D2。VGM 没有 BPM，量化即造假 |
| **F4** | VGM → MIDI 转换 | 数据里不存在 MIDI 事件 |
| **F5** | 用 tracker 纵向表格做界面 | 表格强制"一个事件一个格"，在 UI 层就把时间量化了 |
| **F6** | 改 0x808 的 `src/engine/`（合成器/采样器） | 那是 MIDI 音序器自己的引擎，VGM 模式整体绕过它 |
| **F7** | 改 0x808 的 `transport.c` | BPM/16 分/swing 与 VGM 采样时钟无关 |
| **F8** | 复用 0x808 的 `drum_grid.cpp` / `piano_roll.cpp` | 绑死 `sq_step_t` / `sq_track_t`（16 轨 × 64 step × 16 pattern），VGM 是"任意芯片 × 任意寄存器 × 任意时刻"，塞不进去 |
| **F9** | 在显示还没跑通时去调音高换算公式 | 顺序错。先跑通 导入→显示→回放→进度，再谈准不准 |
| **F10** | 在 JSON 里存 `bpm` / `tick` / `row` / `beat` / `frame` | 见 D1/D2 |
| **F11** | 在 `segments` 里存音高/音名 | 音高是推测，必须放 `view` |
| **F12** | 删除或整篇重写已有设计文档 | 改哪一块只动对应文件；不确定的先留档到 `_recovered/` |
| **F13** | 自己写 `.fui` 解析器 | 直接链接 furnace 的 `DivInstrument::readInsData()`（`instrument.cpp:4316`） |
| **F14** | 认为 FamiStudio 能提供 YM2151 算法 | 它只支持 NES 系；`0x54` 落进 "Unknown VGM Chip Data"（`VgmFile.cs:2398-2403`） |
| **F15** | 用 modizer 的"实时嗅探"路线 | 它的音符提取散落在 26 个芯片核的 `update()` 热循环里，强依赖 App 全局变量。只借它的**合并判据**，不借它的架构 |

---

## 参考库使用边界

| 库 | 可以做的 | 不可以做的 |
|---|---|---|
| `0x808` | 改代码、加模块、换音频源、用它的 ImGui 后端和主题 | 改 `src/engine/` 合成逻辑、改 `transport.c`、复用 `drum_grid`/`piano_roll` |
| `libvgm` | 链接、注入影子核、读写公开 API | 改它源码（除非确认许可允许且必要） |
| `furnace` | **链接 engine 层**（`src/engine/`）、用 `.fui` 读写、用 `sysDef` 能力表、复制 `saveVGM` 的机制 | 链接 GUI 层（`src/gui/`）；抄它的纵向 tracker 表格布局 |
| `libmidi` | 搬 `test/immidi.cpp` 里的 ImGui 绘制函数（MIT） | 搬 `libmidi/` 内核（TiMidity 血统，GPL） |
| `Gridline` | 读代码、抄思路（`Timeline.h` 的 `Snap::None`、`Types.h` 的 `frac`） | 链代码进构建（GPL-3） |
| `nanotracker` | 读 `piano_roll_view.cpp`（776 行）的最小写法 | 链代码（GPLv3）、抄它的整 tick 时间模型 |
| `FamiStudio` | 读 `UpdateChannel()` 的"状态机形状" | 期望它对 YM2151 有用、抄它的帧量化模型 |
| `tks` | 读 `GenericSynthEdit.tks` 的 FM 参数矩阵布局思路 | 全部代码（无 ImGui、LGPL/GPL、TKS 脚本） |
| `modizer` | 搬 bar 合并判据（约 50 行逻辑）、`vgm_getNoteFromFreq`（10 行数学） | 复刻实时嗅探架构、搬 iOS/GLES 渲染代码 |
| `Denjhang` | 抄 YM2151 key-on/音高/ALG 载波规则、shadow 寄存器+边沿检测架构 | 抄它的"只有瞬时状态、无音符段"模型（它没有 roll） |
| `MDPlayer` | 抄寄存器十六进制表布局、每算子 11 字段参数清单、diff 渲染思路 | 抄"KF 不参与音高"（这是它的缺陷） |
| `vgm2mid-valleybell` | 抄音符分块判据、绝对采样取整时间算法、浮点音高表示法 | 抄 VB6 单文件流式结构（我们要持久化音符段） |
| `vgm-conv` | 抄 ALG→载波→平均TL→4bit 的音量映射、hash 前归一化 TL 的技巧 | 抄逐寄存器翻译架构（违反 E5）、抄 `md2608` 的手写 VGM 写出（它的 5 个坑） |
| `vgmtransmute` | 抄"循环偏移与总样本数重算"、"寄存器去重需排除有副作用的寄存器" | 期望它对 YM2151/YM2413 有用（它只有 OPNA→OPNB） |
| `vgm2mid_ws` | 读"无 key-on 芯片"的判定范式（使能位 + 音量） | 抄它手抄 modizer 解析器的做法 |

---

## 许可前提（待用户确认）

**链 furnace engine 层 = 整个项目 GPL-2.0+。**

这是架构决定的硬前提（`ARCHITECTURE.md` §5.2）。若不接受：

| 替代方案 | 代价 |
|---|---|
| 只读 furnace 代码，自己写芯片写序列 | 工作量巨大（81 个芯片 × 各自的时序约束），且第一版只需 YM2151 + YM2413 两个芯片，可以只写这两个 |
| 用 libvgm 的仿真核反向生成寄存器写 | libvgm 没有写出器，仍需自己排时序 |

**第一阶段（只做 YM2151 导入 + 显示 + 回放）不涉及 furnace**，可以先用 MIT + LGPL 组合完成。到步骤 3（输出）再决定是否 GPL。

libvgm 方面：YM2151 要宽松许可就显式选 Nuked 核（LGPL-2.1+），不用默认的 MAME 核（GPL-2.0+）。
