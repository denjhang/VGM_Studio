# Valley Bell vgm2mid (2013-09-09)：YM2151 / YM2413 音高与 MIDI 写入事实梳理

源目录：`E:\working\vscode-projects\Reference_Project\vgm_libs\vgm2mid_2013-09-09_src Valley Bell`

本文只记录在源码中实际读到的内容，所有结论后附 `文件:行号`。凡代码未体现的硬件细节，一律标注「代码中未见」。

涉及文件：

- `Modules/YM2151.bas`（272 行）
- `Modules/YM2413.bas`（310 行）
- `Prog/VGM2MID.bas`（250 行）
- `Modules/Midi_f0.bas`（1008 行）
- `Prog/VGM.cls`（1179 行，ISO-8859/CP1252 编码）
- 辅助：`Prog/frmOptions.frm`、`Modules/YM3812.bas`

---

## A. YM2151 → MIDI

### A.1 `YM2151FNumToMidi` 完整原文

`Modules/YM2151.bas:26-53`：

```vb
Public Function YM2151FNumToMidi(ByVal FNum As Byte, ByVal Block As Byte) As Byte

    Dim NoteVal As Byte
    
    'If FNum > &HF Then
    '    MsgBox "FNumToHz (2151): invalid fnum " & Format$(FNum)
    'End If
    If Block > &H7 Then
        Block = &H7
    End If
    
    FNum = FNum And &HF
    If Not ((FNum And &H3) = &H3) Then
        NoteVal = (FNum And &HC) / &H4
        NoteVal = NoteVal * 3 + (FNum And &H3)
        NoteVal = 61 + NoteVal
    Else
        NoteVal = &HFF
    End If
    
    If NoteVal = &HFF Then
        YM2151FNumToMidi = &HFF
    Else
        YM2151FNumToMidi = NoteVal + (Block - &H4) * 12
        If YM2151FNumToMidi = &H0 Then Stop
    End If

End Function
```

要点（逐条对应行号）：

- 入参 `FNum` 先被 `And &HF` 截取低 4 位（`Modules/YM2151.bas:37`），所以该函数实际只使用「KC 的低 4 位」，高位被静默丢弃（原本的越界提示在 `Modules/YM2151.bas:30-32` 被注释掉）。
- `Block` 上限被钳到 7：`If Block > &H7 Then Block = &H7`（`Modules/YM2151.bas:33-35`）。
- KC 低 4 位低 2 位为 `&H3`（即 3/7/11/15）时判为非法，返回 `&HFF`（`Modules/YM2151.bas:38-44, 46-47`）。
- 返回值 = 半音基础值 61 + 组内偏移 + `(Block - 4) * 12`（`Modules/YM2151.bas:49`）。
- `If YM2151FNumToMidi = &H0 Then Stop` 是一句调试断点（`Modules/YM2151.bas:50`）。按公式推算：`NoteVal` 范围 61..70，Block 0..7，结果最小为 `61 + (0-4)*12 = 13`，因此该 Stop 分支不会命中。

### A.2 KC 低 4 位 → 半音的映射

代码等价式（`Modules/YM2151.bas:39-41`）：

```
NoteVal = 61 + ((n >> 2) * 3) + (n & 3)     其中 n = KC 低 4 位，且 (n & 3) != 3
```

即：把 0..15 每 4 个一组（组号 `n>>2` = 0..3），每组贡献 3 个半音，组内偏移 `n&3` = 0..2；组内第 4 个值（偏移 3）被丢弃。于是 16 个编码里只保留 12 个，正好覆盖一个八度的 12 个半音。

逐个代入（基值 61 = C#4，按 C4=60 的 MIDI 命名；Block=4 时 `(Block-4)*12 = 0`）：

| KC 低 4 位 n | (n>>2)*3 | n&3 | 结果 | 音名（Block=4） |
|---|---|---|---|---|
| 0 | 0 | 0 | 61 | C#4 |
| 1 | 0 | 1 | 62 | D4 |
| 2 | 0 | 2 | 63 | D#4 |
| 3 | — | 3 | — | 丢弃（返回 `&HFF`） |
| 4 | 3 | 0 | 64 | E4 |
| 5 | 3 | 1 | 65 | F4 |
| 6 | 3 | 2 | 66 | F#4 |
| 7 | — | 3 | — | 丢弃 |
| 8 | 6 | 0 | 67 | G4 |
| 9 | 6 | 1 | 68 | G#4 |
| 10 | 6 | 2 | 69 | A4 |
| 11 | — | 3 | — | 丢弃 |
| 12 | 9 | 0 | 70 | A#4 |
| 13 | 9 | 1 | 71 | B4 |
| 14 | 9 | 2 | 72 | C5 |
| 15 | — | 3 | — | 丢弃 |

结论：映射序列为 `C# D D# [空] E F F# [空] G G# A [空] A# B C [空]`（`Modules/YM2151.bas:38-44`）。非法值 3/7/11/15 被丢弃，函数返回 `&HFF`。

调用侧（`Modules/YM2151.bas:180`）：

```vb
Note_2(CH) = YM2151FNumToMidi(FNum_2(CH), Block(CH)) + FNum_LSB(CH) / &H40
```

`FNum_2` 存的是寄存器 0x28 的低 4 位（`FNum_MSB(CH) = Data And &HF`，`Modules/YM2151.bas:176`）。注意此处**未检查 `&HFF`**：非法 KC 时会得到 `255 + 分数`，随后在 `DoNoteOn` 里被 `Prog/VGM2MID.bas:211` 的 `If Note_2 > &HFF Then Note_2 = &HFF` 钳成 0xFF，从而走 note-off 路径（`Prog/VGM2MID.bas:212-219`）。

