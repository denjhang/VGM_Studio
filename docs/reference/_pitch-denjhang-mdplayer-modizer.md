# 三方音高/键位实现事实对照（Denjhang / MDPlayer / modizer）

本文只记录实际读到的代码与行号，不含设计推断。所有行号均为读取时的真实行号。

代码根：

- Denjhang：`E:\working\vscode-projects\YM2163-Midi\Denjhang_Music_Player_v16`
- MDPlayer：`E:\working\vscode-projects\Reference_Project\vgm_libs\MDPlayer-STBL449\MDPlayer\MDPlayerx64`
- modizer：`E:\working\vscode-projects\Reference_Project\vgm_libs\modizer-master-20260408\modizer-master`

---

# 一、Denjhang（YM2151/YM2413 + MIDI 库）

## 1.1 YM2151 key-on

位置：`src\windows\vgm\vgm_window.cpp:1333-1352`（`case DEVID_YM2151`，在 VGM 事件分发里）。

```cpp
            case DEVID_YM2151: {
                int c = ev.chip_num & 1;
                if (ev.addr == 0x08) {
                    // YM2151 keyon strobe: bits0-2=ch, bits6-4=KeyOn bits for slots 1-4
                    int ch = ev.data & 0x07;
                    bool new_kon = (ev.data & 0x78) != 0;
                    if (new_kon && !s_ym2151KeyOn[c][ch]) {
                        s_ym2151_viz[c][ch].key_on_event = true;
                        s_ym2151_viz[c][ch].decay = 1.0f;
                        s_ym2151KeyOff[c][ch] = false;
                        // VgmLog("[2151] ch=%d keyon\n", ch+1);
                    } else if (!new_kon && s_ym2151KeyOn[c][ch]) {
                        s_ym2151KeyOff[c][ch] = true;
                        // VgmLog("[2151] ch=%d keyoff\n", ch+1);
                    }
                    s_ym2151KeyOn[c][ch] = new_kon;
                }
                s_shadowYM2151[c][ev.addr] = ev.data;
                break;
            }
```

（`src\windows\vgm\vgm_window.cpp:1333-1352`）

要点（均据上述代码）：

- 触发寄存器固定为 `0x08`（`vgm_window.cpp:1335`）。
- 通道号取低 3 位 `ev.data & 0x07`（`vgm_window.cpp:1337`）。
- key-on 判定：`(ev.data & 0x78) != 0`，即 4 个 slot 的 key 位（bit4-6）中**任意一位**为 1 就算按下，不区分具体哪个 operator（`vgm_window.cpp:1338`）。
- 采用「状态迁移」：只有 `0 → 1` 才置 `key_on_event`，`1 → 0` 才置 `s_ym2151KeyOff`（`vgm_window.cpp:1339-1347`）。

## 1.2 YM2151 音高（KC/KF 查表）

位置：`src\windows\vgm\vgm_window.cpp:3400-3407`。

```cpp
                    // KC -> coarse note
                    UINT8 kc = s_shadowYM2151[c][0x28 + ch];
                    int oct      = (kc >> 4) & 0x07;
                    int note_idx = kc & 0x0F;
                    int semi = (note_idx < 3) ? note_idx : (note_idx < 7 ? note_idx - 1 : (note_idx < 11 ? note_idx - 2 : note_idx - 3));
                    // KF fine tune (6-bit, reg 0x30+ch bits7:2)
                    int kf = (s_shadowYM2151[c][0x30 + ch] >> 2) & 0x3F;
                    float target_note = (float)(oct * 12 + semi + 24) + kf / 64.0f;  // +24 = 2 octaves up
```

（`src\windows\vgm\vgm_window.cpp:3400-3407`）

要点：

- KC 来自 `0x28 + ch`（`vgm_window.cpp:3401`）。
- 八度 = `(kc >> 4) & 0x07`，0..7（`vgm_window.cpp:3402`）。
- KC 低 4 位是「12 音里去掉 1 个空隙」的排布，用嵌套三目把 0..15 映射成半音 0..11：0,1,2 → 0,1,2；3..6 → 2..5；7..10 → 5..8；11..15 → 8..12（`vgm_window.cpp:3403-3404`）。
- KF 取 `0x30 + ch` 的 bit7..2（6 bit，`>> 2 & 0x3F`，`vgm_window.cpp:3406`）。
- 最终音高 = `oct*12 + semi + 24 + kf/64.0f`，即 KF 以 1/64 半音为单位线性加到音高上，`+24` 是把 OPM 的八度 0 抬到 MIDI 音域（`vgm_window.cpp:3407`）。
- 该音高是**寄存器直接换算**，不经频率、不经查表（`vgm_window.cpp:3401-3407`）。

