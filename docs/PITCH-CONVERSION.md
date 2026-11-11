# 音高转换：五个参考实现的全部方法（事实清单）

> 本文件是**汇总**。每一条都有原始出处文档，原始文档里每条结论都带 `文件:行号`。
>
> | 参考 | 原始文档 |
> |---|---|
> | Valley Bell vgm2mid (2013-09-09, VB6) | [`_pitch-valleybell.md`](_pitch-valleybell.md) |
> | Denjhang / MDPlayer / modizer | [`_pitch-denjhang-mdplayer-modizer.md`](_pitch-denjhang-mdplayer-modizer.md) |
> | vgm-conv + ym-voice | [`_pitch-vgmconv.md`](_pitch-vgmconv.md) |
> | Furnace（反向：音高→寄存器） | [`_pitch-furnace.md`](_pitch-furnace.md) |

---

## 0. 一句话结论（先看这个）

| 芯片 | 音高来源 | 公式 | 时钟依赖 |
|---|---|---|---|
| **YM2151** | `0x28` = KC（oct 3 位 + 音名码 4 位），`0x30` = KF（6 位分数） | **半音索引 = KC − (KC>>2)**，分数 = `KF/64` 半音 | **无**（KC 就是音名，不含时钟） |
| **YM2413** | `0x10+ch` = FNum 低 8 位，`0x20+ch` = FNum bit8 + Block 3 位 | `Hz = FNum × (clock/72) × 2^(Block−19)` | **有**（clock 直接进公式） |

**YM2151 音高与时钟无关**——这是最重要的事实，也是我方 `Opm2Opll` 引入 2 个八度错误的根源（见 §5）。

---

## 1. YM2151：五个实现，同一个半音映射

### 1.1 半音映射（四家完全等价，只有 3/7/11/15 处理不同）

KC 低 4 位的合法码只有 12 个：`0,1,2,4,5,6,8,9,10,12,13,14`（跳过 3/7/11/15）。

| 实现 | 代码 | 位置 |
|---|---|---|
| **Valley Bell** | `NoteVal = 61 + (n>>2)*3 + (n&3)`，`n&3==3` 时返回 `&HFF`（丢弃） | `Modules/YM2151.bas:37-44` |
| **Denjhang** | `semi = idx<3?idx : idx<7?idx-1 : idx<11?idx-2 : idx-3` | `windows/vgm/vgm_window.cpp:3402-3403` |
| **MDPlayer** | `note < 3 ? note : note < 7 ? note - 1 : note < 11 ? note - 2 : note - 3` | `form/KB/frmYM2151.cs:218-227` |
| **vgm-conv** | `KEY_TO_NOTE = [0,1,2,4,5,6,8,9,10,12,13,14]`；反表 `keyCodeToIndex = [0,1,2,3,3,4,5,6,6,7,8,9,9,10,11,12]` | `opm_freq.ts:8`、`opm-clock-converter.ts:5` |
| **Furnace** | `noteMap[12] = {0,1,2,4,5,6,8,9,10,12,13,14}`；`hScale(n) = ((n/12)<<4) + noteMap[n%12]` | `platform/arcade.cpp:286-292` |
| **libvgm 本体** | `kc_channel = (kc − (kc>>2)) * 64 + 768` | `emu/cores/ym2151.c:1067-1068` |

16 个输入值的完整对照表（`n` = KC 低 4 位）：

| n | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `n−(n>>2)` = libvgm | 0 | 1 | 2 | **3** | 3 | 4 | 5 | **6** | 6 | 7 | 8 | **9** | 9 | 10 | 11 | **12** |
| Valley Bell `(n>>2)*3+(n&3)` | 0 | 1 | 2 | (丢) | 3 | 4 | 5 | (丢) | 6 | 7 | 8 | (丢) | 9 | 10 | 11 | (丢) |
| keyCodeToIndex（vgm-conv 反表） | 0 | 1 | 2 | 3 | 3 | 4 | 5 | 6 | 6 | 7 | 8 | 9 | 9 | 10 | 11 | 12 |
| Denjhang / MDPlayer | 0 | 1 | 2 | 2 | 3 | 4 | 5 | 5 | 6 | 7 | 8 | 8 | 9 | 10 | 11 | 12 |