### A.3 八度基准 `(Block - &H4) * 12`

- Block 直接取自寄存器 0x28 的高 4 位：`Block(CH) = Fix(Data / &H10) And &H7`（`Modules/YM2151.bas:175`）。
- Block = 4 时 `(Block-4)*12 = 0`，返回值等于半音基值 61..72，即 **C#4..C5**；也就是说 Block=4 的「基音」是 C#4（MIDI 61），Block 每 +1 升 12 个半音（一个八度）（`Modules/YM2151.bas:49`）。
- 有效输出范围：Block 0 → 13..24（C#0..C1），Block 7 → 97..108（C#7..C7）（按 `Modules/YM2151.bas:33-35, 49` 推算）。

### A.4 KF（寄存器 0x30）与分数半音

`Modules/YM2151.bas:190-201`：

```vb
    Case YM2151_KF To YM2151_KF + &H7
        CH = Register And &H7
        If YM2151_CH_DISABLED(CH) = 1 Then Exit Sub
        
        ' Key Fraction goes from &H00 to &HFC
        ' &H00 is no change, &H100 would be a full semitone
        FNum_LSB(CH) = Fix(Data / &H4) And &H3F
        Note_1(CH) = Note_2(CH)
        Note_2(CH) = YM2151FNumToMidi(FNum_2(CH), Block(CH)) + FNum_LSB(CH) / &H40
        If NoteOn_2(CH) = 1 Then
            DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH)
        End If
```

- `Fix(Data / &H4) And &H3F`：把 8 位 KF 右移 2 位（丢弃低 2 位），再取低 6 位，取值 **0..63**（`Modules/YM2151.bas:196`）。注释说明寄存器满量程是 `&H00`..`&HFC`，对应「一个半音 = &H100」（`Modules/YM2151.bas:194-195`）。
- 小数部分是 `FNum_LSB(CH) / &H40`，即**除以 64**（`&H40 = 64`），不是 63。精度为 **1/64 半音**，最大值 63/64 半音（`Modules/YM2151.bas:198`）。
- VB 的 `/` 是浮点除法，`Note_2` 是 `Double`（`Modules/YM2151.bas:75`），所以音号可以是分数。
- KF 写入单独就会触发音高重算（用当前保存的 `FNum_2` 与 `Block`）；若此前从未写过 0x28，则 `FNum_2 = 0`、`Block = 0`，得到 `61 + (0-4)*12 = 13`（`Modules/YM2151.bas:196-198` 按公式推算）。
- 文件末尾有一段被注释掉的「直接按 KF 发 pitchwheel」写法（`Modules/YM2151.bas:203-207`），未启用。

### A.5 音符触发：0x08 key-on 的边沿检测

`Modules/YM2151.bas:109-126`：

```vb
    Case YM2151_KEY_ON
        CH = Data And &H7
        If YM2151_CH_DISABLED(CH) = 1 Then Exit Sub
        
        Slot(CH) = Fix(Data / &H8) And &HF
        NoteOn_1(CH) = NoteOn_2(CH)
        NoteOn_2(CH) = IIf(Slot(CH) > &H0, &H1, &H0)
        If CBool(NOTE_ON_MODE And &H1) Then
            If NoteOn_2(CH) <> NoteOn_1(CH) And Note_2(CH) <> &H0 Then
                If NoteOn_2(CH) = &H0 Then
                    'MIDI_Event_Write MIDI_NOTE_OFF, CH, Note_2(CH), &H0
                    DoNoteOn Note_1(CH), &HFF, CH, MIDINote(CH), MIDIWheel(CH), 255
                Else
                    'MIDI_Event_Write MIDI_NOTE_ON, CH, Note_2(CH), &H7F
                    DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH), 255
                End If
            End If
        End If
```

- `Slot(CH) = Fix(Data / &H8) And &HF`：取寄存器 0x08 数据的 **bit6..bit3**（4 个运算符 key 位），`Fix(Data/8)` 右移 3 位，`And &HF` 取 4 位（`Modules/YM2151.bas:113`）。`Slot > 0` 表示该通道至少有一个运算符被 key-on。
- `CH = Data And &H7` 取低 3 位通道号（`Modules/YM2151.bas:110`）。
- 边沿检测：`NoteOn_2 <> NoteOn_1`（上一状态存于 `NoteOn_1`，`Modules/YM2151.bas:114-115`），并附加条件 `Note_2 <> &H0`（`Modules/YM2151.bas:117`）。
- `NOTE_ON_MODE` 是常量 `&H1`（`Modules/YM2151.bas:57`），`NOTE_ON_MODE And &H1` 恒真（`Modules/YM2151.bas:116`）。
- 所有 key 事件都以 `Note_Type = 255` 调用 `DoNoteOn`，强制走「重新发 note」路径，不发纯 pitch bend（`Modules/YM2151.bas:120, 123`；`255` 的作用见 `Prog/VGM2MID.bas:186`）。
- key-off 时用 `Note_1` 配 `&HFF` 作为目标音高，即先关音（`Modules/YM2151.bas:120`）。

### A.6 纯改音高：0x28 / 0x30 写入且 key 已开时的 pitch bend 分支

0x28（BLK/FNUM）分支，`Modules/YM2151.bas:171-189`：

```vb
    Case YM2151_BLK_FNUM To YM2151_BLK_FNUM + &H7
        CH = Register And &H7
        If YM2151_CH_DISABLED(CH) = 1 Then Exit Sub
        
        Block(CH) = Fix(Data / &H10) And &H7
        FNum_MSB(CH) = Data And &HF
        FNum_1(CH) = FNum_2(CH)
        FNum_2(CH) = FNum_MSB(CH)
        Note_1(CH) = Note_2(CH)
        Note_2(CH) = YM2151FNumToMidi(FNum_2(CH), Block(CH)) + FNum_LSB(CH) / &H40
```

