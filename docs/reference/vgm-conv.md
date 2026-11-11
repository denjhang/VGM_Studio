# vgm-conv — 跨芯片转换的架构范本（○ 最该抄的转换器骨架）

| 项 | 值 |
|---|---|
| 路径 | `E:\working\vscode-projects\Reference_Project\vgm_libs\vgm-conv-main` |
| 作者 | Mitsutaka Okazaki（digital-sound-antiques） |
| 版本 | v0.14.1 |
| 许可 | **ISC**（宽松，可借鉴架构） |
| 语言 | TypeScript（Node） |
| 规模 | `src/` 24 个转换器 + 主控，合计 **4612 行** |
| 本机可跑 | ❌ 无 `node_modules`（只有源码） |

---

## 1. 它是什么

**"Chip-type and clock converter for VGM"** —— VGM 的芯片类型 + 时钟转换器。

两类能力：
1. **时钟转换**：改芯片 clock 但保持音高（重算 KC/KF 或 fnum/blk）
2. **类型转换**：跨芯片

依赖：`vgm-parser ^0.12`（解析/写回）+ `ym-voice ^0.3.2`（音色格式互转）。

### 1.1 支持的类型转换方向

| FROM | TO |
|---|---|
| ay8910 | ym2151, ym2203, ym2608, ym3526, ym3812, y8950, ymf262 |
| sn76489 | ay8910, ym2203, ym2608 |
| ym2413 | ym2608, ym3812, y8950, ym3526, ymf262 |
| ym2203 / ym2203.ssg | ay8910 / ym3812 / ymf262 |
| **ym2203.fm / ym2608.fm / ym2612.fm** | **ym2413** |
| **ym2612, ym2612.fm, ym2612.dac** | **ym2413** |
| **ym2608, ym2608.fm, ym2608.r** | **ym2413** |
| ym2203.fm, ym2608.fm, ym2612.fm | ym2151 |
| ym3812 | ym3526, y8950, ym2413 |
| ym3526, y8950 | ym3812, ym2413 |

**⚠️ YM2151 只出现在 TO 列，从来不是 FROM。** 全仓 `grep from.chip === "ym2151"` = 0 结果。

原文警告（`README.md:27`）：

> "downgrade conversion (ex. YM2203 to YM2413 conversion) is highly limited thus don't expect much."

**→ 我们要做的 YM2151 → YM2413，它没有实现。但它的 `opn-to-ym2413-converter.ts`（437 行）是我们最该读的 4-op → 2-op 范本。**

---

## 2. 架构：逐寄存器翻译，**没有中间表示**

### 2.1 抽象接口的粒度 = 一条命令进、若干条出

`src/converter/vgm-converter.ts:30, 45-55`：

```ts
abstract convertCommand(cmd: VGMCommand): Array<VGMCommand>;

convert(cmd: VGMCommand): Array<VGMCommand> {
  const tmp = this.convertCommand(cmd);
  if (this._sink) {                    // 支持串联（fm 转换器 → clock 转换器）
    let res = new Array<VGMCommand>();
    for (const cmd of tmp) res = res.concat(this._sink.convert(cmd));
    return res;
  }
  return tmp;
}
```

**整套骨架只有 110 行**（`vgm-converter.ts` 68 + `vgm-write-data-buffer.ts` 42）。

### 2.2 主循环只做逐条 parse → convert → push

`src/index.ts:161-186`：

```ts
while (true) {
  if (input.offsets.data + index === input.offsets.loop) {
    ds.markLoopPoint();
    for (const e of converter.getLoopCommands()) ds.push(e);
  }
  const cmd = parseVGMCommand(data, index);
  if (cmd == null) break;
  for (const e of converter.convert(cmd)) ds.push(e);
  index += cmd.size;
  if (cmd instanceof VGMEndCommand) break;
}
```

### 2.3 每个转换器自建 256 字节 shadow 寄存器页

`opn-to-ym2413-converter.ts:142-144`：

```ts
_div = 0;
_regs = [new Uint8Array(256), new Uint8Array(256)];
_buf = new VGMWriteDataCommandBuffer(256, 1);
```

### 2.4 不认识的命令**直接透传**（不丢、不重建）

`ym2413-to-ym2608-converter.ts:200-204`：

```ts
convertCommand(cmd: VGMCommand): Array<VGMCommand> {
  if (cmd instanceof VGMWriteDataCommand && cmd.chip === "ym2413" && cmd.index === this.from.index) {
    return this._convert(cmd);
  }
  return [cmd];
}
```

