# FamiStudio — 状态机形状参考（○ 只读，**不支持 YM2151**）

| 项 | 值 |
|---|---|
| 仓库 | BleuBleu/FamiStudio |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\FamiStudio` |
| 许可 | **MIT** |
| 语言 | **C# / .NET 8** |
| 规模 | 318 文件 / 566MB / `VgmFile.cs` 2538 行 |
| 本机可编 | ❌ **无 .NET SDK**（只有 runtime 8.0.34，`--list-sdks` 无输出） |
| 有 build 产物 | ❌ 无 |

---

## 1. 它是什么

**Famicom（NES）音乐制作工具** —— NES 风格的 tracker/音序器，能导出 ROM、NSF、VGM。有 VGM 导入和导出。

---

## 2. 最关键的一条：**它不支持 YM2151**

很多人（包括我之前）以为 FamiStudio 的 `VgmFile.cs` 是"寄存器→音符算法"的通用参考。**对 YM2151 完全无效。**

### 2.1 它支持的芯片（VGM 读入路径）

- 2A03 四通道（Pulse1/Pulse2/Triangle/Noise）+ DMC（命令 `0xB4`）
- FDS（`0xB4` 重映射）
- VRC7（命令 `0x51`）
- 5B / EPSM（`0x56/0x52/0x58/0x55`, `0x57/0x53/0x59`, `0xA0`→5B）
- MMC5

### 2.2 它**不支持**的

**YM2151（`0x54`）、YM2612、OPL、VRC6/N163** —— 走 `else` 分支，在 `VgmFile.cs:2398-2403` 记 "Unknown VGM Chip Data"。

> 直接引用：`VgmFile.cs:2398-2403`
> ```csharp
> else
> {
>     // Unknown VGM Chip Data
>     ...
> }
> ```

`VgmHeader` 结构里虽然有 `ym2151clock` 字段（从 VGM 头部读出来），但 `Load()` 里**没有一条 `0x54` 分支**去用它。

---

## 3. 文件结构

`FamiStudio/Source/IO/VgmFile.cs`（**2538 行**）：

| 方向 | 函数 | 行号 | 说明 |
|---|---|---|---|
| **读**（VGM → 工程） | `public Project VgmFile.Load(string filename, int patternLength, int frameSkip, bool adjustClock, bool reverseDpcm, bool preserveDpcmPad, bool importDmcVals, bool ym2149AsEpsm, bool framePacing, int tuning = 440)` | **1669** | 主体 1669–2534（约 865 行） |
| **写**（工程 → VGM） | `public unsafe static void Save(Song song, string filename, string trackTitle, ... bool smoothLoop)` | **77** | 主体 77–428 |
| 入口 UI | `VgmImportDialog` ctor / `ShowDialogAsync` | `Source/App/Common/Dialogs/VgmImportDialog.cs:27/44` | 从 `Source/App/Common/FamiStudio.cs:1105` 调用 |
| 常量借用 | `NotSoFatso.STATE_*` | `Source/IO/NotSoFatso.cs:1-105` | **仅借常量名**，导入路径不走 NSF 模拟器 |

---

## 4. 核心算法（形状可抄，语义不可抄）

| 函数 | 位置 | 作用 |
|---|---|---|
| `int GetState(int channel, int state, int sub)` | `VgmFile.cs:865-1148`（384 行） | 把打包好的寄存器镜像（`apuRegister[]`, `vrc7Register[]`, `epsmRegisterLo/Hi[]`, `s5bRegister[]`, `fds*Table[]`）**按需翻译**成 `STATE_PERIOD / STATE_VOLUME / STATE_DUTYCYCLE / STATE_FMTRIGGER / STATE_FMPATCHREG / ...` |
| `bool UpdateChannel(int p, int n, Channel channel, ChannelState state)` | `VgmFile.cs:1152-1629`（478 行） | 每帧每通道的"状态机 → 音符"：DPCM 分支 1157–1270，普通分支 1271–1626。写音符 `pattern.GetOrCreateNoteAt(n).Value = ...`（:1572），fine pitch（:1596） |
| `int GetBestMatchingNote(int period, ushort[] noteTable, out int finePitch)` | `VgmFile.cs:582-600`（19 行） | **O(N) 线性扫描** 97 项 note table 找 `\|table[i]-period\|` 最小者 |
| `GetDutyInstrument` / `GetFdsInstrument` / `GetVrc7Instrument` / `GetS5BInstrument` / `GetEPSMInstrument` / `GetDPCMInstrument` | `VgmFile.cs:602/619/653/697/760/740` | 由寄存器内容反推/复用乐器 |
| `ChannelState` 结构 | `VgmFile.cs:556` | 持有 `period / note / pitch / volume / octave / state / instrument` |

### 4.1 可抄的部分（纯算法，无 .NET 数据模型依赖）

`GetState`(384) + `UpdateChannel`(478) + `GetBestMatchingNote`(19) + 各 `Get*Instrument`(262) ≈ **1100–1200 行**，是纯整型/位运算 + 若干 `double` 累加器，**可 1:1 直译**。

### 4.2 不可抄的部分（工程模型胶水，约 1300 行）

建通道、建乐器、pattern/note 分配、groove 反推、DPCM 去重、`ConvertToCompoundNotes` —— 全部绑在 `Project`/`Song`/`Channel`/`Pattern`/`Note` 上。**这是重写，不是移植。**

---

## 5. 时间模型：帧量化（**与我们的要求直接冲突**）

| 证据 | 行号 |
|---|---|
| `samplesPerFrame = framePacing ? 44100.0/(NesApu.FreqNtsc/29780.5) : 735;` | `VgmFile.cs:1718`（PAL 版 `:1798` 用 882） |
| `samples = samplesPerFrame * 0.5;` // 半帧偏移 | `:1719` |
| 帧号唯一来源：`p = (frame - frameSkip)/PatternLength; n = (frame - frameSkip)%PatternLength; frame++;` | `:1937-1941`（末尾 `:2449-2451` 同样） |
| 音符只落在整数帧 `n` 上 | `:1570-1572`, `:1596`, `:2079-2080` |
| 输出反推 groove | `:2458-2480` `song.ChangeFamiStudioTempoGroove(...)` |

### 问题

- 所有寄存器事件被**塌缩到 1/60s（16.67ms）或 1/50s（20ms）网格**
- `framePacing` 只是把网格改成 ≈733.79/881.87 样本（更贴 CPU 时钟），**仍是整数帧网格**
- VGM 里 44100Hz 的任意等待（`0x61` 非 735 倍数）会被挤到相邻帧
- 多个事件同帧会**互相覆盖**（`GetOrCreateNoteAt(n)` 只保留一个 Note，`Pattern.cs:144-152`）
- 音高只保留 `Note.Value`(0-95 半音) + `FinePitch`（`sbyte`，`Note.cs:28-29` = -128..127），超 `FinePitchMax` 截断（`VgmFile.cs:1592`）

**结论：无法做采样级还原，也无法保留原始"寄存器写入时刻"。**

这正是我们 `RULES.md` F4 禁止量化的现实依据 —— 照抄 FamiStudio 就会得到它这套帧栅格。

---

## 6. C# → C++ 移植难度

| 依赖 | 出现处 | 难度 |
|---|---|---|
| `unsafe struct VgmHeader { fixed byte Vgm[4]; ... }` | `VgmFile.cs:14-75` | **易**（`#pragma pack(1)` + `memcpy`） |
| `ReadOnlySpan<byte>` / `Span<byte>` / `.ToArray()` | `:1708, 1852, 2103, 2429` | **易**（裸指针 + `(ptr,len)`） |
| LINQ：`Take/SequenceEqual/Select/Where/Any/ToArray` | 全文件 15 处 | **易**（手写循环） |
| `BitConverter.ToInt32/ToUInt32/ToUInt16` | 大量 | **易**（`memcpy` + 字节序） |
| `Array.Fill` / `Enumerable.Repeat` | `:1683` | **易**（`std::fill`） |
| `Dictionary<int,int>` / `List<T>` | `:551, 552, ...` | **易** |
| `GZipStream`（.vgz 解压） | `:1649-1661` | **中**（需 zlib/miniz） |
| **整个 FamiStudio 数据模型** | `:1570-1622, 608-863, 2458-2533` | **难**：`Project.cs`(3423) + `Channel.cs` / `Pattern.cs` / `Note.cs` / `Instrument.cs` / `Envelope.cs` / `DPCMSample.cs` 全要移植或打桩 |
| `NoteTableSet` / `GetOrCreateNoteTableSet`（97 项周期表） | `Source/Player/NesApu.cs:280-467, 799-` | **易**（纯浮点+整型公式，可整段照抄） |