（后续即前面 A.1 引用的 `If NoteOn_2(CH) = 1 Then DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH)`，`Modules/YM2151.bas:187-189`。）

0x30（KF）分支见 A.4（`Modules/YM2151.bas:199-201`）。两处调用都**不传** `Note_Type`（默认 0），因此进入 `DoNoteOn` 的「先试 pitch bend」路径：

`Prog/VGM2MID.bas:181-208`：

```vb
Public Function DoNoteOn(ByVal Note_1 As Double, ByVal Note_2 As Double, ByVal MIDIChannel As Byte, ByRef MIDINote As Byte, ByRef MIDIWheel As Integer, Optional Note_Type As Byte, Optional NoteVolume As Byte = &H7F) As Boolean

    ' the returned value ist used to detect written notes
    ' (and avoid notes of Length 0)
    
    If Note_Type = 255 Or Note_1 = &HFF Or Note_2 = &HFF Then GoTo DoNoteOn
    If Note_1 = 255 Then MsgBox "foo"
    
    Dim dblTestValue As Double
    dblTestValue = MIDIWheel + ((Note_2 - Note_1) * PITCHWHEEL_STEPS)
    
    'If Abs(Note_2 - Note_1) >= 0.5 Then
    '    dblTestValue = -1
    'End If
    
    Select Case dblTestValue
    Case MIDI_PITCHWHEEL_MIN To MIDI_PITCHWHEEL_MAX + 1
        If dblTestValue = MIDI_PITCHWHEEL_MAX + 1 Then
            dblTestValue = MIDI_PITCHWHEEL_MAX
        End If
        MIDIWheel = dblTestValue
        MIDI_Event_Write MIDI_PITCHWHEEL, MIDIChannel, MIDIWheel
    Case Else
        GoTo DoNoteOn
    End Select
        
    DoNoteOn = False    ' only PitchBend written
    Exit Function
```

**只发 pitch bend、不重触发音符的条件**（依据 `Prog/VGM2MID.bas:186-208`）：

1. `Note_Type <> 255`（即调用方没有强制；0x28/0x30 分支满足）；
2. `Note_1 <> &HFF` 且 `Note_2 <> &HFF`（`Prog/VGM2MID.bas:186`）；
3. 计算出的目标弯音值 `MIDIWheel + (Note_2-Note_1)*PITCHWHEEL_STEPS` 落在 `MIDI_PITCHWHEEL_MIN..MIDI_PITCHWHEEL_MAX+1`，即 `0..&H4000`（`Prog/VGM2MID.bas:190, 196-202`；常量见 `Modules/Midi_f0.bas:181-183`）；等于上限时钳到 `&H3FFF`。

默认弯音灵敏度为 2 半音（`Prog/frmOptions.frm:2512, 2593`，`Choose(...,2,4,12,24,48,64)` 取第一项 2），`PITCHWHEEL_STEPS = 8192 / 2 = 4096`（`Prog/frmOptions.frm:2594`），中心 `&H2000`（`Modules/Midi_f0.bas:182`）。所以默认下累计弯音必须在 ±2 半音以内，否则条件 3 失败，走 `GoTo DoNoteOn` 重新发 note-off + note-on（`Prog/VGM2MID.bas:210-243`）。

### A.7 YM2151 其它事实（便于对照）

- 音高来源只有寄存器 0x28（Block + FNum MSB）与 0x30（KF），没有任何振荡器频率换算（`Modules/YM2151.bas:171-201`）；文件里 `Clock2151` 被读入（`Prog/VGM.cls:321`）但不参与 YM2151 音高计算（在 `Modules/YM2151.bas` 全文未出现）。
- 音量取自寄存器 0x60..0x7F 中 slot 索引 3 的 TL：`OP = Fix((Register And &H1F) / &H8)`，`If OP < 3 Then Exit Sub`（`Modules/YM2151.bas:219-225`），转换 `DB2MidiVol(YM3812_Vol2DB(TL) / 4)`（`Modules/YM2151.bas:238`）；`YM3812_Vol2DB(TL) = -TL * 4 / 3`（`Modules/YM3812.bas:298-300`），故 dB = `-TL/3`。仅在数值变化时发出 CC7（`Modules/YM2151.bas:239-242`）。
- 音色（Program Change）= 寄存器 0x20 的 `Data And &H3F`（`Modules/YM2151.bas:167-170`）。
- 声像取 0x20 的 `Fix(Data/&H40) And &H3`：1=左、2=右、3/0=中（`Modules/YM2151.bas:145-161`）。
- 调制轮由 `PMS/AMS` 寄存器 0x38 计算：`TempSng = (Data And &H70)/&H70; TempLng = TempSng^2 * &H7F` 写入 CC1（`Modules/YM2151.bas:208-214`）。
- VGM 分派：命令 `&H54` → 读 reg/data → `YM2151Command_Handle bytRegister, bytData`（`Prog/VGM.cls:272, 425-429`）。

---

## B. YM2413 → MIDI

### B.1 `Hz_YM2413` 与 `fsam2413` 的来源

`Modules/YM2413.bas:46-50`：

```vb
Public Function Hz_YM2413(ByVal FNum As Double, Optional Block As Byte) As Double

    If Block = 0 And FNum = 0 Then Hz_YM2413 = 0 Else Hz_YM2413 = FNum * fsam2413 * 2 ^ (Block - 19)

End Function
```