### 2.5 写出前做微指令级优化

`vgm-write-data-buffer.ts:12-36`：用 `_outRegs` 缓存目标寄存器最后值，**重复值丢弃**、**同寄存器旧值从待发队列里 splice 掉**。

**这是微指令级重排，不是乐句级重建。**

---

## 3. 音色映射：三层机制（★ 核心参考）

### 3.1 自动挑 OPLL ROM 音色（autoMap）

`opn-to-ym2413-converter.ts:171-173`：

```ts
_estimateOPLLVoice(opn: OPNVoice): VoiceDefinition {
  return opn.toOPL()[0].toOPLLROMVoice();
}
```

即：OPN 4-op `OPNVoice` → `toOPL()`（变成 2 个 OPL 音色，取第 0 个）→ `toOPLLROMVoice()`（在 OPLL 15 个 ROM 音色里找最接近的，返回 `{program, volumeOffset, octaveOffset}`）。

**这正是我们 `GOAL.md` §6 的"路径 A：匹配 15 个 ROM 预置"的现成实现思路。**

### 3.2 64 位十六进制哈希作为音色主键

`opn-to-ym2413-converter.ts:186-243`：

```ts
const hash = _normalizeTotalLevel(rawVoice)
  .map((e) => ("0" + e.toString(16)).slice(-2))
  .join("");
...
let vdef = this._voiceDefMap[hash];
if (vdef == null) {
  if (this._autoMap) vdef = this._estimateOPLLVoice(voiceInfo.opnVoice);
  vdef ||= fallback_voice;
  this._voiceDefMap[hash] = vdef;
}
this._currentVoice[ch] = { hash, ...vdef };
```

**关键细节**：`_normalizeTotalLevel` 先按 ALG 把 TL 归一到"最响算子为 0"，**再做 hash**。这样同一音色不同音量会命中同一个 hash。

`fallback_voice = { program: 1, volumeOffset: 15, octaveOffset: 0 }`（`:19`）——program 1 = Violin，volumeOffset 15 ≈ 静音。

### 3.3 用户可覆盖的 voice-table（JS 文件）

`README.md:261-299`：键就是那个 64 位 hex，值 `{i: 音色号, v: 音量偏移, o: 八度偏移}`。可定义 program 16–1023 的自定义音色（8 字节数组）。`autoMap:false` 则无匹配时静音。

### 3.4 音量：按 ALG 平均载波 TL，再降到 4 bit

`opn-to-ym2413-converter.ts:246-283`：

```ts
const amps = [regs[0x40+nch]&0x7f, regs[0x44+nch]&0x7f, regs[0x48+nch]&0x7f, regs[0x4c+nch]&0x7f];
switch (alg) {
  case 4: vol = (amps[2] + amps[3]) / 2; break;
  case 5: case 6: vol = (amps[1] + amps[2] + amps[3]) / 3; break;
  case 7: vol = (amps[0]+amps[1]+amps[2]+amps[3]) / 4; break;
  default: vol = amps[3]; break;      // ALG 0-3 串联，取 carrier
}
const vv = (vol >> 3) + volumeOffset;              // 127 -> 15
const d = (program << 4) | Math.min(15, Math.max(0, vv));
this._y(0x30 + ch, d);
```

**这段直接可搬到我们的 YM2151 → YM2413 转换器**（只需把寄存器偏移从 OPN 的 `0x40/0x44/0x48/0x4c + ch` 改成 OPM 的 `0x60 + op*8 + ch`）。

### 3.5 4-op → 2-op 的另外三份范本

| 文件 | 行数 | 特点 |
|---|---|---|
| **`opn-to-ym2413-converter.ts`** | **437** | 最完整：hash 音色表 + ALG 音量 + 节奏映射 |
| `opl-to-opll-converter.ts` | 130 | 最干净：无表，纯参数编码 `opl.toOPLLVoice().encode()` |
| `ym2413-to-opl-converter.ts` | 189 | 反向 2→4，如何"造出第二个 operator" |
| `ym2413-to-ym2608-converter.ts` | 206 | 反向 2→4，带 KSL 模拟 + AMS 补偿 |

