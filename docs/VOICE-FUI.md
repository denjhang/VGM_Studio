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
- 真机行为：`0x30` 的 inst 高 4 位变化会立刻 `load_instrument()`（libvgm `ym2413.c` 的 `case 0x30`），**不需要**重触发就换音色

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