`fsam2413` 是模块级 `Double`（`Prog/VGM2MID.bas:60`），**运行时**由 VGM 头里的 YM2413 时钟赋值：

`Prog/VGM.cls:319, 322`：

```vb
    Clock2413 = vhFileHeader.lngHz2413 And &H3FFFFFFF
...
    fsam2413 = Clock2413 / 72
```

常量版本（在 `Prog/VGM2MID.bas` 中定义，但 `Hz_YM2413` 并未引用）：

`Prog/VGM2MID.bas:33-35`：

```vb
Public Const OSC1 = 53693100
Public Const fM_YM2413_PSG = OSC1 / 15
Public Const fsam_YM2413_PSG = fM_YM2413_PSG / 72
```

实际数字：

- `OSC1 = 53693100`
- `fM_YM2413_PSG = 53693100 / 15 = 3579540`
- `fsam_YM2413_PSG = 3579540 / 72 ≈ 49715.833…`（VB 的 `/` 是浮点除法，因此不是 49716 的整数）
- 同一组常量在 `Modules/FM.bas:5-7` 重复定义了一次（该文件是另一套 `DoNoteOn` 实现，未被 YM2413 路径调用）。
- 运行时典型值：YM2413 标准时钟 3579545 Hz / 72 ≈ 49716 Hz（`Prog/VGM.cls:322`）。`fsam2413` 的语义是「每个采样点的频率步进基准」，在公式里与 `2^(Block-19)` 配合。

### B.2 FNum / Block 的位域组成

`Modules/YM2413.bas:166-186`：

```vb
    Case YM2413_REG_FNum_LSB To (YM2413_REG_FNum_LSB + 8), _
        YM2413_REG_SUS_KEY_BLOCK_FNum_MSB To (YM2413_REG_SUS_KEY_BLOCK_FNum_MSB + 8)
        'CH = Register - YM2413_REG_SUS_KEY_BLOCK_FNum_MSB
        CH = Register And &HF
        
        If YM2413_CH_DISABLED(CH) = 1 Then Exit Sub
        
        If (Register And &HF0) = YM2413_REG_FNum_LSB Then
            FNum_LSB(CH) = Data
            If YM2413_OPTIMIZED_VGMS = 0 Then
                Exit Sub
            End If
        ElseIf (Register And &HF0) = YM2413_REG_SUS_KEY_BLOCK_FNum_MSB Then
            SUS_ON(CH) = IIf((Data And YM2413_REG_SUS) = 0, 0, 1)
            KEY_ON(CH) = IIf((Data And YM2413_REG_KEY) = 0, 0, 1)
            Block(CH) = ((Data And YM2413_REG_BLOCK) / 2)
            FNum_MSB(CH) = (Data And YM2413_REG_FNum_MSB)
        End If
        
        FNum_1(CH) = FNum_2(CH)
        FNum_2(CH) = (FNum_MSB(CH) * 256) + FNum_LSB(CH)
```

常量（`Modules/YM2413.bas:16, 18-22, 24-26`）：

```vb
Public Const YM2413_REG_FNum_LSB = &H10
Public Const YM2413_REG_SUS_KEY_BLOCK_FNum_MSB = &H20
Public Const YM2413_REG_SUS = 32
Public Const YM2413_REG_KEY = 16
Public Const YM2413_REG_BLOCK = 14
Public Const YM2413_REG_FNum_MSB = 1
...
Public Const YM2413_REG_INST_VOL = &H30
Public Const YM2413_REG_INST = 240
Public Const YM2413_REG_VOL = 15
```

位域结论：

| 寄存器 | 位 | 掩码（十进制/十六进制） | 含义 |
|---|---|---|---|
| 0x10+ch | 7..0 | 0xFF | `FNum_LSB`（FNum 低 8 位） |
| 0x20+ch | bit7..bit6 | — | 代码中未使用 |
| 0x20+ch | bit5 | `YM2413_REG_SUS = 32 = 0x20` | SUS（按键延音/持续） |
| 0x20+ch | bit4 | `YM2413_REG_KEY = 16 = 0x10` | KEY ON（`Data And 16 <> 0` 为开） |
| 0x20+ch | bit3..bit1 | `YM2413_REG_BLOCK = 14 = 0x0E` | BLOCK，代码再 `/2` → 0..7 |
| 0x20+ch | bit0 | `YM2413_REG_FNum_MSB = 1 = 0x01` | FNum 第 9 位（MSB） |
| 0x30+ch | bit7..bit4 | `YM2413_REG_INST = 240 = 0xF0` | 乐器号，`\16` 后取值 |
| 0x30+ch | bit3..bit0 | `YM2413_REG_VOL = 15 = 0x0F` | 音量（TL，0..15） |

- `FNum = FNum_MSB * 256 + FNum_LSB`（`Modules/YM2413.bas:186`），即 9 位 FNum（0..511）。
- `Block = (Data And 14) / 2`（`Modules/YM2413.bas:181`），得 0..7。
- 通道号 `CH = Register And &HF`（`Modules/YM2413.bas:169`）。
- 关键行为：普通（非 optimized）VGM 里，只写 0x10（FNum LSB）时直接 `Exit Sub`，不更新音高；只有写 0x20 时才把 LSB+MSB 合成 FNum 并重算（`Modules/YM2413.bas:173-178`）。只有勾选 "optimized VGMs" 选项（`YM2413_OPTIMIZED_VGMS = 1`）时，单独写 0x10 也会继续往下算（`Prog/frmOptions.frm:2544, 2638`，默认未勾选）。

### B.3 `Note(Hz)` 公式与浮点音号拆分

`Prog/VGM2MID.bas:246-250`：

