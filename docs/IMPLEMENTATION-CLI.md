# CLI 实施记录（第一版，2026-11-11）

> 本文件记录 `cli/` 工程的**实际落地结果**与**踩过的坑**。设计依据见 `CLI.md`，
> 数据格式见 `DATA-MODEL.md`，寄存器语义见 `CHIP-YM2151.md`。
> 本文件只增不删（`RULES.md` F12）。

---

## 1. 工程位置（用户明确要求）

**`cli/` 是独立工程，不与 `0x808` 混为一谈。**

```
VGM_Studio/
├── cli/                        ← 本阶段全部代码
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── RegStream.h
│   │   ├── VgmImportPlayer.{h,cpp}
│   │   ├── AnalyzerYm2151.h
│   │   ├── NoteSplitter.{h,cpp}
│   │   ├── Document.{h,cpp}
│   │   ├── Importer.{h,cpp}
│   │   ├── VgmRebuild.{h,cpp}
│   │   └── cli/{vgminfo,vgm2json,jsondump,json2vgm,vgmroundtrip}.cpp
│   ├── scripts/batch_check.sh
│   └── build/                  ← 构建产物
└── libvgm/                     ← 只链接，不改源码
```

构建：

```bash
cmake -S cli -B cli/build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C cli/build
```

依赖实测可用：`libvgm/build/bin/libvgm-{player,emu,utils}.a`、MSYS2 的 `nlohmann_json 3.12.0`。

---

## 2. 解析：全部交给 libvgm，一行命令解析都不写

用户要求原话：「立刻实现完整全部芯片的全部指令全部兼容最新libvgm的解析器，禁止手写」。

**落实方式**：`VgmImportPlayer.cpp` 里 `ShadowPlayer : public VGMPlayer`，解析路径完全是 libvgm 自己的：

```
VGMPlayer::Render(smplCnt, buf)
  → ParseFile(ticks)                            player/vgmplayer.cpp:2089
      → _CMD_INFO[cmd].func(this)               player/vgmplayer_cmdhandler.cpp:20
          → Cmd_Reg8_Data8 / Cmd_DelaySamples2B / Cmd_DataBlock / Cmd_DACCtrl_* ...
```

覆盖范围因此等于 libvgm 的支持范围，**全部 0x00–0xFF 命令**，包含：

| 命令族 | 覆盖 |
|---|---|
| `0x50–0x5F` / `0xA0–0xAF` 芯片寄存器写 | ✅ 全部（含第二实例） |
| `0x61/0x62/0x63/0x70–0x7F` 等待 | ✅ |
| `0x66` 结束 / `0x67` 数据块 | ✅（含压缩数据块、ROM/RAM 写） |
| `0x68` PCM RAM write | ✅ |
| `0x80–0x8F` YM2612 PCM | ✅ |
| `0x90–0x95` DAC Stream 全套 | ✅ |
| `0x30/0x3F/0x4F/0x31` 特殊 | ✅ |

**我们唯一做的事：注入影子设备把 `write8` 接住。**

```cpp
// 文件里出现几颗芯片，就注册几个影子 DEV_DECL（deviceID 用 libvgm 的 DEVID_*）
SetUserDevices(g_userDevList, 0);
Start();                       // InitDevices() 会按 _devCfgs 逐个 SndEmu_Start2()
Render(chunk, buf);            // 循环跑到 PLAYSTATE_END
```

影子设备 `Start()` 必须回填 `retDevInf->devDef`（真实核用 `INIT_DEVINF` 宏做这件事），
`Update()` 返回静音，`sampleRate = 0` 让 `Resmpl_Init` 走空指针分支。

**时间戳**：`write8` 回调发生在 `ParseFile()` 内部，此刻 `_fileTick` 正是该命令生效时刻，
`Tick2Sample(_fileTick)` 就是精确采样位置 —— 与 libvgm 自己的时间模型同源。

---

## 3. 命令统计不自己扫字节

`vgminfo` 的命令统计用 libvgm 的长度表 `_CMD_INFO[cmd].cmdLen` 走一遍，
只有 `0x67` 需要读负载里的长度（布局见 `Cmd_DataBlock()`），`0x66` 长度为 0 需要显式终止。

---

## 4. 音符分块：判据照抄 libvgm 的 OPM 核

`AnalyzerYm2151.h` 里每一条规则都注明了出处，不是自己推导的：

