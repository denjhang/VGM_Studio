# Valley Bell vgm2mid — 音符分块器的正确参考实现（★★ 最该抄的算法）

| 项 | 值 |
|---|---|
| 路径 | `E:\working\vscode-projects\Reference_Project\vgm_libs\vgm2mid_2013-09-09_src Valley Bell` |
| 作者 | **Valley Bell**（libvgm 作者，VGM 社区权威） |
| 许可 | 见仓库根（未核实；只读参考） |
| 语言 | **Visual Basic 6** |
| 日期 | 2013-09-09 |
| 规模 | 约 **9,500 行**（.bas/.frm/.cls） |
| 支持芯片 | **9+**，**含 YM2151（OPM）** |

---

## 1. 它是什么

**VGM → MIDI 转换器**。作者是 libvgm 的作者，这是 VGM 社区最权威的实现之一。

**这是"寄存器流 → 音符"算法的最完整参考**——不是因为它架构好（VB6 单文件风格），而是因为它是唯一把 9 种以上芯片的 note 检测都做对了的实现，且 YM2151 支持是社区贡献后经作者审核的。

---

## 2. 文件结构

| 文件 | 行数 | 职责 |
|---|---|---|
| `Prog/frmOptions.frm` | 2912 | 选项 UI/默认值（BPM、PPQN、pitchbend 灵敏度…） |
| **`Prog/VGM.cls`** | **1179** | **VGM 主解析循环**（`ConvertVGMtoMID`，`:249`） |
| **`Modules/Midi_f0.bas`** | **1008** | MIDI 写入、时间换算、DAC 模块 |
| `Modules/YM2612.bas` | 553 | OPN2/OPN（含 YM2203/2608/2610） |
| `Modules/YMF278.bas` | 459 | OPL4（24 slot + drums） |
| `Modules/CommDlg.bas` | 420 | 文件对话框 |
| `Prog/frmMain.frm` | 379 | 批处理主界面 |
| `Modules/SN76489.bas` | 331 | PSG（含 T6W28 双芯片） |
| `Modules/YM2413.bas` | 310 | OPLL |
| `Modules/YM3812.bas` | 303 | OPL/OPL2/OPL3 |
| `Modules/basNESAPU.bas` | 289 | NES APU |
| **`Modules/YM2151.bas`** | **272** | **OPM（★ 核心参考）** |
| `Prog/VGM2MID.bas` | 250 | 全局常量、`DoNoteOn`/`Note()`、MIDI 初始化 |
| `Modules/GameBoy.bas` | 232 | GB DMG |
| `Modules/PCM.bas` | 184 | SegaPCM |
| `Modules/AY8910.bas` | 167 | AY-3-8910 |
| `Modules/VGM.bas` | 142 | **旧解析器，未加入 vbp（死代码）** |
| `Modules/GYM.bas` | 61 | GYM |
| `Modules/FM.bas` | 39 | **旧版 DoNoteOn/Note，未加入 vbp（死代码）** |

`vgm2mid.vbp` 模块清单：**没有 basFM，也没有 Modules/VGM.bas** → 这两个是历史残留。活跃实现是 `VGM.cls` + `VGM2MID.bas`。

---

## 3. 主流程

```
frmMain.VGM_Convert (frmMain.frm:127)
  → VGM.OpenFile
  → ConvertToMID (VGM.cls:146-154，按扩展名 vgm/vgz/gym 分派)
  → MID_File_Init (Midi_f0.bas:493)
  → ConvertVGMtoMID (VGM.cls:249)          ← 主循环
  → MID_File_Write (Midi_f0.bas:520)
```

主循环骨架（`VGM.cls:343-650`）：

```vb
Do
    dblWait = 0 : CurChip = 0
    bytCommand = bytFileData(lngFilePos)
    Select Case bytCommand
    Case PSG            : ...                              ' 0x50
    Case YM2413         : YM2413Command_Handle r, d        ' 0x51
    Case YM2612_P0, YM2612_P1, YM2203 To YM2610_P1
                        : YM2612Command_Handle port*3, r, d ' 0x52..0x59
    Case YM2151         : YM2151Command_Handle r, d        ' 0x54  ← 425-428
    Case WAIT_N_SAMPLES : dblWait = ...                    ' 0x61
    End Select
    DeltaTime = DeltaTime + dblWait                        ' 603，单位=采样
Loop Until lngFilePos >= lngEOFOffset Or StopVGM
```