```vb
Public Function Note(ByVal Hz As Double) As Double
    
    If Hz = 0 Then Note = &HFF Else Note = ((Log(Hz) - Log(440)) / (Log(2 ^ (1 / 12)))) + 69

End Function
```

调用：`Hz_2(CH) = Hz_YM2413(FNum_2(CH), Block(CH))`，`Note_2(CH) = Note(Hz_2(CH))`（`Modules/YM2413.bas:189, 192`）。所以 YM2413 的 MIDI 音号是连续的实数（A4=69 为基准，12 平均律），不是查表。

`DoNoteOn` 中的拆分（`Prog/VGM2MID.bas:221-243`）：

```vb
    Dim dblPitchWheel As Double
    Dim NewWheel As Integer
    dblPitchWheel = (Note_2 - Fix(Note_2))
    Note_2 = Abs(Fix(Note_2))
    If dblPitchWheel >= 0.5 Then
        Note_2 = Note_2 + 1
        dblPitchWheel = -1 + dblPitchWheel
    End If
    If MIDINote < &HFF Then
        MIDI_Event_Write MIDI_NOTE_OFF, MIDIChannel, MIDINote, &H0
    End If
    NewWheel = MIDI_PITCHWHEEL_CENTER + (dblPitchWheel * PITCHWHEEL_STEPS)
    If NewWheel <> MIDIWheel Then
        MIDIWheel = NewWheel
        If Not (MIDIChannel = CHN_DAC And Abs(dblPitchWheel) < 0.1) Then
            MIDI_Event_Write MIDI_PITCHWHEEL, MIDIChannel, MIDIWheel
        End If
    End If
    MIDINote = Note_2
    MIDI_Event_Write MIDI_NOTE_ON, MIDIChannel, MIDINote, NoteVolume
```

要点：

- `dblPitchWheel = Note_2 - Fix(Note_2)` 取小数部分；整数部分成为 MIDI note（`Prog/VGM2MID.bas:223-224`）。
- 小数 ≥ 0.5 时把 note 进位 +1，弯音改为 `-1 + 小数`（回到 [-0.5, 0) 区间），使弯音偏移绝对值 ≤ 0.5 半音（`Prog/VGM2MID.bas:225-228`）。
- `NewWheel = MIDI_PITCHWHEEL_CENTER + dblPitchWheel * PITCHWHEEL_STEPS`（`Prog/VGM2MID.bas:232`），默认 `STEPS = 4096`（`Prog/frmOptions.frm:2594`），即 1 个弯音单位 = 1/4096 半音。
- 鼓通道（`CHN_DAC = 9`，`Prog/VGM2MID.bas:6`）在小数偏移 < 0.1 时不发弯音（`Prog/VGM2MID.bas:235-237`）。

### B.4 KEY_ON（bit4）的边沿检测

`Modules/YM2413.bas:178-211`：

```vb
        ElseIf (Register And &HF0) = YM2413_REG_SUS_KEY_BLOCK_FNum_MSB Then
            SUS_ON(CH) = IIf((Data And YM2413_REG_SUS) = 0, 0, 1)
            KEY_ON(CH) = IIf((Data And YM2413_REG_KEY) = 0, 0, 1)
            Block(CH) = ((Data And YM2413_REG_BLOCK) / 2)
            FNum_MSB(CH) = (Data And YM2413_REG_FNum_MSB)
        End If
        
        FNum_1(CH) = FNum_2(CH)
        FNum_2(CH) = (FNum_MSB(CH) * 256) + FNum_LSB(CH)
        
        Hz_1(CH) = Hz_2(CH)
        Hz_2(CH) = Hz_YM2413(FNum_2(CH), Block(CH))
        
        Note_1(CH) = Note_2(CH)
        Note_2(CH) = Note(Hz_2(CH))
        
        NoteOn_1(CH) = NoteOn_2(CH)
        If (Register And &HF0) = YM2413_REG_SUS_KEY_BLOCK_FNum_MSB Then
            NoteOn_2(CH) = KEY_ON(CH)
        End If
        
        If KEY_ON(CH) = 0 Then
            If NoteOn_1(CH) <> 0 And Note_1(CH) <> 0 Then
                MIDI_Event_Write MIDI_NOTE_OFF, CH, MIDINote(CH), &H0
                MIDINote(CH) = &HFF
            End If
        ElseIf KEY_ON(CH) = 1 Then
            If NoteOn_2(CH) <> NoteOn_1(CH) Then
                DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH), 255
            Else 'If (Register And &HF0) = YM2413_REG_SUS_KEY_BLOCK_FNum_MSB And _
                    Note_1(CH) <> Note_2(CH) Then
                DoNoteOn Note_1(CH), Note_2(CH), CH, MIDINote(CH), MIDIWheel(CH)
            End If
        End If
```

- `KEY_ON(CH) = IIf((Data And 16) = 0, 0, 1)`（`Modules/YM2413.bas:180`）：bit4 即 KEY ON 位。
- 边沿由 `NoteOn_1`（上一次）与 `NoteOn_2`（本次）比较实现，且 `NoteOn_2` 只在写 0x20 时被更新（`Modules/YM2413.bas:194-197`）。
- 沿变 0→1：以 `Note_Type = 255` 调用，强制 note-off + note-on 重触发（`Modules/YM2413.bas:205-206`）。
- key 保持为 1 且只是音高变化：不带 `Note_Type` 调用，走 `DoNoteOn` 的「能弯音就弯音」路径（`Modules/YM2413.bas:207-209`）。
- key off：直接发 `MIDI_NOTE_OFF` 用保存的 `MIDINote`，并置 `MIDINote = &HFF`（`Modules/YM2413.bas:199-203`）。
- `SUS_ON` 只被保存（`Modules/YM2413.bas:179`），后续未参与任何 MIDI 输出（全文件中除了 `Erase` 与赋值无其它引用）。

