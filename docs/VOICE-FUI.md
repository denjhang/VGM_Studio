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

### 3.1 ★ 推荐做法：在 `.fur` 工程里改全部音色

逐个导出/覆盖 19 个 `.fui` 很容易改错文件号。更省事的路径是把 19 个目标音色
全部改进一个 Furnace 工程（`out/2151-2413.fur`），让 `opm2opll --fur` 直接读：

```bash
# 只需要一次：把 19 个 ym2413.fui 导入 Furnace 工程并保存为 2151-2413.fur
# 之后每次都在 Furnace 里改这个工程、存盘

$B/opm2opll.exe out/v1/song.json -o out/v6/song.ym2413.json \
    --fur=out/2151-2413.fur --transpose=-12 --vgm=out/v6/song.ym2413.vgm

# 看一眼工程里读到了什么
$B/fuitool.exe fur out/2151-2413.fur
```

配对规则（按优先级）：

1. **名字**：`chXinsY`，可带后缀（`ch2ins2-vib` 也算匹配 `ch2ins2`）。
2. **序号兜底**：名字对不上时，取该类型里第 N 个乐器。N 就是导出时的追加顺序
   （ch1ins1, ch1ins2, ch2ins1…），所以**你在 Furnace 里把 19 个 OPLL 乐器
   改成任何名字都不影响**，只要顺序不变。
3. 两者都失败 → 该音色退回自动匹配，并在输出里明确列出
   `!! N 个目标音色在工程里没找到`。

匹配限定 `type=13`（OPLL）。工程里同时存着源侧 OPM 音色（`ch1ins1-pad`，
type 33），不限类型会拿错；反过来，**给源侧 OPM 乐器改名完全无害**（实测
把 19 个 OPM 乐器全改成别的名字，输出逐字节不变）。

工程里的 `hz`（默认 60）直接当宏的 tick 速率用，不需要再传 `--tick-rate`。

实测：`--fur` 与 `--use-edited-fui` 两条路径对同一套音色生成出的 JSON
与 VGM **逐字节相同**。

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

## 8.5 宏执行（`MacroEngine`）

`cli/src/MacroEngine.{h,cpp}` 实现**全部**宏的展开，逐行对应 Furnace 的两个平台 `tick()`：

| 宏组 | 数量 | 位置 |
|---|---|---|
| 通道级（MA 块） | 22 条 | `arcade.cpp:292-480` / `opll.cpp:150-340` |
| 算子级（O1..O4 块） | 4 × 20 条 | 同上（`chan[i].std.op[j]`） |

### 8.5.1 ★ OPLL 到底消费几条宏（清单）

`.fui` 格式为所有芯片定义了 22 条通道级宏 + 4×20 条算子级宏，但**每条平台的 `tick()`
只读其中一部分**。OPLL（`furnace/src/engine/platform/opll.cpp:150-340`）实际消费：

**通道级 9 条**

| # | 宏 | 代码位置 | OPLL 上的作用 |
|---|---|---|---|
| 1 | `vol` | `opll.cpp:157-172` | 写 `0x30` 低 4 位（通道音量） |
| 2 | `arp` | `opll.cpp:174-181` | 改 baseFreq，重算 fnum |
| 3 | `wave` | `opll.cpp:182-188` | **patch 切换**：写 `0x30` 高 4 位 |
| 4 | `pitch` | `opll.cpp:189-197` | 加到 pitch2，重算 fnum |
| 5 | `phaseReset` | `opll.cpp:198-203` | 重触发（key-off → key-on） |
| 6 | `alg` | `opll.cpp:205-210` | 只存 state.alg；用户音色时置 freqChanged。下次写 `0x20` 时变成 **bit5（SUS/EG-S）**，见 `:258` |
| 7 | `fb` | `opll.cpp:211-216` | 写 `0x03` bit0-2（仅 preset==0） |
| 8 | `fms` | `opll.cpp:217-222` | 写 `0x03` bit4（仅 preset==0） |
| 9 | `ams` | `opll.cpp:223-228` | 写 `0x03` bit3（仅 preset==0） |

**算子级 11 条**（`opll.cpp:230-311`，只对前 2 个算子）