**关键**：`n−(n>>2)` 与 Valley Bell 的 `(n>>2)*3+(n&3)` 在 16 个值上**数学恒等**（Valley Bell 只是额外把 `n&3==3` 当非法丢弃）。所以这两家是同一个公式。

Denjhang / MDPlayer 在 n=3,7,11,15 上取"前一个音"（2,5,8,11），比硬件**低 1 个半音**；这几码在真实曲子里不出现，实际无影响。

### 1.2 八度基准（三家都不同，必须选一个）

| 实现 | 公式 | KC = 0x40（oct=4, 码 0）算出来 | 位置 |
|---|---|---|---|
| **Valley Bell** | `+ (Block − 4) * 12`（Block=4 时基音 61） | MIDI **61** | `YM2151.bas:49` |
| **Denjhang** | `oct*12 + semi + 24` | MIDI **72** | `vgm_window.cpp:3407` |
| **MDPlayer** | `oct*12 + note + hosei`（hosei = 时钟补偿，无固定 24） | 无 24 偏移 | `frmYM2151.cs:227` |
| **我方** | `Ym2151Semitone(kc) + 24` | MIDI **72** | `AnalyzerYm2151.h:35-39` |
| **物理真值**（见 §1.4） | `25 + (kc − (kc>>2))` | MIDI **73** | — |

### 1.3 KF：四家都是 1/64 半音

| 实现 | 读取 | 用法 | 位置 |
|---|---|---|---|
| Valley Bell | `(Data>>2) & 0x3F`，`/64` 加到音号 | 参与音高 | `YM2151.bas:196-198` |
| Denjhang | `(reg>>2) & 0x3F`，`kf/64.0f` | 参与音高 | `vgm_window.cpp:3406-3407` |
| **MDPlayer** | `(reg & 0xFC) >> 2` | **只显示，不进音高公式** | `frmYM2151.cs:247`（缺陷） |
| vgm-conv | `kf = floor(frac*64)` | 参与音高，写回 `kf<<2` | `opm_freq.ts:17`、`opn-to-opm-converter.ts:98` |
| Furnace | 内部 1/128 半音，写 `0x30 = (freq<<1)&0xFC` | 参与音高，粒度为 1/64 | `arcade.cpp:520` |

**结论**：KF 必须计入，精度 1/64 半音。MDPlayer 不用 KF 是它的缺陷。

### 1.4 物理真值推导（★ 决定哪家对）

libvgm 是权威仿真器，它的 OPM 核给出硬件行为（`emu/cores/ym2151.c`）：

```c
// 写 0x28 (Key Code)：
kc_channel = (v - (v>>2))*64;      // v = kc & 0x7f
kc_channel += 768;
kc_channel |= (op->kc_i & 63);     // KF 的 6 位
// 写 0x30 (Key Fraction)：
v >>= 2;  kc_channel = v | (kc_i & ~63);
```

相位增量表（`ym2151.c:436` `phaseinc_rom[768]`，**768 项 = 一个八度 = 12 半音 × 64**）：

```c
for (i=0; i<768; i++) {
    phaseinc = phaseinc_rom[i];            // 10.10 定点
    phaseinc *= scaler;                    // scaler = (clock/64)/sampfreq
    chip->freq[768 + 2*768 + i] = (int)(phaseinc * (1<<(16-10)));  // 八度 2 = 基准
    for (j=0; j<2; j++) chip->freq[768 + j*768 + i] = freq[基准] >> (2-j);
    for (j=3; j<8; j++) chip->freq[768 + j*768 + i] = freq[基准] << (j-2);
}
```

`phaseinc_rom[0] = 1299`（10.10 = 1.26855）。相位累加器每 `1024<<16` 走一个完整周期（`sin_tab[1024]`）：

```
f_ref = (clock/64) × 1.26855 / 1024
      = 55930.4 × 1.26855 / 1024 = 69.29 Hz        （clock = 3579545）
```

索引 `i` 的单位是 1/64 半音，768 项 = 1 个八度，所以表覆盖 8 个八度（`chip->freq` 长度 768×10）。

