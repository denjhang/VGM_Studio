# Furnace：音符 → YM2151(OPM) / YM2413(OPLL) 寄存器的音高代码

| 项 | 值 |
|---|---|
| 仓库 | `E:\working\zcode-projects\VGM_Studio\furnace` |
| 版本 | dev258-16-gd74e7a01a |
| 用途 | **反向参考**：VGM→MIDI 是"寄存器→音高"，这里是"音高→寄存器"，正好互逆，可用来验证我方转换 |

所有行号来自实际读取（`cat -n` / `sed -n`）。

---

## 0. 内部音高单位

`src/engine/dispatch.h:486-487`：

```cpp
  // note: current note, in semitones. 0 is C-(-5) and 60 is C-0.
```

`src/engine/dispatch.h:608-621`（`SharedChannel::calcFreq`）：

```cpp
  int calcFreq(int pitchMult=1) {
    if (rawFreq) return baseFreq+pitch2;
    if (pitchTable==NULL) return 0;
    if (!pitchTable->linearity) {
      return pitchTable->get(baseFreq,pitch*pitchMult,pitch2);
    }
    if (fixedArp) {
      return pitchTable->get(baseNoteOverride<<7,pitch,pitch2);
    }
    return pitchTable->get(baseFreq+(arpOff<<7),pitch,pitch2);
  }
```

`src/engine/dispatch.h:590-598`（`calcBaseFreq`）：线性表时 `getBase(note) = note<<7`（`pitchTable.cpp:173-174`），即 **1 半音 = 128 单位**。

`src/engine/dispatch.h:1755`：

```cpp
#define COLOR_NTSC (315000000.0/88.0)
```

---

## 1. YM2151（OPM）— 线性音高 → KC + KF

### 1.1 音高基准偏移

`src/engine/platform/fmshared_OPM.h:25-26`：

```cpp
#define NOTE_LINEAR(x) (((x)&DIV_NOTE_RAW_FLAG)?((x)&(~DIV_NOTE_RAW_FLAG)):((x)<<7))
#define OFFSET_LINEAR (baseFreqOff+(log2(parent->song.tuning/440.0)-5)*12.0*128.0)
```

`src/engine/platform/arcade.cpp:1196`：

```cpp
  baseFreqOff=round(1536.0*(log((COLOR_NTSC/(double)chipClock))/log(2.0)));
```

- `1536 = 12 半音 × 128`，即"一个八度"在线性音高里的单位数。
- `baseFreqOff` 的作用：把虚拟基准时钟 `COLOR_NTSC = 315000000/88 ≈ 3579545.45 Hz` 与实际 `chipClock` 的差异折算成半音偏移。**OPM 的标准时钟恰好是 3579545，所以 baseFreqOff ≈ 0**。
- `(log2(tuning/440)-5)*12*128`：A4=440 的调音修正，`-5` 把 Furnace 的 note 编号（60 = C-0）对齐到 A4=440 体系。

### 1.2 KC 编码（★ 与 Valley Bell / Denjhang / MDPlayer 完全同一张表）

`src/engine/platform/arcade.cpp:286-292`：

```cpp
static unsigned char noteMap[12]={
  0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14
};

inline int hScale(int note) {
  return ((note/12)<<4)+(noteMap[note%12]);
}
```

即 KC = `(octave << 4) | noteCode`，noteCode 跳过每个八度里 4 个空缺位（3/7/11/15）。与 `opm_freq.ts` 的 `KEY_TO_NOTE`、Valley Bell 的 `YM2151FNumToMidi` 是同一映射的三种写法。

### 1.3 写 KC/KF 的完整代码

`src/engine/platform/arcade.cpp:502-523`：

```cpp
    if (chan[i].freqChanged) {
      if (chan[i].rawFreq) {
        chan[i].freq=chan[i].baseFreq+chan[i].pitch2;
        immWrite(i+0x28,(chan[i].freq>>6));
        immWrite(i+0x30,(chan[i].freq&63)<<2);
      } else {
        chan[i].freq=chan[i].baseFreq+chan[i].pitch-128+chan[i].pitch2;
        if (!parent->song.compatFlags.oldArpStrategy) {
          if (chan[i].fixedArp) {
            chan[i].freq=(chan[i].baseNoteOverride<<7)+chan[i].pitch-128+chan[i].pitch2;
          } else {
            chan[i].freq+=chan[i].arpOff<<7;
          }
        }
        chan[i].freq+=OFFSET_LINEAR;
        if (chan[i].freq<0) chan[i].freq=0;
        if (chan[i].freq>=(95<<7)) chan[i].freq=(95<<7)-1;
        immWrite(i+0x28,hScale(chan[i].freq>>7));
        immWrite(i+0x30,((chan[i].freq<<1)&0xfc));
      }
      hardResetElapsed+=2;
      chan[i].freqChanged=false;
    }
```

推导出的事实：