| # | 宏 | 落点（preset==0） |
|---|---|---|
| 10 | `am` | `0x00+j` bit7 |
| 11 | `ar` | `0x04+j` 高 4 位 |
| 12 | `dr` | `0x04+j` 低 4 位 |
| 13 | `mult` | `0x00+j` 低 4 位 |
| 14 | `rr` | `0x06+j` 低 4 位 |
| 15 | `sl` | `0x06+j` 高 4 位 |
| 16 | `tl` | 调制器 → `0x02` 低 6 位；载波 → `0x30` 低 4 位（**任何 preset 都写**，它同时是通道音量） |
| 17 | `egt` | `0x00+j` bit5（Furnace 内部 `ssgEnv=(val&1)?8:0`，写时 `(ssgEnv&8)<<2`） |
| 18 | `ksl` | 调制器 KSL 变 → 重写 `0x03`；载波 KSL 变 → 重写 `0x02`（两个寄存器互相持有对方的 KSL） |
| 19 | `vib` | `0x00+j` bit6 |
| 20 | `ksr` | `0x00+j` bit4 |

**合计 9 + 11 = 20 条**。

**OPLL 不消费的 22 条**（不是漏实现，`grep "std\.<名字>" opll.cpp` 为零命中）：

| 组 | 宏 | 原因 |
|---|---|---|
| 通道级 13 条 | `duty`、`panL`、`panR`、`ex1`、`ex2`、`ex3`、`ex4`、`ex5..ex10` | OPLL 没有占空比、没有左右声道、没有 LFO 深度/波形寄存器、没有 slot 掩码。`panL`/`panR` 只有 OPM 用（`arcade.cpp:334-342`），且 **`panR` 连 OPM 都不消费** |
| 算子级 9 条 | `dt`、`dt2`、`d2r`、`rs`、`ssg`、`dam`、`dvb`、`sus`、`ws` | OPLL 的 2-op 算子没有 detune/DT2/D2R/RS；`ssg`/`dam`/`dvb`/`ws` 是 OPN/OPL/OPZ 的字段；`sus` 在 OPLL 上是通道级 `0x20` bit5，由 `alg` 宏承担 |

> 正因为 15 个 ROM 预置不可改，**`wave`（换 patch）和 `phaseReset`（重触发）才是 OPLL
> 上最有价值的宏**：前者在 16 个音色间切换，后者在同一个音色上制造第二次 attack。
> 其余算子级宏只在 `preset == 0`（0 号用户槽）时才落地。

### 8.5.2 通道级宏 → 寄存器

| 宏 | OPM 落到 | OPLL 落到 |
|---|---|---|
| `vol` | 4 个算子的 `0x60+j*8`（按 KVS 决定是否叠音量） | `0x30` 低 4 位 |
| `arp` / `pitch` | 音高偏移（arp 半音 ×128，pitch 1/128 半音），调用方重算 fnum | 同 |
| `duty` | `0x0F` | — |
| `wave` | `0x1B`（LFO 波形） | `0x30` 高 4 位（换音色号） |
| `alg` | `0x20` bit0-2 | 不写寄存器；驱动 `0x20` bit5（SUS），只在用户音色生效 |
| `fb` | `0x20` bit3-5 | `0x03` bit0-2 |
| `fms` / `ams` | `0x38` | `0x03` bit4 / bit3 |
| `panL` | `0x20` bit6/7 | — |
| `panR` | 两个平台都不消费 | — |
| `ex1` / `ex2` / `ex3` | `0x19`（PMD）/ `0x19\|0x80`（AMD）/ `0x18`（LFO 频率） | — |
| `phaseReset` | 重触发（重写 key 位） | 同 |
| `ex4` | 改算子掩码 + 重触发 | — |
| `ex5..ex10` | 两个平台都不消费 | — |

### 8.5.3 算子级宏 → 寄存器

| 宏 | OPM | OPLL（preset=0 时） |
|---|---|---|
| `am` | `0xA0` bit7 | `0x00+j` bit7 |
| `ar` / `dr` | `0x80` / `0xA0` | `0x04+j` |
| `mult` | `0x40` 低 4 位 | `0x00+j` 低 4 位 |
| `rr` / `sl` | `0xE0` | `0x06+j` |
| `tl` | `0x60`（按 KVS 叠音量） | 载波 → `0x30`；调制器 → `0x02` |
| `dt` | `0x40` 高 4 位（经 `dtTable`） | — |
| `dt2` / `d2r` | `0xC0` | — |
| `rs` | `0x80` bit6-7 | — |
| `ksr` | — （`arcade.cpp` 无 `m.ksr.had`） | `0x00+j` bit4 |
| `ksl` | — | 调制器变 → `0x03`；载波变 → `0x02` |
| `vib` / `egt` | — | `0x00+j` bit6 / bit5 |
| `sus` | — | 通道级 `0x20` bit5，由 `alg` 宏驱动 |
| `ssg` / `dam` / `dvb` / `ws` | OPM/OPLL 不支持 | — |

