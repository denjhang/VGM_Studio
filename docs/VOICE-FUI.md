# 音色文件（`.fui`）与宏

> 目标：**每个 JSON 都配一套 `.fui`**，让用户在 Furnace 里审计、修改音色，
> 改完再重新生成目标 JSON / VGM。这样不用自己写音色编辑器 GUI。

---

## 1. 为什么用 `.fui`，而不是自己做一套 JSON 音色

`.fui` 是 Furnace 的**乐器文件格式**，Furnace 自己就是音色编辑器。
用它的格式意味着：

- 用户**直接在 Furnace 里打开、试听、改、存**，不需要我们写任何 GUI
- 音色参数、宏、OPLL preset、鼓参数全在同一个文件里
- 格式演进由上游维护（我们只跟 `DIV_ENGINE_VERSION`）

代价：Furnace 是 **GPL-2.0+**，链它的 `instrument.cpp` 意味着本工程也得 GPL-2.0+。
（`docs/RULES.md` 的「许可前提」一节早有记录。）

---

## 2. 文件命名（一一对应）

```
song.json                        ← 原芯片（YM2151）
ch1ins1_ym2151.fui               ← 通道 1 的第 1 个音色（源侧，4-op）
ch1ins2_ym2151.fui
...
song.ym2413.json                 ← 目标芯片（YM2413）
ch1ins1_ym2413.fui               ← 同一位置的音色（目标侧，2-op 或 ROM preset）
ch1ins2_ym2413.fui
```

**配对规则**：同名同序号 = 同一个音色在两侧的样子。
`ch3ins2_ym2151.fui` 与 `ch3ins2_ym2413.fui` 是「通道 3 的第 2 个音色」的源/目标版本。

**序号怎么来的**：按**音色参数字节的哈希**去重，按首次出现的顺序编号。
判据只看定义音色的寄存器：

| 算进哈希 | 不算（会破坏去重） |
|---|---|
| `0x40` DT1/MUL | **`0x60` TL**（每音符音量，实测会让 2515 音符变 2515 个"音色"） |
| `0x80` KS/AR | `0x28`/`0x30` KC/KF（音高） |
| `0xA0` AM/D1R | `0x08` key-on |
| `0xC0` DT2/D2R | `0x20` 的 L/R 位（pan） |
| `0xE0` D1L/RR | |
| `0x20 & 0x3F`（FB+CON） | |
| `0x38`（PMS/AMS） | |

实测结果（`31 Grass Maze`）：8 个通道共 **19 个音色**，最少的通道 2 个，最多的 4 个。

---

## 3. 完整工作流

```bash
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"

# ① VGM → JSON，同时导出源侧音色
$B/vgm2json.exe "$V" -o out/song.json --fui

# ② 原芯片 JSON → 目标芯片 JSON，同时导出目标侧音色
$B/opm2opll.exe out/song.json -o out/song.ym2413.json --fui

# ③ 用户在 Furnace 里改 out/ch1ins1_ym2413.fui ……
#    或者用 fuitool 快速看一眼：
$B/fuitool.exe dump out/ch1ins1_ym2413.fui --tick-rate=60

# ④ 用改过的音色重新生成目标 JSON
$B/opm2opll.exe out/song.json -o out/song.v2.ym2413.json --use-edited-fui

# ⑤ 出 VGM
$B/opm2opll.exe out/song.json -o out/song.v2.ym2413.json \
              --use-edited-fui --vgm=out/song.v2.vgm
```

---

## 4. `.fui` 里到底有什么

`FINS` 容器 + 带长度前缀的特性块。我们用到的两块：

### 4.1 `FM` 块（音色参数）

Furnace 的 `writeFeatureFM`（`instrument.cpp:979-1025`）逐字节：

| 字节 | 内容 |
|---|---|
| 0 | `op3.enable<<7 \| op2<<6 \| op1<<5 \| op0<<4 \| opCount` |
| 1 | `(alg&7)<<4 \| (fb&7)` |
| 2 | `(tremLFO<<5) \| (ams&3)<<3 \| (fms&7)` |
| 3 | `(fmsLFO<<7) \| (amsLFO<<6) \| (ops==4)<<5 \| (opllPreset&31)` |
| 4 | `block&15` |
| 5.. | 每 operator 8 字节（见 `instrument.cpp:1015-1025`） |

### 4.2 `MA` 块（宏）

**22 条宏**，顺序固定（`instrument.cpp:1221-1242`）：

```
vol arp duty wave pitch ex1 ex2 ex3 alg fb fms ams
panL panR phaseReset ex4..ex10
```

每条宏的头部 8 字节（`writeMacro`，`instrument.cpp:1157-1188`）：

| 字段 | 说明 |
|---|---|
| `macroType&31` | 0=vol … 21=ex10 |
| `len` | 步数（0 = 这条宏不存在，整个块会被跳过） |
| `loop` | 循环点（255 = 不循环） |
| `rel` | key-off 时跳到的位置 |
| `mode` | 未使用（写 0） |
| `(open&0x3f) \| wordSize` | **`open` 的 bit1-2 = 模式**（0 序列 / 1 ADSR / 2 LFO）；`open` 的 bit3 = activeRelease；`wordSize` 在 bit6-7 |
| `delay` | 首步额外等待的 tick 数 |
| `speed` | 每步间隔的 tick 数 |
| 之后 | `len` 个值，位宽由 `wordSize` 决定（8/16/32 位有/无符号自动选） |