| 项 | 结论 |
|---|---|
| `chan[i].freq` 单位 | **1/128 半音** |
| KC 写入 | `hScale(freq>>7)`：`freq>>7` = 整数半音（Furnace note 编号），再编码成 octave+noteCode |
| KF 写入 | `(freq<<1) & 0xfc`：把 1/128 半音的小数部分左移 1 位 → **1/64 半音**，取 6 位（0..63），低 2 位补 0 |
| 半音上限 | `95<<7`（freq 上限 95 个半音），超出钳位 |
| 减 128 | `pitch-128`：`pitch` 宏的 0 点在 128，即 pitch 默认 128 = 无偏移 |

**关键对照**：KF 的精度与 Valley Bell 的 `(reg 0x30) = kf<<2`、vgm-conv 的 `_y(0x30+ch, kf<<2)` 完全一致 —— 寄存器 0x30 的低 2 位恒为 0，有效值是 6 位 = 1/64 半音。

### 1.4 rawFreq 分支

`rawFreq` 时 `freq` 本身就是压好的 `(KC<<6)|(KF>>2)` 形式：`>>6` 得 KC，`&63` 得 KF 的 6 位值，`<<2` 补回寄存器位置。这就是寄存器原值直通。

### 1.5 key-on

`src/engine/platform/arcade.cpp:527-534`：

```cpp
    if ((chan[i].keyOn || chan[i].opMaskChanged) && !chan[i].hardReset) {
      immWrite(0x08,(chan[i].opMask<<3)|i);
      hardResetElapsed++;
      chan[i].opMaskChanged=false;
      chan[i].keyOn=false;
    }
```

- `0x08` 的数据 = `(opMask<<3) | ch`，`opMask` 4 位 → 与 `(data & 0x78) != 0` 的判定一致。
- key-off 在 `arcade.cpp:487-489`：`immWrite(0x08,i)`（opMask=0）。
- **先写 KC/KF（`0x28`/`0x30`），后写 key-on（`0x08`）**——同一 tick 内先 0x28/0x30 再 0x08，这个顺序对"寄存器→音符"的解析很重要。

### 1.6 硬复位（会影响"音符起点"的判定）

`src/engine/platform/arcade.cpp:484-499`：key-off 时先把 4 个 op 的 SL/RR 写 `0x0f`（最快释放），再用伪写 `0xfffffffe` 插 127 周期延时，然后重新写 SL/RR 恢复。这意味着**一段真实曲子里，key-off 之后紧跟一串 0xE0/0xE8 写**，解析时不能把它们当成新音符的参数。

---

## 2. YM2413（OPLL）— 非线性音高表 → F-Number + Block

### 2.1 音高表初始化

`src/engine/platform/opll.cpp:30`：

```cpp
#define CHIP_FREQBASE 1180068
```

`src/engine/platform/opll.cpp:1199-1201`：

```cpp
void DivPlatformOPLL::notifyPitchTable(int sample) {
  pitchTable.init(parent->song.tuning,chipClock,CHIP_FREQBASE,0xffff,false,parent->song.compatFlags.linearPitch);
}
```

参数含义（`src/engine/pitchTable.cpp:177-218`）：`isPeriod=false`（频率表不是周期表）、`maximum=0xffff`、`isLinear` 由兼容开关决定（默认 false）。

建表核心 `src/engine/pitchTable.cpp:205-212`：

```cpp
  for (int i=0; i<=12; i++) {
    int nbase=i+(shift-5)*12;
    double fbase=(period?(tuning*0.0625):tuning)*pow(2.0,(float)(nbase+3)/(12.0));
    int bf=period?
           round((clock/fbase)/divider):
           round(fbase*(divider/clock));
    pitch[i]=bf;
  }
```

即表项 `bf = round(freq * CHIP_FREQBASE / chipClock)`，存在 `pitch[0..12]`，八度差用移位实现（`getBase`，`pitchTable.cpp:137-175`）。

### 2.2 toFreq：从表值到 (block, fnum)

`src/engine/platform/opll.cpp:406-429`：

```cpp
#define OPLL_C_NUM 343

int DivPlatformOPLL::octave(int freq, int fixedBlock) {
  if (fixedBlock>0) {
    return 1<<(fixedBlock-1);
  }
  freq/=OPLL_C_NUM;
  if (freq==0) return 1;
  return 1<<bsr(freq);
}

int DivPlatformOPLL::toFreq(int freq, int fixedBlock) {
  int block=0;
  if (fixedBlock>0) {
    block=fixedBlock-1;
  } else {
    block=freq/OPLL_C_NUM;
    if (block>0) block=bsr(block);
  }
  if (block>7) block=7;
  freq>>=block;
  if (freq>0x1ff) freq=0x1ff;
  return (block<<9)|freq;
}
```

- `bsr(x)` = 最高有效位位置（`floor(log2(x))`）。
- block = `bsr(freq / 343)`，fnum = `freq >> block`，上限 `0x1ff`（9 位）。
- 返回值 `(block<<9)|fnum` 是一个打包值，之后拆成两个寄存器。

### 2.3 写寄存器

`src/engine/platform/opll.cpp:341-401`（关键三行）：