音符上屏前还有一次 clamp（`vgm_window.cpp:3442-3443`）：

```cpp
                    int nt = kon ? (int)roundf(vs.start_note) : -1;
                    if (nt >= 0) { if (nt > 127) nt = 127; }
```

（`src\windows\vgm\vgm_window.cpp:3442-3443`）

## 1.3 YM2413 音高

位置：`src\windows\vgm\vgm_window.cpp:3456-3463`。

```cpp
                for (int ch = 0; ch < 9; ch++) {
                    UINT8 lo   = s_shadowYM2413[c][0x10 + ch];
                    UINT8 hi   = s_shadowYM2413[c][0x20 + ch];
                    UINT8 vol  = s_shadowYM2413[c][0x30 + ch] & 0x0F;
                    int fnum2413 = ((hi & 0x01) << 8) | lo;
                    int blk2413  = (hi >> 1) & 0x07;
                    int nt = (fnum2413 > 0) ? FrequencyToMIDINote(
                        (float)fnum2413 * (float)clk / (float)(72 * (1 << (19 - blk2413)))) : -1;
```

（`src\windows\vgm\vgm_window.cpp:3456-3463`）

要点：

- F-number = `0x10+ch`（低 8 位）拼 `0x20+ch` 的 bit0 作最高位 → 9 bit（`vgm_window.cpp:3460`）。
- BLOCK = `0x20+ch` 的 bit3..1（`(hi >> 1) & 0x07`，`vgm_window.cpp:3461`）。
- 频率公式：`fnum * clk / (72 * 2^(19 - block))`，clk 默认 3579545（`vgm_window.cpp:3453`、`3462-3463`）。
- 再经 `FrequencyToMIDINote` 转 MIDI 音号（`vgm_window.cpp:3462`）。

`FrequencyToMIDINote` 定义在 `libvgm-modizer\chip_params.h:155-162`：

```cpp
inline int FrequencyToMIDINote(float frequency) {
    if (frequency <= 0.0f) return -1;
    float note = 12.0f * log2f(frequency / 440.0f) + 69.0f;
    int midiNote = (int)(note + 0.5f);  // Round to nearest
    if (midiNote < 0) return 0;
    if (midiNote > 127) return 127;
    return midiNote;
}
```

（`libvgm-modizer\chip_params.h:155-162`）

YM2413 key-on 判定在 `vgm_window.cpp:1353-1373`，取 `0x20..0x28` 的 bit4 作 KEY，同样做上升/下降沿判定：

```cpp
                    if (ev.addr >= 0x20 && ev.addr <= 0x28) {
                        int ch2413 = ev.addr - 0x20;
                        bool prev_kon = (s_shadowYM2413[c][ev.addr] >> 4) & 1;
                        bool new_kon  = (ev.data >> 4) & 1;
```

（`src\windows\vgm\vgm_window.cpp:1357-1360`）

## 1.4 MIDI 库的情况（库名、路径、API、能否写）

- 库名：Craig Sapp 的 `smf::MidiFile`（midifile / Midifile 库）。
  证据：`src\midi\midi_player.h:12` 包含 `#include "midifile/include/MidiFile.h"`，`src\midi\midi_player.h:16-18` 使用 `smf::MidiFile` / `smf::MidiEvent` / `smf::MidiEventList` 命名空间。
- 源码路径（CMake 显式编译）：`CMakeLists.txt:62-68` 编译 `midifile/src/Binasc.cpp`、`MidiEvent.cpp`、`MidiEventList.cpp`、`MidiFile.cpp`、`MidiMessage.cpp`、`Options.cpp`；头文件路径 `CMakeLists.txt:37`。
- 头文件目录：`midifile/include/` 下为 `Binasc.h`、`MidiEvent.h`、`MidiEventList.h`、`MidiFile.h`、`MidiMessage.h`、`Options.h`。

读取方式：

- 载入：`g_midiPlayer.midiFile.read(...)`（`src\midi\midi_player.cpp:419` Windows 宽字符重载、`:432` 其他平台），随后 `makeAbsoluteTicks()`（`:446`）与 `joinTracks()`（`:447`）。
- 事件遍历是按「合并到 track 0 后的索引」顺序取的，不是 `getEvent`：

```cpp
    MidiEventList& track = g_midiPlayer.midiFile[0];

    // Advance time using high-precision counter
    ...
    while (g_midiPlayer.currentTick < (int)track.size()) {
        MidiEvent& event = track[g_midiPlayer.currentTick];
```

（`src\midi\midi_player.cpp:726`、`737-738`）

