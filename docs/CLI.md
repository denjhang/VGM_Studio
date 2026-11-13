# 命令行版本设计（CLI First）

> 决策（2026-11-11）：**先做命令行，UI 和可视化往后放。**
> 理由：业务拆分后每个环节都是独立的可执行程序，最容易调试、最容易验证、最容易替换。
> 目标：`YM2151 VGM → JSON → 编辑 → YM2413 JSON → YM2413 VGM` 全链路跑通，全程可用文本工具检查每一步。

---

## 1. 为什么要 CLI 优先

### 1.1 现有转换器的根本问题：写死的

| 项目 | 问题 |
|---|---|
| `vgm-conv` | 每个转换方向是一个 `.ts` 文件（`opn-to-ym2413-converter.ts` 437 行）。要改映射规则、换音色表、调音量偏移，**必须改代码重新编译**。用户无法干预 |
| `vgmtransmute` | 只有一个方向（OPNA→OPNB），寄存器重映射写死在 `ym2610b.js:67-91` 的 if-else 链里。想加一个方向就得改代码 |
| 两者共同 | 中间状态是**内存里的 shadow 寄存器数组**，无法观察、无法编辑、无法存档 |

### 1.2 JSON 存在的意义就是这个

用户原话：

> JSON 存在的意义就是在于便于修改。
> 这些转换都是写死的代码，你没有办法改任何东西。

所以 JSON 不是"为了好看"，它是**唯一的可编辑中间态**：

- 用文本编辑器能改
- 用 `jq` 能批量改
- 用脚本能生成
- 用 diff 能看出改了什么
- **音色映射表本身就是一份 JSON/TSV，用户改文件就能换映射，不用重新编译**

### 1.3 每个业务一个程序

拆开的好处：

| 好处 | 说明 |
|---|---|
| 单步可测 | 每步的输入输出都是文件，可以独立 diff |
| 失败可定位 | 哪一步输出错了，一眼看出 |
| 可替换 | 想换解析器只改 `vgm2json`，其他不动 |
| 可组合 | `vgm2json a.vgm | jq '.channels[0]'` 之类 |
| 无 UI 干扰 | 崩溃时没有渲染线程、没有 ImGui 状态需要猜 |

---

## 2. 程序清单

全部在独立的 `cli/` 工程里（**不碰 `0x808/`**，见 `RULES.md` F6–F8），
每个程序一个 `main`，共用 `cli/src/` 下的 `vgmcore` 静态库。

| 程序 | 输入 | 输出 | 职责 |
|---|---|---|---|
| **`vgminfo`** | `.vgm` | stdout | 打印头部信息 + 命令统计（**不解析语义**，只报账） |
| **`vgm2json`** | `.vgm` | `.json` (+ `--fui` 时出 `.fui`) | **导入 + 结构化 + 音色提取** |
| `jsondump` | `.json` | stdout | 人读的摘要（通道数、音符数、寄存器写数、完整性校验结果） |
| **`json2vgm`** | `.json` | `.vgm` | **输出**：走 `VgmRebuild` 直接写寄存器流（不经过引擎重放） |
| **`opm2opll`** | source `.json` (+ `.fur` / 库) | target `.json` (+ `.vgm` / `.fui`) | **跨芯片转换**（时域/通道/音色/宏展开） |
| `fuitool` | `.fui` / `.fur` | stdout / `.fui` / `.json` | **音色工具**：`dump` / `check` / `rewrite` / `fur` / `lib` / `unpack` |
| `vgmroundtrip` | `.vgm` | stdout | **`vgm → json → vgm` 往返校验**（字节级 diff） |
| **`json2mid`** | `.json` | `.mid` | **音高验证**：把任意芯片的 JSON 转成标准 MIDI，两份 MIDI 直接对比音高，见 `docs/PITCH-CONVERSION.md` |

`cli/tools/` 下是脚本（不进 `vgmcore`）：`batch_convert.sh` 批量转换、
`probe_volume.py` / `probe_channels.py` 音量实测。

### 2.1 为什么 `vgminfo` 和 `vgmroundtrip` 是独立程序

- `vgminfo` 是**诊断工具**：确认"这个文件到底是什么"。它不涉及任何语义，只回答"用了哪些芯片、多少条命令、总长多少"。
- `vgmroundtrip` 是**正确性守门员**：`vgm → json → vgm` 如果字节不一致，说明 `vgm2json` 或 `json2vgm` 有 bug。**在接任何转换之前必须先让它通过。**

---

## 3. 完整链路（当前实际用法）

一首曲子（以 `33 Oily Way` 为例，改动最少就能跑通）：

