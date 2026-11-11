# vgmtransmute — OPNA→OPNB 寄存器重映射（○ 小参考）

| 项 | 值 |
|---|---|
| 路径 | `E:\working\zcode-projects\VGM_Studio\grauw\vgmtransmute` |
| 作者 | **Laurens Holst（grauw）** —— MSX 社区知名开发者，vgmplay-msx / libvgm / openMSX 的作者之一 |
| 许可 | **BSD-2-Clause**（宽松） |
| 语言 | JavaScript（Node ESM），**零 npm 依赖** |
| 规模 | **1340 行** |
| 功能 | **只有一个方向：YM2608 (OPNA) → YM2610B (OPNB)** |

---

## 1. 它是什么

"VGM chip transmuter"。README 只有 42 行，功能一句话：**"Converts music in VGM format from one to another chip."**

用法：`vgmtransmute [options] CHIP input.vgm output.vgm`，选项只有 `-sa`（SSG 衰减 dB），支持 `.vgz`。

**没有任何 `ym2413` / `opll` / `ym2151` / `opm` 代码**（全仓 grep 只命中两处头部注释占位：`vgm.js:98 // YM2413 clock`、`vgm.js:106 // YM2151 clock`，写头时置 0）。

---

## 2. 源文件

| 文件 | 行数 | 作用 |
|---|---|---|
| `vgm.js` | 325 | VGM 头/命令类 + `read()` + `write()`（**手写字节**） |
| `ym2610b.js` | 165 | `convertYM2608ToYM2610B()`：寄存器块重映射 + ADPCM 重编码 + `optimize()` |
| `ym2608.js` | 50 | YM2608 寄存器/内存状态机（0x200 regs、0x40000 memory） |
| `ym2608rhythm.js` | 531 | 6 个节奏 PCM 常量数组（42262 字节） |
| `adpcm.js` | 102 | OKI/YM2608 式 ADPCM-A 编解码（encode/decode/encode2x）；`DeltaT` 只有 TODO 空类 |
| `cli.js` | 85 | CLI + gzip/gunzip |
| `utils.js` | 3 | `clamp()` |
| `adpcmtest.js` | 21 | 自测：decode 后 re-encode 必须字节完全一致（round-trip 断言） |
| `package.json` | 16 | — |

---

## 3. 转换逻辑（`cli.js:26-33`）

```js
if (cli.chip == "ym2610b") {
  const vgm = new YM2610VGM();
  vgm.read(await readFile(cli.inputPath));
  vgm.convertYM2608ToYM2610B();
  if (cli.ssgAttenuation) vgm.attenuateSSG(-cli.ssgAttenuation);
  vgm.optimize();
  await writeFile(cli.outputPath, vgm.write());
}
```

七步：

1. 读 VGM/GZIP（检测 `1F 8B`）
2. **YM2608→YM2610B 寄存器块重映射**
3. **节奏 ROM 数据搬到 ADPCM-A**（`ym2610b.js:53-62`），其中 i>=4 的样本先 `ADPCM.decode` 再 `encode2x` 做**双倍采样率重编码**（补偿 OPNA 与 OPNB 的节奏采样率差异）
4. **ADPCM-B 重定位**：OPNA 的 ADPCM-B（register 0x100-0x10C + ROM type 0x81 数据块）搬成 OPNB 的 ADPCM-B
5. `attenuateSSG(dB)`：只调 SSG 音量寄存器 0x008-0x00A，`value + round(dB/3)` clamp 0..15（`ym2610b.js:132-148`）
6. `optimize()`：寄存器去重（`ym2610regs[register] != value` 才写），但 `0x012-0x015`（ADPCM-B start/end）**永不去重**（`ym2610b.js:149-164`）
7. 写出 VGM 1.51（`vgm.js:97` 硬编码 `0x151`），`.vgz` 用 `Z_BEST_COMPRESSION`

### 3.1 寄存器块重映射（`ym2610b.js:67-91`）

```js
if (register >= 0x000 && register < 0x010) {
  commands.push(new YM2610Command(register, value));
} else if (register >= 0x010 && register < 0x020) {
  commands.push(new YM2610Command(register + 0xF0, value));   // ADPCM-A 区平移
} else if (register >= 0x020 && register < 0x100) {
  commands.push(new YM2610Command(register, value));
} else if (register >= 0x100 && register < 0x10C) {
  ...
  if (register < 0x102 || register >= 0x109) {
    commands.push(new YM2610Command(register - 0xF0, value)); // ADPCM-B 区平移
  }
} else if (register >= 0x130 && register < 0x200) {
  commands.push(new YM2610Command(register, value));
}
ym2608.setRegister(register, value);
```