**总评**：算法核心易（1–2 天），工程模型难（1–2 周）。**且因只支持 NES 系，YM2151 场景下性价比极低。**

---

## 7. 复杂度分析

外层是"逐命令"解析（`while (vgmDataOffset < vgmFile.Length)` `:1827`），但**每遇到 wait 就 `while (samples >= samplesPerFrame)` 逐帧推进**（`:1935-1941`），每帧再对**全部通道**调 `UpdateChannel`（`:2079-2080`）。

```
O(VGM命令数 + 总帧数 × 通道数 × (音符表97 + 乐器查重))
```

性能热点：逐帧 × 全通道（分钟级曲子 ≈ 数万帧 × 10~15 通道），以及 `GetVrc7Instrument`/`GetS5BInstrument` 里对 `project.Instruments` 的**线性查重**（`:658-666, 621-632`）。

**不是"纯逐事件"**，这一点我们不要学 —— 我们的 `atSample` 是逐事件的，没有逐帧扫描。

---

## 8. 我们应该从它拿什么

| 拿 | 不拿 |
|---|---|
| `ChannelState` 的字段设计（`period / note / pitch / volume / octave / state / instrument`） | 帧量化时间模型 |
| `UpdateChannel` 的"差分检测"思路：状态变化 → 产出事件 | 逐帧 × 全通道的扫描复杂度 |
| `GetBestMatchingNote` 的"找最近音符 + 输出 finePitch"模式 | 它对 NES 寄存器语义的硬编码（`apuRegister[0x4015]`、length counter 表 `:491-497`、DMC 率表 `:499-509`） |
| `Get*Instrument` 的"由寄存器反推乐器 + 指纹去重"套路 | 整个 `Project`/`Song` 数据模型 |

**YM2151 的差分逻辑必须自己写。** FamiStudio 只提供"状态机的形状"。

---

## 9. 结论

**MIT 许可是它唯一的优势，但用不上**：本机无 .NET SDK 编不了，且**它不支持 YM2151**。

它的价值仅限于：读它的 `UpdateChannel()` 理解"状态机怎么把寄存器流变成音符事件"这个模式，然后**用 C++ 自己写 YM2151 版本**。