### B.5 YM2413 音量与打击乐（补充事实）

- 音量：`Volume(CH) = Data And &HF`（`Modules/YM2413.bas:256`），`MIDIVolume = DB2MidiVol(YM2413_Vol2DB(Volume))`（`Modules/YM2413.bas:260`），`YM2413_Vol2DB(TL) = -TL * 3`（`Modules/YM2413.bas:306-310`），即每级 3 dB。
- 乐器：`Instrument(CH) = (Data And 240) \ 16`，经 `Select Case` 映射到 `YM2413_MIDI_PATCH(n)`（`Modules/YM2413.bas:217-252`）。
- 打击乐在寄存器 0x0E 上按位处理，且要求 `(Data And 32) <> 0` 才生效：BD=bit4→note 35、SD=bit3→38、TOM=bit2→45、HH=bit0→42、TCT=bit1→46（`Modules/YM2413.bas:83-154`）。
- 鼓音量寄存器 0x36..0x38（`YM2413_REG_INST_VOL + 6..8`），分别取 `Data And 15`、`(Data And 240)\16` 与 `Data And 15`、`(Data And 240)\16` 与 `Data And 15`（`Modules/YM2413.bas:265-290`）。
- VGM 分派：命令 `&H51` → `YM2413Command_Handle bytRegister, bytData`（`Prog/VGM.cls:269, 386-390`）。

---

## C. 时间与 MIDI 写入

### C.1 `MID_File_Init`：TickspQuarter 与 DeltaFact

`Modules/Midi_f0.bas:493-518`：

```vb
Public Sub MID_File_Init()

    Select Case CNV_ACCURACY
    Case &H0
        TickspQuarter = 30
    Case &H10
        TickspQuarter = 480
    Case &H80
        TickspQuarter = 2205
    Case &HFF
        TickspQuarter = 22050
    Case Else
        TickspQuarter = 22050
    End Select
    DeltaFact = 22050 / TickspQuarter
    If TEMPO_MOD = &H0 Then
        DeltaFact = DeltaFact * 120 / TEMPO_BPM
    ElseIf TEMPO_MOD = &H1 Then
        DeltaFact = DeltaFact * TEMPO_MULT / TEMPO_DIV
    End If
    WrittenDelayPos = &H0
    CurSamplePos = &H0
    MID_TrackSize = &H0
    Erase MID_Trackdata()

End Sub
```

四个取值与选项界面对应（`Prog/frmOptions.frm:176-212`，索引 0..3 = "Frame (30)" / "Good (480)" / "High (2205)" / "Sample (22050)"；`Prog/frmOptions.frm:2600` 的 `Choose(..., &H0, &H10, &H80, &HFF)`）：

| `CNV_ACCURACY` | `TickspQuarter` | UI 名称 | `DeltaFact = 22050/TickspQuarter` |
|---|---|---|---|
| `&H0` | 30 | Frame (30) | 735 |
| `&H10` | 480 | Good (480) | 45.9375 |
| `&H80` | 2205 | High (2205) | 10 |
| `&HFF` | 22050 | Sample (22050) | 1 |

默认取 `optConversionAccuracy(1).Value = True`（`Prog/frmOptions.frm:2518`）→ 索引 1 → `&H10` → **480 ticks/quarter**（`Prog/frmOptions.frm:2600`）。

22050 的来历（数字层面的对应关系）：

- VGM 的等待量以 44100 Hz 采样为单位：`dblWait` 直接取 WAIT 命令的采样数（`Prog/VGM.cls:470-479`），并累加进 `DeltaTime`（`Prog/VGM.cls:603`）。GYM 路径里 1 帧固定 735 采样 = 44100/60（`Prog/VGM.cls:191-192`）。
- 22050 = 44100/2，即 0.5 秒对应的采样数；120 BPM 下四分音符正好是 0.5 秒（tempo meta 默认 500000 µs）。所以「1 quarter = 22050 采样」正好对齐 44100 Hz 时基与 120 BPM。
- `DeltaFact` 的单位因此是「每个 tick 对应的采样数」：`samples = ticks * DeltaFact`（在 `MIDI_Event_Write` 里用除法体现，见 C.2）。
- 若选了更低的 `TickspQuarter`（480/2205/30），一个 tick 要覆盖更多采样，量化误差随之增大。
- `TEMPO_MOD = 0` 时用 `DeltaFact * 120 / TEMPO_BPM` 直接把时间轴按 BPM 缩放（`Modules/Midi_f0.bas:508-509`）；`TEMPO_MOD = 1` 时用 `TEMPO_MULT/TEMPO_DIV`（`Modules/Midi_f0.bas:510-511`）。默认 `TEMPO_MOD = 0`、`TEMPO_BPM = 120`（`Prog/frmOptions.frm:2522-2523`），此时 DeltaFact 不变。

`MID_File_Init` 在每次转换前被调用一次（`Prog/frmMain.frm:175`）。

### C.2 `MIDI_Event_Write` 的绝对值取整（无漂移）

`Modules/Midi_f0.bas:625-641`：