```
vgm/Spindizzy_II_(Sharp_X68000)/33 Oily Way.vgm
        │
        │  cp … song.ym2151.vgm            ← 原版留一份，方便 A/B
        │  vgm2json … --fui                ← 源 JSON + 源侧 .fui
        ▼
  song.json                  ← YM2151 源 JSON（可编辑，**不入库**，太大）
  chXinsY_ym2151.fui         ← 源侧音色
        │
        │  （在 Furnace 里改 Oily Way ins.fur：音色 + 宏 + note:/vol:/noteoff:/link:）
        ▼
  Oily Way ins.fur           ← 唯一的音色编辑入口（入库）
        │
        │  opm2opll song.json -o song.ym2413.json \
        │      --fur="Oily Way ins.fur" --library=docs/instrument-library.json \
        │      --transpose=-12 --fui-dir=fui/ --vgm=song.ym2413.vgm
        ▼
  song.ym2413.json           ← YM2413 目标 JSON（可编辑，不入库）
  song.ym2413.vgm            ← 输出 VGM（入库）
  fui/                       ← 转换后每个音色的 .fui（入库）
        │
        │  vgminfo + 播放器试听 + 原版对照
        ▼
      验收
```

**整张专辑**用 `cli/tools/batch_convert.sh`，每首一个目录，详见 `RULES.md` 之外的
`out/README.md`（那个文件只讲目录结构与产物清单）。

**每一步都能停下来检查。** 这就是 CLI 优先的价值。

---

## 4. 各程序的详细设计

### 4.1 `vgminfo` — 诊断

```bash
vgminfo 31\ Grass\ Maze.vgm
```

输出：

```
file          : 31 Grass Maze.vgm
size          : 77910 bytes
version       : 1.51
eofOffset     : 0x13000
dataOffset    : 0x80
totalSamples  : 2406316  (00:54.56 @44100)
loopOffset    : 0x0f00
loopSamples   : 1200000
chips:
  YM2151      clock=4000000
commands:
  0x54 (YM2151 write)      45678
  0x61 (wait N samples)     3421
  0x62 (wait 735)             12
  0x63 (wait 882)              0
  0x70-0x7F (wait 1-16)    12034
  0x66 (end)                   1
```

**实现**：直接调 libvgm 的 `VGMPlayer::LoadFile()` + `GetFileHeader()`，命令统计用 `_CMD_INFO[0x100]` 的 `cmdLen` 走一遍（**不自己写解析器**，见 `RULES.md` F2）。

**如果 libvgm 不提供命令计数**，用方案 A 影子核跑一遍统计（反正 `vgm2json` 要做同样的事）。

### 4.2 `vgm2json` — 导入

```bash
vgm2json <input.vgm> -o <output.json> [--extract-instruments=DIR] [--pretty]
```

| 选项 | 说明 |
|---|---|
| `-o` | 输出 JSON 路径（默认输入同名 `.json`） |
| `--extract-instruments=DIR` | 把识别出的音色导出为 `.fui` 文件到 DIR |
| `--pretty` | 缩进输出（默认紧凑，便于大文件） |
| `--no-raw` | 不写 `raw.commands`（减小文件，但失去位精确往返） |
| `--budget=N` | 每个音符块最多保留多少条寄存器写（0 = 不限，默认 0） |

**内部流程**：

```
1. libvgm 影子核遍历 → RegStream (chip, port, reg, val, atSample)
2. NoteSplitter → notes[] / orphans[] / globals[]     （规则见 CHIP-YM2151.md）
3. AnalyzerYm2151 → view 字段（音高/音名/音量参考值）
4. 音色提取 → 每个音符块的 operator 参数集合 → 指纹去重 → .fui 文件
5. 完整性校验：notes ∪ orphans ∪ globals == RegStream（必须通过，否则报错退出）
6. 输出 JSON
```

**退出码**：

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 参数错误 |
| 2 | 文件读失败 |
| 3 | VGM 解析失败 |
| **4** | **完整性校验失败**（寄存器写有丢失） |

### 4.3 `jsondump` — 检查

```bash
jsondump 31_Grass_Maze.json
```

输出：

```
file          : 31_Grass_Maze.json
format        : vgm-studio/song v1
source        : 31 Grass Maze.vgm (2406316 samples, 00:54.56)
chips         : ym2151.0  YM2151 @4000000  8 ch
regStream     : 61130 writes
notes         : 1284  (FM1: 210, FM2: 178, FM3: 156, ...)
orphans       : 342 writes
globals       : 47 segments, 312 writes
lanes         : 96 points (pms: 42, ams: 12, pan: 42)
integrity     : OK  (61130 == 61130)
instruments   : 8 unique  (hash: a91f3c.., 3b02e1.., ...)
```

**`integrity` 这一行是核心**：`notes ∪ orphans ∪ globals` 的条数必须等于 `regStream` 条数。

### 4.4 `json2vgm` — 输出

```bash
json2vgm <input.json> -o <output.vgm> [--chip=ym2151|ym2413] [--check]
```