| 规则 | 来源 |
|---|---|
| **音高**：`半音数 = kc - (kc >> 2)`，分数 `(kf>>2)&63` / 64 | `emu/cores/ym2151.c:1066-1068` 的 `kc_channel = (v - (v>>2))*64` 与 `:1106` 的 `\|= (kf>>2)&63` |
| **载波掩码** `{0x8,0x8,0x8,0x8,0xC,0xE,0xE,0xF}` | `emu/cores/ym2151.c:743-830` `set_connect()` 里各 ALG 谁接 `chanout[cha]` |
| **寄存器分类**：`reg == 0x08 \|\| reg >= 0x20` 是通道级 | `emu/cores/ym2151.c` `write()` 的 `case 0x20/0x28/0x30/0x38/0x40/0x60/0x80/0xA0/0xC0/0xE0` |
| **音量**：载波最小 TL | `CHIP-YM2151.md` §3.2（三方一致） |

分块判据（`NoteSplitter.cpp`）：

| 条件 | 动作 | 依据 |
|---|---|---|
| `0x08` 的 `(val & 0x78) != 0` 上升沿 | 开新块 | `CHIP-YM2151.md` §1.1（边沿，不是电平） |
| 下降沿 | 结束块，key-off 写进块内 | 同上 |
| KC 变化且 `\|Δ半音\| > 0.5` | legato 切新块 | `CHIP-YM2151.md` §2.5（DH 判据） |
| 载波 TL 变响（minTL 变小） | legato 切新块（重触发） | modizer bar 合并判据 |
| 其余 | 落进当前块的 `segments` | — |

**完整性不变量的落实**：每一条写都有归属，`structuredWrites == totalWrites` 才算通过。
不属于任何音符的写进 `channels[].orphans[]`（带 `reason`），全局寄存器进 `globals[]`。

---

## 5. 实测结果（`31 Grass Maze.vgm`）

```
file          : 31 Grass Maze.vgm
version       : 1.51
dataOffset    : 0x80
eofOffset     : 0x13056
totalSamples  : 2406316  (00:54.56 @44100)
loopOffset    : 0x6D0
loopSamples   : 2393734
chips         : 0x03 YM2151 clock=4000000 channels=8
commands      : 0x54 × 18773，各类 wait，0x66 × 1
```

```
vgm2json:  regWrites file=18773  stream=18773  structured=18773   integrity OK
           notes 2515   globals 2 segments   lanes 413 points
vgmroundtrip: PASS  原文件 77910 bytes / 重建 77910 bytes / 字节一致 YES
```

通道音符分布（8 通道都有音符，说明 key-on 的通道号取自数据位而不是地址）：

```
ch0 327  ch1 240  ch2 327  ch3 291  ch4 388  ch5 291  ch6 326  ch7 325
```

`notes` 里 2515 条中只有 4 条零长度、9 条短于 100 采样，属于真实的极短重触发。

另外 40 个同源文件也曾跑过一轮（`cli/scripts/batch_check.sh`）：import 41/41、roundtrip 41/41，
早期只测 `31 Grass Maze.vgm`；**2026-11-13 起已扩到全专辑 41 首**
（`cli/tools/batch_convert.sh`，41/41 全部成功），`31 Grass Maze` 仍是主力调音曲。

---

## 6. 踩过的坑（都已被修正，留档防复发）

| # | 坑 | 现象 | 修法 |
|---|---|---|---|
| 1 | 把代码先放进 `0x808/src/vgm/` | 违反"CLI 独立工程" | 全部删除，迁到 `cli/` |
| 2 | 影子设备 `Start()` 没回填 `retDevInf->devDef` | `ImportVgm` 段错误 | 回填 `&g_shadowDefs[I]`（真实核靠 `INIT_DEVINF`） |
| 3 | 影子上下文没带 `vgmChipType` | 所有写都被标成 chipType=0，`IsChannelReg` 全落空，音符数为 0 | 注册影子设备时把 `sd.vgmChipType` / `sd.instance` 写进 ctx |
| 4 | 寄存器分类只枚举了 `(reg & 0xF8)` 的十个值 | 10000+ 条写被判"未分类" | 改成 `reg == 0x08 \|\| reg >= 0x20`（与 libvgm `write()` 的 case 分支等价） |
| 5 | 自己写 KC 半音查表 `idx<3?idx:...` | 音高正确但与 libvgm 不是同一套算法 | 直接用 libvgm 的 `kc - (kc>>2)` |
| 6 | `0x08` key-on 的通道号取自 `reg & 7` | 所有音符挤到 ch0 | 通道号取自 `val & 7` |
| 7 | 同一采样点重复写同一寄存器被去重 | 完整性校验差 2 条 | 不去重，原样保留（`0x19` 的 AMD/PMD 就是同址两次写） |

---

## 7. 当前边界（明确没做的）

| 项 | 状态 | 为什么 |
|---|---|---|
| 从 JSON 重排命令生成 VGM（编辑后输出） | ❌ | `RULES.md` E3/F1：必须走 furnace dispatch，不自己拼 `0x54`/`0x61` 字节 |
| `.fui` 音色读写 | ❌ | 要接 furnace 的 `DivInstrument`，属步骤 3 |
| `opm2opll` 跨芯转换 | ✅ | 41 首全部转换通过，见 `INSTRUMENT-LIBRARY.md` §3.1d |
| `fuitool` | ❌ | 同上 |
| 非 YM2151 芯片的音符结构化 | ❌ | 第一阶段只做 YM2151；其他芯片的写仍被完整接住并落到 orphans，不丢 |