**基准八度（j=2）的最低音 = 69.29 Hz = C#2 = MIDI 37**（MIDI 60 = C4）。

KC 的 oct 字段 k 对应 `j = k + 1`（因为 `+768`），所以：

```
j = k + 1，i = (kc − (kc>>2) − 12k)*64 + kf

Hz = 69.29 × 2^( (kc − (kc>>2) − 12 + kf/64) / 12 ) × (clock / 3579545)
```

反算 MIDI 音号：

```
MIDI = 37 + 12×log2(Hz/69.29)
     = 37 + (kc − (kc>>2) − 12) + kf/64
     = 25 + (kc − (kc>>2)) + kf/64
```

**验算 KC = 0x40（oct=4，码 0）**：`25 + 48 = 73` = C#5 = 554.4 Hz。
用上公式：`69.29 × 2^((48−12)/12) = 69.29 × 8 = 554.3 Hz` ✓ 自洽。

**所以：**

| 实现 | KC=0x40 给出 | 与真值 73 的差 |
|---|---|---|
| 物理真值 | 73 | 0 |
| Valley Bell | 61 | **−12（1 个八度）** |
| Denjhang / 我方 | 72 | **−1（1 个半音）** |
| MDPlayer（无 24） | ≈0 | 取决于 hosei |

1 个八度/1 个半音的**常数**偏移不影响旋律正确性（听感上只是移调），但会影响"和原曲同调"的比较。**因此 json2mid 必须带 `--octave-offset` 参数，并以物理真值为默认（+25 而不是 +24）。**

---

## 2. YM2151 的 key-on 与分块（四家一致）

| 实现 | 判定 | 位置 |
|---|---|---|
| Valley Bell | `Slot = (Data/8) & 0xF`；`Slot > 0` = ON；`NoteOn_1/NoteOn_2` 边沿 | `YM2151.bas:109-126` |
| Denjhang | `new_kon = (data & 0x78) != 0`，`ch = data & 7` | `vgm_window.cpp:1335-1349` |
| MDPlayer | `(dData & 0x78) != 0` → `fmKeyOn[ch] = (dData&0x78)\|1` | `ChipRegister.cs:1252-1270` |
| Furnace（反向） | 写 `0x08 = (opMask<<3)\|ch`，key-off 写 `(0x08, ch)` | `arcade.cpp:487-489, 529` |
| modizer | **不看 0x08**，靠 `pan != 0 && chanout 变化 && oper[3].freq != 0` | `emu/cores/ym2151.c:1991-1996` |

**定稿规则**（我方 `NoteSplitter.cpp:96-139` 已实现）：
- 通道号 = `data & 7`（地址恒为 0x08）
- 上升沿 = `(data & 0x78) != 0` 且上一状态为 0
- 下降沿 = `(data & 0x78) == 0` 且上一状态非 0
- 中间电平不变的重复写不切块

**Furnace 的写序**：同一 tick 内**先写 `0x28`/`0x30`，再写 `0x08`**（`arcade.cpp:502-534`）。解析器必须容忍"key-on 时音高已就位"，不能假设 key-on 之后才有音高。

---

## 3. YM2413：全部用 Hz 公式

### 3.1 寄存器位域

| 寄存器 | 内容 |
|---|---|
| `0x10 + ch` | FNum 低 8 位 |
| `0x20 + ch` | `bit0` = FNum bit8；`bit1-3` = Block；**`bit4` = KEY ON**；`bit5` = SUS |
| `0x30 + ch` | 高 4 位 = 音色号（INST）；低 4 位 = 音量（VOL） |

（Valley Bell `YM2413.bas:16-26, 178-183`；Furnace `opll.cpp:396-398`；vgm-conv `ym2413-clock-converter.ts:21-33`）

### 3.2 频率公式（Valley Bell，唯一给出完整公式的）

```vb
Public Function Hz_YM2413(ByVal FNum As Double, Optional Block As Byte) As Double
    If Block = 0 And FNum = 0 Then Hz_YM2413 = 0 Else Hz_YM2413 = FNum * fsam2413 * 2 ^ (Block - 19)
End Function
```
`Modules/YM2413.bas:46-50`

