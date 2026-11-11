# YM2151（OPM）语义表（已由三方交叉验证）

> 本文件是**已确认**的寄存器语义，不是待核对的草稿。
> 三个独立实现给出完全一致的结论，见每条的"来源"。
> 用法：`NoteSplitter` 与 `IPitchMapper` 的默认实现直接照此编码。

**来源缩写：**

| 缩写 | 实现 |
|---|---|
| **DH** | Denjhang Music Player v16（C++ ImGui）`docs/reference/Denjhang.md` |
| **MD** | MDPlayer form/KB（C# WinForms）`docs/reference/MDPlayer.md` |
| **VB** | Valley Bell vgm2mid（VB6）`docs/reference/vgm2mid-valleybell.md` |

---

## 1. Key-On / Key-Off（★ 分块边界）

**寄存器 `0x08`**

```
bit 7-6 : 未使用
bit 3-6 : 通道号（ch = data & 0x07 —— 注意只有低 3 位是通道）
          实际：ch = data & 0x07
bit 3-6 : slot 掩码（M1 / C2 / M2 三个 slot）
          实际：mask = (data >> 3) & 0x0F
bit 0-2 : 同上，含在 mask 里
```

三种一致的说法：

| 来源 | 判定 | 位置 |
|---|---|---|
| **DH** | `new_kon = (data & 0x78) != 0`，`ch = data & 7` | `vgm_window.cpp:1335-1349` |
| **MD** | `fmKeyOn[ch] = (data & 0x78) \| 1`（非零则置位），`ch = data & 7` | `ChipRegister.cs:1252-1270` |
| **VB** | `Slot = (Data / &H8) And &HF`，`Slot > 0` = ON，`CH = Data And &H7` | `YM2151.bas:109-126` |

**统一结论：**

```cpp
int ch   = data & 0x07;
bool kon = (data & 0x78) != 0;   // 任一 slot 的 KeyOn 位置位
```

**→ 这是音符块的开始/结束边界。**

### 1.1 边界检测必须是"边沿"，不是"电平"

`VB` 用 `NoteOn_1`（上一状态）/ `NoteOn_2`（当前状态）两个变量：

```vb
NoteOn_1(CH) = NoteOn_2(CH)
NoteOn_2(CH) = IIf(Slot(CH) > 0, 1, 0)
If NoteOn_2(CH) <> NoteOn_1(CH) Then    ' 只有变化才动作
```

`DH` 用 `key_on_event` 布尔量（本帧是否发生上升沿）。

**→ 我们：只有 `false → true` 才开新块，`true → false` 才关块。重复写同样的 keyon 值不算新音符。**

---

## 2. 音高：KC + KF

**寄存器 `0x28 + ch` = KC（Key Code），`0x30 + ch` = KF（Key Fraction）**

### 2.1 KC 的结构

```
bit 7   : 未使用
bit 6-4 : octave（0-7）      oct = (KC >> 4) & 0x07
bit 3-0 : 半音码（0-11 有效）idx = KC & 0x0F
```

### 2.2 KC 低 4 位 → 半音的查表（★ 三方一致）

OPM 的 KC 低 4 位**不是直接的半音数**，而是跳过了每八度中 3 个"空格"（因为 C# / D# / F# / G# / A# 是 5 个黑键，编码只用了 12 个位置中的 12 个但排列不同）：

```cpp
int idx  = KC & 0x0F;
int semi = idx < 3  ? idx
         : idx < 7  ? idx - 1
         : idx < 11 ? idx - 2
         :            idx - 3;
```

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12+ |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| semi | 0 | 1 | 2 | 2 | 3 | 4 | 5 | 5 | 6 | 7 | 8 | 8 | — |

实际是 C, C#, D, D#, E, F, F#, G, G#, A, A#, B 的映射。

| 来源 | 代码 | 位置 |
|---|---|---|
| **DH** | `semi = idx<3?idx : idx<7?idx-1 : idx<11?idx-2 : idx-3` | `vgm_window.cpp:3402-3403` |
| **MD** | `note = note<3?note : note<7?note-1 : note<11?note-2 : note-3` | `frmYM2151.cs:218-227` |
| **VB** | `(FNum And &HC)/&H4 * 3 + (FNum And &H3) + 61`（另一种编码，等价） | `YM2151.bas:26-53` |