`DeltaTime` 是**全局采样计数**，各芯片 handler 发 MIDI 事件时由 `MIDI_Event_Write` 统一折算。

---

## 4. YM2151 的音符判定（★ 核心）

`Modules/YM2151.bas`：

| 项目 | 寄存器 | 位/掩码 | 行号 |
|---|---|---|---|
| **Key On/Off** | **`0x08 + ch`** | `Slot = (Data/8) And 0xF`，**`Slot > 0` = ON** | **109-126** |
| Key Code / Block+FNum | `0x28 + ch` | `Block = (Data/0x10) And 7`；`FNumMSB = Data And 0x0F` | 171-189 |
| Key Fraction | `0x30 + ch` | `FNumLSB = (Data/4) And 0x3F`（0x00–0xFC，0x100 = 1 半音） | 190-201 |
| Pan/FB/Con | `0x20 + ch` | `(Data/0x40) And 3` → L/R/center | 140-170 |
| TL（音量） | `0x60 + ch*8 + op` | 只取 `OP == 3`（carrier）→ `DB2MidiVol(YM3812_Vol2DB(TL)/4)` | 219-242 |

### 4.1 边沿检测（109-125）

```vb
Case YM2151_KEY_ON                                   ' &H8
    CH = Data And &H7
    Slot(CH) = Fix(Data / &H8) And &HF               ' 高 3 位 = operator mask
    NoteOn_1(CH) = NoteOn_2(CH)                      ' 保存上一状态
    NoteOn_2(CH) = IIf(Slot(CH) > &H0, &H1, &H0)     ' 当前状态
    If NoteOn_2(CH) <> NoteOn_1(CH) And Note_2(CH) <> &H0 Then
        If NoteOn_2(CH) = &H0 Then
            DoNoteOn Note_1(CH), &HFF, CH, MIDINote(CH), MIDIWheel(CH), 255   ' → Note Off
        Else
            DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH), 255
        End If
    End If
```

**关键**：`NoteOn_1` = 上一状态、`NoteOn_2` = 当前状态，**只有 `0→1` 才 Note On，`1→0` 才 Note Off**。

纯改音高（`0x28`/`0x30` 写入）在 key 已开时走 `DoNoteOn` → **pitch bend 分支**，不重触发。

**这正是 Denjhang 的 `key_on_event` 布尔量的 VB6 版本，也是我们 `NoteSplitter` 要实现的语义。**

### 4.2 音高：粗查表 + KF 小数

`Modules/YM2151.bas:26-53`：

```vb
Public Function YM2151FNumToMidi(ByVal FNum As Byte, ByVal Block As Byte) As Byte
    If Block > &H7 Then Block = &H7
    FNum = FNum And &HF                       ' 只用高 4 位
    If Not ((FNum And &H3) = &H3) Then
        NoteVal = (FNum And &HC) / &H4
        NoteVal = NoteVal * 3 + (FNum And &H3)
        NoteVal = 61 + NoteVal
    Else
        NoteVal = &HFF                        ' fnum&3 == 3 视为非法 → Note Off
    End If
    If NoteVal = &HFF Then YM2151FNumToMidi = &HFF
    Else YM2151FNumToMidi = NoteVal + (Block - &H4) * 12
End Function
```

KF 补小数（`196-198`）：

```vb
FNum_LSB(CH) = Fix(Data / &H4) And &H3F                  ' 0x00-0xFC → 0..63
Note_2(CH) = YM2151FNumToMidi(FNum_2(CH), Block(CH)) + FNum_LSB(CH) / &H40  ' /64 半音
```

**要点**：

- OPM 分支是"**粗查表 + KF 小数**"
- 音高分辨率只有 FNum 高 4 位（`fnum & 3 == 3` 视为非法 → Note Off）
- **精度差但绝对音高正确**
- `FNum_LSB` 这个名字有误导：它其实是 **reg 0x30 = KF**，真 FNum LSB 从未读取

**对比 Denjhang**（`reference/Denjhang.md`）：Denjhang 用 `KC` 的 octave + 半音查表 + `KF/64`，**没用 `0x28` 的 FNumMSB**。两者思路不同但都给出正确音高，因为 OPM 的 `0x28` 就是 KC（octave + 半音码），不是 FNum。**Valley Bell 把 KC 的低 4 位当 "FNumMSB" 是一种历史命名遗留**，实际上是同一回事。

---

## 5. 其它芯片的 key-on（★ 一张表覆盖全部）