- `fsam2413` 运行时 = `Clock2413 / 72`（`Prog/VGM.cls:322`）；clock=3579545 时 = 49716.0
- 指数 `2^(Block−19)`，Block 0..7
- 然后 `Note = (log2(Hz/440))*12 + 69`（`Prog/VGM2MID.bas:246-250`）

**验算**：FNum=0x100(256)、Block=4 → `256 × 49716 / 32768 = 388.4 Hz` = G4。合理。

### 3.3 其他三家的写法

| 实现 | 公式 | 位置 |
|---|---|---|
| Denjhang | `freq = fnum * clk / (72 * 2^(19-blk))`，再 log2 | `vgm_window.cpp:3460-3463` |
| MDPlayer | `searchSegaPCMNote(fnum/172.0) + (oct-4)*12`，**常数 172.0 写死不随时钟变** | `form/KB/frmYM2413.cs:110-113` |
| Furnace（反向） | 查表 `pitch[i] = round(freq * 1180068 / clock)`，`block = bsr(freq/343)`，`fnum = freq>>block` | `pitchTable.cpp:205-212`、`opll.cpp:406-429` |

**定稿**：用 Valley Bell 的 `Hz = FNum × (clock/72) × 2^(Block−19)`，然后标准 Hz→MIDI。MDPlayer 的 172.0 是错的（不随时钟）。

### 3.4 YM2413 key-on

`0x20 + ch` 的 bit4，边沿检测（Valley Bell `YM2413.bas:194-211`）：

```vb
NoteOn_1(CH) = NoteOn_2(CH)
If (Register And &HF0) = YM2413_REG_SUS_KEY_BLOCK_FNum_MSB Then
    NoteOn_2(CH) = KEY_ON(CH)
End If
If KEY_ON(CH) = 0 Then
    ... MIDI_NOTE_OFF ...
ElseIf KEY_ON(CH) = 1 Then
    If NoteOn_2(CH) <> NoteOn_1(CH) Then DoNoteOn ..., 255   ' 重触发
    Else DoNoteOn ...                                        ' 只改音高
```

---

## 4. 时间与 MIDI 写入（Valley Bell 的算法，直接抄）

### 4.1 无漂移的时间换算

`Prog/Midi_f0.bas:493-514`：

```vb
Select Case CNV_ACCURACY
    Case &H0  : TickspQuarter = 30
    Case &H10 : TickspQuarter = 480
    Case &H80 : TickspQuarter = 2205
    Case &HFF : TickspQuarter = 22050
End Select
DeltaFact = 22050 / TickspQuarter          ' 480 PPQN → 45.9375 samples/tick
If TEMPO_MOD = &H0 Then DeltaFact = DeltaFact * 120 / TEMPO_BPM
WrittenDelayPos = 0 : CurSamplePos = 0
```

`Prog/Midi_f0.bas:625-631`（每个事件开头）：

```vb
CurSamplePos = CurSamplePos + DeltaTime
DeltaTime = 0
TempLng = Int(CurSamplePos / DeltaFact + 0.5)      ' 绝对 tick（四舍五入）
DeltaVal = TempLng - WrittenDelayPos               ' 增量
WrittenDelayPos = WrittenDelayPos + DeltaVal
```

**要点**：累加绝对采样位置 → 取整 → 减去已写出的 tick。整数误差不累积。

22050 的来历：44100 Hz 下 120 BPM 的**半拍** = 0.5 s = 22050 samples（一个四分音符）。所以 `22050 / PPQN` = 每 tick 的采样数。

### 4.2 浮点音高 + 弯音

`Prog/VGM2MID.bas:221-243`：