```vb
    CurSamplePos = CurSamplePos + DeltaTime
    DeltaTime = 0
    TempLng = Int(CurSamplePos / DeltaFact + 0.5)
    DeltaVal = TempLng - WrittenDelayPos
    WrittenDelayPos = WrittenDelayPos + DeltaVal
    
    If DeltaVal >= &H80 Then
        ' New Delay-Calculation
        DeltaLen = ValueToMIDI(DeltaVal, DeltaArr())
        'ReDim Preserve MID_Trackdata(MidFilePos + DeltaLen - 2) As Byte
        Call MIDI_Resize(MidFilePos + DeltaLen - 1)
        For TempLng = &H0 To DeltaLen - 2
            MID_Trackdata(MidFilePos) = DeltaArr(TempLng)
            MidFilePos = MidFilePos + 1
        Next TempLng
        DeltaVal = DeltaArr(DeltaLen - 1)
    End If
```

为什么无漂移：

- `CurSamplePos` 是**累计**采样位置（`Modules/Midi_f0.bas:625`；初值 0 见 `Modules/Midi_f0.bas:514`）。
- `TempLng = Int(CurSamplePos / DeltaFact + 0.5)` 是「累计位置 → 绝对 tick」的四舍五入结果，**每次都对累计量取整**，误差不继承（`Modules/Midi_f0.bas:627`）。
- `DeltaVal = TempLng - WrittenDelayPos` 是「本事件绝对 tick 减去已写出的 delta 总和」，随后 `WrittenDelayPos = WrittenDelayPos + DeltaVal`，即 `WrittenDelayPos` 恒等于最后一次写出的绝对 tick（`Modules/Midi_f0.bas:628-629`）。
- 因此每个事件的 delta 误差上限为 ±0.5 tick，且不会逐事件累积（因为从不基于上一次舍入后的值继续累加）。
- 变长量编码：`DeltaVal >= &H80` 时先写入前面的变长字节，最后 1 字节留作事件自身的 delta（`Modules/Midi_f0.bas:631-641`）。变长编码实现在 `ValueToMIDI`（`Modules/Midi_f0.bas:584-604`），7 位一组、低位在前、高位字节置 bit7。

`DeltaTime` 由 VGM 主循环累加：`DeltaTime = DeltaTime + dblWait`（`Prog/VGM.cls:603`），并被 `MIDI_Event_Write` 清零（`Modules/Midi_f0.bas:626`）。

### C.3 默认 BPM 与 tempo meta 事件

写入位置 `Prog/VGM2MID.bas:96-108`：

```vb
Public Sub MID_Data_Init(ByVal PSGOn As Boolean)
    
    Dim CH As Byte
    Dim TempoVal As Double
    
    'MIDI_Event_Write MIDI_META_EVENT, META_TEMPO, 500000
    TempoVal = 500000
    If TEMPO_MOD = &H0 Then
        TempoVal = TempoVal * 120 / TEMPO_BPM
    ElseIf TEMPO_MOD = &H1 Then
        TempoVal = TempoVal * TEMPO_MULT / TEMPO_DIV
    End If
    MIDI_Event_Write MIDI_META_EVENT, META_TEMPO, TempoVal
```

- 默认 tempo 值 500000 µs/quarter = 120 BPM（`Prog/VGM2MID.bas:102`）；UI 默认 BPM 文本 "120"（`Prog/frmOptions.frm:76, 2523`），默认 `TEMPO_MOD = 0`（`Prog/frmOptions.frm:2522`）。`TEMPO_MOD = 0` 时 `500000 * 120 / TEMPO_BPM`，BPM=120 时仍为 500000（`Prog/VGM2MID.bas:103-104`）。
- 编码为 3 字节大端，事件格式 `FF 51 03 tttttt`：`Modules/Midi_f0.bas:759-773`：

```vb
        Case META_TEMPO:
            TempLng = CLng(Param3)
            
            'ReDim Preserve MID_Trackdata(MidFilePos + 6) As Byte
            Call MIDI_Resize(MidFilePos + 7)
            
            MID_Trackdata(MidFilePos) = DeltaVal
            MID_Trackdata(MidFilePos + 1) = Command
            MID_Trackdata(MidFilePos + 2) = EventType
            MID_Trackdata(MidFilePos + 3) = &H3 ' Event Length
            MID_Trackdata(MidFilePos + 4) = Int(TempLng / &H10000) And &HFF
            MID_Trackdata(MidFilePos + 5) = Int(TempLng / &H100&) And &HFF
            MID_Trackdata(MidFilePos + 6) = Int(TempLng / &H1&) And &HFF
            
            MidFilePos = MidFilePos + 7
```

- 调用顺序：`Prog/VGM.cls:336` 清零 `DeltaTime` → `337` `SegaPCM_Init` → `338` `WriteMidiTag`（GD3 标签会写成 META_TRACK_NAME / META_TEXT，`Prog/VGM.cls:756, 767, 772, 776, 779`）→ `339` `MID_Data_Init`。因此若 VGM 带 GD3 标签，tempo 事件前面还会有 delta=0 的文本类 meta 事件；tempo 本身 delta 为 0（`DeltaTime` 尚未被任何 WAIT 累加）。
- 文件头/轨头：Format 0、1 轨、`NumTicks = TickspQuarter`（`Modules/Midi_f0.bas:531-538`）；写文件前追加 `FF 2F 00`（`Modules/Midi_f0.bas:528`）。
- 循环点用 text meta `loopStart` / `loopEnd` 标记（`Prog/VGM.cls:609, 615, 656`；常量 `Modules/Midi_f0.bas:128-129`）。

### C.4 音量换算的辅助函数

`Modules/Midi_f0.bas:991-1008`：

```vb
Public Function DB2MidiVol(ByVal DB As Single) As Byte

    Dim TempSng As Single
    
    TempSng = 10# ^ (DB / 40#)
    DB2MidiVol = TempSng * &H7F

End Function

Public Function Lin2DB(ByVal LinearVal As Single) As Single

    If LinearVal > 0! Then
        Lin2DB = Log(LinearVal) / Log(2) * 6
    Else
        Lin2DB = -400   ' results in volume 0
    End If

End Function
```