| 芯片 | Key On 寄存器 | 条件 | 位置 |
|---|---|---|---|
| **YM2151** | **`0x08 + ch`** | `(Data/8) And 0xF > 0` | `YM2151.bas:109-126` |
| YM2612 / OPN | `0x28` | `Slot = (Data And 0xF0)/8`，`Slot == 0` → Note Off | `YM2612.bas:151-176` |
| YM2413 / OPLL | `0x20 + ch` | **bit4** (`YM2413_REG_KEY = 16`) | `YM2413.bas:166-203` |
| YM3812 / OPL | `0xB0 + ch` | **bit5** (`YM3812_REG_KEY = &H20`) | `YM3812.bas:152-194` |
| SN76489 / PSG | **无 key-on 寄存器** | `FNum 变化` + `Attenuation == 0xF`（可选 `PSG_VOLDEP_NOTES`） | `SN76489.bas:76-200` |
| YM2413 Rhythm | `0x0E` | BD/SD/TOM/TCT/HH 位 | `YM2413.bas:82-155` |

### 5.1 YM2612 的边沿检测（151-176）

```vb
Case YM2612_SLOT_CH
    CH = (Data And YM2612_CH)                 ' &H7
    CH = IIf(CH > 3, CH - 1, CH)              ' 跳过 ch3 special
    Slot(CH) = (Data And YM2612_SLOT) / 8     ' &HF0
    If Slot(CH) = 0 Then
        MIDI_Event_Write MIDI_NOTE_OFF, CH, MIDINote(CH), &H0
        MIDINote(CH) = &HFF : NoteOn_2(CH) = 0
    Else
        If NoteOn_2(CH) = 0 Then
            DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH), 255
            NoteOn_2(CH) = 1
        End If
    End If
```

### 5.2 PSG（无 key-on 寄存器）

`SN76489.bas:76-200`：

- 音调寄存器 `0x80 | ch<<5`：`FNum = ((LSB And 0x3F) << 4) + (MSB And 0xF)`
- 衰减寄存器 `0x90 | ch<<5`（4bit，15 = 静音）
- Note On/Off 由 `FNum 变化` 与 `Attenuation == 0xF` 共同决定
- 可选 `PSG_VOLDEP_NOTES`（音量依赖的 Note On/Off）

---

## 6. 音高换算公式（全部）

### 6.1 通用：浮点音高

`Prog/VGM2MID.bas:246-250`：

```vb
Public Function Note(ByVal Hz As Double) As Double
    If Hz = 0 Then Note = &HFF Else _
        Note = ((Log(Hz) - Log(440)) / (Log(2 ^ (1 / 12)))) + 69
End Function
```

`Note()` 返回**浮点 MIDI 音号**：整数部分 = note，小数部分在 `DoNoteOn` 里转 pitch bend。

### 6.2 pitch bend 转换（`VGM2MID.bas:221-244`）

```vb
dblPitchWheel = (Note_2 - Fix(Note_2))
Note_2 = Abs(Fix(Note_2))
If dblPitchWheel >= 0.5 Then Note_2 = Note_2 + 1 : dblPitchWheel = -1 + dblPitchWheel
If MIDINote < &HFF Then MIDI_Event_Write MIDI_NOTE_OFF, MIDIChannel, MIDINote, &H0
NewWheel = MIDI_PITCHWHEEL_CENTER + (dblPitchWheel * PITCHWHEEL_STEPS)
...
MIDINote = Note_2
MIDI_Event_Write MIDI_NOTE_ON, MIDIChannel, MIDINote, NoteVolume
```

- `PITCHWHEEL_STEPS = 8192 / PITCHWHEEL_SENSITIVITY`（`frmOptions.frm:2594`，默认灵敏度 2 → 4096 步/半音）
- `MIDI_PITCHWHEEL_CENTER = &H2000`（`Midi_f0.bas:182`）
- 若音高变化落在弯音范围内，`DoNoteOn` **只发 pitch bend 不重触发**（`:196-208`）

**★ 这是"音高 + 微分音"的统一表示法，极其简洁。我们 `view.nearestNote` + `view.cents` 就是这个思想的 JSON 版。**

### 6.3 各芯片 Hz 公式