---

## 5. ★ 宏的速率不在 `.fui` 里

这是最容易搞错的一点，也是本模块的设计前提。

| 在哪 | 存了什么 |
|---|---|
| **`.fui` 里** | 每条宏的 `speed`（每步隔几个 tick）、`delay`、`len/loop/rel`、模式 |
| **`.fur` 工程里** | `divSong::hz`（Furnace 界面叫 `tickRate`，默认 60）—— **一秒有多少 tick** |

Furnace 的宏解释器 `DivMacroInt::next()` 由宿主引擎的 tick 推动
（`engine.cpp:2227` `divider=curSubSong->hz`；
`macroInt.cpp:308` `subTick=e->tickMult`）。
**乐器文件本身完全不含中断速率。**

所以：

```
一个宏步的绝对时长 = speed / tickRate  秒
一个宏步的采样数   = speed * sampleRate / tickRate
```

因此本工程的 `opm2opll` 必须显式接收 `--tick-rate`（默认 60），
`fuitool dump` 也是（只影响打印的毫秒数）。

---

## 6. 宏执行（`MacroRunner`）

`cli/src/FuiVoice.cpp` 里的 `MacroRunner` 逐行对应
`furnace/src/engine/macroInt.cpp:83-296` 的 `DivMacroStruct::doMacro`：

| 模式 | 行为 |
|---|---|
| **序列**（type 0） | 每步取 `val[pos++]`；到 `rel` 之前不循环；`pos>=len` 时回到 `loop`（若合法）或停在最后值（`linger`）或结束 |
| **ADSR**（type 1） | `val[0..8]` 是 low/high/AR/DR/HT/SL/ST/SR/RR；用 16 位定点 `pos` 累加 |
| **LFO**（type 2） | `val[11..15]` 是 speed/wave/phase/loop/global；三角/锯齿/脉冲三种 |

`Release()` 对应 key-off：ADSR 跳到 release，序列模式跳到 `rel`。

**实测**（`31 Grass Maze` ch0 第 2 个音符，音量宏 `speed=2`、5 步）：

```
segs=8
  0     [(16,1), (32,27), (48,50)]     ← key-on：fnum / key / inst+vol
  0     [(48,48)]                      ← 宏第 1 步 → 音量 48
  1470  [(48,51)]                      ← 1470 = 2 tick × 735 采样
  2940  [(48,55)]
  4410  [(48,58)]
  5880  [(48,61)]
  7350  [(48,48)]                      ← 循环回 loop=0 再走一遍
  7480  [(32,11)]                      ← key-off
```

`1470 采样 = 2 × (44100/60)`，与 `speed/tickRate × sampleRate` 完全吻合。

---

## 7. 寄存器 ⇄ `FuiVoice` 的映射依据

| 方向 | 依据（Furnace 原码） |
|---|---|
| OPM 影子寄存器 → `FuiVoice` | `platform/arcade.cpp:562-602` `commitState`（唯一权威的反向映射） |
| OPM `dt` ↔ 寄存器 DT1 | `platform/fmsharedbase.h:42` 的 `dtTable[8]={7,6,5,0,1,2,3,4}` |
| OPLL 用户音色 0x00-0x07 | `platform/opll.cpp:453-466` |
| OPLL preset 0x30 高 4 位 | `platform/opll.cpp:493` |

**注意**：OPLL 的 `opllPreset != 0` 时参数来自 ROM 表，`.fui` 里的 `op*` 字段只是冗余副本。
改 preset 号（`0x30` 高 4 位）才是改音色的有效手段。

---

## 8. 工具：`fuitool`

```bash
fuitool dump <voice.fui> [--tick-rate=60]     # 全部参数 + 宏（含每步毫秒）
fuitool check <voice.fui> [...]               # 批量校验能否被 Furnace 解码
fuitool rewrite <in> <out> [--name=] [--alg=] [--fb=]   # 读→改→写（往返验证）
```

`dump` 的输出示例（`ch1ins1_ym2413.fui`）：

```
type          : 13 (OPLL(YM2413))
name          : ch1ins1
alg/fb        : 0 / 3
fms/ams       : 0 / 0   (ops=2, opllPreset=14)

  op   mult tl  ar  dr  sl  rr  dt  dt2 ksr am  vib
  0    1   21  14  4   0   3   0   0   0   0   0
  1    1   0   9   0   0   2   0   0   0   0   0

  macros (tickRate=60 Hz, 1 tick = 16.6667 ms)
    vol        seq  len=5   speed=2  delay=0  loop=0   rel=255
      step = 33.33 ms   values: 127 100 70 40 20   total 166.7 ms
```

---

## 9. 当前限制

| 项 | 状态 |
|---|---|
| 序列宏（vol/arp/pitch…） | ✅ 已展开到寄存器写 |
| ADSR / LFO 宏 | ⚠️ `MacroRunner` 已实现三种模式，但 `opm2opll` 只展开序列宏（ADSR/LFO 需要音频级逐步推进，属实时回放侧） |
| 宏写到 OPM 侧 | ⚠️ 目前只展开「音量宏 → OPLL 0x30」；其它宏（arp/pitch 等）会改变音高，需与 `FreqToOpllFnum` 联动 |
| `.fui` 的采样/波表块 | 不涉及（FM 音色没有） |
| 鼓（OPLL preset 16） | 导出为普通音色，鼓位映射未做 |