| 选项 | 说明 |
|---|---|
| `--chip` | 目标芯片（默认从 JSON 的 `chips[0].type` 读） |
| `--check` | 输出后再跑一遍 `vgminfo` 验证可加载 |
| `--no-optimize` | 关掉寄存器写去重（调试用） |

**内部流程**：

```
1. 从 JSON 重建寄存器写序列（按 atSample 排序，同一采样点保持原顺序）
2. 按芯片写初始化序列（见 §6）
3. 直接编码成 VGM 命令字节
4. 写文件（含头部、loop 偏移与总样本数重算、GD3 透传）
```

**为什么不经 furnace dispatch**：本项目的 JSON 里存的就是**寄存器原值**
（`DATA-MODEL.md`：绝不量化、只存寄存器），重建时不需要引擎重新算一遍，
直接按时间戳吐写即可。`vgmroundtrip` 对 41 首全部字节一致，证明这条路是对的。
furnace dispatch 是将来做「参数级编辑 → 寄存器」时才需要的。

**循环偏移与总样本数必须重算**，不能照抄输入（`vgmtransmute` 的正确做法，`md2608` 的错误做法）。

### 4.5 `opm2opll` — 跨芯片转换（★ 核心）

```bash
opm2opll <source.json> -o <target.json> \
    --target=ym2413 \
    [--bank=ym2413] [--clock=3579545] \
    [--volume-bias=0] [--transpose=-12] \
    [--fui | --fui-dir=DIR] [--use-edited-fui] \
    [--fur=PROJECT.fur] \
    [--library=LIB.json] \
    [--tick-rate=60] [--no-macros] \
    [--user-voice] [--report] [--vgm=OUT.vgm]
```

| 选项 | 说明 |
|---|---|
| `--target` | 目标芯片（`ym2413` / `ym2151` / ...） |
| `--bank` | OPLL 音色库：`ym2413` / `vrc7` / `ymf281b` |
| `--clock` | 目标芯片时钟（默认 3579545） |
| `--volume-bias` | 音量偏移（nibble 单位，-15..15），叠加在 TL→音量的换算结果上。**默认 0**：换算已按物理比例 `round(TL/4)` 做，整曲 RMS 与源对齐，不需要再补（详见 [VOICE-MAPPING.md §5.1](VOICE-MAPPING.md)） |
| `--transpose` | 整体移调（半音）。**默认 -12**：实测不降这一八度会比源高一个八度 |
| `--fui` / `--fui-dir` | 为每个源音色写出 `chXinsY_ym2413.fui` |
| `--use-edited-fui` | 读同目录下改过的 `.fui` 覆盖自动匹配（含宏） |
| `--fur` | **从 Furnace 工程一次读全部音色**（先按乐器名 `chXinsY`，对不上再按 OPLL 乐器顺序兜底），并用工程的 `hz` 作为宏 tick 速率。优先级高于 `--use-edited-fui`；找不到的音色会在输出里显式列出 |
| `--library` | **跨曲目乐器库**（`fuitool lib --json=` 导出的那份）。按 **YM2151 的 FM 参数**匹配（不是指纹），命中即整套搬用已调好的目标音色 + 宏 + `note/vol/noteoff/link` 参数。优先级最高；完全一致直接命中，否则取参数最近的（距离 ≤ 48），都没命中才退回 `--fur` / ROM 自动匹配 |
| `--tick-rate` | 宏的 tick 速率（Furnace 的 `song.hz`，默认 60）。**`.fui` 里没有这个数** |
| `--no-macros` | 不执行 `.fui` 里的宏 |
| `--report` | 输出逐音符有损报告 |
| `--vgm` | 同时写一份试听 VGM（含循环点与 GD3 透传） |

**内部流程**：

```
1. 读 source.json
2. 时域映射：原样搬 startSample/endSample（零工作量）
3. 通道映射：8 → 9 一一对应
4. 音色映射：
   a. 对每个源音色，按源寄存器指纹（不含 TL）去重编号 chXinsY
   b. 跨曲目库 `--library` 按 **源侧 FM 参数**（不含 TL）查表；命中就用库里的
      目标音色 + 宏 + 名字参数
   c. 未命中时自动匹配 ROM 预置（最近邻）；有 --fur / --use-edited-fui 时
      以工程或 .fui 里的 opllPreset 为准（含 0 = 用户槽）
   d. 生成/读入 target 的 .fui，展开其中的宏
5. 音量映射：载波 TL → 0x30 低 4 位
6. 音高映射（关键）：
   · 起点：KC/KF → Hz → OPLL block/fnum，叠加 ML 八度补偿与 --transpose
   · 音符内部：逐点跟随源的 0x28(KC)/0x30(KF)，重算 fnum
   · key 事件：按源 0x08 逐条重放，不给 legato 切块补 key-on/off
7. 生成 target.json（同一 schema，chips 换成目标芯片，tags 原样搬）
8. 输出有损报告
```

