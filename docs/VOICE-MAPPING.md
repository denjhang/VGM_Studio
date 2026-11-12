# 乐器指纹与音色映射（参考 vgm-conv + ym-voice）

> 本文件是 `json2json` 的音色映射规格。**参考实现已下载到 `docs/_vendor/ym-voice-0.3.2/`（ISC 许可），数据表可直接搬。**
> 数据格式 → `DATA-MODEL.md`；CLI 流程 → `CLI.md`；YM2151 寄存器 → `CHIP-YM2151.md`。

---

## 1. 为什么要参考 vgm-conv

用户原话：

> 其实 cli 版本重点参考 vgm-conv 里面的乐器指纹，什么 opll 乐器定义，其实 vgm-conv 做了 ym2612 转 ym2413，你看。

核实结果：**vgm-conv 做了完整的 YM2612 (OPN) → YM2413 (OPLL) 转换**，含：

1. **64 字节乐器指纹**（`opn-to-ym2413-converter.ts:186-243`）
2. **OPLL 的 15 个 ROM 音色定义**（实际在 `ym-voice` 包里，`opll-voice.js:6-27`）
3. **最近邻匹配算法**（`opl-voice.js:170-222` 的 `toOPLLROMVoice`）
4. **音量映射**（按 ALG 取载波 → 平均 → 降 4bit）

**这是本项目唯一真正需要"抄"的算法**，因为它是唯一做过 4-op → 2-op 的实际实现。

---

## 2. 乐器指纹（`_identifyVoice`）

### 2.1 源（`opn-to-ym2413-converter.ts:186-243`）

```ts
const rawVoice = [
  regs[0x30 + nch], regs[0x34 + nch], regs[0x38 + nch], regs[0x3c + nch],  // DT/ML
  regs[0x40 + nch], regs[0x44 + nch], regs[0x48 + nch], regs[0x4c + nch],  // TL
  regs[0x50 + nch], regs[0x54 + nch], regs[0x58 + nch], regs[0x5c + nch],  // KS/AR
  regs[0x60 + nch], regs[0x64 + nch], regs[0x68 + nch], regs[0x6c + nch],  // AM/DR
  regs[0x70 + nch], regs[0x74 + nch], regs[0x78 + nch], regs[0x7c + nch],  // SR
  regs[0x80 + nch], regs[0x84 + nch], regs[0x88 + nch], regs[0x8c + nch],  // SL/RR
  regs[0x90 + nch], regs[0x94 + nch], regs[0x98 + nch], regs[0x9c + nch],  // SSG-EG
  regs[0xb0 + nch] & 0x3f,   // FB/CON
  regs[0xb4 + nch] & 0x37,   // AMS/PMS
];

const hash = _normalizeTotalLevel(rawVoice)
  .map((e) => ("0" + e.toString(16)).slice(-2))
  .join("");   // → 60 个字符的十六进制字符串
```

**指纹 = 30 个寄存器值 × 2 字符 = 60 字符**。

### 2.2 归一化（`_normalizeTotalLevel`，`:44-76`）★ 关键

```ts
function _normalizeTotalLevel(data: number[]): number[] {
  const alg = data[28] & 7;
  let min = 0;
  switch (alg) {
    case 0: case 1: case 2: case 3:
      data[7] = 0;              // 只有 OP4 是载波，把它归 0
      break;
    case 4:
      min = Math.min(data[6], data[7]);
      data[6] -= min; data[7] -= min;
      break;
    case 5: case 6:
      min = Math.min(data[5], data[6], data[7]);
      data[5] -= min; data[6] -= min; data[7] -= min;
      break;
    case 7:
      min = Math.min(data[4], data[5], data[6], data[7]);
      data[4] -= min; data[5] -= min; data[6] -= min; data[7] -= min;
      break;
  }
  return data;
}
```

**索引含义**：`data[4..7]` 是四个 operator 的 TL（因为 `rawVoice[4..7]` 对应 `0x40..0x4c`）。

**为什么要归一化**：同一音色在曲子不同段落会调音量（TL 变化）。不归一化，同一个音色会被算成多个指纹，音色表爆炸。

**归一化规则**：把载波的 TL 全部减去载波中的最小值 —— 即"最响的载波永远 TL=0"。这样指纹只反映音色的**相对形状**，不反映绝对音量。