### 8.5.4 音量缩放公式

Furnace 的 `compatFlags.newVolumeScaling` 默认 `true`（`song.cpp:1165`），所以：

```
VOL_SCALE_LOG_BROKEN(x, y, range) = CLAMP(x + y - range, 0, range)
```

- OPM：`range=127`，`x=127-op.tl`，`y=通道音量`
- OPLL：`range=15`，`x=15-carrierTl`，`y=通道音量`

#### ★ 两个音量域的极性相反（踩过的坑）

| 域 | 单位 | 0 | 15 / 127 |
|---|---|---|---|
| `chan.vol` / `outVol`（Furnace 内部音量） | 音量 | 静音 | **最响** |
| `0x30` 低 4 位（OPLL 寄存器音量） | **衰减量** | **最响** | 静音 |
| `0x60` 低 7 位（OPM 的 TL） | **衰减量** | **最响** | 静音 |

`opll.cpp:168` 的换算是 `0x30低4位 = 15 - VOL_SCALE_LOG(outVol, 15-carrierTl, 15)`。
也就是说寄存器里写的是「衰减」，越大越轻。

**调用 `ExpandMacros` 时的 `baseVol` 必须传「音量」域的值**（0=静音，15=最响），
而转换器算出的 `vol` 是「衰减」域（0=最响）。二者必须转换：

```cpp
mi.baseVol = 15 - vol;   // ✅  直接把 vol 当 baseVol 会反向衰减
```

**踩坑后果**（v7 实测）：源音量衰减 `vol=2`（很响）被当成 `baseVol=2`（很轻），
`VOL_SCALE_LOG(2, 15-0, 15)` 只剩 2，于是 `0x30` 写成 `15-2=13`——本应最响的音符
变成几乎听不见。所有带音量宏的通道平均寄存器音量从 2 掉到 12（0=最响），
听感就是「很多通道声音非常小」。修正后 v8 平均回到 3.5–6.6，与源 RMS 对齐。

#### ★ 宏只在真实 key-on 启动一次（v8 的「颤音/弯音回归」）

`NoteSplitter` 在源 KC 跳变 >0.5 半音时会切出新块，但那些块**在源里没有 key 事件**——
它们是同一次发声的 legato 延续（见 §5.5b）。最初的实现给每个块都从 `t=0` 调用一次
`ExpandMacros`，等于把宏重新 `Init` 一遍：

| 后果 | 表现 |
|---|---|
| `vol` 宏重新吐第一帧 | 很多音色的第一帧是 0 → **每个块边界静音一拍（16.7 ms）** |
| `arp` 宏从 0 重新扫 | 每个块边界多一次向下的音高扫掠 → **听起来像颤音/弯音** |
| 带 `rel` 的宏被当成新音符 | 半路掉到释放值，音量突变 |

实测 `31 Grass Maze`：ch1 有 125 个 legato 块、ch7/ch8 各 123/124 个，
v8 在整曲的 legato 块起点总共写了 371 次「音量 15 = 静音」的一拍。

**正确做法**：块不是发声单元，**真实 key 上升沿才是**。转换器为每块传两个参数：

```cpp
// 从真实 key-on 到这个块起点已经过去了多少 tick
mi.skipTicks   = round((n.startSample - keyOnSample) * tickRate / sampleRate);
// 这个块末尾有没有真实 key-off（legato 块没有 → 不许进 release）
mi.releaseAtEnd = endsWithKeyOff;
```

引擎把 `skipTicks` 拍**空跑**一遍（只推进宏状态，`warming=true` 时 `emit()` 直接丢弃，
`pitches/susPoints/retriggers` 也不记录），然后从第 0 拍开始正常输出。宏的相位因此
连续，`vol` 不会回到 0，`arp` 不会重扫，`rel` 只在真 key-off 时触发。

v9 实测：legato 块边界的静音帧从 **371 个降到 1 个**；所有旋律通道的音高轨迹与 v5
逐写入一致（FM1 808、FM2 240、FM3 327、FM4/6 333、FM7 801、FM8 798），
只有鼓通道 FM5 因为 arp 宏本就连续扫描而变化（388 → 1648 次 fnum 写）。