**音色不做"映射表文件"**：早期设计里有一份 `opm2opll.map.json`，实际实现没有采用。
音色映射走的是「源 FM 参数 → 目标音色」的**查表**（`--library` 的 `pairs` 段，
或 `.fur` 里按 `chXinsY` 命名的乐器），表就是这两个文件本身，用户改它们即可，
不用重新编译——目的达到了，还少了一层间接。

**宏展开是照抄 Furnace 语义自己实现的**（`MacroEngine.cpp` 逐行对应
`opll.cpp:150-340` / `arcade.cpp:292-480`），没有链接 furnace 的 engine 层。
这样既能精确控制「宏只在真实 key-on 启动一次」「延续块接续宏相位」这些
本项目特有的需求，也避开了 furnace 那 6 个空 submodule。

### 4.6 `fuitool` — 音色管理

```bash
fuitool dump   voice.fui                     # 音色参数 + 22 条宏
fuitool fur    project.fur                   # .fur 里的全部乐器
fuitool lib    project.fur [--json=OUT]      # 导出乐器库（stdout = Markdown）
fuitool unpack project.fur outdir --type=13  # 解包成 .fui，文件名 = 乐器全名
fuitool check  a.fui b.fui ...               # 批量校验能否被 Furnace 解码
fuitool rewrite in.fui out.fui --name=X      # 读改写往返
```

**`fuitool fur`**：`.fur` 是 zlib 压缩的 Furnace 工程。读取路径与
`opm2opll --fur` 完全一致——zlib 解压（`fileOpsCommon.cpp:49-140`）→
头解析（`fur.cpp:1035-1345` 旧 INFO / `800-1030` 新 INF2）→ 每个乐器的字节
交回 `DivInstrument::readInsData`。**不是自己写的解析器**，所以 Furnace
升级格式后只要同步 vendored 的 `instrument.cpp` 即可。

**实现**：直接调 furnace 的 `DivInstrument::readInsData()` / `save()`（`RULES.md` F13，不自己写 `.fui` 解析器）。

**`fuitool to-json` 的意义**：`.fui` 是二进制，人读不了。转成 JSON 之后可以 diff、可以手改、可以生成。改完再转回 `.fui` 灌进 furnace，用 furnace 音色编辑器（`insEdit`）预览。

### 4.7 `vgmroundtrip` — 往返校验

```bash
vgmroundtrip 31\ Grass\ Maze.vgm
```

```
PASS  or  FAIL
原文件        77910 bytes
重建文件      77910 bytes
字节一致      YES / NO
首个差异偏移  0x1234  (原 0x54 0x28 0x1E / 新 0x54 0x28 0x1F)
```

**这是任何转换之前的门禁。** 往返不通过就不许做转换。

---

## 5. 映射表格式（`*.map.json`）——**未采用，仅存档**

> 实际实现没有独立的映射表文件。音色映射由 `docs/instrument-library.json`
> 的 `pairs` 段（跨曲目）和 `.fur` 里的 `chXinsY` 命名（本曲）承担，
> 见 §4.5。下面这份 schema 保留下来是为了说明当时的设想，**不是当前行为**。

### 5.1 完整结构

```jsonc
{
  "format": "vgm-studio/chipmap",
  "version": 1,

  "from": { "chip": "YM2151", "channels": 8, "operators": 4 },
  "to":   { "chip": "YM2413", "channels": 9, "operators": 2 },

  // ── 通道映射 ──────────────────────────────────────
  "channelMap": [0, 1, 2, 3, 4, 5, 6, 7, null],   // null = 空置

  // ── 音量映射 ──────────────────────────────────────
  "volume": {
    "source": "carrierMinTL",        // 源音量取法
    "target": "reg0x30High4",        // 目标音量落点
    "scale": { "from": [0, 127], "to": [0, 15] },
    "curve": "linear",               // linear | log | table
    "table": null                     // curve=table 时用
  },

  // ── 音色映射 ──────────────────────────────────────
  "instrument": {
    "mode": "presetMatch",           // presetMatch | explicitOnly | explicitPlusAuto
    "presetBank": "ym2413",          // ym2413(15 个 ROM) | ymf281 | ds1001
    "defaultProgram": 1,
    "defaultVolumeOffset": 15,       // 无匹配时的音量（15 ≈ 静音）

    // 显式条目：源音色 hash → 目标音色
    "entries": {
      "a91f3c...": { "program": 5, "volumeOffset": -2, "octaveOffset": 0 },
      "3b02e1...": { "program": 12, "volumeOffset": 0, "octaveOffset": -1 }
    },

    // 源侧音色的提取规则（哪些寄存器构成一个音色指纹）
    "sourceParams": [
      "0x20+ch", "0x38+ch",
      "0x40+op*8+ch", "0x60+op*8+ch", "0x80+op*8+ch",
      "0xA0+op*8+ch", "0xC0+op*8+ch", "0xE0+op*8+ch"
    ],
    "normalizeBeforeHash": true      // 按 ALG 把 TL 归一到最响算子为 0
  },

  // ── 丢弃与降精度声明（生成有损报告用）────────────
  "lossy": {
    "scaleDown": [
      { "param": "ar", "from": [0,31], "to": [0,15], "by": ">>1" },
      { "param": "dr", "from": [0,31], "to": [0,15], "by": ">>1" },
      { "param": "tl", "from": [0,127], "to": [0,15], "by": ">>3" }
    ],
    "drop": ["dt2", "d2r", "rs", "ssgEnv", "op3", "op4"],
    "degrade": [
      { "param": "alg", "from": [0,7], "to": [0,1], "rule": "carrierCountToSus" },
      { "param": "ksr", "from": [0,3], "to": [0,1], "rule": "takeHighBit" }
    ]
  },

  // ── 鼓处理（★ 见 §6）──────────────────────────────
  "rhythm": {
    "enabled": true,
    "source": null,                  // YM2151 没有硬件鼓，源侧恒 null
    "presetDrums": true,             // 用 YM2413 的 preset drums
    "template": "vgm-conv"           // 参考 vgm-conv 的初始化序列
  }
}
```