### 2.3 载波掩码与 `_calcMainTotalLevel`（`:26-42`）

```ts
function _calcMainTotalLevel(data: number[]): number {
  const alg = data[28] & 7;
  switch (alg) {
    case 0: case 1: case 2: case 3: return data[7];
    case 4: return Math.min(data[6], data[7]);
    case 5: case 6: return Math.min(data[5], data[6], data[7]);
    default: return Math.min(data[5], data[6], data[7], data[8]);
  }
}
```

注意 **ALG 7 用了 `data[8]`**（OP1 的 TL），因为全加法时 4 个 operator 都是载波。这与 `CHIP-YM2151.md` 的 `kAlgCarriers[7] = 0b1111` 一致。

---

## 3. OPLL 的 15 个 ROM 音色定义（★ 数据表，直接搬）

源：`docs/_vendor/ym-voice-0.3.2/dist/opll-voice.js:6-27`

```js
const _OPLL_ROM_PATCHES = [
  [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],  // 0: User（用户音色槽）
  [0x71,0x61,0x1e,0x17,0xd0,0x78,0x00,0x17],  // 1: Violin
  [0x13,0x41,0x1a,0x0d,0xd8,0xf7,0x23,0x13],  // 2: Guitar
  [0x13,0x01,0x99,0x00,0xf2,0xd4,0x21,0x23],  // 3: Piano
  [0x11,0x61,0x0e,0x07,0x8d,0x64,0x70,0x27],  // 4: Flute
  [0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28],  // 5: Clarinet
  [0x31,0x22,0x16,0x05,0xe0,0x71,0x00,0x18],  // 6: Oboe
  [0x21,0x61,0x1d,0x07,0x82,0x81,0x11,0x07],  // 7: Trumpet
  [0x33,0x21,0x2d,0x13,0xb0,0x70,0x00,0x07],  // 8: Organ
  [0x61,0x61,0x1b,0x06,0x64,0x65,0x10,0x17],  // 9: Horn
  [0x41,0x61,0x0b,0x18,0x85,0xf0,0x81,0x07],  // A: Synthesizer
  [0x33,0x01,0x83,0x11,0xea,0xef,0x10,0x04],  // B: Harpsichord
  [0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12],  // C: Vibraphone
  [0x61,0x50,0x0c,0x05,0xd2,0xf5,0x40,0x42],  // D: Synthesizer Bass
  [0x01,0x01,0x55,0x03,0xe4,0x90,0x03,0x02],  // E: Acoustic Bass
  [0x41,0x41,0x89,0x03,0xf1,0xe4,0xc0,0x13],  // F: Electric Guitar
  // 以下 3 个是节奏（鼓）
  [0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d],  // R: Bass Drum
  [0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x68],  // R: High-Hat(M) / Snare Drum(C)
  [0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55],  // R: Tom-tom(M) / Top Cymbal(C)
];
```

另外还有两套变体（同样的 8 字节格式）：

| 变体 | 位置 | 说明 |
|---|---|---|
| `vrc7` | `opll-voice.js:29-49` | VRC7 的 15 个预置（音色不同） |
| `ymf281b` | `opll-voice.js:51-71` | YMF281B 的 15 个预置 |

**注**：这 8 字节是 **OPLL 寄存器的原始编码**（`encode()` 的逆），可直接写进 `0x00-0x07`。

### 3.1 与 Nuked-OPLL 的 `patch_ym2413[]` 对照

`furnace/extern/Nuked-OPLL/opll.c:139` 的 `patch_ym2413[opll_patch_max]` 是**同一份数据的结构体形式**：

```c
typedef struct {
    uint8_t tl, dc, dm, fb;
    uint8_t am[2], vib[2], et[2], ksr[2], multi[2], ksl[2];
    uint8_t ar[2], dr[2], sl[2], rr[2];
} opll_patch_t;
```

**两处数据一致**（都是 YM2413 官方 ROM 表）。**我们用 Nuked-OPLL 的版本**，因为要链接它做输出。

**但 `ym-voice` 的 8 字节版本更方便**：它就是 `0x00-0x07` 的寄存器写序列，直接 `for i in 0..7: write(0x00+i, byte[i])` 就能定义用户音色。

---

## 4. 最近邻匹配（`toOPLLROMVoice`）★ 核心算法

源：`docs/_vendor/ym-voice-0.3.2/dist/opl-voice.js:170-222`