反向 2→4 的关键（`ym2413-to-ym2608-converter.ts:21-23, 83-107`）：
- `getOPLLRomVoices(variant).map(e => ({opll: e, opn: e.toOPN()}))`
- `_setInstVolume` 把 OPLL 的 2 个 slot 展开成 OPN 4 个 slot（写 0x30/0x50/0x60/0x70/0x80 各 4 次）
- **KSL 模拟**（`KSLTable`，`:7`；`_setSlotVolume` `:66-81`）
- **AMS 强行设 2**：补偿 OPLL 4.5dB 与 OPNA 5.9dB 的 AM 深度差

---

## 4. 时间与通道

### 4.1 时间：完全原样搬运采样数

转换器只处理 `VGMWriteDataCommand`。所有 `VGMWaitCommand / WaitWord / WaitNibble / Wait735 / Wait882 / WaitN` 都走 `return [cmd]` 原样输出。主循环不累加也不重算等待。

**唯一的例外**：YM2612→YM2413 的 DAC 仿真需要插等待（`ym2612-to-ym2413-converter.ts:72-78`）：

```ts
const freq = (fnum * goalClock) / 72 / (1 << 19);
const cycleInSeconds = 1.0 / freq;
const nnnn = Math.round(44100 * (cycleInSeconds / 4));
this._buf.push(new VGMWaitWordCommand({ count: nnnn }), false);
```

**→ 这印证了我们 `GOAL.md` §6 的判断：YM2151 → YM2413 的时域完全相通，不需要重排时间。**

### 4.2 通道映射：OPN 6 通道 ↔ OPLL 9 通道索引重排

`opn-to-ym2413-converter.ts:181-184`：

```ts
_identifyVoice(ch: number) {
  const port = ch < 3 ? 0 : 1;
  const nch = ch < 3 ? ch : (ch + 1) & 3;   // 内部 ch = port*3+nch
```

实际写 OPLL 时统一用线性 `ch = port*3 + nch`（`:247-248`），即 0,1,2,3,4,5。

**节奏通道**（`ym2608-to-ym2413-converter.ts:5-13`）用位重排：

```ts
function _convertRhythmFlags(flags: number) {
  const bd = flags & 1;  const sd = (flags >> 1) & 1;  const top = (flags >> 2) & 1;
  const hh = (flags >> 3) & 1;  const tom = (flags >> 4) & 1;  const rim = (flags >> 5) & 1;
  return (bd << 4) | (sd << 3) | (tom << 2) | (top << 1) | (rim | hh);
}
```

### 4.3 时钟转换：改 clock 不改时间，改 KC/KF

`opm-clock-converter.ts:22-24`：

```ts
this._ratio = this.to.clock / from.clock;
this._keyDiff = Math.round(12 * Math.log2(1.0 / this._ratio) * 256);
this._lfoDiff = Math.round(16 * Math.log2(1.0 / this._ratio));
```

即：clock 比例 → KC 的 keyOffset + KF 修正。

---

## 5. 输出：用库，不手拼字节

`src/index.ts:152-190` 用 `vgm-parser` 的 `VGMDataStream` + `parseVGMCommand` 组装，然后：

```ts
const converted = convertVGM(vgm, from, to, opts);
if (options["no-gd3"]) converted.gd3tag = undefined;
converted.volumeModifier = 0x00;
const compress = /\.vgz/i.test(output);
const res = Buffer.from(converted.build({ compress }));
fs.writeFileSync(output, res);
```

**头部重写、GD3 保留、`.vgz` 自动 gzip 全由 `vgm-parser` 的 `VGM.parse/build` 负责。**

---

## 6. `md2608` 子项目：一个 C++ 重写的教训

`md2608/` 是仓库里额外加的 C++ 重写（YM2612+SN76489 → YM2608），**不属上游**。

| 文件 | 行数 | 作用 |
|---|---|---|
| `converter.cpp` | 462 | 三步转换：FM 直映 / SN→SSG / DAC→ADPCM |
| `adpcm_encoder.cpp` | 138 | YM2608 ADPCM 4bit 编码器（移植 node-spfm） |
| `vgm_parser.cpp` | 219 | 只支持 v1.50+，命令子集 |
| `vgm_writer.cpp` | 193 | **手写字节拼 VGM** |
| `vgm_types.h` | 149 | `VGMHeader`（0x00-0xFF 全字段） |
| `main.cpp` | 145 | CLI |

### 6.1 它的坑（`md2608/TEST_RESULTS.md:286-305`）★