### 5.2 为什么 `channelMap` 是数组而不是整数

因为将来可能要做"两个源通道合并到一个目标通道"或"一个源通道拆成两个"。数组留出了这个空间（元素可以是 `null`、整数、或 `{from:[0,1], mix:"avg"}`）。

### 5.2b 音色指纹与匹配算法见 `VOICE-MAPPING.md`

`instrument.entries` 的键是 **60 字符十六进制指纹**（30 个寄存器值 × 2 字符），算法细节在 [`VOICE-MAPPING.md`](VOICE-MAPPING.md)：

- 指纹的构造：`_identifyVoice`（`opn-to-ym2413-converter.ts:186-243`）
- **归一化**：按 ALG 把载波 TL 归到"最响为 0"（`:44-76`）—— 不归一化的话同音色不同音量会被算成多个音色
- **最近邻匹配**：`toOPLLROMVoice`（`opl-voice.js:170-222`），含 6 个关键设计（ML 比值、持续/打击音分流、八度补偿、音量补偿）
- **OPLL 的 15 个 ROM 音色表**：`_OPLL_ROM_PATCHES`（`opll-voice.js:6-27`），已 vendor 到 `docs/_vendor/ym-voice-0.3.2/`
- **vgm-conv 的 voice-table 格式可直接复用**（`{i:5,v:2,o:0}`），注意它 `v` 是取负的

**约 150 行 C++ 就能实现全部。**

### 5.3 `explicitPlusAuto` 是最实用的模式

- 有显式条目就用显式条目（用户手动调过）
- 没有就自动匹配（ROM 预置最近邻）
- **用户只需要为"自动匹配得不好"的那几个音色写条目**

这比 vgm-conv 的"全自动"和 vgmtransmute 的"全写死"都好。

---

## 6. 预设鼓声（★ 参考 vgm-conv）

用户指出：**预设鼓声参考 vgm-conv**。已核实，源在 `vgm-conv/src/converter/opn-to-ym2413-converter.ts`。

### 6.1 vgm-conv 的鼓初始化序列

`opn-to-ym2413-converter.ts:383-404` 的 `getInitialCommands()`：

```ts
getInitialCommands(): Array<VGMCommand> {
  // select FM 6-ch mode
  this._y(14, 32);                                  // 0x0E = 0x20 → bit5 = 节奏模式使能

  // set rhythm freq
  const goalClock = this.to.relativeClock ? this.from.clock * this.to.clock : this.to.clock || 3579545;
  const ratio = 3579545 / goalClock;
  this._y(0x16, 0x20 * ratio);   // BD  鼓音高/频率
  this._y(0x17, 0x50 * ratio);   // HH + SD
  this._y(0x18, 0xc0 * ratio);   // TOM + CYM
  this._y(0x26, 0x05);           // BD 音量（低 4 位）
  this._y(0x27, 0x05);           // HH(高) + SD(低)
  this._y(0x28, 0x01);           // TOM(高) + CYM(低)
  this._y(0x36, 0x00);           // BD 输出电平
  this._y(0x37, 0x20);           // HH=2, SD=0
  this._y(0x38, 0x02);           // TOM=0, CYM=2
  return this._buf.commit();
}
```

**要点**：

1. `0x0E = 0x20` 打开节奏模式（bit5）
2. `0x16/0x17/0x18` 是**鼓的"音高"**（实际是频率参数），要按 `3579545 / 目标时钟` 比例缩放
3. `0x26/0x27/0x28` 是**鼓的音量**（每个寄存器装两个鼓：高 4 位 + 低 4 位）
4. `0x36/0x37/0x38` 是**鼓的输出电平**（`0x36` 只装 BD，`0x37` 装 HH+SD，`0x38` 装 TOM+CYM）