```js
toOPLLROMVoice(variant = "ym2413") {
  let diff = Infinity;
  let program = 0;
  for (let i = 1; i < 16; i++) {
    const opll = OPLLVoiceMap[variant][i];
    if (i == 13) continue;              // 跳过 #13（Synthesizer Bass，避免误匹配）
    let d = 0;
    // ① 调制器/载波的频率比（抵消倍频差异）
    const ml_a = this.slots[1].ml / _ml_tbl[this.slots[0].ml];
    const ml_b = opll.slots[1].ml / _ml_tbl[opll.slots[0].ml];
    d += Math.abs(ml_a - ml_b) << 1;    // 权重 ×2
    // ② 反馈
    d += Math.abs(this.fb - opll.fb) >> 1;
    // ③ 起音率（两个 slot）
    d += Math.abs(this.slots[0].ar - opll.slots[0].ar);
    d += Math.abs(this.slots[1].ar - opll.slots[1].ar);
    // ④ 衰减率
    d += Math.abs(this.slots[0].dr - opll.slots[0].dr);
    d += Math.abs(this.slots[1].dr - opll.slots[1].dr);
    // ⑤ 延音电平（TL 有波形补偿）
    d += Math.min(63, 4*Math.abs(this.slots[0].sl - opll.slots[0].sl) +
        Math.abs(this.slots[0].tl - (opll.slots[0].tl + opll.slots[0].ws ? 8 : 0))) >> 3;
    // ⑥ 持续音 vs 打击音的分流
    if (this.slots[1].rr === 0) {              // 源是持续音
      if (opll.slots[1].eg === 0) continue;    // 目标也必须是持续音
      d += Math.abs(this.slots[1].sl - opll.slots[1].sl);
    } else {                                    // 源是打击音
      if (opll.slots[1].eg === 1) continue;    // 目标也必须是打击音
      d += Math.abs(this.slots[1].rr - opll.slots[1].rr);
    }
    if (d < diff) { program = i; diff = d; }
  }
  // ⑦ 八度补偿：按载波的 ML 比值算
  const opll = OPLLVoiceMap[variant][program];
  const ooff = Math.floor(Math.log2(_ml_tbl[this.slots[1].ml] / _ml_tbl[opll.slots[1].ml]) / 2);
  // ⑧ 音量补偿：有波形时补
  let voff = 1;
  if (opll.slots[1].ws) { voff -= 2; if (opll.slots[0].ws) voff -= 2; }
  return { program, volumeOffset: voff, octaveOffset: ooff };
}
```

### 4.1 六个关键设计（都值得抄）

| # | 设计 | 为什么 |
|---|---|---|
| ① | **用 ML 比值而不是绝对 ML** 做频率比较 | 不同音色的 ML 不同，但"调制器/载波的频率比"才是音色特征 |
| ② | **反馈权重 ×2，再右移 1** | 反馈对音色影响大但值域小（0-7），要提权 |
| ⑤ | **TL 加权 4 倍并入 SL** | TL 和 SL 共同决定音色的"亮度变化" |
| ⑥ | **区分持续音 / 打击音**（按 `rr == 0` 和 `eg`） | 长音不能用拨弦音色，反之亦然。**这是硬约束，不是软评分** |
| ⑦ | **八度补偿**：`log2(ML比) / 2` | 匹配到的 ROM 音色频率不同，要移八度补偿 |
| ⑧ | **音量补偿**：有波形（`ws`）时补 2-4 | OPLL 的正弦/半正弦波形响度不同 |

`_ml_tbl = [0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15]`（`opl-voice.js:7`）——**这是 ML 值对应的实际倍频**，注意 ML=10 和 11 都是 10 倍，12 和 13 都是 12 倍。

### 4.2 `program = 0` 的含义

- `program 1-15`：ROM 预置
- `program 0`：用户音色槽（写 `0x00-0x07`）
- `program 16+`：节奏（`program >> 10` 得到鼓的位掩码）

---

## 5. 音量映射（`_updateInstVol`）

源：`opn-to-ym2413-converter.ts:246-283`

