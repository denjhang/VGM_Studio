# 架构

> 本文件讲**系统由哪些部分组成、各自职责、如何拼接**。
> 目标与步骤 → `GOAL.md`；JSON schema → `DATA-MODEL.md`；界面 → `DESIGN.md`。

---

## 1. 四个角色，各管一段

```
┌───────────────────────────────────────────────────────────────────────┐
│ ① 0x808 —— 壳                                                        │
│    窗口、ImGui、走带条、通道列表、布局、主题、撤销栈                     │
│    只认「文档模型」，不知道芯片、不知道 VGM                              │
├───────────────────────────────────────────────────────────────────────┤
│ ② 我们的结构化层（本项目唯一的核心代码）                                │
│    VGM 寄存器流 ⇄ VGSJ JSON（按音符分块）                              │
│    音符切分规则、视图推算（音高/音名）、可替换 mapper                    │
├───────────────────────────────────────────────────────────────────────┤
│ ③ libvgm —— 解析与回放引擎                                            │
│    VGM 命令解析、60+ 芯片仿真、采样级渲染、播放位置                     │
│    导入时提取寄存器流，播放时出声                                       │
├───────────────────────────────────────────────────────────────────────┤
│ ④ furnace dispatch —— 芯片写入引擎（输出阶段）                         │
│    每个芯片的「正确写序列」、寄存器记录、VGM 字节生成                    │
│    只负责「怎么让这颗芯片正确出声」                                     │
└───────────────────────────────────────────────────────────────────────┘
```

**为什么是四个而不是两个**：解析和写入是两件不同的事，不能互相替代。

- libvgm 能解析 VGM，**不能生成 VGM**（它只有解析器，没有写出器）。
- furnace 能生成 VGM（`saveVGM`），**不能读取 VGM**（`fileOpsCommon.cpp` 的 magic 分派里没有 `"Vgm "`）。
- 所以：**导入走 libvgm，输出走 furnace**，中间由我们自己的 JSON 格式承载。

---

## 2. 四条数据流

### 2.1 导入（VGM → JSON）

```
input.vgm
  │
  ├─ libvgm 解析（命令表 _CMD_INFO[0x100] + ParseFile 的采样推进）
  │     ↓
  │  (chip, port, reg, val, atSample) 扁平寄存器流
  │     ↓
  ├─ 我们的分块器：按音符事件切块（规则见 §3）
  │     ↓
  └─ source.json（schema 见 DATA-MODEL.md）
```

寄存器流的提取方式有三条（见 `reference/libvgm.md` §5），推荐**方案 A：注入影子核**。

### 2.2 显示（JSON → 界面）

```
source.json
  │
  ├─ channels[].notes[]  →  横向音条（位置 = startSample，长度 = durationSamples）
  ├─ notes[].view         →  音条纵向位置 + 音名标签（参考值，可标注 exact=false）
  ├─ channels[].lanes[]   →  折叠在通道行内的自动化曲线
  └─ globals[]            →  顶部全局条（LFO/noise 状态）
```

**UI 绝不读原 VGM 文件。** 这是"看到的数据"和"听到的声音"同源的保证。

### 2.3 播放（JSON → 声音）

```
source.json
  │
  ├─ 重建：把 notes[].segments[] + orphans[] + globals[] 按 atSample 排序
  │         → 生成 VGM 命令字节（0x54 rr vv + 0x61/0x62/0x63/0x70-0x7F 等待）
  │     ↓
  ├─ 喂给 libvgm（内存 DataLoader + 真实芯片核）
  │     ↓
  └─ VGMPlayer::Render() → float 帧 → SDL 音频
```

**进度**：每帧读 `VGMPlayer::GetCurPos(PLAYPOS_SAMPLE)`，直接是采样号，与时间轴同一单位，不需要换算。

### 2.4 输出（JSON → target.vgm）

```
target.json（目标芯片）
  │
  ├─ 初始化：furnace dispatch 的 reset()（含 softReset 静音序列）
  │     ↓
  ├─ 按音符驱动：每个音符块 → 调用 dispatch 的写入接口
  │      （不是自己拼寄存器，而是让 chip driver 决定怎么写）
  │     ↓
  ├─ 采集：dispatch 的 regWrites 队列（dumpWrites 打开时自动记录）
  │     ↓
  └─ 编码：按 (time, order) 排序 → VGM 头 + 命令字节 → target.vgm
```

**这是 furnace 的 VGM 导出机制**：`saveVGM` 会先自己播放整首歌，让芯片 dispatch 在真实播放中产生写，再从 `regWrites` 收集。我们不重写这套逻辑，直接复用。

---

## 3. 音符分块规则

### 3.1 规则来源

参考 modizer 把 VGM 画成钢琴条时的合并判据（`RenderUtils.mm:5965-6015`，约 50 行），经过实战验证。**但时间单位要从它的"1/60 秒帧行"换成采样数。**