### 6.2 鼓的位分布（两个寄存器各装两个鼓）

| 寄存器 | 高 4 位 | 低 4 位 |
|---|---|---|
| `0x26` | — | BD（只有 BD） |
| `0x27` | HH | SD |
| `0x28` | TOM | CYM |
| `0x36` | — | BD 电平 |
| `0x37` | HH 电平 | SD 电平 |
| `0x38` | TOM 电平 | CYM 电平 |

`updateRhythmVolume(r, vv)`（`:285-309`）：

```ts
switch (r) {
  case 1: // HH
    this._rVolumes.hh = v;
    this._y(0x37, (this._rVolumes.hh << 4) | this._rVolumes.sd);
    break;
  case 2: // CYM
    this._rVolumes.cym = v;
    this._y(0x38, (this._rVolumes.tom << 4) | this._rVolumes.cym);
    break;
  case 4: // TOM
    this._rVolumes.tom = v;
    this._y(0x38, (this._rVolumes.tom << 4) | this._rVolumes.cym);
    break;
  case 8: // SD
    this._rVolumes.sd = v;
    this._y(0x37, (this._rVolumes.hh << 4) | this._rVolumes.sd);
    break;
  case 16: // BD
    this._rVolumes.bd = v;
    this._y(0x36, this._rVolumes.bd);
    break;
}
```

**注意 `r` 的编码**：`1=HH, 2=CYM, 4=TOM, 8=SD, 16=BD` —— 这是 OPLL 的 `0x0E` 寄存器位上每个鼓对应的 bit 位置。

### 6.3 节奏触发（`0x0E` 的 bit 位）

`ym2608-to-ym2413-converter.ts:5-13`：

```ts
function _convertRhythmFlags(flags: number) {
  const bd = flags & 1;  const sd = (flags >> 1) & 1;  const top = (flags >> 2) & 1;
  const hh = (flags >> 3) & 1;  const tom = (flags >> 4) & 1;  const rim = (flags >> 5) & 1;
  return (bd << 4) | (sd << 3) | (tom << 2) | (top << 1) | (rim | hh);
}
```

YM2413 的 `0x0E` 位布局（触发位）：

| bit | 鼓 |
|---|---|
| 4 | BD |
| 3 | SD |
| 2 | TOM |
| 1 | CYM（源码里叫 top，实为 top cymbal） |
| 0 | HH（`rim \| hh` 合并） |

另外 `0x0E` 的 **bit5 = 节奏模式使能**（写 `0x20`）。

### 6.4 furnace 的"兼容鼓"序列（当目标芯片不支持真鼓时）

`furnace/src/engine/platform/opll.cpp:468-486`：

```cpp
if (chan[ch].state.opllPreset==16) { // compatible drums mode
  if (ch>=6) {
    if (!properDrumsSys) {
      crapDrums=true;
      immWrite(0x16,0x20);   immWrite(0x26,0x05);
      immWrite(0x16,0x20);   immWrite(0x26,0x05);   // ← 写两次
      immWrite(0x17,0x50);   immWrite(0x27,0x05);
      immWrite(0x17,0x50);   immWrite(0x27,0x05);   // ← 写两次
      immWrite(0x18,0xC0);   immWrite(0x28,0x01);
    }
  }
}
```

**与 vgm-conv 的值完全一致**（`0x20 / 0x50 / 0xC0`，`0x05 / 0x05 / 0x01`），但 furnace 把 `0x16/0x26` 和 `0x17/0x27` **各写了两次**。

**这两次写不是冗余**——某些 OPLL 变体（VRC7 等）需要重复写才生效。`vgmtransmute` 的 `optimize()` 会做寄存器去重，如果不把这类寄存器排除，就会把第二次写吃掉，导致鼓不出声。

**→ 我们的 `json2vgm` 必须**：
1. 初始化鼓用 §6.1 的序列
2. 去重时**排除** `0x16/0x17/0x18/0x26/0x27/0x28` 和 `0x0E`
3. 写成 `vgm-conv` 那样只写一次（标准 YM2413），或 furnace 那样写两次（兼容模式），由 `mapping.rhythm.template` 决定

### 6.5 YM2151 没有硬件鼓

源侧是 YM2151，它没有 OPLL 那样的节奏模式。所以：

- `rhythm.source` 恒为 `null`
- 目标侧的鼓**只能通过通道 6/7/8 + preset 16（compatible drums）实现**，或者干脆不用鼓
- 第一版：**`rhythm.enabled = false`**，只做纯 FM 通道映射（8 → 9，实际用 8 个）

---

