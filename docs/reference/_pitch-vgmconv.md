# vgm-conv + ym-voice：频率 ↔ KC/KF/F-Number 的全部音高公式

| 项 | 值 |
|---|---|
| 仓库 | `E:\working\vscode-projects\Reference_Project\vgm_libs\vgm-conv-main` |
| 依赖 | `E:\working\zcode-projects\VGM_Studio\docs\_vendor\ym-voice-0.3.2\dist` |
| 用途 | 我方 `FmVoice.cpp` 就是搬这份代码；本文件记录音高相关的事实 |

行号全部来自实际读取。

---

## 一、频率 ↔ OPM 的 KC/KF

### 1.1 `freqToOPMNote` 原文

`src/converter/opm_freq.ts:1-21`（全文件 21 行）：

```ts
export type OPMNote = {
  kc: number;
  kf: number;
};

const BASE_FREQ = 277.2; // C#4 = 60
//                   C# D  D# E  F  F# G  G# A   A#  B   C
const KEY_TO_NOTE = [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14];

// clockRatio should be `3579545 / (Output OPM Clock)`
export function freqToOPMNote(freq: number, clockRatio: number): OPMNote {
  if (freq == 0 || freq == Infinity) {
    return { kc: 0, kf: 0 };
  }
  const key = Math.max(0, 60 + Math.log2((freq * clockRatio) / BASE_FREQ) * 12);
  const frac = key - Math.floor(key);
  const kf = Math.floor(frac * 64);
  const note = KEY_TO_NOTE[Math.floor(key) % 12];
  const oct = Math.min(7, Math.floor(key / 12));
  return { kc: (oct << 4) | note, kf };
}
```

事实：

| 项 | 值 |
|---|---|
| 基准 | `277.2 Hz = C#4`，且 `key 60 = C#4` |
| `clockRatio` | `3579545 / 目标 OPM 时钟`（注意是**反比**：目标时钟越大，ratio 越小） |
| `key` | 浮点 MIDI 音号（60 = C#4） |
| `kf` | `floor(frac * 64)`，范围 0..63，**1/64 半音** |
| `kc` 高 3 位 | `min(7, floor(key/12))` —— octave |
| `kc` 低 4 位 | `KEY_TO_NOTE[key%12]` —— 跳过 3/7/11/15 |

### 1.2 逆运算

**库中无逆运算**。`freqToOPMNote` 只有正向。我方 `FmVoice.cpp:515-528` 的 `OpmKcKfToFreq` 是自己写的逆运算（同一张表反查）。

### 1.3 与 Valley Bell 的差异（★ 重要）

| 项 | vgm-conv | Valley Bell |
|---|---|---|
| 基准音 | C#4 = 277.2 Hz，key 60 | Block=4 时 `61 + ...`，即 C#4 = 61 |
| 音号数值 | 60 = C#4 | 61 = C#4 |
| 差异 | **整整差 1**（vgm-conv 的 key 60 vs Valley Bell 的 61） |

这个 1 的差来自"60 = C-4 vs 61 = C#4"两套不同的 C 基准（MIDI 60 = C4，而 C#4 = 61）。**两者换算出的绝对频率一致**，只是编号约定差 1。

### 1.4 OPM 时钟换算时的音高保持

`src/converter/opm-clock-converter.ts:5-6`：

```ts
const keyCodeToIndex = [0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12];
const keyIndexToCode = [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14];
```

`opm-clock-converter.ts:17-21`：

```ts
    this._ratio = this.to.clock / from.clock;
    this._keyDiff = Math.round(12 * Math.log2(1.0 / this._ratio) * 256);
    /* LFO_FREQ = CLOCK * POWER(2, LFRQ/16 - 32) = CLOCK' * POWER(2, LFRQ'/16 - 32) */
    /* => LFRQ' = 16 * LOG2(CLOCK/CLOCK') + LFRQ */
    this._lfoDiff = Math.round(16 * Math.log2(1.0 / this._ratio));
```

`opm-clock-converter.ts:39-64`：

```ts
      if ((0x28 <= cmd.addr && cmd.addr <= 0x2f) || (0x30 <= cmd.addr && cmd.addr <= 0x37)) {
        const ch = cmd.addr - (cmd.addr < 0x30 ? 0x28 : 0x30);
        const orgKeyIndex = keyCodeToIndex[this._regs[0x28 + ch] & 0xf];
        const orgKey = (orgKeyIndex << 8) | (this._regs[0x30 + ch] & 0xfc);
        let octave = (this._regs[0x28 + ch] >> 4) & 0x7;
        let newKey = orgKey + this._keyDiff;
        ...
        const okc = (octave << 4) | keyIndexToCode[newKey >> 8];
        this._y(0x28 + ch, okc);
        const kf = newKey & 0xfc;
        this._y(0x30 + ch, kf);
```