```ts
const alg = regs[0xb0 + nch] & 7;
const amps = [regs[0x40+nch]&0x7f, regs[0x44+nch]&0x7f, regs[0x48+nch]&0x7f, regs[0x4c+nch]&0x7f];
let vol;
switch (alg) {
  case 4: vol = (amps[2] + amps[3]) / 2; break;
  case 5: case 6: vol = (amps[1] + amps[2] + amps[3]) / 3; break;
  case 7: vol = (amps[0] + amps[1] + amps[2] + amps[3]) / 4; break;
  default: vol = amps[3]; break;    // ALG 0-3 串联，取 OP4
}
const vv = (vol >> 3) + volumeOffset;                 // 127 → 15（>>3）
const d = (program << 4) | Math.min(15, Math.max(0, vv));
this._y(0x30 + ch, d);
```

**这条可以直接搬到 YM2151 → YM2413**，只需把寄存器偏移换成 OPM 的：

| OPN | OPM |
|---|---|
| `0xb0+nch`（ALG） | `0x20+ch` 的 bit2-0 |
| `0x40/0x44/0x48/0x4c+nch`（TL） | `0x60+op*8+ch`（op=0..3） |

### 5.1 ★ `>> 3` 对 OPM 是错的，必须是 `÷ 4`

这段 TS 的 `(vol >> 3)` 是**给 OPN 用的**近似，`volumeOffset: 1`（§4 的 ⑧）
正是为了补这个粗移位。搬到 OPM 上会双重出错。实测（`cli/tools/probe_volume.py`，
用 libvgm 渲染单音后取 RMS）：

| 平台 | 音量台阶 | 实测 dB/单位 |
|---|---|---|
| YM2151 载波 TL（7 bit） | 1 | **0.7526 dB**（TL 0→80 共 −60.98 dB，严格线性） |
| YM2413 `0x30` 低 4 位（4 bit） | 1 | **3.01 dB**（0→12 共 −36.07 dB） |

比值正好 **4**，和寄存器位宽完全自洽：

```c
// libvgm/emu/cores/ym2151.c:1143
op->tl   = (v & 0x7f) << (ENV_BITS - 7);              // <<3
// libvgm/emu/cores/ym2413.c:1695
SLOT->TL = ((v & 0x0f) << 2) << (ENV_BITS - 2 - 7);   // <<2，再 <<1
```

两者都把值左移进同一个 10 bit 包络域，所以 **nibble n ≡ TL 4n**。

用 `>> 3` 的后果（`31 Grass Maze` 实测）：源载波 TL 分布 13..26，动态范围
9.8 dB 被压成 `1..3` 两步（6 dB）；响的音符整体偏轻一个档位。改用
`round(TL / 4)` 后 nibble 落在 3..7，动态范围恢复到 9.6 dB。

**`volumeOffset` 的基准也要改**：ym-voice 的 `voff = 1` 是补 `>> 3` 的偏差，
按 `÷ 4` 换算时应为 `0`。保留 1 会让整曲再多衰减约 3 dB。实测整曲 RMS：

| 映射 | voff | 整曲 RMS |
|---|---|---|
| `>> 3` | 1（v9） | −16.4 dBFS（比源响 2.6 dB） |
| `÷ 4` | 1（v10） | −22.0 dBFS（比源轻 3.0 dB） |
| `÷ 4` | **0（v11）** | **−19.0 dBFS（与源一致）** |

源 `01_original_OPM_YMF2151.wav` 本身是 −19.0 dBFS。

### 5.2 与乐器宏的比例乘法

转换器算出的 nibble 是**该音符的基础音量** `vol`，宏在此基础上做相对变化。
两层的合成走 Furnace 的 `VOL_SCALE_LOG`（`dispatch.h:1747`，dB 域相加后 clamp）：

```cpp
mi.baseVol = 15 - vol;                                   // Opm2Opll.cpp：转成 chan.vol 极性
// MacroEngine.cpp：x = baseVol, y = 宏值
//   outVol  = CLAMP(baseVol + min(15,v) - 15, 0, 15)
//   nibble  = 15 - VOL_SCALE_LOG(outVol, 15-载波TL, 15)
```

所以宏写 `0`（静音）时 nibble 被推到 15（全衰减），宏写 `15` 时 nibble 回到
`vol` 附近 —— 正是"按比例缩放"的语义。实测 ch1（`vol` 宏 `{0, 15}`、
基础 nibble 3）在 v11 的输出：

```
rel=0    0x30 = 0x?f   (宏第 1 帧 0 → 全衰减)
rel=0    0x30 = 0x?3   (宏第 2 帧 15 → nibble 3，即基础音量)
rel=735  0x30 = 0x?3   (循环回第 2 帧)
```