## 7. 音色对齐 `.fui`（与 furnace 互通）

### 7.1 为什么要对齐 `.fui`

用户原话：

> 音色直接对齐 fui，可以借用 furnace tracker 来改音色，这肯定就方便多了，因为毕竟输出引擎还要复用 fur 的。

所以：

```
我们的 JSON  ←→  .fui  ←→  furnace 音色编辑器（insEdit）
```

- **导入时**：从 YM2151 寄存器提取的参数 → 写成 `.fui` → 用户可以用 furnace 打开改
- **改完**：furnace 保存 `.fui` → 我们读回来 → 更新 JSON 的音色部分
- **输出时**：`.fui` 的参数 → 目标芯片的寄存器写

### 7.2 `.fui` 的内存表示就是 `DivInstrument`

不自己定义音色结构，直接用 furnace 的 `DivInstrument` / `DivInstrumentFM`（`instrument.h:173, 1149`）：

```cpp
struct DivInstrumentFM {
    unsigned char alg, fb, fms, ams, ops, opllPreset, block;
    bool fmsLFO, amsLFO, tremLFO;
    struct Operator {
        bool enable;
        unsigned char am, ar, dr, mult, rr, sl, tl, dt2, rs, dt, d2r, ssgEnv;
        unsigned char dam, dvb, egt, ksl, sus, vib, ws, ksr;
        unsigned char kvs;
    } op[4];
};
```

**YM2151 用 `op[0..3]` 全部；YM2413 用 `op[0..1]` + `opllPreset`。**

### 7.3 音色提取规则（导入时）

从 YM2151 寄存器流提取音色的时机：**每个音符块开始时**，读当时的 shadow 寄存器：

| `DivInstrumentFM` 字段 | YM2151 来源 |
|---|---|
| `alg` | `0x20+ch` bit2-0（CON） |
| `fb` | `0x20+ch` bit5-3 |
| `fms` | `0x38+ch` bit6-4（PMS） |
| `ams` | `0x38+ch` bit1-0 |
| `ops` | 恒 4 |
| `op[i].dt` | `0x40+i*8+ch` bit6-4（DT1） |
| `op[i].mult` | `0x40+i*8+ch` bit3-0（MUL） |
| `op[i].tl` | `0x60+i*8+ch` bit6-0 |
| `op[i].ks` → `ksr` | `0x80+i*8+ch` bit7-6 |
| `op[i].ar` | `0x80+i*8+ch` bit4-0 |
| `op[i].am` | `0xA0+i*8+ch` bit7 |
| `op[i].dr` → `d1r` | `0xA0+i*8+ch` bit4-0 |
| `op[i].dt2` | `0xC0+i*8+ch` bit7-6 |
| `op[i].d2r` | `0xC0+i*8+ch` bit4-0 |
| `op[i].sl` | `0xE0+i*8+ch` bit7-4 |
| `op[i].rr` | `0xE0+i*8+ch` bit3-0 |

**注意**：OPM 的 `0x80` 里 bit7-6 是 KS（Key Scale），不是 KSR。映射到 Furnace 的 `ksr` 时要注意语义差异（Furnace 的 `ksr` 在 OPN 上是 `0x30` 的 bit6，在 OPM 相等价于 KS）。

### 7.4 音色指纹（用于去重与映射）

```cpp
// 归一化：按 ALG 把载波 TL 调到最响为 0
void normalizeTL(DivInstrumentFM& fm) {
    int minTL = 127;
    for (int i = 0; i < 4; i++)
        if ((kAlgCarriers[fm.alg] >> i) & 1)
            minTL = std::min(minTL, (int)fm.op[i].tl);
    for (int i = 0; i < 4; i++)
        fm.op[i].tl = std::min(127, std::max(0, (int)fm.op[i].tl - minTL));
}

// 指纹：16 字节
std::array<uint8_t, 16> fingerprint(const DivInstrumentFM& fm);
```

**先归一化再算指纹**（vgm-conv 的 `_normalizeTotalLevel` 技巧）：否则同一音色不同音量会算成不同指纹。

---

## 8. 构建与目录

**独立工程 `cli/`，不碰 `0x808/`**（`RULES.md` F6–F8）。`0x808` 只作为将来的 UI 壳。