| 芯片 | 公式 | 位置 |
|---|---|---|
| **YM2151 / OPM** | **KC/KF 查表（见 §4.2），不用 Hz** | `YM2151.bas:26-53` |
| YM2612 / OPN | `Hz = FNum * fsam2612 * 2^(Block-22)`，`fsam = clock/72` | `YM2612.bas:57-59` |
| YM2413 / OPLL | `Hz = FNum * fsam2413 * 2^(Block-19)` | `YM2413.bas:46-50` |
| YM3812 / OPL | `Hz = FNum * fsam3812 * 2^(Block-20)` | `YM3812.bas:32-40` |
| SN76489 | `Hz = (ClockPSG/32) / FNum` | `SN76489.bas:36-38` |
| AY8910 | `Hz = (ClockAY/16) / FNum` | `AY8910.bas:31-34` |
| NES APU | `Hz = NES_CLK_BASE / (FNum+1)` | `basNESAPU.bas:29-32` |
| GB DMG | `Hz = 131072 / (2048 - FNum)` | `GameBoy.bas:28-31` |
| YMF278 / OPL4 | 对数式 `Note = Octave*12 + Pitch*12` | `YMF278.bas:34-68` |

---

## 7. 时间换算（★ 我们该抄的算法）

### 7.1 累加绝对采样

`VGM.cls:489-500, 603`：`0x61` 的 16bit 采样数、`0x62`=735、`0x63`=882 全部累加进全局 `DeltaTime`，单位 = samples @44100。

### 7.2 落笔时算绝对 tick（不留余数）★

`Midi_f0.bas:493-514` 初始化：

```vb
Select Case CNV_ACCURACY          ' 0/16/128/255 → PPQN
    Case &H0  : TickspQuarter = 30
    Case &H10 : TickspQuarter = 480
    Case &H80 : TickspQuarter = 2205
    Case &HFF : TickspQuarter = 22050
End Select
DeltaFact = 22050 / TickspQuarter
If TEMPO_MOD = &H0 Then DeltaFact = DeltaFact * 120 / TEMPO_BPM   ' 默认 120 BPM
WrittenDelayPos = 0 : CurSamplePos = 0
```

`Midi_f0.bas:625-631`（每个事件开头）：

```vb
CurSamplePos = CurSamplePos + DeltaTime
DeltaTime = 0
TempLng = Int(CurSamplePos / DeltaFact + 0.5)      ' 绝对 tick
DeltaVal = TempLng - WrittenDelayPos               ' 增量 tick
WrittenDelayPos = WrittenDelayPos + DeltaVal
```

**关键**：

- **累加绝对采样位置，再取整**
- 用 `WrittenDelayPos` 记录已写出的 tick 位置
- **整数截断误差不会累积漂移**

默认 480 PPQN、120 BPM 时 `DeltaFact = 22050/480 = 45.9375` samples/tick。

**这个"绝对位置取整 + 差值"的算法比"保留余数"更简洁，且无漂移。我们的播放位置就应该是绝对采样。**

---

## 8. 鼓通道与 DAC 特殊处理

统一走 **MIDI channel 9**（`CHN_DAC = 9`，`VGM2MID.bas:6`；初始化 `:142-144`）。

| 场景 | 处理 | 位置 |
|---|---|---|
| YM2612 DAC (`0x2A`) | 数据字节**当音高**用：`Data >= &H20` → 停止；否则先 off 旧 note 再 `Note On ch9, Data` | `YM2612.bas:178-219`（核心 206-213） |
| YM2413 Rhythm (`0x0E`) | BD=35、SD=38、TOM=45、HH=42、TCT(cym)=46，各带独立音量 | `YM2413.bas:82-155` |
| YM3812 Rhythm | BD/SD/TOM/CYM/HH，`CH = CHN_DAC` | `YM3812.bas:196-` |
| NES DMC | `CH=4 → MIDIChannel = CHN_DAC` | `basNESAPU.bas:94, 259` |
| SegaPCM | 所有通道写 ch9；地址（=采样）硬编码映射成鼓 note（`&H437C → 0x38` 等） | `PCM.bas:12-90+` |
| YMF278 OPL4 drums | `ToneIns bit7 = 1` → `DrumMode=True`，`SelChn = CHN_DAC` | `YMF278.bas:70-95, 103-140` |
| DAC Stream (`0x90-0x95`) | 用 **MIDI channel 15**（`&HF`）打 note 表示 stream 切换，data block 索引当音高 | `VGM.cls:570-640` |
| 弯音抑制 | ch9 且弯音 < 0.1 半音时不发 pitch bend，避免鼓音被乱弯 | `VGM2MID.bas:235` |

---

## 9. `vgm2mid_Update.txt` 记录的改进

全文 19 行：