---


---

## 7.5 json2mid：音高验证工具（2026-11-11 新增）

跨芯片转换的音高"不可控、没有参考"，所以引入 **JSON → MIDI** 作为独立验证手段：
原芯片 JSON 和目标芯片 JSON 各转一份 MIDI，逐音符对比音号即可量化音高误差。

### 7.5.1 结构与依赖

| 项 | 内容 |
|---|---|
| 源码 | `cli/src/cli/json2mid.cpp` |
| MIDI 库 | `cli/third_party/midifile`（Craig Sapp smf，**从 `Denjhang_Music_Player_v16/midifile` 原样复制**，10,362 行） |
| 构建 | `add_library(midifile STATIC ...)` + `add_executable(json2mid ...)`，链接 `vgmcore midifile` |
| 参考算法 | 全部来自 `docs/PITCH-CONVERSION.md`，原始出处 `docs/reference/_pitch-*.md` |

### 7.5.2 用法

```bash
json2mid 31.json      -o 31.opm.mid  --report > r_opm.txt
json2mid 31.opll.json -o 31.opll.mid --report > r_opll.txt
```

参数：`--ppqn=480`（Valley Bell 的 4 档精度之一）、`--bpm=120`、`--octave-offset=25`、
`--no-pitchbend`、`--report`。

### 7.5.3 音高公式（两个芯片）

| 芯片 | 公式 |
|---|---|
| YM2151 | `pitch = 25 + (kc−(kc>>2)) + kf/64 + 12*log2(clock/3579545) + 12*log2(载波ML)` |
| YM2413 | `Hz = fnum*(clock/72)*2^(block−19)`；`pitch = 69+12*log2(Hz/440) + 12*log2(载波ML)` |

时间轴用 Valley Bell 的**绝对采样取整 + 差值**（`Midi_f0.bas:625-631`），无漂移。
弯音 ±2 半音（4096 步/半音），只对实际用到的通道发 RPN。

### 7.5.4 由此发现并修掉的 4 个音高 bug

| # | 文件 | bug | 实测症状 | 修法 |
|---|---|---|---|---|
| 1 | `FmVoice.cpp` `OpmKcKfToFreq` | 基准写成 `keyf−60`，实际应为 `keyf−36` | 整体低 2 个八度（`97.57 Hz` 应为 `390.27 Hz`） | 常数改正 |
| 2 | `FmVoice.cpp` `FreqToOpllFnum` | 照抄 OPL 的 `while (fnum > 1023)`，OPLL 只有 9 位 | 183 个音符 fnum 溢出，音高掉到 1~2 Hz | `> 511`、`blk` 从 0 起 |
| 3 | `FmVoice.cpp` `MatchOpllRom` | 照抄 vgm-conv 的 `floor(log2(ML比)/2)`，多除了 2 | 整轨低 12~28 半音 | `lround(log2(ML比))` |
| 4 | `json2mid.cpp` | 只算 KC/fnum 音高，漏乘载波 ML | 对比出 ±12/±28 假误差 | 两芯片都乘 `12*log2(载波ML)` |

### 7.5.5 验证结果（`31 Grass Maze.vgm`，2514 个音符配对）

| 阶段 | delta 分布 | 无效音符 |
|---|---|---|
| 修复前 | `{−34:299, +2:244, −24:202, −27:159, −10:129, ...}`，均值 −27.84 | 183 |
| 修复后 | `{0:2295, −4:165, −2:54}`，均值 −0.32 | 0 |

91.3% 的音符两边完全一致。剩余 219 个是 −2/−4 半音，全部在 ch4（源载波 ML=10）——
OPLL 无法表达 ML=10 这种非 2 次幂倍频，是芯片能力上限，不是 bug。

### 7.5.6 仍需人工确认

`--octave-offset=25` 是从 libvgm `phaseinc_rom` 推的物理真值，与 Valley Bell（等价 13）
差 12、与 Denjhang 差 1。不影响旋律，只影响与原曲是否同调，建议用耳朵确认一次。

---

## 8. 下一步

1. **听感复核音高**：用 `31.opm.mid` / `31.opll.mid` 与原 VGM 对照，确认 `--octave-offset=25`。
2. `opm2opll` 的 ML 残差（ch4 的 −2/−4 半音）：OPLL 无 ML=10，考虑改用 `0x30` 的 INST 换预置或加 detune。
3. `fuitool`：接 furnace `DivInstrument::readInsData()` / `save()`。
4. `json2vgm --from-data`：接 furnace dispatch，替换当前的 raw 直通。