**这不是异构架构转换**：两者都是 OPN 家族，FM/SSG 寄存器几乎一致，主要差异是：
- ADPCM 寄存器块整体平移 0xF0
- 节奏采样搬进 ADPCM-A ROM
- 0x10C-0x130 区间必须丢弃

---

## 4. 它有一个真正的芯片状态机

`ym2608.js:3-50`：`registers[0x200]` + `memory[0x40000]`。

`setRegister` 里监听 `0x102/0x103`（设 ADPCM-B 起始地址）、`0x108`（写 ADPCM-B RAM，且要求 `0x100` 的 bit5-7 == 0x60），并用 `getMemoryStart/getMemoryEnd` 按 `0x101` 的 ROM/RAM 模式算 `<<2` 或 `<<5` 地址移位。

**这是唯一需要"理解语义"的地方**——因为要搬迁采样数据。其他都是纯粹的寄存器重映射。

---

## 5. 它没有的东西（我们想要的都没有）

| 没有 | 说明 |
|---|---|
| 按通道聚合 | ❌ |
| 按 note 分块 | ❌ |
| 按音色去重 | ❌（只有寄存器级 `optimize()`） |
| YM2413 / OPLL | ❌ |
| YM2151 / OPM | ❌ |
| 任何音色库或 4-op ↔ 2-op 映射 | ❌ |

**它的"结构化"只到"命令对象 + 芯片状态机"这一层：**

- 解析阶段把字节流变成**带类型的命令对象**：`YM2608Command / YM2610Command / WaitCommand / DataBlockCommand / LoopCommand / StreamSetup / StreamSetData / StreamSetFrequency / StreamStart / StreamStop / StreamStartFast`（`vgm.js:200-325`）
- 但结构单元是 **VGM 命令**，不是音乐事件

---

## 6. 它的两个值得学的地方

### 6.1 循环偏移和总样本数**重算**，不是照抄

`vgm.js:141-143`：

```js
loopOffset = output.length;
loopSamples = totalSamples;
```

写的时候重新计算。**这比 vgm-conv 的 `md2608` 正确**（md2608 是照抄输入偏移，导致循环不准，见 `reference/vgm-conv.md` §6.1 第 2 条）。

### 6.2 寄存器去重但保留有副作用的寄存器

`optimize()`（`ym2610b.js:149-164`）：

```js
if (ym2610regs[register] != value) { ym2610regs[register] = value; ... }
// 但 0x012-0x015（ADPCM-B start/end）永不去重
```

**"同一寄存器写同值可以丢弃"这个优化，必须排除"写同值也有副作用"的寄存器。** 对我们导出时的去重有参考价值。

---

## 7. 命令覆盖度（不完整，遇到未知命令直接抛错）

`vgm.js:38-79` 的 `read()` 是**白名单式**解析，末尾 `else throw new Error("Unsupported command ...")`。

**支持**：`0x56/0x57`（YM2608 端口 0/1）、`0x58/0x59`（YM2610 端口 0/1）、`0x61`、`0x62/0x63`、`0x66`、`0x67`、`0x70-0x7F`、`0x90-0x95`（DAC Stream 全套）

**不支持（会抛错）**：`0x4x` 双芯片、`0x50` SN76489、`0x51-0x55`、`0x5A-0x5F`（OPL 系列）、`0x60`、`0x68`、`0xA0` AY8910、`0xB0-0xBF`、`0xC0`、`0xD0`、`0xE0` PCM seek、`0x30-0x3F`、`0x80-0x8F` YM2612 DAC

### 7.1 三个会咬人的实现细节

1. 头部写死 **VGM 1.51**，SN/YM2413/YM2612/YM2151 等时钟全写 0，只写 YM2608/YM2610（`vgm.js:95-111`）
2. GD3 读取是"从 gd3 offset 一直读到文件尾"（`vgm.js:27-31`），假设 GD3 在文件末尾且无其他尾随数据
3. `DataBlockCommand(0x82/0x83)` 用 `setROMData(0x1000000, 0, data)` 把 ROM 大小硬设为 16MB（`ym2610b.js:117, 122`）

---

## 8. 结论

**它是一个小而干净的"同族芯片寄存器重映射"实现（1340 行，BSD-2），但不是我们的参考对象。**

对 YM2151 → YM2413 没有直接价值（它没有 OPM/OPLL 代码，也没有音色映射）。

**唯一值得记的两条经验**：

1. **循环偏移和总样本数要重算，不能照抄**（这是 `md2608` 犯的错）
2. **寄存器去重必须排除"写同值有副作用"的寄存器**（如 ADPCM start/end）

两条都写进了 `RULES.md` F1 的依据里。