### 2.3 KF 的结构

```
bit 7-6 : 未使用
bit 5-0 : 分数（0-63）        kf = (KF >> 2) & 0x3F
```

**KF 是"分数半音"**，`kf/64` 就是一个半音内的偏移。

| 来源 | 处理 | 位置 |
|---|---|---|
| **DH** | `+ KF/64.0f` 加入音高 | `vgm_window.cpp:3404-3407` |
| **MD** | **不参与音高**，只单独显示（这是它的缺陷） | `frmYM2151.cs:254` |
| **VB** | `+ FNum_LSB/&H40`（即 /64） | `YM2151.bas:196-198` |

**→ 采用 DH + VB 的做法：KF 参与音高。**

### 2.4 完整音高公式

```cpp
// 返回浮点 MIDI 音号（整数 = note，小数 = 半音内的偏移）
float ym2151Pitch(int KC, int KF) {
    int oct  = (KC >> 4) & 0x07;
    int idx  = KC & 0x0F;
    int semi = idx < 3 ? idx : idx < 7 ? idx - 1 : idx < 11 ? idx - 2 : idx - 3;
    int kf   = (KF >> 2) & 0x3F;
    return (float)(oct * 12 + semi) + kf / 64.0f + kOctaveOffset;
}
```

`kOctaveOffset` 是"对齐到 MIDI 音域"的常数：

- **DH** 用 `+ 24`（对齐到 C1）
- **VB** 用 `(Block - 4) * 12` 的基准（等价于把 octave 4 当作基准）

**→ 我们的 `IPitchMapper` 实现里应把这个偏移做成可配置项**，默认 `+24`（与 DH 一致，音域落在 C1-B7，正好是 7 个八度）。

### 2.5 音高跳变 → 新音符（DH 的判据）

`vgm_window.cpp:3200-3232`：

```cpp
} else if (fabsf(fnt - vs.target_note) > 0.5f) {
    // 音高跳变 > 半个半音：视为新音锚点
    vs.start_note  = fnt;
    vs.target_note = fnt;
    vs.decay       = 1.0f;
}
```

**→ 我们的分块规则应加一条：即使 key-on 位没变，若 KC/KF 变化导致音高跳变 > 0.5 半音，也要切新块。**

理由：OPM 上"快速连续换音不重新 keyon"是常见技法（所谓的 legato / portamento），但在视觉上应该显示成两个音。

**阈值可配置**：0.5 半音是 DH 的经验值；保守起见可以设 1 半音。

---

## 3. 音量：ALG → 载波 → 最小 TL

### 3.1 ALG 载波表（★ 三方一致，可以定稿）

```cpp
// bit0 = OP1, bit1 = OP2, bit2 = OP3, bit3 = OP4
static const uint8_t kAlgCarriers[8] = {
    0b1000,  // ALG 0: OP1→OP2→OP3→OP4      仅 OP4
    0b1000,  // ALG 1: OP1+OP2→OP3→OP4      仅 OP4
    0b1000,  // ALG 2: OP1+(OP2→OP3)→OP4    仅 OP4
    0b1000,  // ALG 3: (OP1→OP2)+(OP3→OP4)  仅 OP4（OP2/OP4 混合后进 OP4）
    0b1100,  // ALG 4: (OP1→OP2)+(OP3→OP4)  OP2 + OP4
    0b1110,  // ALG 5: OP1→(OP2,OP3,OP4)    OP2 + OP3 + OP4
    0b1110,  // ALG 6: OP1+(OP2,OP3,OP4)    OP2 + OP3 + OP4
    0b1111,  // ALG 7: 全加法               全部 4 个
};
// = { 0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF }
```

| 来源 | 表 | 位置 |
|---|---|---|
| **DH** | `{0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF}` | `vgm_window.cpp:3394` |
| **MD** | `{0x40, 0x40, 0x40, 0x40, 0x50, 0x70, 0x70, 0x78}`（左移 3 位的等价编码） | `frmYM2151.cs:228-242` |
| **VB** | 只取 `OP == 3`（carrier），即 ALG 0-3 的情况 | `YM2151.bas:219-242` |