- 使用的判定/取值 API：`event.isNoteOn()`、`event.isNoteOff()`、`event.isTempo()`、`event.isController()`、`event.getChannel()`、`event.getKeyNumber()`、`event.getVelocity()`、`event.getTempoMicroseconds()`、`event[1]` / `event[2]`（`midi_player.cpp:743-746`、`811-813`、`819`、`822-823`）。
- `MidiMessage` 类在本项目源码中**未被直接使用**（`midi_player.h` 没有 `using smf::MidiMessage`，`.cpp` 里也没有 `getMessage` 调用；头文件 `MidiMessage.h` 只是库的一部分）。

能否写 MIDI：

- 库本身具备写能力：`midifile/include/MidiFile.h:75-85` 声明了 `write(const std::string&)`、`write(std::ostream&)`、`writeBase64`、`writeHex`、`writeBinasc` 等。
- 但 Denjhang 项目**只读不写**：全仓库 `src/` 中没有对 `midiFile.write` / `writeBase64` / `addNoteOn` / `addNoteOff` 的任何调用（检索 `src\midi\`、`src\windows\` 仅有 `midifile` 的 `read` 调用，`src\midi\midi_player.cpp` 中 `.read(` 出现 2 次，即 `:419` 与 `:432`）。
- 结论：Denjhang 走的是「读 SMF → 转 YM2163 寄存器」，没有 MIDI 回写链路。

---

# 二、MDPlayer

## 2.1 YM2151 key-on 与音高

### key-on（写入侧，`setYM2151Register`）

位置：`ChipRegister.cs:1249-1269`。

```csharp
                if (dAddr == 0x08) //Key-On/Off
                {
                    int ch = dData & 0x7;
                    if (ch >= 0 && ch < 8)
                    {
                        if ((dData & 0x78) != 0)
                        {
                            byte con = (byte)(dData & 0x78);
                            fmKeyOnYM2151[chipID][ch] = con | 1;
                            fmVolYM2151[chipID][ch] = 256 * 6;
                        }
                        else
                        {
                            fmKeyOnYM2151[chipID][ch] &= 0xfe;
                        }
                    }
                }
```

（`ChipRegister.cs:1252-1268`，所在函数 `setYM2151Register` 起于 `ChipRegister.cs:1214`）

要点：

- key-on 判定同样是 `(dData & 0x78) != 0`，即 SLOT 位任意一位为 1（`ChipRegister.cs:1257`）。
- 但 MDPlayer 存的是 `con | 1`：低 bit 当「key on 标志」，高 4 位保存哪些 slot 被 key（`ChipRegister.cs:1259-1260`）。
- key-off 只清最低位（`fmKeyOnYM2151[chipID][ch] &= 0xfe`，`ChipRegister.cs:1265`），不重置 slot 掩码。
- 同一段逻辑在 `ChipRegister.cs:1555-1568` 的 `setYM2414Register`（YM2414，起于 `ChipRegister.cs:1524`）里重复出现。

### 音高

位置：`form\KB\frmYM2151.cs:218-227`。

```csharp
                int note = (ym2151Register[0x28 + ch] & 0x0f);
                note = (note < 3) ? note : (note < 7 ? note - 1 : (note < 11 ? note - 2 : note - 3));
                int oct = ((ym2151Register[0x28 + ch] & 0x70) >> 4);
                //newParam.ym2151[chipID].channels[ch].note = (fmKeyYM2151[ch] > 0) ? (oct * 12 + note + Audio.vgmReal.YM2151Hosei + 1 + 9) : -1;
                int hosei = 0;
                if (Audio.DriverVirtual != null)//is vgm)
                {
                    hosei = (Audio.DriverVirtual).YM2151Hosei[chipID];
                }
                newParam.channels[ch].note = ((fmKeyYM2151[ch] & 1) != 0) ? (oct * 12 + note + hosei) : -1;
```

（`form\KB\frmYM2151.cs:218-227`）

要点：

- KC 同样取 `0x28 + ch`，低 4 位用完全相同的嵌套三目映射到半音（`frmYM2151.cs:218-219`）。
- 八度 `oct = (reg & 0x70) >> 4`（`frmYM2151.cs:220`）。
- 音高 = `oct * 12 + note + hosei`，`hosei` 是 VGM 时钟补偿（`frmYM2151.cs:222-227`）。**不含 `+24`**，与 Denjhang 的 `+24` 形成两套八度基准。
- key 状态用 `fmKeyYM2151[ch] & 1`，即只认最低位（`frmYM2151.cs:227`）。

KF：读取到但**不进入 note 计算**（`form\KB\frmYM2151.cs:247`）：

```csharp
                newParam.channels[ch].kf = ((ym2151Register[0x30 + ch] & 0xfc) >> 2);
```

（`form\KB\frmYM2151.cs:247`）

注：任务里提到的「254 行」在实读中不是音高行——`frmYM2151.cs:254` 是 `newParam.amd = Audio.GetYM2151AMD(chipID);`。音高公式在 `frmYM2151.cs:227`。

## 2.2 YM2413 key-on 与音高

### key-on

位置：`ChipRegister.cs:1999-2015`（函数 `setYM2413Register`，起于 `ChipRegister.cs:1982`）。

```csharp
            if (dAddr >= 0x20 && dAddr <= 0x28)
            {
                int ch = dAddr - 0x20;
                int k = dData & 0x10;
                if (k == 0)
                {
                    kiYM2413[chipID].Off[ch] = true;
                }
                else
                {
                    if (kiYM2413[chipID].Off[ch]) kiYM2413[chipID].On[ch] = true;
                    kiYM2413[chipID].Off[ch] = false;
                }

                //mask適用
                if (maskFMChYM2413[chipID][ch]) dData &= 0xef;
            }
```

（`ChipRegister.cs:1999-2015`）

要点：

- KEY = bit4（`dData & 0x10`，`ChipRegister.cs:2002`）。
- 只有「之前处于 Off」才置 `On[ch] = true`，即这里也是上升沿语义（`ChipRegister.cs:2009-2010`）。
- 节奏通道在 `0x0E` 上按 bit4..0 逐个处理，见 `ChipRegister.cs:2035-2052`。

### 音高

位置：`form\KB\OPL\frmYM2413.cs:110-113`。

```csharp
                int freq = ym2413Register[0x10 + ch] + ((ym2413Register[0x20 + ch] & 0x1) << 8);
                int oct = ((ym2413Register[0x20 + ch] & 0xe) >> 1);

                nyc.note = Common.searchSegaPCMNote(freq / 172.0) + (oct - 4) * 12;
```

（`form\KB\OPL\frmYM2413.cs:110-113`）

要点：

- 不是先算真频率再取 log，而是**查表**：`freq / 172.0` 作为「倍率」喂给 `searchSegaPCMNote`，再加 `(oct - 4) * 12` 做八度平移（`frmYM2413.cs:113`）。
- `freq` 是 9 bit F-number（低 8 位 + bit0 高位），`oct` 是 BLOCK（bit3..1）。
- 该表达式与 `clk` 无关，`172.0` 是固定常数，即隐含固定采样/时钟基准。

## 2.3 频率→音符的查找表

`common.cs` 里没有 `searchYM2413Note` 之类的函数；三个相关函数如下。

### searchSegaPCMNote（YM2413 用的就是这个）

`common.cs:392-406`：

```csharp
        public static int searchSegaPCMNote(double ml)
        {
            double m = double.MaxValue;
            int n = 0;
            for (int i = 0; i < 12 * 8; i++)
            {
                double a = Math.Abs(ml - (Tables.pcmMulTbl[i % 12 + 12] * Math.Pow(2, ((int)(i / 12) - 4))));
                if (m > a)
                {
                    m = a;
                    n = i;
                }
            }
            return n;
        }
```

（`common.cs:392-406`）

- 表长：`12 * 8` = 96 项（8 个八度 × 12 半音），线性扫描取绝对值最小，无提前 break，返回下标 `n`（`common.cs:396-405`）。
- 每个候选值 = `pcmMulTbl[i%12 + 12] * 2^(i/12 - 4)`（`common.cs:398`）。
- 用到的 `Tables.pcmMulTbl` 定义在 `Tables.cs:36-62`，长度 24：下标 0-11 是半音比再 `/2`，下标 12-23 是半音比本身（同度 = 1.0）。

### searchSSGNote

`common.cs:374-390`：

```csharp
        public static int searchSSGNote(float freq)
        {
            float m = float.MaxValue;
            int n = 0;
            for (int i = 0; i < 12 * 8; i++)
            {
                //if (freq < Tables.freqTbl[i]) break;
                //n = i;
                float a = Math.Abs(freq - Tables.freqTbl[i]);
                if (m > a)
                {
                    m = a;
                    n = i;
                }
            }
            return n;
        }
```

（`common.cs:374-390`）

- 表长：`12 * 8` = 96，逐项绝对差最小（`common.cs:378-389`）。
- 表 `Tables.freqTbl` 定义在 `Tables.cs:24-34`：8 行 × 12 列，从 `C4/8` 一直到 `C9`（`*32`），即覆盖 8 个八度，基准是 C4=261.6255653。
- 注意 `Tables.cs:33` 这一行存在明显的复制粘贴错误：C9 行里后面几个值写成了 `*16.0f`（`349.994...*16`、`369.99...*16`、`391.99...*16`、`415.30...*16`、`440*16`、`466.16...*16`），只有首尾两个是 `*32.0f`。

### searchYM2608Adpcm

`common.cs:425-443`：

```csharp
        public static int searchYM2608Adpcm(float freq)
        {
            float m = float.MaxValue;
            int n = 0;

            for (int i = 0; i < 12 * 8; i++)
            {
                if (freq < Tables.pcmMulTbl[i % 12 + 12] * Math.Pow(2, ((int)(i / 12) - 3))) break;
                n = i;
                float a = Math.Abs(freq - (float)(Tables.pcmMulTbl[i % 12 + 12] * Math.Pow(2, ((int)(i / 12) - 3))));
                if (m > a)
                {
                    m = a;
                    n = i;
                }
            }

            return n + 1;
        }
```

（`common.cs:425-443`）

- 表长 `12 * 8` = 96，但指数是 `i/12 - 3`（比 SegaPCM 高一个八度基），且返回 `n + 1`（`common.cs:430-442`）。
- 与 `searchSegaPCMNote` 不同，这里先做 `if (freq < 候选) break` 的单向剪枝（`common.cs:432`）。

另有 `searchFMNote`（`common.cs:356-372`）与 `searchPCMNote`（`common.cs:408-423`），前者表长 `12*5` 且返回 `n - 12*3`，后者表长 12 且先对 `ml` 取模。

---

# 三、modizer

## 3.1 频率→MIDI 音号

位置：`src\ModizMusicPlayer.mm:922-951`。

```objc
static int vgm_getNoteFromFreq(double freq)
{
    const double LOG2_440 = 8.7813597135246596040696824762152;
    const double LOG_2 = 0.69314718055994530941723212145818;
    const int NOTE_440HZ = 69;//0x69;
    
    if(freq>1.0)
        return round(12 * ( log(freq)/LOG_2 - LOG2_440 ) + NOTE_440HZ);
    else
        return 0;
}

static int vgm_getSubNoteFromFreq(double freq)
{
    const double LOG2_440 = 8.7813597135246596040696824762152;
    const double LOG_2 = 0.69314718055994530941723212145818;
    const double NOTE_440HZ = 69;
    
    if(freq>1.0) {
        double note=((12 * ( log(freq)/LOG_2 - LOG2_440 ) + NOTE_440HZ));
        int note_int=round(note);
        int note_dec8=round((double)(note-(double)note_int)*8.0f);
        if (note_dec8>7) note_dec8=7;
        if (note_dec8<-7) note_dec8=-7;
        
        return (note_dec8>=0?note_dec8:note_dec8+7+8);
    }
    else
        return 0;
}
```

（`src\ModizMusicPlayer.mm:922-951`）

要点：

- 主音号：`round(12 * (log2(freq) - log2(440)) + 69)`，即标准 `69 + 12*log2(f/440)`（`ModizMusicPlayer.mm:929`）。
- `freq <= 1.0` 返回 0（注意是 0 而不是 -1），`ModizMusicPlayer.mm:928-931`。
- subnote：先取 `note - round(note)` 的 1/8 半音量化，夹到 ±7，再映射为 0..15（负数 `+15`），`ModizMusicPlayer.mm:941-947`。
- subnote 只有在设置 `GLOB_FXMIDIBarVibrato` 打开时才被上层使用（`ModizMusicPlayer.mm:963`）。
- 上游取值：`vgm_last_note[ch]` 被当作**频率**（Hz）传给上述函数（`ModizMusicPlayer.mm:955-957`、`964-966`）。

## 3.2 YM2151 嗅探条件

位置：`libs\libvgm\libvgm\emu\cores\ym2151.c:1988-2006`（在 `ym2151_update` 内，函数体起于 `ym2151.c:1965` 附近）。

```c
    //YOYOFR
    if (m_voice_ofs>=0) {
        for (int ii=0;ii<8;ii++) {
            if (!(PSG->Muted[ii]) && (PSG->pan[2*ii]|PSG->pan[2*ii+1]) && (PSG->chanout[ii]!=last_chanout[ii])) {
                if (PSG->oper[ii*4+3].freq==0) {
                    
                } else {
                    int freq=PSG->oper[ii*4+3].freq;
                    vgm_last_note[ii+m_voice_ofs]=freq/604.0f/2;
                    vgm_last_instr[ii+m_voice_ofs]=m_voice_ofs+ii;
                    
                    int newvol=PSG->oper[ii*4].key+1;
                    vgm_last_vol[ii+m_voice_ofs]=newvol;
                }
            }
            last_chanout[ii]=PSG->chanout[ii];
        }
    }
    //YOYOFR
```

（`libs\libvgm\libvgm\emu\cores\ym2151.c:1988-2006`）

要点（逐条对应代码）：

1. 通道静音检查：`!(PSG->Muted[ii])`（`ym2151.c:1991`）。
2. pan 检查：`(PSG->pan[2*ii] | PSG->pan[2*ii+1])` 非零，左右任一侧被打开即通过（`ym2151.c:1991`）。
3. 输出变化检查：`PSG->chanout[ii] != last_chanout[ii]`，即本采样块内该通道输出与上次记录不同才更新（`ym2151.c:1991`）。`last_chanout` 是 `static INT32 last_chanout[8]`（`ym2151.c:1968`），每通道循环末尾无条件刷新（`ym2151.c:2003`）。
4. 频率来源：`PSG->oper[ii*4+3].freq`，即**第 4 个 operator（slot 4）**的频率（`ym2151.c:1992`、`1995`）。
5. 换算：`vgm_last_note = freq / 604.0f / 2`，把 OPM 内部相位增量换成大致 Hz（`ym2151.c:1996`），常量 604.0 硬编码。
6. 音量：`PSG->oper[ii*4].key+1`，取自第 1 个 operator 的 key 值（`ym2151.c:1999`）。

注意：这里**没有读取 `0x08` 寄存器做 key-on**，完全靠「pan 非零 + 输出变化 + slot4 频率非零」来嗅探，因此 note 是在声音变化时才刷新，而不是在寄存器 key-on 事件时刷新。

## 3.3 bar 合并判据

位置：`utils\GLES3\RenderUtils.mm:5966-6015`。

```objc
    int data_bar2draw_count=0;
    
    for (int i=0; i<256; i++) { //for each channels
        int j=MIDIFX_LEN-data_midifx_len;
        while (j<MIDIFX_LEN) {  //while not having reach roof
            if (data_midifx_note[j][i]) {  //do we have a note ?
                unsigned int instr=data_midifx_instr[j][i];
                unsigned int vol=data_midifx_vol[j][i];
                unsigned int st=data_midifx_st[j][i];
                unsigned int note=data_midifx_note[j][i];
                int subnote=data_midifx_subnote[j][i];
                
                if (vol&&(st&VOICE_ON)) {  //check volume & status => we have something
                    data_bar2draw[data_bar2draw_count].startidx=j-(MIDIFX_LEN-data_midifx_len);
                    data_bar2draw[data_bar2draw_count].note=note;
                    data_bar2draw[data_bar2draw_count].subnote=subnote;
                    data_bar2draw[data_bar2draw_count].instr=instr;
                    data_bar2draw[data_bar2draw_count].size=0;
                    data_bar2draw[data_bar2draw_count].played=0;
                    data_bar2draw[data_bar2draw_count].frameidx=st>>4;
                    while ( (data_midifx_instr[j][i]==instr)&&
                           (data_midifx_note[j][i]==note)&&
                           (data_midifx_vol[j][i]<=vol)&&
                           (data_midifx_st[j][i]&VOICE_ON) ) {  //while same bar (instru & notes), increase size
                        data_bar2draw[data_bar2draw_count].size++;
                        //propagate lowest frame nb
                        data_midifx_st[j][i]=st;
                        if ((settings[GLOB_FXMIDIBarVibrato].detail.mdz_switch.switch_value==1)&&(data_midifx_subnote[j][i]!=subnote)) break;
                        //take most recent subnote if before playing bar
                        if (j<MIDIFX_LEN-MIDIFX_OFS) data_bar2draw[data_bar2draw_count].subnote=data_midifx_subnote[j][i];
                        if (j==(MIDIFX_LEN-MIDIFX_OFS-1)) data_bar2draw[data_bar2draw_count].played=1;
                        //update vol to latest encountered one, allow to retrigger if volume increases from last bar to new one and manage special case with vol = 2111121111
                        vol=data_midifx_vol[j][i];
                        j++;
                        if (settings[GLOB_FXMIDICutLine].detail.mdz_switch.switch_value==1) {
                            if (j==(MIDIFX_LEN-MIDIFX_OFS-1)) break;
                        }
                        /*if (data_midifx_vol[j][i]>vol) {
                         break;
                         }*/
                        if (j==MIDIFX_LEN) break;
                    }
                    data_bar2draw_count++;
                    //j++;
                } else j++;
            } else j++;
            if (data_bar2draw_count==MAX_BARS) break;
        }
        if (data_bar2draw_count==MAX_BARS) break;
    }
    qsort(data_bar2draw,data_bar2draw_count,sizeof(t_data_bar2draw),qsort_CompareBar);
```

（`utils\GLES3\RenderUtils.mm:5966-6016`）

合并判据（`RenderUtils.mm:5986-5989`），四个条件同时成立才继续延长同一根 bar：

1. `data_midifx_instr[j][i] == instr`：乐器号不变。
2. `data_midifx_note[j][i] == note`：音号不变。
3. `data_midifx_vol[j][i] <= vol`：音量不超过当前记录值（后面 `vol` 会更新为最新值，所以是对「非递增」的容忍，`RenderUtils.mm:5998`）。
4. `data_midifx_st[j][i] & VOICE_ON`：仍处于发声状态。

额外的中断条件：

- 若 `GLOB_FXMIDIBarVibrato` 打开且 `subnote` 变化 → break（`RenderUtils.mm:5993`）。
- 若 `GLOB_FXMIDICutLine` 打开且已到达播放线 `MIDIFX_LEN-MIDIFX_OFS-1` → break（`RenderUtils.mm:6000-6002`）。
- 到达 `MIDIFX_LEN` → break（`RenderUtils.mm:6006`）。

起始条件（`RenderUtils.mm:5971`、`5978`）：格子有 note 且 `vol != 0` 且 `st & VOICE_ON`。

`data_midifx_st[j][i] = st` 在循环内被回写（`RenderUtils.mm:5992`），会改写历史状态。

---

# 四、三方对照表

| 维度 | Denjhang | MDPlayer | modizer |
|---|---|---|---|
| YM2151 key-on 判定 | 读 `0x08`，`(data & 0x78) != 0` 即按下，任何 slot 位为 1 都算；做 0→1 / 1→0 边沿判定，置 `key_on_event` / `s_ym2151KeyOff`（`vgm_window.cpp:1335-1348`） | 读 `0x08`，`(dData & 0x78) != 0` 即按下；存 `con | 1`，key-off 只清 `&0xfe`，保留 slot 掩码（`ChipRegister.cs:1252-1268`） | 不读 `0x08`；靠 `!Muted && (pan_l|pan_r) && chanout != last_chanout && oper[4].freq != 0`（`ym2151.c:1991-1996`） |
| KC 半音查表 | 嵌套三目：0..2→0..2，3..6→2..5，7..10→5..8，11..15→8..12（`vgm_window.cpp:3403-3404`） | 完全相同的嵌套三目（`frmYM2151.cs:218-219`） | 无 KC 查表；只有 OPM 内部 `oper[].freq`，经 `freq/604/2` 得 Hz（`ym2151.c:1996`） |
| KF 是否参与音高 | 参与，`+ kf/64.0f`，kf 为 `reg[0x30+ch]>>2 & 0x3F`（`vgm_window.cpp:3406-3407`） | 不参与。kf 仅存到 `newParam.channels[ch].kf` 用于显示（`frmYM2151.cs:247`），note 公式里无 kf（`frmYM2151.cs:227`） | 不涉及寄存器；但用 `vgm_getSubNoteFromFreq` 给出 0..15 的 1/8 半音 subnote（`ModizMusicPlayer.mm:934-951`） |
| YM2413 音高公式 | 先算 `fnum*clk/(72*2^(19-blk))` 得 Hz，再 `12*log2(f/440)+69` 取整（`vgm_window.cpp:3460-3463` + `chip_params.h:155-162`） | 查表：`searchSegaPCMNote(fnum/172.0) + (oct-4)*12`（`frmYM2413.cs:110-113`） | 未找到 YM2413 专用音高代码 |
| 八度基准/偏移 | YM2151 显式 `+24`（`oct*12+semi+24`，`vgm_window.cpp:3407`）；YM2413 走 Hz→MIDI 绝对音高 | YM2151 **无** `+24`，只有 VGM 时钟补偿 `hosei`（`frmYM2151.cs:227`）；YM2413 用 `(oct-4)*12`（`frmYM2413.cs:113`） | 无八度偏移，直接绝对 MIDI 音号（`ModizMusicPlayer.mm:929`） |
| 频率→音号公式 | `12*log2(f/440)+69`，`+0.5` 取整，`<=0` 返回 -1，clamp 0..127（`chip_params.h:155-162`） | 无通用公式；全部走查表（`common.cs:356`、`374`、`392`、`408`、`425`），表长分别 `12*5` / `12*8` / `12*8` / `12` / `12*8` | `round(12*(log(f)/log(2) - 8.7813597135) + 69)`，`f<=1` 返回 0，无 clamp（`ModizMusicPlayer.mm:929`） |

---

# 五、各自明确做不到 / 明确做错的

## Denjhang

1. **YM2151 key-on 不区分 slot**：`(data & 0x78) != 0` 只要有一位就整体当 key-on（`vgm_window.cpp:1338`），对「部分 slot 被 key」的情形会误报为整通道发声。
2. **key-off 判定不看 slot 残留**：与 YM2612 分支的 `other_keyon` 检测（`vgm_window.cpp:1310-1322`）不同，YM2151 分支把 `0x08` 的 key 位为 0 一律视为 key-off（`vgm_window.cpp:1344-1347`）。
3. **音高靠寄存器直算，不做时钟补偿**：`oct*12+semi+24+kf/64` 里没有任何 `clk` 或 `hosei` 项（`vgm_window.cpp:3407`），与它自己 YM2413 分支（用 `clk`，`vgm_window.cpp:3453`、`3463`）不一致。
4. **`+24` 是硬编码的八度平移**（`vgm_window.cpp:3407`），对实际曲目的八度是否正确没有依据，只是固定抬两个八度。
5. **MIDI 只读不写**：库有 `write()`（`midifile/include/MidiFile.h:75-85`），但项目内无任何写调用，也没有反向「VGM → MIDI」链路。

## MDPlayer

1. **YM2151 的 KF 完全不参与音高**：`newParam.channels[ch].kf` 只在 `frmYM2151.cs:247` 被赋值用于显示，`frmYM2151.cs:227` 的 note 公式里没有 kf，半音级以下的偏移在显示层面丢失。
2. **YM2151 音高没有 `+24`**：`oct*12 + note + hosei`（`frmYM2151.cs:227`），与 Denjhang 的基准相差两个八度，两套实现无法直接对齐。
3. **key-on 存 `con | 1` 的位复用**：低 bit 是标志、高 4 位是 slot 掩码（`ChipRegister.cs:1259-1260`），`con = dData & 0x78` 本身不含 bit0，所以 `| 1` 是安全的；但 key-off 用 `& 0xfe` 只清标志（`ChipRegister.cs:1265`），槽位掩码会残留到下次 key-on 覆盖。
4. **YM2413 音高依赖固定常数 172.0**：`searchSegaPCMNote(freq / 172.0)`（`frmYM2413.cs:113`）未引入 YM2413 主时钟，换时钟会整体跑调。
5. **有序表数据错误**：`Tables.cs:33` 的 C9 行中 6 个值写成了 `*16.0f`（`349.994*16`、`369.99*16`、`391.99*16`、`415.30*16`、`440*16`、`466.16*16`），导致 `freqTbl` 最后 12 项内部不一致，直接影响 `searchSSGNote`（`common.cs:374-390`）在高音区的匹配结果。
6. **`searchSegaPCMNote` 无剪枝**：`common.cs:396-405` 全表 96 项线性扫，与 `searchYM2608Adpcm` 的 `break` 剪枝（`common.cs:432`）不一致；且返回值 `n` 而 `searchYM2608Adpcm` 返回 `n+1`（`common.cs:442`），两者基音定义相差 1。

## modizer

1. **YM2151 音高只是「近似」**：`freq / 604.0f / 2`（`ym2151.c:1996`）用一个硬编码常数把 OPM 相位增量折算成 Hz，没有考虑 `DT1`/`DT2`（detune）和 `MUL` 的完整组合，也不做八度对齐。
2. **嗅探依赖「输出变化」而非 key-on**：`PSG->chanout[ii] != last_chanout[ii]`（`ym2151.c:1991`）意味着持续不变的音不会刷新，且 `last_chanout` 在每轮无条件更新（`ym2151.c:2003`），一旦某块输出恰好相同就漏一次更新。
3. **频率只取 slot 4**：`PSG->oper[ii*4+3].freq`（`ym2151.c:1995-1996`），算法为 0-3（只用 C2/OP4 输出）时是正确的，但算法 4/5/6/7 中若 slot 4 不是唯一载波，音高表达仍固定用 slot 4，可能与实际听到的音高不一致。
4. **`freq <= 1.0` 返回 0 而非 -1**（`ModizMusicPlayer.mm:928-931`），0 是合法 MIDI 音号（C-1），造成「无音」与「极低音」不可区分。
5. **`vgm_getNoteFromFreq` 无 0..127 clamp**（`ModizMusicPlayer.mm:929`），超范围值直接返回。
6. **bar 合并会回写历史状态**：`data_midifx_st[j][i] = st`（`RenderUtils.mm:5992`）在绘制过程中修改状态数组，同一帧内可能影响后续通道/后续 bar 的 `st & VOICE_ON` 判定。
7. **bar 合并会截断**：`data_bar2draw_count == MAX_BARS` 时同时跳出内层 `while` 和外层通道循环（`RenderUtils.mm:6012-6014`），剩余通道的 bar 完全丢失。
8. **音量抬升会拆 bar**：判据是 `data_midifx_vol[j][i] <= vol`（`RenderUtils.mm:5988`），一旦音量变大就结束当前 bar 另起一根，因此渐强被视为多次重触发。