### 3.2 判据

| 条件 | 动作 |
|---|---|
| 音高改变（KC/KF 变化超出阈值） | 断开，新块 |
| 通道改变 | 断开（本来就在按通道处理） |
| 音量上升（重触发） | 断开，新块 |
| `VOICE_ON` 清零 / key-off | 块结束 |
| 音量下降（连奏衰减） | **不断开**，继续延长 |

### 3.3 YM2151 的具体触发点

**完整语义表见 [`CHIP-YM2151.md`](CHIP-YM2151.md)**（已由三个独立实现交叉验证，不是草稿）。

| 寄存器 | 作用 | 分块影响 |
|---|---|---|
| `0x08`：`ch = data & 0x07`，`mask = data & 0x78` | key on/off（mask 非零 = on） | **块的开始/结束边界（边沿检测）** |
| `0x28+ch` KC | octave(`>>4 & 7`) + 半音码(`& 0x0F`，需查表) | 变化 → 可能新块 |
| `0x30+ch` KF | 分数半音（`(val >> 2) & 0x3F`，/64 半音） | 与 KC 一起算浮点音高 |
| `0x60 + op*8 + ch` TL（仅载波，按 ALG 选） | 音量 | 上升 → 新块；下降 → 延续 |

**额外的切分条件**（来自 Denjhang 的实践）：即使 key-on 位没变，若 KC/KF 变化导致**音高跳变 > 0.5 半音**，也切新块。OPM 上"快速换音不重新 keyon"是常见技法，但视觉上应显示成两个音。阈值可配置。

### 3.4 不属于任何块的写 → `orphans[]`

音色初始化、音符前的 KC/KF 预设等。见 `DATA-MODEL.md` §7.4。

### 3.5 不变量

```
notes[].segments[] ∪ orphans[] ∪ globals[]  ==  完整原始寄存器流
```

任何一条原始写都不能丢。这是导入正确性的唯一硬指标。

---

## 4. 跨芯片转换的架构

### 4.1 转换发生在哪一层

**只在 JSON 层转换，不在寄存器层转换。**

```
source.json (YM2151)  ──转换器──►  target.json (YM2413)  ──furnace──►  output.vgm
```

理由：寄存器层转换（把 `0x54 0x28 0x1E` 翻译成 `0x51 0x10 0x2A`）是机械翻译，会失败（见 `reference/furnace.md` §4 的 4 个失败点）。JSON 层转换保留了语义（音符、音高、时值、音色参数），再由目标芯片的 driver 决定怎么写。

### 4.2 转换器的三个部件

| 部件 | 输入 | 输出 | 难度 |
|---|---|---|---|
| **时域映射** | `startSample` / `endSample` / 音高 | 同样的采样数和音高 | **几乎为零**（直接搬） |
| **通道映射** | 8 通道 | 9 通道 | 低（一一对应，多出的空置） |
| **音色映射** | 4-op 自由参数 | 2-op + preset | **唯一真正的工作量** |

### 4.3 音色映射（详见 `GOAL.md` §6）

主路径：在 YM2413 的 15 个 ROM 预置里做最近邻匹配，只设 `opllPreset` + 载波 `tl`。
备选：用户音色槽（preset 0），只在整曲单音色时可行。

---

## 5. 代码落点

全部在 `0x808/` 内新增，不新起工程。

```
0x808/
├── src/vgm/                          ← 我们的核心代码（新增）
│   ├── L0Document.{h,c}              顶层文档模型 + JSON 读写
│   ├── RegStream.{h,c}               (chip,port,reg,val,atSample) 序列
│   ├── NoteSplitter.{h,c}            音符分块（§3 的规则）
│   ├── AnalyzerYm2151.{h,c}          YM2151 语义：KC/KF → 音高推测、ALG → 载波
│   ├── Mappers.{h,c}                 IPitchMapper / IGridMapper（可替换）
│   ├── VgmImport.{h,c}               libvgm 影子核 → RegStream → Document
│   ├── VgmRebuild.{h,c}              Document → 内存 VGM 字节流
│   ├── VgmPlayerHost.{h,c}           libvgm VGMPlayer 生命周期 + 采样时钟
│   ├── FuiReader.{h,c}               读 furnace .fui 音色文件
│   ├── ChipConvert.{h,c}             跨芯片转换（时域/通道/音色）
│   └── FurnaceWriter.{h,c}           Document → furnace dispatch → VGM
│
├── src/gui/
│   ├── vgm_seq.{h,cpp}               通道列表 + 横向音条（主视图）
│   ├── vgm_toolbar.{h,cpp}           走带条（采样时钟模式）
│   ├── vgm_reg_view.{h,cpp}          寄存器十六进制表
│   ├── vgm_ins_edit.{h,cpp}          音色编辑器（抄 furnace insEdit 的组织）
│   └── vgm_convert_dialog.{h,cpp}    通道映射 + 音色映射设置
│
├── src/app/sq_app.h                  加 VGM 面板枚举
└── src/standalone/main_gui.c         音频回调改喂 VGMPlayer；加"打开 VGM"
```