**MD 的编码是"每个 operator 占 2 bit"，DH 的是"每个 operator 占 1 bit"。两者等价：**
`0x40 → 0x8`、`0x50 → 0xC`、`0x70 → 0xE`、`0x78 → 0xF`。

**→ 这张表定稿，不再标"待核对"。**

### 3.2 音量 = 载波的最小 TL

```cpp
int minTL = 127;
for (int op = 0; op < 4; op++) {
    if ((kAlgCarriers[alg] >> op) & 1) {
        int tl = reg[0x60 + op*8 + ch] & 0x7F;
        minTL = std::min(minTL, tl);
    }
}
```

**DH 的做法**：`vgm_window.cpp:3396-3399`。
**MD 的做法**：同样（`frmYM2612.cs:200-214`、`frmYM2151.cs:228-242`）。

**为什么取最小**：TL 是衰减量（0 = 最响，127 = 最轻）。多个载波时，最响的那个决定整体响度。

---

## 4. 其它相关寄存器

| 寄存器 | 含义 | 用途 |
|---|---|---|
| `0x20 + ch` | PAN(bit7-6) / FB(bit2-0) / CON(bit3) | `lane:pan`、音色 |
| `0x38 + ch` | PMS(bit6-4) / AMS(bit1-0) | `lane:pms`、`lane:ams` |
| `0x40 + op*8 + ch` | DT1(bit6-4) / MUL(bit3-0) | 音色 |
| `0x60 + op*8 + ch` | TL（7 bit） | 音量 |
| `0x80 + op*8 + ch` | KS(bit7-6) / AR(bit4-0) | 音色（起音） |
| `0xA0 + op*8 + ch` | AM(bit7) / D1R(bit4-0) | 音色 |
| `0xC0 + op*8 + ch` | DT2(bit7-6) / D2R(bit4-0) | 音色 |
| `0xE0 + op*8 + ch` | SL(bit7-4) / RR(bit3-0) | 音色 |
| `0x18` | LFO 频率 | 全局 lane |
| `0x19` | AMD(bit6-0) / PMD(bit7) | 全局 lane（写两次，bit7 区分） |
| `0x1B` | LFO 波形 | 全局 lane |
| `0x0F` | Noise 频率(bit4-0) / 通道使能(bit7) | 全局 + 通道级 |
| `0x01` | LFO Reset(bit1) / Test(bit0) | 元信息 |
| `0x14` | 定时器控制 | 元信息（不影响声音） |

---

## 5. 三种实现的差异汇总（我们的取舍）

| 维度 | DH | MD | VB | 我们采用 |
|---|---|---|---|---|
| key-on 判定 | `(data & 0x78) != 0` | 同 | `Slot > 0` | **同** |
| 边沿检测 | `key_on_event` | `fmKeyOn[]` 位 | `NoteOn_1/2` | **同** |
| 音高 | KC 查表 + KF/64 + 24 | KC 查表（**KF 不用**） | KC 查表 + KF/64 | **DH + VB（KF 参与）** |
| 音量 | 载波 min TL | 同 | 只取 OP3 | **DH/MD（载波 min TL）** |
| 音高跳变切块 | **> 0.5 半音** | 无 | 无 | **采用（可配阈值）** |
| 时间单位 | 44100 采样 | 快照 | 44100 采样 | **采样** |
| 音符段持久化 | ❌ 只有瞬时状态 | ❌ | ❌ | **✅ 我们要做** |

---

## 6. 与 `DATA-MODEL.md` 的对应关系

| 本文件的规则 | JSON 里的落点 |
|---|---|
| key-on 边沿 → 块边界 | `notes[].startSample` / `notes[].endSample` |
| 音高跳变 > 0.5 半音 → 新块 | 同上（切分依据之二） |
| KC/KF → 浮点音高 | `notes[].view.nearestNote` + `notes[].view.cents` |
| ALG 载波表 | `AnalyzerYm2151` 的常量，不进 JSON |
| 载波 min TL | `notes[].view` 可加一个 `volumeRef`（音色音量参考） |
| 所有原始写 | `notes[].segments[]`（不经过任何换算） |

**关键：§2–§3 的所有计算都只产生 `view` 里的参考值。`segments[]` 里永远是 `(reg, val, atSample)` 原值。**