#### ★ 两端的音量台阶是 4:1，不是 8:1（v11）

`ExpandMacros` 的 `baseVol` 不是满量程常数，它是**转换器算出的该音符音量**。
所以 TL → 0..15 的换算精度直接决定整曲响度。实测（`cli/tools/probe_volume.py`）：

| 平台 | 台阶 | dB/单位 |
|---|---|---|
| YM2151 载波 TL | 1 / 127 | **0.7526** |
| YM2413 `0x30` 低 4 位 | 1 / 15 | **3.01** |

比值正好 4（`4 × 0.7526 = 3.0104`），与寄存器位宽自洽：
`ym2151.c:1143` 是 `tl << 3`、`ym2413.c:1695` 是 `v << 2 << 1`，
两者把值左移进同一个 10 bit 包络域。所以 **nibble = round(TL / 4)**。

旧代码用 `tl >> 3`（vgm-conv 对 OPN 的近似），把 9.8 dB 的源动态范围压成 2 步。
配套的 `volumeOffset` 基准也从 1 改成 0（那个 1 是在补 `>> 3` 的偏差）。
整曲 RMS 变化：v9（`>>3`, voff 1）−16.4 → v10（`÷4`, voff 1）−22.0 →
**v11（`÷4`, voff 0）−19.0 dBFS**，与源 `01_original_OPM_YMF2151.wav` 的 −19.0 一致。

### 8.5.5 OPLL 的两个常用宏：patch 切换与重触发

这两个在 OPLL 上最常用，语义和 OPM 不同，单独说明。

#### patch 切换（`wave` 宏）

```cpp
// furnace/src/engine/platform/opll.cpp:182-186
if (chan[i].std.wave.had && chan[i].state.opllPreset!=16) {
  chan[i].state.opllPreset=chan[i].std.wave.val;
  if (i<9) {
    rWrite(0x30+i, ((15-VOL_SCALE_LOG_BROKEN(chan[i].outVol,15-chan[i].state.op[1].tl,15))&15)
                   | (chan[i].state.opllPreset<<4));
  }
}
```

- 值 = 音色号（0..15），写 `0x30` 的**高 4 位**
- `preset == 16`（兼容鼓模式）时**不切**
- ★ 切换后必须更新状态机里的当前 preset：后续所有算子参数宏只在 `preset == 0`（用户音色）时才写 `0x00-0x07`，切到 ROM 音色后这些写必须停掉
- 真机行为：`0x30` 的 inst 高 4 位变化会立刻 `load_instrument()`（libvgm `ym2413.c:1679-1725` 的 `case 0x30`），**不需要**重触发就换音色

#### ★ 为什么 patch 宏是突破「15 个预设」的唯一手段

先把 OPLL 的通道/音色关系说准确，别和 OPL2 混淆：

- **OPLL 9 个通道，每个通道都可以用不同的音色。** 这是通道级的 `0x30` 高 4 位，
  每个通道独立，互不影响。9 个通道同时跑 9 个不同音色是**完全正常**的用法。
- **OPL2 的区别在于「自定义音色」的数量**：OPL2 有 9 个可自由写入的音色槽
  （`0x20`–`0x35` 共 9 组、每组 11 字节），**每个通道都能塞自己的一套参数**；
  OPLL 把这些全砍掉，只留 **1 个用户音色槽**（0 号，对应 `0x00`–`0x07`）+
  **15 个不可改的 ROM 预置**。

所以真正的限制不是「9 个通道共用一个音色」——而是**自定义音色只有 1 种**。
15 个 ROM 预置改不了任何一个字节，0 号槽又只有一份。

想得到 15 个预设之外的自定义音色，只有两条路：

1. **改 0 号槽**（`0x00`-`0x07`）——但全芯片只有一份，同一时刻只能是一种自定义音色。
2. **`Patch` 宏**（通道级 `0x30` 高 4 位）——按通道、按时间在 16 个槽位之间轮换。
   这是**唯一**能在一个通道内跨越 15 个不可改预置的手段，也是把「1 种自定义」
   扩展成「16 种可实时切换」的唯一途径。

两种手段叠在一起，才突破了 15 个 ROM 预置的边界：