---

## 6. 我们该怎么做（C++ 移植）

### 6.1 不用移植整包

`ym-voice` 是 TypeScript，1318 行。我们要的部分：

| 需要 | 来源 | 行数 |
|---|---|---|
| OPLL 的 15 个 ROM 音色表 | `opll-voice.js:6-27` | 22 |
| OPM/OPN/OPLL 的编解码位布局 | `opm-voice.js:150-175`、`opn-voice.js:170-230`、`opll-voice.js:250-290` | 注释里的表就够 |
| 最近邻匹配 | `opl-voice.js:170-222` | 53 |
| `_normalizeTotalLevel` | `opn-to-ym2413-converter.ts:44-76` | 33 |
| 音量映射 | `opn-to-ym2413-converter.ts:246-283` | 38 |

**合计约 150 行 C++ 就能实现。**

### 6.2 数据结构

```cpp
// FMVoice.h —— 统一表示一颗 FM 芯片的音色
struct FMSlot {
    uint8_t dt1, dt2, ml, tl, ks, ar, dr, sr, sl, rr, am, ws;
};
struct FMVoice {
    uint8_t fb, con, ams, pms;
    FMSlot slot[4];
};

// 从 YM2151 的 shadow 寄存器提取
FMVoice extractOPM(const uint8_t regs[256], int ch);

// 从 YM2413 的寄存器提取
FMVoice extractOPLL(const uint8_t regs[256], int ch);

// 最近邻匹配（移植 toOPLLROMVoice）
struct OpllMatch {
    uint8_t program;       // 1-15 = ROM 预置, 0 = 用户槽
    int8_t volumeOffset;   // -15..15
    int8_t octaveOffset;   // -2..2
};
OpllMatch matchOPLL(const FMVoice& src, const OpllRomPatch* bank);
```

### 6.3 指纹与映射表

```cpp
// 指纹：60 字符十六进制（先归一化 TL）
std::string fingerprint(const FMVoice& v);

// 映射表条目
struct VoiceMapEntry {
    uint8_t program;
    int8_t volumeOffset;
    int8_t octaveOffset;
};
std::unordered_map<std::string, VoiceMapEntry> g_voiceMap;
```

**映射表的键就是那个 60 字符指纹**，与 vgm-conv 的 `_voiceDefMap` 完全兼容 —— **vgm-conv 生成的 voice-table 文件可以直接拿来用**（`README.md:261-299` 的格式）。

### 6.4 三档工作模式

| 模式 | 行为 | 用途 |
|---|---|---|
| `explicitOnly` | 只查映射表，查不到就用 `fallback`（静音） | 完全手动控制 |
| `presetMatch` | 忽略映射表，全部自动匹配 | 快速预览 |
| `explicitPlusAuto` | 先查表，查不到才自动匹配 | **默认，最实用** |

---

## 7. 与 `CLI.md` 的映射表格式对接

`CLI.md` §5 的 `instrument.entries` 就是这里的 `g_voiceMap`：

```jsonc
"instrument": {
  "mode": "explicitPlusAuto",
  "presetBank": "ym2413",
  "entries": {
    "a91f3c...60字符...": { "program": 5, "volumeOffset": -2, "octaveOffset": 0 }
  }
}
```

**vgm-conv 的 voice-table 格式（`_voiceMapEntryString`，`:100-109`）**：

```js
"{i:5,v:2,o:0}"   // i=program, v=-volumeOffset（注意符号相反）, o=octaveOffset
```

**注意符号差异**：vgm-conv 的 `v` 是**取负**的（`volumeOffset: e.v != null ? -e.v : 0`，`:112-118`）。我们的 JSON 用正的 `volumeOffset`，转换时要注意。

---

## 8. 待办

- [ ] 把这 150 行 C++ 实现写进 `0x808/src/vgm/lib/FmVoice.{h,cpp}`
- [ ] 用 vgm-conv 生成的 `.opm` 文件验证 OPM 音符解码
- [ ] 验证 `patch_ym2413[]`（Nuked-OPLL）与 `_OPLL_ROM_PATCHES`（ym-voice）的 8 字节编码是否一致
- [ ] 把 vgm-conv 的 voice-table 格式支持成 `fuitool` 的一个子命令（`fuitool import-voice-table`）