- `DB2MidiVol`：0 dB → 127，-40 dB → 12.7（线性幅度 0.1 × 127）。
- `Lin2DB = 6 * log2(x)`，等价于 `20 * log10(x)` 的常用近似（6.0206 ≈ 20/log10(2)... 实际 `6*log2(x)` 与 `20*log10(x)` 相差约 0.34%；代码就是 6 倍 log2）。
- `Lin2DB` 只被 `Modules/basNESAPU.bas:118` 与 `Modules/GameBoy.bas:124` 调用，YM2151/YM2413 路径**未使用**（grep 结果）。

---

## D. 总结表

| 项 | YM2151 | YM2413 | 代码位置 |
|---|---|---|---|
| 音高来源寄存器 | 0x28（BLK + FNum MSB 4 位）、0x30（KF，用高 6 位） | 0x10（FNum LSB 8 位）、0x20（FNum bit8 + BLOCK 3 位） | `Modules/YM2151.bas:171-201`；`Modules/YM2413.bas:166-186` |
| 音高公式 | 查表：`61 + (n>>2)*3 + (n&3)`，`n = KC 低 4 位`，再 `+ (Block-4)*12`；`n&3 == 3` 时返回 `&HFF` | `Hz = FNum * fsam2413 * 2^(Block-19)`，再 `Note = (log2(Hz/440))*12 + 69` | `Modules/YM2151.bas:37-49, 180`；`Modules/YM2413.bas:48, 186-192`；`Prog/VGM2MID.bas:246-250` |
| 八度基准 | Block=4 → MIDI 61（C#4…C5）；Block 0..7 钳位 | 指数项 `2^(Block-19)`，Block 0..7；`Block=0 且 FNum=0` 时频率记 0 | `Modules/YM2151.bas:33-35, 49`；`Modules/YM2413.bas:46-48` |
| 分数半音精度 | KF 取 `Fix(Data/4) And &H3F` = 0..63，除以 64（`&H40`）→ 1/64 半音 | 无寄存器分数；`Hz → Note` 是实数，靠弯音表示，默认 1/4096 半音（`PITCHWHEEL_STEPS = 8192/2`） | `Modules/YM2151.bas:196-198`；`Prog/VGM2MID.bas:221-232`；`Prog/frmOptions.frm:2593-2594` |
| key-on 判定 | 0x08：`Slot = Fix(Data/8) And &HF`，`Slot > 0` 为开；`NoteOn_2 <> NoteOn_1` 且 `Note_2 <> 0` 触发 | 0x20 bit4：`KEY_ON = (Data And 16) <> 0`；`NoteOn_1/NoteOn_2` 边沿比较，只在写 0x20 时更新 | `Modules/YM2151.bas:109-126`；`Modules/YM2413.bas:178-211` |
| 音量来源 | 0x60..0x7F 中 slot 索引 3 的 TL（`OP = (Reg And &H1F)/8`，`OP<3` 直接返回），`DB2MidiVol(-TL/3)` → CC7 | 0x30 低 4 位 `Volume`，`DB2MidiVol(-Volume*3)` → CC7；鼓音量在 0x36..0x38 | `Modules/YM2151.bas:219-242`；`Modules/YM2413.bas:255-262, 265-290, 306-310`；`Modules/Midi_f0.bas:991-998` |
| 音符切分判据 | key 沿：强制 `Note_Type=255`（先 note-off 再 note-on）；key 已开且仅音高变化：`DoNoteOn` 若目标弯音落在 0..&H3FFF 则只发 pitchwheel，否则重发 note | key 沿 0→1：`Note_Type=255` 重触发；key 保持：不带 `Note_Type` → 弯音够用就弯音，超出范围则 note-off + note-on；key=0：直接 note-off | `Prog/VGM2MID.bas:181-243`；`Modules/YM2151.bas:116-126, 187-201`；`Modules/YM2413.bas:199-211` |

---

## 附：本文未找到 / 代码中未见的内容

- YM2151 的振荡器时钟（`Clock2151`，`Prog/VGM.cls:321`）**未参与**音高计算；`Modules/YM2151.bas` 全文没有 Hz/频率换算，只有查表 + KF 分数。
- YM2413 的 `SUS_ON` 只保存不使用（`Modules/YM2413.bas:179`，无其它引用）。
- YM2413 音高公式里 `fsam2413` 在 `Prog/VGM.cls:322` 由 `Clock2413 / 72` 赋值；`Prog/VGM2MID.bas:33-35` 的 `OSC1 / fM_YM2413_PSG / fsam_YM2413_PSG` 常量虽定义，但 `Hz_YM2413` 未使用（`Modules/YM2413.bas:48` 只用变量 `fsam2413`）。
- `Modules/FM.bas:1-30` 另有一套 `DoNoteOn`（使用 `PITCHBEND_STEPS`、`MIDI_PITCH_WHEEL` 等不同的全局名）。该文件**不在** `Prog/vgm2mid.vbp` 的模块清单里（`Prog/vgm2mid.vbp:4-19` 只列出 YM2413/SN76489/CommDlg/VGM2MID/YM2612/Win32API/Midi_f0/YM2151/PCM/YM3812/YMF278/GameBoy/basNESAPU/AY8910 等，无 FM.bas），因此不参与本工程的链接与调用。
- YM2151 `YM2151FNumToMidi` 中的 `Stop`（`Modules/YM2151.bas:50`）在任何有效输入下都不会命中。
- tempo meta 的 `META_TEMPO` 只有一处写入（`Prog/VGM2MID.bas:108`），转换过程中没有中途变速。