| 行 | 内容 |
|---|---|
| 6 | 支持 VGM ≤ v1.51 |
| **7** | **YM2151 支持（NeoLogiX 贡献），新增 YM2151 选项页** |
| 8 | SegaPCM 支持（先只做 OutRun 鼓） |
| 9 | YM2203/2608/2610（含 FM ADPCM 鼓） |
| 10 | YM3526/3812/8950/YMF262 |
| **11** | **重写延迟计算 + 可调转换精度（CNV_ACCURACY）** |
| 12 | 一般 DAC 支持；对 optvgm 优化过的 DAC 支持良好 |
| 13 | 双芯片 & 完整 T6W28 |
| **14** | **PSG 依赖音量的 Note On/Off** |
| 15 | GD3 英文标签转换 |
| 16 | Loop 标记 `loopStart`/`loopEnd` |
| 17 | YM2413 Rhythm 改进 |
| 18 | 完整 YMF278（外挂音色文件） |
| 19 | 多次循环选项 |

`Readme.txt:89-99` 还有一条经验：*"All supported soundchips are now in 'perfect' tune"*，以及 DAC 是通过**检测 DAC 的 PAN 变化**触发 Bass Drum（"does not work with all games"，因此提供关闭开关）。

---

## 10. 对我们最有价值的 6 条（★ 直接写进 NoteSplitter 设计）

| # | 规则 | 依据 |
|---|---|---|
| 1 | **分块主键 = (chip, channel/slot)**，状态存 `NoteOn_1/NoteOn_2`（上一状态/当前状态）做**边沿检测** | `YM2151.bas:114-125`、`YM2612.bas:162-176` |
| 2 | **一个音符段 = 一次 `0→1` 到下一次 `1→0`**；中间的频率/音量写入是"段内更新"（pitch bend / CC），**不是新音符** | 全仓实现一致 |
| 3 | **浮点音高表示**（`Note()` 返回 double，整数 = note、小数 = pitchbend）是极简的"音高 + 微分音"统一表示，优于绝对 Hz | `VGM2MID.bas:246-250` |
| 4 | **时间用绝对 samples 累加，落笔时 `round(pos/fact) - written_ticks`**，永不留余数 | `Midi_f0.bas:625-631` |
| 5 | **YM2151 必须用 `0x08` 做 key on/off，`0x28` 只给 Block/音名，`0x30` 是 KF（分数半音）**；不要把 `0x28` 当触发 | `YM2151.bas:109-198` |
| 6 | **PSG/WS 这类无 key-on 芯片**：用 `period 变化` + `衰减寄存器 == 最大` 组合判定 | `SN76489.bas:213-232` |

### 10.1 与我们现有设计的对照

| 我们的设计 | Valley Bell 的对应 | 结论 |
|---|---|---|
| `NoteSplitter` 按 key-on/key-off 切块 | `NoteOn_1/NoteOn_2` 边沿检测 | ✅ 一致 |
| `notes[].segments[]` 存块内所有写 | 段内 pitch bend / CC 更新 | ✅ 一致 |
| `view.nearestNote` + `view.cents` | `Note()` 浮点音高（整数 + 小数） | ✅ 同思想 |
| `startSample` / `endSample` | `CurSamplePos` 绝对采样 | ✅ 一致 |
| 不存 BPM | `DeltaTime` 只累加采样，BPM 只用于写 MIDI tick | ✅ 一致 |

**三条独立实现（我们、Denjhang、Valley Bell）在分块判据上完全一致。这个设计可以定稿。**

---

## 11. 结论

**这是"寄存器流 → 音符"算法的最权威参考。**

它比 Denjhang 更有价值的地方：

1. **YM2151 是完整的（含 pitch bend 处理）**，不是"只画键盘"
2. **9+ 芯片的 key-on 判定汇总成一张表**，将来扩展芯片直接查
3. **时间算法（绝对位置取整 + 差值）无漂移**，比 modizer 的帧行方案正确
4. **浮点音高表示法**（整数 + 小数）与我们 `view.nearestNote` + `view.cents` 完全同构

它不如 Denjhang 的地方：

1. VB6 代码风格古老，没有现代 C++ 的抽象
2. 没有"音符段"数据结构（只在发 MIDI 时即时处理）
3. 音高精度受 FNum 高 4 位限制（`fnum & 3 == 3` 直接丢弃）

**我们的设计应该：抄它的判据 + 时间算法 + 音高表示法，但不抄它的单文件流式处理方式**（我们要把音符段作为持久数据结构存进 JSON）。