### 5.1 硬约束

| 约束 | 原因 |
|---|---|
| **不改** `src/engine/`（合成器/采样器） | 那是 MIDI 音序器自己的引擎，VGM 模式整体绕过 |
| **不改** `src/engine/transport.c` | BPM/16 分/swing 与采样时钟无关 |
| **不复用** `src/gui/drum_grid.cpp` / `piano_roll.cpp` | 绑死 `sq_step_t` / `sq_track_t`（16 轨 × 64 step），VGM 塞不进去 |
| **不做** GTK 前端对齐 | 壳的 `CLAUDE.md` 要求两前端一致，VGM 视图明确只做 ImGui 一份 |

### 5.2 与 furnace 的关系

**只链接它的 engine 层，不链接 GUI。**

需要从 furnace 引入的部分：

| 内容 | 位置 | 用途 |
|---|---|---|
| `DivDispatch` 基类 + 各芯片 dispatch | `src/engine/platform/*.cpp` | 芯片写入 |
| `DivRegWrite` / `addWrite` / `regWrites` / `dumpWrites` | `src/engine/dispatch.h:39,841,1272-1276` | 寄存器写记录 |
| `DivInstrument` / `DivInstrumentFM` | `src/engine/instrument.h:173,1149` | 音色结构（也是 .fui 的内存表示） |
| `instrument.cpp` 的 `.fui` 读写 | `:3405` / `:4429` | 读 FUI |
| `sysDef.cpp` 的芯片能力表 | `:1091`（YM2151）/ `:1257`（OPLL） | 能力查询 |
| `vgmOps.cpp` 的 VGM 编码 | `:566 saveVGM` / `:2161-2221` | 写序列 → 字节 |

**代价**：furnace 是 GPL-2.0+。链接它的 engine 层意味着整个项目必须 GPL。这是需要用户确认的前提（见 `RULES.md`）。

---

## 6. 三套引擎的边界（防混用）

| 场景 | 用谁 | 绝不用 |
|---|---|---|
| 读 VGM 文件头、解析命令、推进时间 | **libvgm** | furnace（它没有导入器） |
| 导入时提取寄存器流 | **libvgm**（影子核） | 自己写解析器 / 自己 hexdump |
| 播放出声 | **libvgm**（真实芯片核 + `Render`） | furnace（它的音频层是 tracker 引擎） |
| 取播放位置 | **libvgm** `GetCurPos(PLAYPOS_SAMPLE)` | 墙钟累加 |
| 生成 VGM 字节 | **furnace** `saveVGM` 的机制 | 自己拼 `0x54` 字节（会漏掉时序约束） |
| 决定"某芯片某参数该写哪几个寄存器" | **furnace** chip driver | 自己查手册拼寄存器 |
| 窗口 / ImGui / 走带 | **0x808** | 其他 |

---

## 7. 时序与线程

```
主线程（GUI）
  ├─ ImGui 渲染
  ├─ 读 VGMPlayerHost 的当前位置（原子读）→ 画播放头
  └─ 用户操作 → 命令总线 → 修改 Document

音频线程（或推送线程）
  ├─ VGMPlayer::Render(frames) → float 帧
  └─ 写入 SDL 音频队列

导入（一次性，可在后台）
  ├─ libvgm 影子核遍历 → RegStream
  └─ NoteSplitter → Document（完成后原子替换）
```

- **Document 只在主线程改**。音频线程只读已发布的快照。
- **播放位置只由播放器提供**，UI 不做插值补偿（插值只影响视觉平滑，不影响逻辑）。
- 0x808 的音频是**推送式**（`SDL_QueueAudio`，`main_gui.c:143`），所以 `Render` 在推送线程里调。

---

## 8. 为什么不用其他方案

| 方案 | 否决理由 |
|---|---|
| 用 furnace 当壳 | 没有 VGM 导入；30 万行；GPL；6 个空 submodule 编不了 |
| 用 Gridline 当壳 | 46.5 万行；GPL-3；178 个不需要的乐器 |
| 自己写 VGM 解析器 | libvgm 已有完整命令表，重写只是制造第二套真相 |
| 自己写芯片仿真 | libvgm 有 60+ 核 |
| 自己拼 VGM 写出 | 会漏掉 addr→data 配对、同址多次写、硬复位延时等时序约束（见 `reference/furnace.md` §4） |
| 直接做寄存器翻译（A 芯片寄存器 → B 芯片寄存器） | 机械翻译必然失败；必须走"JSON 语义层 → 目标芯片 driver" |
| 数据层存 BPM/网格 | VGM 是日志格式，没有 BPM |