```vb
dblPitchWheel = (Note_2 - Fix(Note_2))        ' 小数部分
Note_2 = Abs(Fix(Note_2))                     ' 整数部分
If dblPitchWheel >= 0.5 Then                  ' 靠近下一个音就进一位
    Note_2 = Note_2 + 1
    dblPitchWheel = -1 + dblPitchWheel
End If
If MIDINote < &HFF Then MIDI_Event_Write MIDI_NOTE_OFF, MIDIChannel, MIDINote, &H0
NewWheel = MIDI_PITCHWHEEL_CENTER + (dblPitchWheel * PITCHWHEEL_STEPS)
If NewWheel <> MIDIWheel Then ... MIDI_Event_Write MIDI_PITCHWHEEL ...
MIDINote = Note_2
MIDI_Event_Write MIDI_NOTE_ON, MIDIChannel, MIDINote, NoteVolume
```

- `PITCHWHEEL_STEPS = 8192 / 灵敏度`，默认灵敏度 2 → **4096 步/半音**（`frmOptions.frm:2593-2594`）
- `MIDI_PITCHWHEEL_CENTER = 0x2000`（`Midi_f0.bas:182`）
- 单音符精度远高于 1/64 半音，足以表示 KF 和 YM2413 的实数直算

### 4.3 key 已开时只改音高 → 只发弯音

`Prog/VGM2MID.bas:186-208`：`DoNoteOn(Note_Type=255)` 强制重触发；不带 `Note_Type` 时先试弯音：

```vb
dblTestValue = MIDIWheel + ((Note_2 - Note_1) * PITCHWHEEL_STEPS)
Select Case dblTestValue
Case MIDI_PITCHWHEEL_MIN To MIDI_PITCHWHEEL_MAX + 1
    MIDIWheel = dblTestValue
    MIDI_Event_Write MIDI_PITCHWHEEL, MIDIChannel, MIDIWheel
Case Else
    GoTo DoNoteOn        ' 超出弯音范围 → 重发 note off/on
End Select
```

---

## 5. 我方现状：已定位并修复的音高缺陷

### 5.1 ~~`Opm2Opll` 的 2 个八度偏低~~（已修）

`FmVoice.cpp` 的 `OpmKcKfToFreq` 原来写 `277.2 * 2^((keyf-60)/12)`，基准错了 24 个半音。

**修法**：`freq = 277.2 * 2^((keyf - 36) / 12)`，其中 `keyf = kc - (kc>>2)`（36 是 KC=0x30 的值，对应 C#4 = 277.2 Hz）。实测该通道频率从 97.57 Hz 纠正到 390.27 Hz（正好 ×4）。

### 5.2 ~~OPLL fnum 上限用了 OPL 的 1023~~（已修）

`FreqToOpllFnum` 原来照抄 vgm-conv 的 `while (fnum > 1023)`，但 OPLL 的 fnum 只有 9 位（0..511）。fnum ≥ 512 时 bit8 溢出被截断，音高掉到 1~2 Hz。实测有 **183 个音符**因此无效。

**修法**：`while (fnum > 511)`，`blk` 从 0 起（不是 1），`blk=7` 时 fnum 再钳到 255。

### 5.3 ~~`octaveOffset` 公式少了因子 2~~（已修）

vgm-conv 的 `opl-voice.js:214` 写 `floor(log2(ML比)/2)`。**载波 ML 翻倍 = 基频翻倍 = 正好 1 个八度**，不该再除 2。

**修法**：`lround(log2(ML源/ML目标))`。实测：源 ML=2、目标 ML=1 时旧公式给 0（整轨低 12 半音），新公式给 +1。

### 5.4 ~~json2mid 漏了载波 ML~~（已修）

FM 的**实际发声基频 = 音高频率 × 载波 ML**。json2mid 一开始只算 KC/KF 或 fnum 的音高，没乘载波 ML，导致两边对比出 ±12/±28 半音的差。

**修法**：两个芯片都乘 `12*log2(载波ML)`。OPM 的载波由 ALG 掩码定位；OPLL 的载波 ML 来自 ROM 表 `slots[1].ml`。

### 5.5 修复后的实测结果（`31 Grass Maze`，2514 个音符配对）

```text
delta = OPLL_MIDI_pitch − OPM_MIDI_pitch

修复前: mean −27.84  分布 {−34:299, +2:244, −24:202, −27:159, −10:129, ...}
        183 个 OPLL 音符算出 ≤0（无效）

修复后: mean −0.32   median −0.02
        分布 {0: 2295, −4: 165, −2: 54}
        0 个无效音符
```