| 手段 | 写入 | 能得到的音色 | 限制 |
|---|---|---|---|
| 只用 0 号槽 | `0x00`–`0x07` 一次性 | 1 种自定义 | 全芯片共享一份 |
| 9 个通道各选预置 | 各通道 `0x30` 高 4 位（静态） | 9 路不同（取自 15 个 ROM） | 全部是 ROM，不可改 |
| `Patch` 宏轮换 | `0x30` 高 4 位随时间变 | 同一通道上 16 种（0 + 15 ROM） | ROM 内部参数仍不可改 |
| `Patch` 宏 + 0 号槽边响边改 | `0x30` 高 4 位 + `0x00`–`0x07` | **15 + 任意多自定义** | 0 号槽同一时刻只有一份，但可以边响边换成下一份 |
| `Patch` 宏 + 吃 `op1_out` 残留 | 快速连续切 `0x30` | 上述组合之外再凭空多出混合音色 | 见 8.5.5.1，不可精确复现 |

这也是为什么 Furnace 在 OPLL 的乐器编辑器里把 `waveMacro` 的显示名改成 `Patch`
（`furnace/src/gui/insEdit/opll.cpp:76`），而 OPM 同一个槽位叫 `LFO Shape`
（`insEdit/opm.cpp:39`）—— 名字是按芯片语义定的。

★ **0 号槽是全局的，不是每通道一份**。`0x00`–`0x07` 写下去后，
`update_instrument_zero()`（`ym2413.c:1383-1398`）会遍历**所有**
`instvol_r[chan] & 0xf0 == 0` 的通道，一起改：

```c
for (chan=0; chan<chan_max; chan++)
    if ((chip->instvol_r[chan]&0xf0)==0)
        set_mul(chip, chan*2, inst[0]);
```

所以多个通道同时停在 0 号槽时，改一次参数是**一起变**的。想让它们各有各的自定义音色，
必须靠 `Patch` 宏把其中一部分通道挪到 ROM 预置或吃残留，让它们不再共用 0 号槽。

### 8.5.5.1 真机残留状态：切 patch 不重置运行态

这是 OPLL 的**芯片固有特征**（不是仿真 bug，libvgm / Nuked-OPLL / emu2413 都保留了它）。
切 patch 时 `load_instrument()` 只重写 8 个参数字节：

```c
// libvgm/emu/cores/ym2413.c:1371-1381
static void load_instrument(YM2413 *chip, UINT32 chan, UINT32 slot, UINT8* inst)
{
	set_mul         (chip, slot,   inst[0]);
	set_mul         (chip, slot+1, inst[1]);
	set_ksl_tl      (chip, chan,   inst[2]);
	set_ksl_wave_fb (chip, chan,   inst[3]);
	set_ar_dr       (chip, slot,   inst[4]);
	set_ar_dr       (chip, slot+1, inst[5]);
	set_sl_rr       (chip, slot,   inst[6]);
	set_sl_rr       (chip, slot+1, inst[7]);
}
```

`OPLL_SLOT`（`ym2413.c:145-180`）里这些运行态字段**全部原样保留**：

| 残留字段 | 内容 | 后果 |
|---|---|---|
| `op1_out[0]`、`op1_out[1]` | 反馈延迟线的两级历史输出 | **旧音色的输出被新音色的 FB 增益重新注入调制器** |
| `phase` | 相位累加器 | 新音色从旧音色的相位位置接着跑 |
| `volume` | 当前包络电平 | 旧音色的衰减进度直接套到新音色上 |
| `state` | EG 阶段（ATT / DEC / SUS / REL / OFF） | 切换时不重新起音 |

关键在反馈路径（`ym2413.c:894-905`）：

```c
out = SLOT->op1_out[0] + SLOT->op1_out[1];
SLOT->op1_out[0] = SLOT->op1_out[1];
phase_modulation = SLOT->op1_out[0];
SLOT->op1_out[1] = 0;
if (env < ENV_QUIET)
    SLOT->op1_out[1] = op_calc1(SLOT->phase, env, (out<<SLOT->fb_shift), SLOT->wavetable);
```

被乘的数据 `out` 是**旧** patch 吐出来的采样，乘数 `fb_shift` 来自**新** patch 的
`0x03` 低 3 位。这个组合在任何一条 ROM 记录里都不存在 —— 这就是"凭空造音色"的来源。
切得越快，延迟线越来不及衰减，混合音色就越稳定。