事实：

- **key 的内部表示 = `(半音序号 << 8) | (KF 6 位)`**，1 半音 = 256 单位，KF 粒度为 4（低 2 位恒 0）。
- 改时钟时 `_keyDiff = round(12*log2(1/ratio)*256)`，直接在 1/256 半音单位上加偏移，然后重新编码 KC+KF。
- 越过八度边界时手动 `octave--/++` 并 `newKey ±= 12<<8`，到界钳位到 0 或 `(12<<8)-1`。
- `keyCodeToIndex` 把 16 个 KC 低 4 位值映射成 0..12 的连续半音序号（3/7/11/15 复用前一个值），`keyIndexToCode` 是它的反表。

**这是"KC 低 4 位 → 连续半音"的最显式写法**，可以直接抄进我方解析器：
`keyCodeToIndex = [0,1,2,3, 3,4,5,6, 6,7,8,9, 9,10,11,12]`。

---

## 二、OPN → OPM（频率搬运）

### 2.1 `_fnumToFreq` 与 key 写入

`src/converter/opn-to-opm-converter.ts:20-39`：

```ts
    this._clockRatio = 3579545 / this.convertedChipInfo.clock;
    this._clockDiv = from.chip == "ym2203" ? 1.0 : 2.0;
...
  _fnumToFreq(fnum: number, blk: number): number {
    return (fnum * this.from.clock) / ((72 * this._clockDiv) << (20 - blk));
  }

  _fnumToNote(fnum: number, blk: number): OPMNote {
    const freq = this._fnumToFreq(fnum, blk);
    return freqToOPMNote(freq, this._clockRatio);
  }
```

`opn-to-opm-converter.ts:88-99`：

```ts
    // F-Number and Block
    if ((0xa0 <= adr && adr <= 0xa2) || (0xa4 <= adr && adr < 0xa6)) {
      const nch = adr & 3;
      const ch = (cmd.port == 0 ? 0 : 4) + nch;
      const al = 0xa0 + nch;
      const ah = 0xa4 + nch;
      const fnum = (((regs[ah] & 7) << 8) | regs[al]) >> 2;
      const blk = (regs[ah] >> 3) & 7;
      const { kc, kf } = this._fnumToNote(fnum, blk);
      this._y(0x28 + ch, kc);
      this._y(0x30 + ch, kf << 2);
    }
```

事实：

| 项 | 值 |
|---|---|
| OPN fnum 位域 | `0xA4+nch` 的 bit0-2 是 fnum 高 3 位，bit3-5 是 block（3 位），`0xA0+nch` 是低 8 位 |
| **右移 2** | OPN 的 fnum 是 11 位，代码 `>>2` 只取高 9 位 → **损失低 2 位精度** |
| `_clockDiv` | `ym2203=1`，其它 OPN（含 YM2612）=2（因为 YM2612 内部时钟是 clock/2） |
| OPN Hz 公式 | `Hz = fnum * clock / (72 * div * 2^(20-blk))` |
| KF 写入 | `kf << 2`（1/64 半音放到寄存器高 6 位） |

### 2.2 key-on 直通

`opn-to-opm-converter.ts:51-56`：

```ts
      } else if (adr == 0x28) {
        const ch = cmd.data & 0x7;
        const slots = (cmd.data & 0xf0) >> 4;
        this._y(0x08, (slots << 3) | ch, false);
```

OPN 的 `0x28`（`(slotMask<<4)|ch`）→ OPM 的 `0x08`（`(slots<<3)|ch`）。**slot 掩码从 4 位左移 3 位变成 4 位**，语义一一对应。

---

## 三、OPL → OPLL

### 3.1 `_key`：fnum/blk **原样搬**，不重算

`src/converter/opl-to-opll-converter.ts:76-81`：

```ts
    _key(ch: number, kon: number, blk: number, fnum: number) {
        const vdef = this._voices[ch];
        const _blk = Math.min(7, Math.max(0, blk + vdef.octaveOffset));
        this._y(0x10 + ch, fnum & 0xff);
        this._y(0x20 + ch, (kon << 4) | (_blk << 1) | (fnum >> 8));
    }
```

`opl-to-opll-converter.ts:116-123`：