```cpp
      int mul=2;
      int fixedBlock=chan[i].state.block;
      if (!parent->song.compatFlags.linearPitch) {
        mul=octave(chan[i].baseFreq,fixedBlock)*2;
      }
      chan[i].freq=chan[i].calcFreq(mul);
      ...
      int freqt=chan[i].rawFreq?chan[i].freq:toFreq(chan[i].freq,fixedBlock);
      ...
      chan[i].freqL=freqt&0xff;
      ...
        if (i<9) {
          immWrite(0x10+i,freqt&0xff);
        }
      ...
      chan[i].freqH=(freqt>>8)&15;
```

`src/engine/platform/opll.cpp:393-399`：

```cpp
    } else if ((chan[i].keyOn || chan[i].freqChanged) && i<9) {
      //immWrite(0x28,0xf0|konOffs[i]);
      if (!(i>=6 && properDrums)) {
        if (i<9) {
          immWrite(0x20+i,(chan[i].freqH)|(chan[i].active<<4)|(chan[i].state.alg?0x20:0));
        }
      }
      chan[i].keyOn=false;
    }
```

位域事实：

| 寄存器 | 内容 |
|---|---|
| `0x10 + ch` | fnum 低 8 位 |
| `0x20 + ch` | `fnumH(bit8) \| (active<<4 = KEY) \| (SUS?) <<5 \| (block<<1)` |

注意 `freqH = (freqt>>8) & 15` 已经包含 `block<<1 | fnumbit8` 两项（因为 `freqt = (block<<9)|fnum`），所以 `0x20` 的写法是 `freqH | (key<<4) | (sus<<5)`。

**结论**：Furnace 的 OPLL block 上限 7、fnum 上限 0x1ff，与 YM2413 硬件一致；`fnum` 只有 9 位（0x10 存 8 位 + 0x20 存 1 位）。

---

## 3. Furnace 与"寄存器→音符"的互逆关系（用于验证我方转换）

| 方向 | Furnace（音高→寄存器） | 我方（寄存器→音高） |
|---|---|---|
| OPM 音名 | `hScale(note)` = `(note/12<<4) \| noteMap[note%12]` | 反查 `noteMap` 得半音序号 |
| OPM 分数 | `KF = (freq<<1)&0xfc`，1/64 半音 | `KF>>2`，1/64 半音 |
| OPM 八度 | KC 高 3 位 | KC 高 3 位 |
| OPM 基准 | `baseFreqOff + (log2(tuning/440)-5)*12*128`，标准钟下 ≈0 | Valley Bell：`(Block-4)*12 + 61 + ...` |
| OPLL | block=bsr(freq/343)，fnum=freq>>block | Valley Bell：`Hz = FNum*fsam*2^(Block-19)` 的反算 |

**可直接用的一条验证判据**：把 Furnace 的 `hScale`/`noteMap` 与 Valley Bell 的 `YM2151FNumToMidi` 对同一 KC 求值，结果必须相同（差值只可能来自八度基准常数）。

---

## 4. 未找到 / 代码中未见

| 项 | 状态 |
|---|---|
| OPM 的 `pitchTable` 使用 | **未使用**。`arcade.cpp` 全程用 `NOTE_LINEAR`（`x<<7`）线性音高，不建表；只有 OPLL/OPN 等用 `pitchTable` |
| OPM 的 `0x28` 高 3 位直接叫 "block" | 代码里无 `block` 变量，只有 `hScale` 产物；octave 隐含在 `note/12` |
| 全局移调 | `OFFSET_LINEAR` 内已含 tuning；未见单独的 transpose 字段参与本文件计算 |
| `0x30` 的 detune（DT1/DT2） | 属音色参数，不在音高换算里 |

---

## 5. VGM 导出侧（`vgmOps.cpp`）

`src/engine/vgmOps.cpp` 里 YM2151 的写命令（`case DIV_SYSTEM_YM2151` 附近，约 281 行）：

```cpp
w->writeC(4|baseAddr1); w->writeC(write.addr&0xff); w->writeC(write.val);
```

即 VGM 命令 `0x54`（`4|0x50`）后跟 addr、val。OPLL 为 `0x51`。这与写序列的"addr 在前、val 在后"由 `order` 字段保证（详见 `docs/reference/furnace.md` §3.3）。

---

## 6. 可直接照抄的常量

| 常量 | 值 | 位置 |
|---|---|---|
| `noteMap[12]` | `{0,1,2,4,5,6,8,9,10,12,13,14}` | `arcade.cpp:286-288` |
| `COLOR_NTSC` | `315000000.0/88` = 3579545.45 | `dispatch.h:1755` |
| `CHIP_FREQBASE`（OPLL） | `1180068` | `opll.cpp:30` |
| `OPLL_C_NUM` | `343` | `opll.cpp:406` |
| OPM 每半音单位数 | `128`（`NOTE_LINEAR`） | `fmshared_OPM.h:25` |
| OPLL fnum 位宽 | 9 位（0x1ff） | `opll.cpp:428` |