```
cli/
├── CMakeLists.txt
├── src/                            ← vgmcore 静态库（CLI 与将来的 UI 共用）
│   ├── RegStream.h                 寄存器流
│   ├── Document.{h,cpp}            JSON 读写 + 完整性校验
│   ├── NoteSplitter.{h,cpp}        KC 跳变切块（>0.5 半音）
│   ├── AnalyzerYm2151.h            载波 / ALG 判定
│   ├── VgmImportPlayer.{h,cpp}     libvgm 影子设备（只取寄存器写）
│   ├── Importer.{h,cpp}            VGM → Document
│   ├── VgmRebuild.{h,cpp}          Document → 内存 VGM
│   ├── PreviewVgm.{h,cpp}          写试听 VGM（循环点 + GD3 透传）
│   ├── FmVoice.{h,cpp}             OPM→OPN→OPL→OPLL 映射链 + 最近邻匹配
│   ├── FuiVoice.{h,cpp}            .fui 读写（包 Furnace 的 instrument.cpp）
│   ├── FurFile.{h,cpp}             .fur 解压 + 乐器表 + 名字参数标签
│   ├── InstrumentLibrary.{h,cpp}   跨曲目音色库（按源 FM 参数匹配）
│   ├── MacroEngine.{h,cpp}         22 条通道宏 + 4×20 算子宏展开
│   └── Opm2Opll.{h,cpp}            跨芯片转换主体
├── src/cli/                        ← 8 个可执行程序
│   ├── vgminfo.cpp      vgm2json.cpp    jsondump.cpp
│   ├── json2vgm.cpp     opm2opll.cpp    fuitool.cpp
│   └── vgmroundtrip.cpp json2mid.cpp
├── tools/                          ← 脚本（不在 vgmcore 里）
│   ├── batch_convert.sh            整张专辑批量转换
│   ├── probe_volume.py             两芯片音量台阶实测
│   └── probe_channels.py           逐通道响度对比
├── tests/                          ← macro_engine_test.cpp（76 条断言）
└── third_party/furnace/            ← 只复制 instrument.cpp 等 4 个文件（GPL-2.0+）
```

### 8.1 CMake（实际）

```cmake
# Furnace 的 .fui/.fur 编解码（不自写解析器）
add_library(furnace_fui STATIC
    third_party/furnace/src/engine/instrument.cpp
    third_party/furnace/src/engine/safeReader.cpp
    third_party/furnace/src/engine/safeWriter.cpp
    third_party/furnace/stubs.cpp)

# 共用库
add_library(vgmcore STATIC ...全部 src/*.cpp...)
target_link_libraries(vgmcore PUBLIC
    libvgm::vgm-player nlohmann_json::nlohmann_json ZLIB::ZLIB furnace_fui)

# 每个 CLI
foreach(tool vgminfo vgm2json jsondump json2vgm opm2opll fuitool vgmroundtrip json2mid)
    add_executable(${tool} src/cli/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE vgmcore)
endforeach()
```

**构建必须先加 MinGW 到 PATH**，否则 `c++.exe` 静默失败（缺 `libmpfr-6.dll`）：

```bash
export PATH="/d/msys64/mingw64/bin:$PATH"
cmake --build cli/build
```

### 8.2 依赖状态（实测）

| 依赖 | 状态 |
|---|---|
| libvgm 三个 `.a` | ✅ 已编出（`libvgm/build/bin/`） |
| nlohmann/json | ✅ MSYS2 自带 |
| zlib | ✅ MSYS2 自带（`.fur` 是 zlib 压缩的） |
| Furnace `instrument.cpp` | ✅ 只复制 4 个文件进 `cli/third_party/furnace/`，不拉整个工程 |
| Furnace engine 层（`platform/opll.cpp` 等） | ❌ 未接入。当前宏展开是**照抄** `opll.cpp`/`arcade.cpp` 的逻辑自己实现的（`MacroEngine.cpp` 逐行对应），不是链接它 |

---

## 9. 实现顺序（每步都能独立验收）

| # | 程序 | 验收 |
|---|---|---|
| 1 | `vgminfo` | 对 `31 Grass Maze.vgm` 输出正确的头部信息与命令统计 |
| 2 | `vgm2json` | 生成 JSON；`integrity: OK`；`jsondump` 能读 |
| 3 | `jsondump` | 人能看懂；`integrity` 行正确 |
| 4 | `json2vgm`（YM2151 → YM2151） | 输出能播放；`vgmroundtrip` 字节一致 |
| 5 | `vgmroundtrip` | PASS |
| 6 | `fuitool` | `.fui` ↔ JSON 互转；能被 furnace 打开 |
| 7 | `opm2opll`（YM2151 → YM2413） | 生成 target JSON；有损报告完整 |
| 8 | `json2vgm`（YM2413） | 输出能播放；音符位置与源一致 |

**第 4、5 步是分水岭**：在它们通过之前，不要碰任何转换逻辑。

---

## 10. 与 UI 的关系

CLI 全部跑通之后，UI 只是"调同一套库 + 画图"：

- `vgm_seq` 读 `Document`（与 `jsondump` 同一个结构）
- 播放用 `VgmRebuild` + libvgm（与 `json2vgm` 同一条路径）
- 编辑操作改 `Document` 后调 `save()`（与 CLI 同一个 writer）

**没有第二套实现。** 这是"先 CLI"的另一个好处：UI 阶段不会出现"CLI 能跑但 UI 不对"的分裂。