```ts
            } else if ((0xa0 <= cmd.addr && cmd.addr <= 0xa8) || (0xb0 <= cmd.addr && cmd.addr <= 0xb8)) {
                // KEYON - BLOCK - FNUM
                const rflag = (this._regs[0xbd] & 32);
                const ch = cmd.addr & 0xf;
                if (ch < 9) {
                    let blkfnum = (this._regs[0xb0 + ch] << 8 | this._regs[0xa0 + ch]) >> 1;
                    this._key(ch, (blkfnum >> 12) & 1, (blkfnum >> 9) & 7, blkfnum & 0x1ff);
                }
            }
```

事实：

| 项 | OPL | OPLL |
|---|---|---|
| fnum 位宽 | 10 位（`0xB0` 的 bit0-1 是 fnum bit8-9） | 9 位（`0x20` 的 bit0） |
| block | `0xB0` 的 bit2-4（3 位） | `0x20` 的 bit1-3（3 位） |
| key | `0xB0` 的 bit5 | `0x20` 的 bit4 |
| 转换 | `blkfnum = (0xB0<<8 | 0xA0) >> 1`，然后 `>>12` 取 key、`>>9` 取 blk、`&0x1ff` 取 fnum | 直接写入 |

**关键**：`(0xB0<<8 | 0xA0) >> 1` 这一步把 OPL 的位布局 `[key(1) blk(3) fn(10)]` 右移成 `[con(1) key(1) blk(3) fn(9)]`，再取 `0x1ff` 就是从 10 位 fnum **截断成 9 位**（丢最低 1 位，即半个 fnum 单位）。

### 3.2 `octaveOffset` 的数据来源

`dist/opl-voice.js:213-221`（`toOPLLROMVoice` 末尾）：

```js
        const opll = opll_voice_1.OPLLVoiceMap[variant][program];
        const ooff = Math.floor(Math.log2(_ml_tbl[this.slots[1].ml] / _ml_tbl[opll.slots[1].ml]) / 2);
        let voff = 1;
        if (opll.slots[1].ws) {
            voff -= 2;
            if (opll.slots[0].ws) {
                voff -= 2;
            }
        }
        return {
            program,
            volumeOffset: voff,
            octaveOffset: ooff,
        };
```

事实：

- `octaveOffset = floor(log2(ML_源载波 / ML_目标载波) / 2)`，即按**载波倍频比**折算八度（除以 2 是因为一个八度 = 倍频 ×2）。
- 范围：`_ml_tbl` 是 `{0.5,1,2,3,4,5,6,7,8,9,10,10,12,12,15,15}`，比值 0.5..30，`log2` 后 -1..4.9，除 2 后 -0.5..2.45，`floor` 后 **-1..2**。
- `volumeOffset` 初值 1；目标载波有波形（ws）时 -2，调制器也有波形时再 -2 → 取值 `1 / -1 / -3`。
- 匹配循环 `dist/opl-voice.js:170-210`：`for i in 1..15`，**跳过 i==13**（Synth Bass），6 个评分项（ML 比值、FB、AR×2、DR×2、SL/TL 加权、持续/打击硬约束），与 `FmVoice.cpp:411-468` 逐行等价。

### 3.3 `_getVoiceArr` 的寄存器取址

`opl-to-opll-converter.ts:46-60`：

```ts
    _getVoiceArr(ch: number) {
        const off = Math.floor(ch / 3) * 8 + (ch % 3);
        return [
            this._regs[0x20 + off],
            this._regs[0x23 + off],
            this._regs[0x40 + off],
            this._regs[0x43 + off],
            this._regs[0x60 + off],
            this._regs[0x63 + off],
            this._regs[0x80 + off],
            this._regs[0x83 + off],
            0x00,
            this._regs[0xc0 + ch],
        ];
```

即 OPL 的 operator 偏移 `off = (ch/3)*8 + ch%3`，调制器在 `+0`、载波在 `+3`。

---

## 四、OPN → YM2413

### 4.1 音高搬运

`src/converter/opn-to-ym2413-converter.ts:345-356`：

```ts
          const blk = this._blkFnums[ch] >> 9;
          const fnum = this._blkFnums[ch] & 0x1ff;
          this._keyFlags[ch] = key;
          const fkey = program >= 1024 ? 0 : key;
          const blk_o = Math.min(7, Math.max(0, octaveOffset + blk));
          const dl = fnum & 0xff;
          const dh = (fkey << 4) | (blk_o << 1) | (fnum >> 8);
          this._y(0x20 + ch, dh);
          this._y(0x10 + ch, dl);
```