另一条不对称：`load_instrument` 会通过 `set_ksl_tl` 把**调制器 TL 恢复成 ROM 值**
（覆盖你之前通过 `0x02` 做的调整），但**载波 TL 不会** —— 它是 `0x30` 低 4 位在
`ym2413.c:1693` 单独设的，属于"音量"不属于"音色"。所以切 patch 时自己调过的
通道音量能留住，调制器深度会被 ROM 冲掉。

#### 本项目的实现状态

**不需要为此写任何新代码。** `MacroEngine.cpp` 的两条路径已经自动获得这个效果：

1. `Patch` 宏（`kMacroWave` 的 OPLL 分支）按 `speed` 节奏写 `0x30` 高 4 位，
   连续切换就会吃到 `op1_out` 残留（`cli/src/MacroEngine.cpp` 的 `case kMacroWave`）。
2. `preset == 0` 时，算子级宏（`tl` / `ar` / `dr` / `sl` / `rr` / `mult` / `ksl` /
   `vib` / `egt` / `ksr` / `am`）直接改 `0x00`–`0x07`，真机走
   `update_instrument_zero()`（`ym2413.c:1383-1470`）**对正在发声的 SLOT 立即生效**，
   同样不重置 EG。这是"边响边改音色"的正规通道。

渲染交给 libvgm 时，`op1_out` / `phase` / `volume` 的残留会自然出现，无需伪造。

**推荐的组合写法**（`.fui` 里）：

```
opllPreset = 0            ← 启用 0 号槽
Patch       = {1,2,4,8,12,3,5,...}  speed=1..2   ← 快速轮换，吃 FB 残留
Phase Reset = {1}         speed=6                ← 可选：让每个 patch 重新起音
算子 tl/ar/dr 宏          speed=1..4             ← 可选：0 号槽边响边改
```

去掉 `Phase Reset` 才能吃到残留；加上它则每个 patch 都是干净的独立起音，二者是互斥的两种效果。


#### 重触发（`phaseReset` 宏）

```cpp
// opll.cpp:196-200
if (chan[i].std.phaseReset.had) {
  if (chan[i].std.phaseReset.val==1 && chan[i].active) {
    chan[i].keyOn=true;
  }
}
```

★ **关键差异**：YM2413 在 key 位已置位时再写 key-on **不会**重触发。

```c
// libvgm/emu/cores/ym2413.c 的 KEY_ON
INLINE void KEY_ON(OPLL_SLOT *SLOT, UINT32 key_set) {
  if( !SLOT->key ) {
    /* do NOT restart Phase Generator (verified on real YM2413) */
    SLOT->phase = DMP;
  }
  SLOT->key |= key_set;
}
```

只有 `key` 从 0 变 1 才回到 `EG_DMP`。所以重触发必须是**两条写**：

```
0x20+ch = dhBase          (key off，清 bit4)
0x20+ch = dhBase | 0x10   (key on)
```

同一采样点、按顺序执行。这正是 Furnace 的做法（`arcade.cpp:484-533`：先 `immWrite(0x08,i)`，随后 `immWrite(0x08,(opMask<<3)|i)`）。

OPM 同理：libvgm `ym2151.c:687-703` 的 `KEY_ON` 也是 `if (!op->key)` 才清相位。

#### 实测（`31 Grass Maze` ch1 notes[1]）

注入 `wave = {14,3,7,0}`（speed=8）、`phaseReset = {0,1,0,1}`（speed=3）：

```
segs 6
      0  ['10=01','20=1B','30=E2']   ← key-on：fnum/key/inst=14
      0  ['30=E0']                    ← 音量宏
   2205  ['20=0B','20=1B']            ← phaseReset：key-off 再 key-on（2205 = 3×735）
   5880  ['30=30']                    ← patch 切到 3（5880 = 8×735）
   6615  ['20=0B','20=1B']            ← 第二次重触发
   7480  ['20=0B']                    ← 音符结束 key-off
```

### 8.5.6 验证

`cli/tests/macro_engine_test.cpp`（52 条断言，`cli/build/macro_test.exe`）：

- 前 23 条：通用宏（序列 / ADSR / LFO 三种模式、OPM 全部落点、patch 切换、重触发）
- 后 29 条：**逐条覆盖 OPLL 消费的 20 条宏**，并反向断言另外 22 条在 OPLL 上不产生任何写