1. **输出总样本数与输入不一致**（命令优化/合并改变了 delay 累加）—— 样本计数算法需改进
2. **循环偏移直接从输入复制**，没按新文件重算 → 循环不准
3. 不解析/不生成 GD3
4. **ADPCM-B 未使用**；`converter.cpp:458` 留着 `// TODO: Map YM2612 DAC commands to YM2608 ADPCM key-on commands`，**DAC 音符时序映射没做完**（最大缺陷）
5. 时钟 YM2608=7,987,200 Hz 写死

**这 5 条正是"手写 VGM 写出"的典型失败模式。** 印证了 `RULES.md` F1（禁止自己拼字节）。

### 6.2 `md转ym2608日志2.txt` 不是踩坑记录

15 行，是**给 AI 的任务提示词**：要参考 vgm-conv 写一个 C++ 版，把 YM2612.DAC → YM2608.ADPCM 需要重新编码，研究 node-spfm 的两种 DAC→ADPCM 方法。

---

## 7. `analyze_vgm.py`（135 行）

Python 3 单文件，无第三方依赖，**只读分析器**。

- 校验 magic、读 version/EOF/total/loop samples（`:19-30`）
- 读 v1.10+ 的 SN76489/YM2413/YM2612/YM2151 clock（`:33-38`），v1.51+ 再读 YM2203/YM2608（`:41-56`）
- 线性扫命令统计个数
- **但只处理命令子集**，其他命令一律 `pos += 1`（`:70-105`）——**遇到不认识的命令会错位，是快速 hack，不是健壮解析器**

**这就是 `RULES.md` F2 禁止的那种做法**（手工脚本核对 VGM）。它之所以存在，是因为它只是"摸清用了几次写"的临时工具，不是产品代码。**我们不学。**

---

## 8. 我们能从 vgm-conv 拿什么

### 8.1 架构（110 行骨架）

| 内容 | 位置 | 用途 |
|---|---|---|
| `VGMConverter` 抽象基类 | `vgm-converter.ts`（68 行） | `convertCommand: cmd → cmd[]` + `pipeTo` 串联 |
| `VGMWriteDataCommandBuffer` | `vgm-write-data-buffer.ts`（42 行） | 写前去重（同寄存器旧值 splice 掉） |
| `getInitialCommands` / `getLoopCommands` 钩子 | — | 初始化和循环点处理 |

**但这套骨架和我们的架构不同**：我们是 "JSON 语义层 → 目标芯片 driver"，它是 "寄存器流 → 寄存器流"。**我们不用它的架构，只用它的音色映射算法。**

### 8.2 音色映射（★ 真正该抄的）

| 内容 | 位置 | 用于 |
|---|---|---|
| **ALG → 载波 → 平均 TL → 4bit** | `opn-to-ym2413-converter.ts:246-283` | 我们的音量映射（改寄存器偏移即可） |
| **`toOPLLROMVoice()` 自动匹配 15 个 ROM 预置** | `:171-173` | 我们 `GOAL.md` §6 路径 A 的实现参考 |
| **hash 前先 `_normalizeTotalLevel`（按 ALG 归一到最响算子为 0）** | `:186-243` | 音色指纹算法（避免音量影响匹配） |
| **voice-table 可用户覆盖** | `README.md:261-299` | 给用户手动修正映射的机制 |
| **反向 2→4 的 KSL 模拟 + AMS 补偿** | `ym2413-to-ym2608-converter.ts:66-107` | 将来做反向转换时参考 |

### 8.3 明确不拿

| 不拿 | 原因 |
|---|---|
| 逐寄存器翻译架构 | 违反我们 `RULES.md` E5（转换只在 JSON 语义层） |
| `md2608` 的手写 VGM 写出 | 它的 5 个坑就是反例（`RULES.md` F1） |
| `analyze_vgm.py` 的命令扫描 hack | 遇到未知命令就错位（`RULES.md` F2） |

---

## 9. 结论

**vgm-conv 是我们最该读的"4-op → 2-op 音色映射"参考，但不是架构参考。**

三个最重要的收获：

1. **`opn-to-ym2413-converter.ts:246-283` 的 ALG 音量映射可以直接搬到 YM2151 → YM2413**（OPM 的载波 TL 偏移是 `0x60 + op*8 + ch`）
2. **hash 前先归一化 TL** 这个技巧——否则同一音色不同音量会被当成不同音色
3. **它印证了"时域完全相通"**：所有 wait 命令原样搬运，只有 DAC 仿真才插等待

而它的 `md2608` 子项目是**反面教材**：手写 VGM 写出，结果总样本数不对、循环偏移不对、GD3 没有、DAC 时序没做完。