`opn-to-ym2413-converter.ts:368-384`（A0/A4 分支）：

```ts
      const al = 0xa0 + nch;
      const ah = 0xa4 + nch;
      const fnum = (((regs[ah] & 7) << 8) | regs[al]) >> 2;
      const { hash, program, octaveOffset } = this._currentVoice[ch];
      const blk = (regs[ah] >> 3) & 7;
      const blk_o = Math.min(7, Math.max(0, octaveOffset + blk));
      const key = program >= 1024 ? 0 : this._keyFlags[ch];
      const dl = fnum & 0xff;
      const dh = (key << 4) | (blk_o << 1) | (fnum >> 8);
      ...
        this._blkFnums[ch] = (blk << 9) | fnum;
        this._y(0x20 + ch, dh);
        this._y(0x10 + ch, dl);
```

事实：

| 项 | 值 |
|---|---|
| fnum | OPN 11 位 `>>2` → 9 位，只保留高 9 位（**丢 2 位**） |
| block | OPN 3 位（0..7），`+ octaveOffset` 后钳位 0..7 |
| 写入顺序 | **先 `0x20`（含 key 位），再 `0x10`** —— 与 OPL→OPLL 相反 |
| `_blkFnums` | 缓存 `(blk<<9)|fnum`，key-on 时才写 |
| 节奏 voice | `program >= 1024` 时 `fkey = 0`（不打 key-on），改走 `0x0e` 鼓位 |

### 4.2 octaveOffset 的另一个来源：voice 表字符串解析

`opn-to-ym2413-converter.ts:100-119`：

```ts
function _voiceMapEntryString(voice: VoiceDefinition) {
  const res = [`i:${_programString(voice.program)}`];
  if (voice.volumeOffset != 0) {
    res.push(`v:${-voice.volumeOffset}`);
  }
  if (voice.octaveOffset != 0) {
    res.push(`o:${voice.octaveOffset}`);
  }
  return `{${res.join(",")}}`;
}

function _voiceMapEntryToVoiceDefinition(e: VoiceMapEntry): VoiceDefinition {
  return {
    program: e.i,
    volumeOffset: e.v != null ? -e.v : 0,
    octaveOffset: e.o || 0,
  };
}
```

- 用户可通过 `--voice-table` 提供 `o:` 字段手动指定八度偏移；缺省 0。
- 自动匹配时走 `_estimateOPLLVoice`（即 `toOPLLROMVoice`）算出 `octaveOffset`（-1..2）。
- 兜底 `fallback_voice = { program: 1, volumeOffset: 15, octaveOffset: 0 }`（`opn-to-ym2413-converter.ts:19`）。

### 4.3 音色识别：60 字节 hash

`opn-to-ym2413-converter.ts:181-217`：取 30 个寄存器（每个通道的 `0x30/0x34/.../0x9c` + `0xb0&0x3f` + `0xb4&0x37`），归一化 TL 后拼成 60 个 hex 字符做 key。这与 `_voiceDefMap` 的默认空表项 `"000...0"` 对应。

---

## 五、AY8910 → OPL 的 fnum 公式（唯一直算 fnum 的地方）

`src/converter/ay8910-to-opl-converter.ts:85-95`：

```ts
  _updateFreq(ch: number, freq: number) {
    let fnum = Math.floor((freq << 19) / (this._oplClock / 72));
    let blk = 1;
    while (fnum > 1023) {
      fnum >>= 1;
      blk++;
    }
    if (blk > 7) blk = 7;
    this._y(0xb0 + ch, 0x20 | ((blk & 7) << 2) | ((fnum >> 8) & 3));
    this._y(0xa0 + ch, fnum & 0xff);
  }
```

公式：`fnum = floor(freq * 2^19 / (clock/72))`，然后 `while (fnum > 1023) { fnum >>= 1; blk++; }` 从 `blk=1` 起。**我方 `FreqToOpllFnum`（`FmVoice.cpp:494-511`）就是这段的直译**（OPLL 用 `2^19`、上限 1023、blk 从 1 起）。

---

## 六、YM2413 时钟换算时的音高保持

`src/converter/ym2413-clock-converter.ts:15-37`：