```
$ ./build/macro_test.exe
vol seq timing               PASS  ← speed=2 @60Hz → 0/1470/2940 采样
vol changes carrier TL       PASS  ← 127→30→93，调制器 TL 不变
pitch emits offsets          PASS
...
OPLL vol 0x30 low nibble     PASS  ← 15 → 0x30 衰减=10（=载波 TL），7 → 15
OPLL arp 12 semitones        PASS  ← arp 单位是半音，输出 12×128
OPLL pitch 1/128 semitone    PASS  ← pitch 单位是 1/128 半音
OPLL pitch accumulate        PASS  ← mode=1 是累加（100,100,100 → 300）
OPLL wave = patch            PASS
OPLL alg SUS on              PASS  ← 用户音色下 immediate=true，立刻重写 0x20
OPLL alg rom preset deferred PASS  ← ROM 预置下 state.alg 照样存，但不立刻写
OPLL fb 0x03                 PASS
OPLL fms bit4 / ams bit3     PASS
OPLL phaseReset retrig       PASS
OPLL op am/ar/dr/mult/rr/sl  PASS
OPLL op tl mod 0x02          PASS
OPLL op tl car 0x30          PASS
OPLL car tl on rom preset    PASS  ← 载波 TL 就是通道音量，ROM 预置也写
OPLL op egt bit5             PASS
OPLL op ksl mod->0x03        PASS
OPLL op ksl car->0x02        PASS
OPLL op vib bit6             PASS
OPLL op ksr bit4             PASS
OPLL ignores 13 unused ch macros PASS
OPLL ignores 9 unused op macros  PASS
ALL PASS (0 fail)   ← 52 条
```

**端到端实测**（`--use-edited-fui`，把 `ch1ins1_ym2413.fui` 的 `opllPreset` 改成 0
并加 `vol={15,10,5}` + `alg={0,1}`）：

```
$ ./cli/build/opm2opll.exe song.json -o out.json --fui --use-edited-fui --tick-rate=60 --vgm=out.vgm
fui applied   : 19 edited voices
preview vgm   : out.vgm (49408 bytes)

globals  = 00=01 01=01 02=55 03=03 04=E4 05=90 06=03 07=02   ← 用户音色 0 号槽
ch1 note1: 2940 ['20=3B']  ← 0x20 = 0b0011_1011：bit5 = SUS 置位（alg=1）
…共 18 处带 SUS 的 0x20 写
```

---

## 9. 当前限制

| 项 | 状态 |
|---|---|
| 通道级 22 条宏 | ✅ 全部实现（序列 / ADSR / LFO 三种模式） |
| 算子级 4×20 条宏 | ✅ 全部实现 |
| OPLL 实际消费的 20 条 | ✅ 逐条实现并有断言，见 §8.5.1 |
| OPLL 不消费的 22 条 | ✅ 展开为空，有反向断言 |
| 音高类宏（arp / pitch） | ✅ 输出音高偏移点，单位已按平台语义区分（arp=半音、pitch=1/128 半音，pitch 支持累加模式） |
| 重触发类（phaseReset / ex4） | ✅ 输出重触发点（OPLL 只有 phaseReset，ex4 不消费） |
| OPLL 载波 TL 宏 | ✅ 任何 preset 都写 `0x30`（它同时是通道音量），只是 ROM 预置下改了不改变音色 |
| `.fui` 的采样/波表块 | 不涉及（FM 音色没有） |
| 鼓（OPLL preset 16） | 导出为普通音色，鼓位映射未做 |

**已知偏差**（都是 Furnace 语义本身决定的，不是漏实现）：

1. `ex5..ex10` 在 OPM/OPLL 两个平台都不被消费（`grep "std.ex[5-9]\|std.ex10"` 零命中），展开为空。
2. `panR` 在 OPM 上也不被消费（`arcade.cpp` 只有 `std.panL`），只有 `panL` 会同时设左右声道。
3. OPLL 的 `sus` 位由 `0x20` bit5 承担、由 `alg` 宏驱动，是 `MacroSus` 事件而不是普通寄存器写——因为它必须等下一次 0x20 写入时才带出去（`opll.cpp:258`）。
4. `ssg / dam / dvb / ws` 是 OPN/OPL/OPZ 的参数，OPM/OPLL 没有对应寄存器。
5. OPLL 的 `ksl` 是双寄存器耦合：`0x02` 存调制器 KSL+TL，`0x03` 存载波 KSL+FMS+AMS+FB，改任一个都要重写含对方的那个。