- **2295/2514（91.3%）完全一致**（差 0 个半音）
- 剩余 219 个是 −2/−4 半音，集中在 ch4（ALG=3，源载波 ML=10）——**OPLL 无法表达 ML=10 这种非 2 次幂倍频**，`round(log2(10/ML目标))` 只能补到最近的整八度，残差 log2(10/8)=0.32 八度 = 3.9 半音。这是芯片能力上限，不是 bug。
- ch0/1/2/3/5/6/7 全部 0 偏差。

### 5.6 仍需人工确认的项

| # | 项 | 说明 |
|---|---|---|
| 1 | **abs 音高的八度基准** | `--octave-offset=25` 是我从 libvgm `phaseinc_rom` 推的物理真值。它与 Valley Bell（等价 13）差 12、与 Denjhang 差 1。**听感上不影响旋律，只影响与原曲同调**。建议用耳朵或频谱确认一次。 |
| 2 | **OPM 时钟补偿** | OPM 的 KC 是音名，但绝对频率随时钟变（`clock/3579545`）。31 号文件的 OPM 时钟是 4 MHz，整曲比标准高 1.92 半音。json2mid 已按 `12*log2(clock/3579545)` 补偿。 |
| 3 | **`0x20` 与 `0x10` 的写入顺序** | 我方先 0x10 后 0x20（vgm-conv 的 `_key` 同）；Furnace 先 0x28/0x30 后 0x08。对解析无影响，只影响实机时序。 |

---

## 6. json2mid 的规格（已实现）

`cli/src/cli/json2mid.cpp`，构建目标 `json2mid`，MIDI 库 `cli/third_party/midifile`（从 Denjhang 直接复制）。

```
json2mid <input.json> -o <out.mid> [--ppqn=480] [--bpm=120]
                        [--octave-offset=25] [--no-pitchbend] [--report]
```

| 输入芯片 | 音高来源 | 公式 |
|---|---|---|
| `chips[].type == "YM2151"` | 每个音符段内 `0x28`/`0x30` | `pitch = 25 + (kc−(kc>>2)) + kf/64 + 12*log2(clock/3579545) + 12*log2(载波ML)` |
| `chips[].type == "YM2413"` | 每个音符段内 `0x10+ch`/`0x20+ch` | `Hz = fnum*(clock/72)*2^(block−19)`；`pitch = 69+12*log2(Hz/440) + 12*log2(载波ML)` |

- 时间：`tick = round(atSample / (22050/ppqn * 120/bpm))`（Valley Bell 的无漂移算法）
- 音高：整数部分 → Note On/Off；小数部分 → pitch bend，范围 ±2 半音（4096 步/半音）
- `--report`：打印每个音符的 `start,end,ch,note,cents,pitch`，两份可直接 diff

**已知限制**：`json2mid` 的 MIDI 是**验证用**的，不做音色（只有载波 ML 的八度影响）、不做包络、不做 LFO。

---

## 7. 验证流程（三步）

```bash
# 1) 原芯片 JSON → MIDI
json2mid 31.json -o 31.opm.mid --report > r_opm.txt

# 2) 目标芯片 JSON → MIDI
opm2opll 31.json -o 31.opll.json
json2mid 31.opll.json -o 31.opll.mid --report > r_opll.txt

# 3) 对比
python compare.py r_opm.txt r_opll.txt     # 按 (ch, startSample) 配对，输出 delta 分布
```

对比脚本的核心逻辑（直接跑过，见 §5.5）：

```python
import re
from collections import Counter
def load(p):
    d = {}
    for line in open(p):
        m = re.match(r'\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+([+-][\d.]+)\s+(-?[\d.]+)', line)
        if m:
            st, en, ch, note, cents, pitch = m.groups()
            d[(int(ch), int(st))] = float(pitch)
    return d
a, b = load('r_opm.txt'), load('r_opll.txt')
delta = [b[k] - a[k] for k in set(a) & set(b) if a[k] > 0 and b[k] > 0]
print(Counter(round(x) for x in delta).most_common(10))
```