```ts
  convertWriteDataCommand(cmd: VGMWriteDataCommand): VGMCommand[] {
    if (this._ratio !== 1.0 && cmd.addr != null) {
      if (0x10 <= cmd.addr && cmd.addr < 0x30) {
        this._regs[cmd.addr] = cmd.data;
        const al = (cmd.addr & 0xf) + 0x10;
        const ah = (cmd.addr & 0xf) + 0x20;
        const fnum = ((this._regs[ah] & 1) << 8) | this._regs[al];
        let new_fnum = Math.round(fnum / this._ratio);
        let new_blk = (this._regs[ah] & 0xe) >> 1;
        while (new_fnum > 0x1ff) {
          new_fnum >>= 1;
          new_blk++;
        }
        if (new_blk > 7) {
          new_blk = 7;
          new_fnum = 0x1ff;
        }
        const dl = new_fnum & 0xff;
        const dh = (this._regs[ah] & 0xf0) | (new_blk << 1) | (new_fnum >> 8);
```

事实：

- `ratio = 目标时钟 / 源时钟`（`ym2413-clock-converter.ts:12`）。
- 频率不变要求 `fnum' = fnum / ratio`，然后重新规格化 block（`>>1` + `blk++`），超界钳到 `blk=7, fnum=0x1ff`。
- **`0x20` 的高 4 位（INST）与 `0x30` 不受影响，原样保留。**

---

## 七、总结表

| 项 | 公式 | 精度 | 代码位置 |
|---|---|---|---|
| freq → OPM KC/KF | `key = 60 + 12*log2(freq*ratio/277.2)`；`kf = floor(frac*64)` | 1/64 半音 | `opm_freq.ts:15-20` |
| OPM KC 音名码 | `KEY_TO_NOTE = [0,1,2,4,5,6,8,9,10,12,13,14]` | — | `opm_freq.ts:8` |
| OPM 时钟补偿 | `_keyDiff = round(12*log2(1/ratio)*256)` | 1/256 半音 | `opm-clock-converter.ts:18` |
| OPN fnum → Hz | `fnum * clock / (72*div*2^(20-blk))` | — | `opn-to-opm-converter.ts:33` |
| OPN → OPM | fnum `>>2`（11→9 位）+ `freqToOPMNote` | 丢 2 位 | `opn-to-opm-converter.ts:94-98` |
| OPL → OPLL | fnum 原样搬，10 位截成 9 位 | 丢 1 位 | `opl-to-opll-converter.ts:78-80,121-122` |
| OPN → YM2413 | fnum `>>2`（11→9）+ `blk+octaveOffset` | 丢 2 位 | `opn-to-ym2413-converter.ts:368-384` |
| octaveOffset | `floor(log2(ML_car_src/ML_car_dst)/2)` | -1..2 | `opl-voice.js:214` |
| AY → OPL fnum | `floor(freq*2^19/(clock/72))`，`while>1023 {>>1; blk++}` | — | `ay8910-to-opl-converter.ts:86-92` |
| YM2413 时钟换算 | `new_fnum = round(fnum/ratio)`，重新规格化 block | — | `ym2413-clock-converter.ts:22-31` |

---

## 八、与本项目的关系（事实陈述）

| 我方文件 | 对应的原文 | 状态 |
|---|---|---|
| `FmVoice.cpp:472-486` `FreqToOpmNote` | `opm_freq.ts:11-21` | 逐行等价 |
| `FmVoice.cpp:494-511` `FreqToOpllFnum` | `ay8910-to-opl-converter.ts:85-95` | 逐行等价 |
| `FmVoice.cpp:515-528` `OpmKcKfToFreq` | **库中无此逆运算**，我方自写 | 需验证 |
| `FmVoice.cpp:411-468` `MatchOpllRom` | `opl-voice.js:170-222` | 逐行等价 |
| `Opm2Opll.cpp:144-151` 音高写入 | `opl-to-opll-converter.ts:76-81` | 顺序不同（我方先 0x10 后 0x20，vgm-conv 亦同） |

**关键疑点（待验证）**：`OpmKcKfToFreq`（`FmVoice.cpp:515-528`）用 `kNoteToKey = {0,1,2,4,5,6,8,9,10,12,13,14}` 反查，但该表**不是双射**（值域只有 12 个值，定义域 12 个，是双射）；真正的问题是 `key` 的基准：`FreqToOpmNote` 用 `60 = C#4`，逆运算用 `keyf = oct*12 + key + kf/64` 再 `277.2 * 2^((keyf-60)/12)`，**两者基准一致**，这部分正确。真正需要核对的是 `Opm2Opll.cpp:145-151` 里 `kc` 直接取自 `shadow[0x28+ch]` 后**未做八度对齐**就传给 `OpmKcKfToFreq`，而 Valley Bell/Furnace 的 block 基准都含固定偏移 —— 这可能是音高整体偏移的来源之一。
