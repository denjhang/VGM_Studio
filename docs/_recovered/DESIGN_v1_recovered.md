# VGM Studio 设计文档

> 状态：草案（2026-11-11）
> 工作目录：`E:\working\zcode-projects\VGM_Studio`
> 本文是主设计文档。任何实现细节与本文冲突时，以本文为准；确需变更时先改本文。

---

## 1. 定位

VGM Studio 是一个**图形化的 VGM 结构化 / 转换 / 移植工具**：

```
VGM ──解析──► 寄存器流 ──结构化──► VGSJ(中间格式) ──生成──► VGM
                                        ▲
                                        │
                                    MIDI 导入
```

初步目标：**把不同芯片类型的 VGM 互相转换和移植**。

真正的难点不在 VGM 读写（那是 800 行的体力活），而在中间那一步：**VGM 不是 tracker 数据，也不是 MIDI 数据，必须按通道把寄存器流结构化**。以 YM2151 为例，结构化 =

> 把这个 VGM 里全部 YM2151 寄存器写入，还原成"8 个通道各自在什么时刻、发了什么音、音高多少、音长多少、音量多少、音色参数怎么变"。

只有这一步做对，后面的改写与跨芯片转换才有依据。

---

## 2. 第一原则：寄存器原值是唯一真值，其余全是参考

**这是本项目最重要的一条设计约束，也是一切架构的源头。**

### 2.0 为什么要这样

VGM 的时间真实来源是**采样计数**（`0x61 nn nn` 等 nn 采样、`0x62` 等 735、`0x63` 等 882），寄存器写入落在采样边界上，事件位置是任意值——**VGM 里没有 BPM**。

更关键的是：**把寄存器值换算成"音符 / 音高 / 音量"需要芯片特定的换算公式，而这些公式在框架阶段无法确定。** 例如 YM2151 的 KC+KF → 频率涉及芯片时钟、分频、调音基准；把 period 反查音符表还要处理音域边界、微调、Pitch LFO。这些公式一旦写死，换一批 VGM 就会出现偏移，而且**看不出来错在哪**。

因此本项目的立场是：

> **只把原始寄存器值（以及它们的时间戳）当作真值落进 JSON。**
> **音符、音名、音高、网格位置、行号——全部是"参考显示值"，由可替换的换算器实时算出来，绝不写回数据、绝不当成事实。**

由此推出四条硬约束：

| # | 约束 |
|---|---|
| R1 | **数据层只存原始寄存器写流**：`(chip, port, reg, val, atSample)`。这是唯一无损、唯一权威的东西。 |
| R2 | **结构化事件（note/gateoff/instrument）是从寄存器流推导出来的"视图"**，带 `confidence` 与 `derivedFrom`（指向寄存器写流的索引范围）。可以随时丢弃重算。 |
| R3 | **换算公式全部可插拔**（`IPitchMapper` / `IGridMapper` / `IVolumeMapper`），因为不确定，所以不能硬编码。默认实现放在可替换的模块里。 |
| R4 | **音序器画面上没有"真实音高"**。音条纵向位置是参考音高，横向位置是参考位置；两者都必须能显示出"这是个近似值"。 |

### 2.1 三层数据，真值只有一层

| 层 | 内容 | 权威性 | 可否丢弃 |
|---|---|---|---|
| **L0 寄存器流** | 有序的 `(chip, port, reg, val, atSample)` | ✅ **唯一真值** | ❌ 绝不可丢 |
| **L1 结构视图** | 从 L0 推导的 note / gateoff / instrument 变化 / lane 点 | 参考值，带 confidence | ✅ 随时重算 |
| **L2 显示** | 音名、音条像素位置、行号、网格线 | 纯视觉 | ✅ 随时重算 |

**关键推论：保存文件时 L0 必须完整；L1 存或重算都行；L2 一律不存。**

### 2.2 时间表示：采样位置是时间维度的真值

| 层 | 类型 | 语义 | 是否原始数据 |
|---|---|---|---|
| **S0 采样位置** | `int64`（VGM 采样，标准 44100Hz） | 时间维度的唯一真值。每条寄存器写都带 | ✅ 从 VGM 直接读出 |
| S1 微秒 | `double` | 显示用，由 S0 固定换算 | 派生 |
| S2 tick（含 frac） | `int64 + double frac` | 仅供绘制/吸附，用 `IGridMapper` 算 | 派生 |
| S3 行/小节 | `int row` | 纯视觉 | 纯视觉 |

**注意**：时间方向上 S0 是真值（采样数就是采样数，不存在歧义）。但**音高方向上没有等价的"S0"**——音高的真值就是寄存器里的 KC/KF 字节本身，把它变成"note 60"永远是一次有损且有争议的换算。这就是为什么 R1 要求存寄存器原值。

### 2.3 换算器接口（因为不确定，所以抽象）

```cpp
// 音高参考值 —— 唯一职责：把寄存器原值算成人类可读的近似音高
struct PitchView {
  int    nearestNote;  // 最接近的 MIDI 音号（仅参考）
  double centsOffset;  // 偏离多少音分（仅参考）
  bool   exact;        // 换算是精确的还是近似的
  const char* method;  // 用了哪个换算器（UI 上要显示出来）
};

class IPitchMapper {
public:
  virtual ~IPitchMapper() = default;
  // 输入芯片状态（寄存器快照），输出参考音高
  virtual PitchView map(const ChipRegState& regs, int channel) const = 0;
  virtual const char* name() const = 0;
};

// 网格/位置参考值
class IGridMapper {
public:
  virtual ~IGridMapper() = default;
  virtual double sampleToTick(int64_t sample) const = 0;   // 供绘制
  virtual int64_t tickToSample(double tick) const = 0;
  virtual const char* name() const = 0;                     // 例如 "120 BPM 4/4 (assumed)"
};
```

默认实现可以很朴素（固定 BPM 假设、标准音高表），但**接口边界必须存在**，因为后面会不断替换。

### 2.4 显示层必须暴露"这是近似"

这是产品诚实性的要求，不是可选装饰：

- 音条可以用半透明/虚线边框表示 `exact == false`
- 状态栏常驻显示当前用的换算器名字（`pitch: OPM/standard-clock`、`grid: 120BPM-assumed`）
- 悬停音条显示**原始寄存器值**（`KC=0x2E KF=0x1A → ≈D#4 +12c`），而不是只显示音名

用户拍板这句是对的：**"音序器里面所有网格线都只是参考。"**


---

## 3. 中间格式 VGSJ

文件：`.vgsj`（JSON，可读、可 diff、可手改）。

### 3.1 三层结构（对应 §2.1 的 L0/L1/L2）

```jsonc
{
  "format": "vgm-studio/song",
  "version": 1,

  // ── L0：真值层（唯一权威，绝不可丢）─────────────────────
  "timeBase": {
    "sampleRate": 44100,      // 时间单位
    "vgmRate": 60,            // header rate（60/50），仅影响 0x62/0x63
    "loopSample": -1,         // 循环点（采样位置）
    "totalSamples": 1234567
  },
  "chips": [
    { "id": "ym2151.0", "type": "YM2151", "vgmChipType": 0x54,
      "clock": 3579545, "ports": 2, "instances": 1,
      "rom": null }           // DataBlock ROM/PCM：外链或 base64
  ],
  // 寄存器流：唯一真值。扁平、有序、已差分压缩
  "regStream": {
    "encoding": "delta",      // 见 3.4
    "events": [
      // [chipIdx, port, reg, val, sampleDelta]
      [0, 0, 0x20, 0xC7, 0],
      [0, 0, 0x28, 0x3E, 0],
      [0, 0, 0x30, 0x1A, 0],
      [0, 0, 0x08, 0x78, 12],   // key-on
      [0, 0, 0x08, 0x00, 8820]  // key-off
    ]
  },
  // 原始未解析内容与诊断
  "raw": {
    "vgmDataOfs": 291,
    "commands": "base64:...",  // 完整原命令流，用于位精确往返
    "unhandled": [             // 解析器没处理/主动跳过的命令
      { "cmd": "0x93", "atSample": 44100, "note": "DAC Stream 透传" }
    ]
  },

  // ── L1：结构视图（参考值，可丢弃重算）───────────────────
  "views": {
    "generatedBy": { "structurer": "ym2151/v1", "pitchMapper": "opm-std",
                     "gridMapper": "120bpm-assumed" },
    "instruments": [
      { "id": 3, "chip": "ym2151.0", "kind": "FM",
        "hash": "a91f3c...",   // 参数指纹，用于复用判等
        "sourceRegs": [0, 18], // 溯源：L0 里哪段寄存器写产生了它
        "fm": { "alg": 4, "fb": 5,
                "ops": [ { "dt1":3,"mul":2,"tl":0,"ks":1,"ar":31,
                           "d1r":8,"d2r":4,"sl":2,"rr":6,"dt2":0,"am":0 } /* ×4 */ ] } }
    ],
    "channels": [{
      "chip": "ym2151.0", "index": 0, "name": "FM1", "kind": "FM",
      "events": [
        { "type": "note", "atSample": 0, "durSample": 8820,
          "nearestNote": 60, "cents": 12.0, "exact": false,  // ← 全是参考值
          "ins": 3, "vel": 15, "attack": true,
          "sourceRegs": [3, 7],      // 溯源到 L0
          "confidence": 0.98 },
        { "type": "gateoff", "atSample": 8820, "sourceRegs": [7] }
      ],
      "lanes": [
        { "param": "pms", "points": [[0, 3], [17640, 0]] },  // [sample, raw value]
        { "param": "pan", "points": [[0, 64]] }
      ]
    }]
  }
}
```

### 3.2 为什么这样分层

**L0 是合同的底线。** 结构化必然有损——跨芯片时更是彻底有损，甚至同一芯片换一个音符表实现都会变。若只存 L1，一次 VGM→VGSJ→VGM 就会丢信息。

- **原路往返**：直接走 `raw.commands`（甚至不用 regStream），位精确。
- **编辑后往返**：用 `regStream` 重新生成命令流；未被编辑的段仍可回退到 raw。
- **L1 丢了也不怕**：换一个 `structurer` / `pitchMapper` 重算即可。这就是为什么 L1 里每个事件都带 `sourceRegs` 和 `confidence`。

`unhandled` 是诊断面板：它告诉你"这个 VGM 里有多少东西我没搞懂"，比任何"导入成功"的提示都有用。

### 3.3 编辑如何落地

用户在音序器里拖动一个音条 → **不修改 L1 的音符，而是回写 L0 的寄存器写**：

1. 找到该事件的 `sourceRegs` 范围；
2. 按新的时间/音高反算出对应的寄存器值（用同一个 mapper）；
3. 替换 L0 中那几条 `(reg, val, atSample)`；
4. 标记 L1 该事件为 dirty，下次重算时刷新。

这样**编辑也是无损的**：编辑后的结果同样是一份完整的寄存器流，而不是"结构化数据 + 一堆假设规则"。这是本项目与常规 tracker 的根本区别。

### 3.4 编码

`regStream.encoding`：
- `"flat"`：`[chip, port, reg, val, absSample]`，最直观，方便调试与 AI 读写。
- `"delta"`：最后一项是**相对上一条的采样增量**，省空间；无符号增量总为正。
- 大文件可选 `"binary"` 旁路（外部 `.vgsb`），JSON 里只留 `{"binary": "song.vgsb"}`。

第一版默认 `"flat"`，性能有问题再换。

### 3.5 lanes 与 events 的分界

判据：**能不能表达成"某时刻按下某键、这个键持续多久"？**

- 能 → `events`（note / gateoff / 采样触发）
- 不能 → `lanes`（连续量：pan、LFO 深度、PMS/AMS、逐 operator 的 TL、弯音曲线）

lane 的 `points` 里存的是**原始寄存器值**，不是换算后的物理量。

---

## 4. GUI：横向 MIDI 音条，不是 tracker 表格

### 4.1 硬性决定

**界面形态 = 步进音序器 / 钢琴卷帘式的横向音条布局。**

明确否定的做法：Furnace 那种纵向 tracker 表格（时间向下、通道向右、每行一个格）。原因回到 §2——**tracker 表格强制"一个事件一个格"，而无损时序对不上格**。用表格就等于在 UI 层强行量化，用户看到的位置是假的。

### 4.2 布局

```
┌──────────────────────────────────────────────────────────────────────┐
│  Transport: ▶ ■ ⏮ ⏭   [采样位置 00:01:23.456]  [网格 1/16 ▾] [BPM 120]│
├────────────┬─────────────────────────────────────────────────────────┤
│ 通道列表    │  时间轴（横向，像素 ↔ 采样线性映射）                     │
│ ┌────────┐ │  ┌────────────────────────────────────────────────────┐ │
│ │▶ FM1  M │ │  │       ████████        ██████                       │ │ ← 音符=音条
│ │▶ FM2  M │ │  │  ██████████     ████████████████                   │ │
│ │▶ FM3  M │ │  │           ████████         ██████                  │ │
│ │▶ SSG1 M │ │  │  ▂▃▅▇▅▃▂   (pan lane 折叠显示在通道行内)            │ │
│ └────────┘ │  │   │    │    │    │    │    │    │                  │ │ ← 网格线(参考)
│            │  └────────────────────────────────────────────────────┘ │
├────────────┴─────────────────────────────────────────────────────────┤
│ 参数 lane 区（展开时）：PMS / AMS / PAN / LFO Freq ...                │
└──────────────────────────────────────────────────────────────────────┘
```

要点：

1. **横向时间轴**，像素位置 ↔ 采样位置线性映射（缩放只改比例，不改数据）。
2. **音符画成音条**：横向长度 = 时值（`durSample`），纵向位置 = **参考音高**。音条左右边缘可以是**任意像素**，不必对齐网格线。
3. **网格线是背景参考，不是约束**。吸附默认关闭；需要对齐时显式开启。
4. **所有显示值都必须标明是参考值**（§2.4）：
   - 音条起点与最近网格线的偏离量用小标记显示
   - 音条边框用虚线/半透明表示 `exact == false`
   - 状态栏常驻显示当前 `pitchMapper` / `gridMapper` 名字
   - 悬停显示**原始寄存器值**（`KC=0x2E KF=0x1A → ≈D#4 +12c`）
5. 通道行左侧是 M/S 按钮 + 通道名 + 芯片标记，复用 Gridline `ChannelRack` 的组织方式。

### 4.4 用户操作如何落到数据

| 操作 | 落点 |
|---|---|
| 拖动音条（改时间） | 回写 L0 的 `atSample`（§3.3） |
| 拖动音条（改音高） | 用 `IPitchMapper` 反算 → 回写 `KC/KF` 寄存器 |
| 拉长/缩短音条 | 回写 gate-off 的采样位置 |
| 改音量 | 回写 TL/音量寄存器 |
| 改网格 BPM | **只改 `IGridMapper` 参数，不碰任何数据** |
| 改换算器 | **只重算 L1/L2，不碰 L0** |

最后两条是这个设计能不能站住的关键：**换尺子不该改变被测物体。**

### 4.4 复用与实现来源

| 需要的东西 | 复用来源 | 说明 |
|---|---|---|
| ImGui 基础设施、渲染后端 | **Furnace**（`extern/imgui_patched`） | 已 vendored，跟主工程一致 |
| 横向时间轴 / 缩放 / 吸附 / 网格梯 | **Gridline** `src/core/Timeline.h`、`src/app/PianoRoll.cpp` | 它把"网格只是视觉层"这件事实现得很干净（`Snap::None`、缩放自适应网格、`frac` 亚 tick） |
| 音符 = 横向音条 + 拖拽/改长 | **Gridline** `PianoRoll.cpp`、**nanotracker** `src/ui/piano_roll_view.cpp`（776 行，更小更好读） | 两个都 ImGui |
| 通道机架 / 步进格 | **Gridline** `ChannelRack.cpp`、**0x808** `src/gui/drum_grid.cpp` | |
| 参数 lane（自动化曲线） | **Gridline** `src/app/AutomationEditor.cpp` | 直接对应 VGSJ 的 `lanes` |
| MIDI 读写 | **Furnace** `src/engine/fileOps/midi.cpp`，或 **Gridline** `src/core/MidiImport.h` | |
| 通用小件（undo 栈、主题、旋钮） | **0x808** `src/gui/undo.c`、`theme.cpp`、`knobs.cpp` | 可直接搬 |

**重要的取舍**：布局与交互参考 Gridline / nanotracker，**代码基础是 ImGui（来自 Furnace 的 vendored 副本），不引入 Qt / .NET**。

### 4.5 参考库定位（已核实，2026-11-11）

| 目录 | 仓库 | UI 框架 | 用途 |
|---|---|---|---|
| `nanotracker/` | savannah-i-g/nanotracker | Dear ImGui（FetchContent） | **抄 pattern 编辑 + piano roll 的最小写法**。`src/ui/pattern_view.cpp`(875)、`piano_roll_view.cpp`(776)、`note_entry_view.cpp`(160)、`midi_view.cpp`(241)；`src/midi/` 有 midi_io / midi_learn / midi_out_thread |
| `Gridline/` | lolitemaultes/Gridline | Dear ImGui 1.92.9b | **功能面最全**。`src/app/PianoRoll.cpp`(2318)、`ChannelRack.cpp`(1299)、`src/core/StepNotes.cpp`、`src/core/Timeline.h`、`src/core/Types.h`（`Note` 带 `frac/endFrac` 亚 tick）|
| `0x808/` | averagenative/0x808 | Dear ImGui 1.91.8 | **引擎/UI 彻底分离**的范例：`src/{engine,core,gui,standalone,plugin,formats}`。GUI 约 9900 行，含 `drum_grid.cpp`(1108)、`piano_roll.cpp`(421)、`virtual_keyboard.cpp`、`undo.c`、`theme.cpp`、`knobs.cpp` |
| `libmidi/` | opencodewin/libmidi_public | Dear ImGui 1.91.1 | TiMidity 播放器（非编辑器）。ImGui 里画钢琴键盘 / 事件卷轴 / SoundFont 选择，含百万音符级性能处理，中文注释 |
| `tks/` | bsp2/tks（**Synergy**） | ❌ **自研 tkui**，非 ImGui | 功能设计参考：`tks-projects/apps/synergy/` 的 `.tks/.xfm`（逻辑与界面分离），以及 `synergy_replay/` 独立回放器 |

> ⚠️ **更正**：Synergy (`tks/`) 完全没用 ImGui——它是 OpenGL 4.1 + SDL2 + FreeType 的自研 `tkui`，界面用 XML 表单（`.xfm`）定义。全仓 `grep -ril imgui` 零命中。**不能当 ImGui 代码参考**，只能读它的功能组织方式。

均为 `--depth 1` 浅克隆。

---

## 5. 结构化流水线

每个芯片写一个 Analyzer，共用下面五个阶段：

### 阶段 1 · 解析

VGM 命令流 → 带采样时间戳的 `(chipId, port, reg, val, sample)` 序列。

必须处理的命令族：

| 命令 | 含义 | 处理 |
|---|---|---|
| `0x50-0x5F` | 单芯片写（SN76489 除外） | 主路径 |
| `0xA0-0xBF` | 第二芯片实例写 | 主路径（多实例） |
| `0x30-0x3F` | 双芯片写 | 主路径 |
| `0x61 nn nn` | 等待 nn 采样 | 推进时间 |
| `0x62` / `0x63` | 等 735 / 882 采样（NTSC/PAL 帧） | 推进时间 |
| `0x70-0x8F` | 短等待 (n+1) 采样 | 推进时间 |
| `0x67` | DataBlock（ROM/PCM/wavetable） | 存入 chip 的 `rom` |
| `0x68` | PCM RAM 写 | 见阶段 1.1 |
| `0x90-0x95` | DAC Stream Control（采样回放） | 第一版**透传 raw 层**，不结构化 |
| `0x66` | 结束 | 收尾，记 loop 点 |
| `0x4F/0x50/0x31…` | 双芯片/保留 | 按 VGM 规范 |

### 阶段 2 · 寄存器影子

每芯片一份 `uint8 regs[256]`，逐条写入更新。Furnace 的 `getRegisterPool()`（如 `arcade.cpp:1089`，YM2151 返回 256 字节池）已经是这个模型，可直接借用其语义。

### 阶段 3 · 事件分段

每次时间推进后，对每个通道产出一个"快照"，与上一快照做差分，检测：

- key-on / key-off
- 音高变化
- 音量变化
- 音色参数变化（→ 触发音色指纹比对）

参考实现：FamiStudio `VgmFile.cs:1180` 的 `UpdateChannel(p, n, channel, state)` 每帧对每通道调用一次，`ChannelState`（`:556`）持有 `period/note/pitch/volume/octave/state/instrument`。**这套状态机可直接直译成 C++。**

### 阶段 4 · 音符量化（关键，也是最容易做错的一步）

`period → note + finePitch`：用芯片的音符周期表找最近的音符，差值即细音高。

**但注意方向**：这里量化的是**音高**（离散的、本来就是离散的），不是时间。时间是 §2 里连续保留的采样位置，**绝不量化**。

FamiStudio 的 VGM 导入把时间量化成 60Hz 帧栅格（`p=(frame-skip)/PatternLength; n=(frame-skip)%PatternLength`）——那是因为它的目标是生成 NES tracker 歌曲。**本工具不这么做**：时间永远保留采样精度，帧/行只用于显示。

音高表来源：FamiStudio `NesApu.GetNoteTableForChannelType()` 与 `VgmFile.cs:582 GetBestMatchingNote()`。YM 系列则用芯片自身的 F-Number / KC-KF 换算。

### 阶段 5 · 音色去重

每次音色参数变化算一个指纹，命中已有 instrument 则复用，否则新建。参考 FamiStudio 的 `GetDutyInstrument` / `GetFdsInstrument` / `GetVrc7Instrument` / `GetEpsmInstrument`（全是这个套路）。

---

## 6. YM2151 结构化映射（第一个实现目标）

YM2151 选它作首个目标的理由：结构规整（8 通道、4 operator、8 种算法），且**三个参考仓库都没有它的 VGM 导入**——FamiStudio 的 `VgmHeader` 虽有 `ym2151clock` 字段，但整个 `Load()` 没有一条 `0x54` 分支。所以这是真正需要自己写的第一块。

| 寄存器 | 含义 | 结构化目标 |
|---|---|---|
| `0x08` | Key on/off：bit0-2 = M1/C2/M2 slot 掩码，bit3-6 = 通道号 | **note / gateoff 事件**（掩码非零 = key on）。真正的发声起点由 ALG 决定哪些 slot 才是载波 |
| `0x28+ch` | KC：音名码（octave + 半音） | `note` |
| `0x30+ch` | KF：6 bit 小数音高 | `pitch`（细音高） |
| `0x20+ch` | PAN(L/R) / FB / ALG | `instrument`（alg/fb）+ `lane:pan` |
| `0x40,0x60,0x80,0xA0,0xC0,0xE0` (+slot 偏移) | DT1/MUL、TL、KS/AR、AM/D1R、DT2/D2R、SL/RR | **`instrument` 参数**；TL 是音量，但需先按 ALG 判定哪些 operator 是载波 |
| `0x38+ch` | PMS / AMS | `lane:pms`、`lane:ams` |
| `0x18 / 0x19 / 0x1B` | LFO 频率 / AMD-PMD / LFO 波形 | **全局** → 所有通道共享的 lane |
| `0x0F` | Noise 控制（噪声频率 + 各通道使能） | `lane:noise` + 通道级噪声开关 |
| `0x14` | LFO 复位 / 定时器 | 元信息 |

**命门是算法→载波表**：ALG 0-7 决定 4 个 operator 的连接方式，只有"载波"operator 的 TL 才直接决定输出音量。这张表必须写死成常量，否则音量提取全错。

```cpp
// ALG → 载波 operator 位掩码（bit0=OP1 … bit3=OP4）
static const uint8_t kAlgCarriers[8] = {
  0b1000, // ALG0: OP1→OP2→OP3→OP4 串联，仅 OP4 是载波
  0b1000, // ALG1: OP1+OP2→OP3→OP4，OP4
  0b1000, // ALG2: OP1+(OP2→OP3)→OP4，OP4
  0b1000, // ALG3: (OP1→OP2)+(OP3→OP4)… 见下
  0b1100, // ALG4: (OP1→OP2)、(OP3→OP4) 双载波
  0b1110, // ALG5: OP1→(OP2,OP3,OP4)
  0b1110, // ALG6: OP1+(OP2,OP3,OP4)
  0b1111, // ALG7: 全加法，4 个都是载波
};
```

> ⚠️ 上表 ALG0-3 需按 YM2151 官方手册核对后再定稿（ALG3 是多分支结构）。实现时以芯片文档为准，并写单元测试用真实 VGM 反推验证。

---

## 7. 跨芯片转换

有了 structured 层，转换 = 两步：

### 7.1 能力表匹配

`capabilities(chip)` 至少包含：通道数、是否 FM、operator 数、算法数、支持 wavetable/采样、音量分辨率、pan 粒度、LFO 种类、是否支持 retrigger/portamento、音域。

- **厚植**（目标芯片更强）：YM2151(8FM) → YM2608(6FM + 6RHY + 3SSG) 有余量。
- **降维**（目标芯片更弱）：YM2151 → NES，8 个 FM 通道要塞进 2 方波 + 1 三角 + 1 噪声。

### 7.2 有损报告

每次跨芯片转换输出一份 diff：

- 哪些 operator / 参数被丢弃
- 哪些音符超出目标音域（并说明如何处理：折叠八度/移调/丢弃）
- 复音数不足时丢了哪几个音
- 哪些 lane 被近似（LFO→?、pan→?）
- 哪些事件**时间位置**因目标芯片最小时间分辨率而偏移 ❗（这一项尤其重要，因为它直接违反 §2 原则，必须显式告知）

**有损报告比"转换成功"更重要**——这是这类工具能不能用的分界线。

---

## 8. 技术选型与依赖

### 8.0 工程形态：独立工程，不 fork Furnace

**修订（2026-11-11 二次）：** 起点不是改造 Furnace，而是**新建独立的 `studio/` 工程**，它是一个基于 ImGui 的 VGM 步进音序器。

理由：
- Furnace 是 tracker，UI 骨架（纵向表格）与我们要求的横向音条完全相反，改造比重写还贵。
- 我们第一版要的能力很窄：VGM 导入 → 结构化 → 横向音条显示正确。不需要 81 个芯片 dispatch、不需要打开 .fur、不需要 tracker 那一整套。
- 依赖只需要 ImGui + SDL2 + nlohmann/json，MSYS2 一装就有，比 Furnace 那套（SDL + fmt + freetype + libsndfile + zlib + 一堆 submodule）轻得多。
- Furnace 保留为**参考实现**：芯片寄存器语义、`saveVGM` 的命令编码规则，需要时去读、去抄。

| 项 | 选择 | 理由 |
|---|---|---|
| 工程 | `studio/`（新建） | 见 §8.0 |
| 构建 | CMake + Ninja | MSYS2 统一管理 |
| 编译器 | **MSYS2 MinGW-w64 GCC**（`/d/msys64/mingw64`） | 用户要求全部工具链走 MSYS2 |
| GUI | Dear ImGui 1.92.9b（vendored 在 `studio/third_party/`） | 与 Furnace 同版本，便于对照 |
| 窗口/输入/GL | SDL2 + OpenGL3（`imgui_impl_sdl2` + `imgui_impl_opengl3`） | MSYS2 包，跨平台 |
| JSON | nlohmann/json（MSYS2 包 `mingw-w64-x86_64-nlohmann-json`） | 与 Furnace 同源 |
| AI 操作接口 | **cpp-httplib**（单头文件，vendored）+ 命令总线 | 见 §8.4 |
| VGM 解析 | **自己写**（约 800 行） | 需要完全控制权来做 L0 寄存器流；格式简单 |
| 中间格式 | VGSJ（JSON） | 见 §3 |
| 仿真/回放（后续） | 前期不接；需要时优先接 Furnace dispatch 或 libvgm | 第一版不做音频输出也不影响"显示正确" |
| MIDI | 后期接 Furnace `fileOps/midi.cpp` 或 Gridline `core/MidiImport.h` | |

### 8.1 已就绪的本机工具链（2026-11-11 实测）

全部走 MSYS2，统一在 `/d/msys64`：

```bash
# 已安装并验证
cmake   4.4.5   (/d/msys64/mingw64/bin/cmake)
ninja   1.13.2
pkgconf 2.x     (pkg-config)
gcc/g++ 16.2.0  (Rev6, MSYS2 project)
SDL2    2.32.10 (pkg-config --modversion sdl2)
nlohmann-json 3.12.0
```

构建命令（必须在 MSYS2 MINGW64 环境下）：

```bash
export MSYS2_PATH_TYPE=inherit
/d/msys64/usr/bin/bash.exe -lc '
  cd /e/working/zcode-projects/VGM_Studio/studio
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build build
'
```

> ⚠️ **pacman 排障记录（2026-11-11）**：初始 `pacman -Sy` 报 `签名无效`，原因是阿里云镜像的 db 签名校验失败。处理：
> 1. `pacman-key --init && pacman-key --populate msys2`
> 2. 镜像换回官方：`/etc/pacman.d/mirrorlist.mingw` → `https://repo.msys2.org/mingw/$repo/`
> 3. 仍失败则临时把 `/etc/pacman.conf` 的 `SigLevel = Required` 改成 `SigLevel = Never`（**备份在 `pacman.conf.bak`，装完应改回**）。
>
> 另外 `pacman.exe` 直接在 Git Bash 里跑会因为路径问题找不到 `pacman-key`，必须经 `/d/msys64/usr/bin/bash.exe -lc` 执行。

### 8.2 许可证

- 新建工程：**GPLv3**（与参考的 Gridline / nanotracker / 0x808 一致，且与可能借用的 Furnace GPLv2+ 代码兼容）。
- ImGui：MIT。cpp-httplib：MIT。nlohmann/json：MIT。
- libvgm：核心仿真器多为 BSD-3，但仓库 license 字段为空，逐文件读头注释后再定。

### 8.3 两个已知的坑

1. `furnace/src/engine/vgmOps_after_surgery.cpp` 是**废文件**：3295 行，含 4 处未解决 `<<<<<<< HEAD` 冲突标记，`CMakeLists.txt` 从未引用。可当参考读物，**不要在上面开发**。
2. Furnace **没有 VGM 导入**（只有 `saveVGM`），也**没有 JSON 导入**。这两块都要新写。

### 8.4 AI 操作接口（不是 API 给程序用，是给 AI 用）

**这是硬需求，从第一版就要有。**

目标：让 AI（我）能**直接操作这个软件的运行时**，而不是只能改代码重启。方式要轻，**明确不要 MCP 服务器**（太重：要配客户端、要 subprocess、要 schema 协商）。

选用方案：**进程内嵌一个本地 HTTP + JSON 接口**（cpp-httplib，单头文件，MIT），监听 `127.0.0.1:<port>`，端口写在运行时文件里。AI 用 `curl` 就能操作。

```
┌─────────────────────────────────────────┐
│ VGM Studio (单进程)                      │
│                                          │
│  ImGui UI  ──┐                          │
│              ├──►  CommandBus  ──►  Document (L0/L1/L2)
│  HTTP :7777 ─┘        ▲                  │
│      ▲                │                  │
└──────┼────────────────┼──────────────────┘
       │                │
   curl/JSON        UI 也走同一条总线
```

**关键设计：UI 和 HTTP 走同一条命令总线。**

这样做的价值：
- AI 能做的事 = 用户能做的事，没有任何"AI 专用后门"；
- 每条命令都是可测试的纯函数（`Command → Result`），UI 只是它的一个调用者；
- 录制命令序列就能回放，天然可复现 bug。

#### 接口设计

```http
GET  /health                  → {"ok":true,"version":"0.1.0","uptime":123}
GET  /state                   → 完整文档摘要（不含 regStream 全文）
GET  /state/regs?chip=0       → 某芯片的当前寄存器影子值
GET  /state/channels          → 通道列表 + 每个通道的 L1 事件（参考值）
GET  /state/events?ch=0&from=0&to=44100
POST /command                 → 执行一条命令，body 是 JSON
GET  /log?tail=100            → 最近日志
```

`POST /command` 的 body 形如：

```json
{ "op": "importVgm",        "args": { "path": "song.vgm" } }
{ "op": "setGridBpm",       "args": { "bpm": 140 } }
{ "op": "setMapper",        "args": { "kind": "pitch", "name": "opm-alt" } }
{ "op": "moveEvent",        "args": { "ch": 0, "idx": 3, "toSample": 8820 } }
{ "op": "exportVgm",        "args": { "path": "out.vgm" } }
{ "op": "screenshot",       "args": { "path": "shot.png" } }
{ "op": "dumpJson",         "args": { "path": "song.vgsj" } }
```

#### 为什么值得从第一版就做

1. **AI 能看见**：`/state` 让 AI 读到软件当前的真值（L0 寄存器流 + L1 参考视图），而不是靠猜。
2. **AI 能验证**：改完立刻 `exportVgm` + 比对，形成闭环。
3. **AI 能截图**：`screenshot` 命令把当前 ImGui framebuffer 存成 PNG，AI 能看渲染结果。这比读代码可靠得多。
4. **调试成本低到几乎为零**：一条 curl 就能复现用户描述的问题。

#### 安全与边界

- **只监听 `127.0.0.1`**，不对外。
- 启动时随机生成一个 token 写到 `<exe目录>/studio.api`，写命令需带 `X-Studio-Token` 头；读命令可免。
- 用参数 `--no-api` 可完全关闭。
- 端口冲突时自动找空闲端口，并把实际端口写进 `studio.api`。

#### 不做的事

- ❌ 不做 MCP 服务器（太重）。
- ❌ 不做 WebSocket 推送（第一版轮询够用）。
- ❌ 不做远程访问（仅本机）。
- ❌ 不做完整的 REST 资源模型（命令式比资源式更适合编辑器操作，也更省事）。

---

## 9. 里程碑

每个里程碑都要有可验收的产出。**M0-M1 是"能看见"阶段，M2 才开始做"能转换"。**

| # | 内容 | 验收标准 |
|---|---|---|
| **M0** ✅ | 独立 `studio/` 骨架：CMake + Ninja + SDL2 + ImGui + 空窗口；AI 操作接口探活 | `cmake --build` 出 exe，起窗口；`curl 127.0.0.1:7777/health` 返回 ok |
| **M1** ✅ | VGM 解析器（L0 寄存器流）+ 横向音条 UI 显示 | 打开任意 YM2151/OPN VGM，音条在正确时间位置、正确排序画出；网格线可开关；悬停显示原始寄存器值 |
| **M2** ✅ | 换算器框架（`IPitchMapper`/`IGridMapper`）+ YM2151 音高参考 | 同一个 VGM，换 `pitchMapper` 音条纵向位置随之变化而 **L0 一字节不动** |
| **M3** 🚧 | 编辑回写 L0 + 导出 VGM | 拖动一个音条 → 导出 → 与原 VGM 逐命令 diff，只有预期的那几条寄存器写变了。**API 侧 `moveEvent`/`retuneEvent` 已完成并验证；鼠标拖框交互待做** |
| **M4** | 扩展芯片：OPN2(YM2612) / OPN(YM2203) / SSG | 多芯片 VGM 能正确分通道显示 |
| **M5** | 跨芯片转换 + 有损报告 | 至少一条完整链路（YM2151 → YM2608）+ 报告 UI |
| **M6** | MIDI 导入 | MIDI → VGSJ → VGM 全链路可用 |
| **M7** | 音乐量化（可选 pass） | 显式操作、可撤销、输出量化误差报告 |

### 9.1 未决问题

- [ ] ALG → 载波表需按 YM2151 手册核对（§6）
- [ ] DAC Stream（`0x90-0x95`）的结构化方案——第一版透传，之后是否要做采样通道
- [ ] 多芯片实例（同型号多颗）的通道 ID 规则
- [ ] VGSJ 是否需要二进制旁路格式（大文件 JSON 性能）
- [ ] 用户自定义网格 BPM 的推断算法（自动估算原曲速度，仅供参考）
- [ ] 第一版是否要接芯片仿真（做音频回放）——**暂定不接**，显示正确优先

---

## 10. 参考代码地图

```
工作区
├── studio/                       ← 本项目（GPLv3，新建，ImGui + SDL2）
│   ├── CMakeLists.txt
│   ├── third_party/
│   │   ├── imgui-1.92.9b/             Dear ImGui（MIT）
│   │   └── httplib.h                  cpp-httplib（MIT，AI 操作接口）
│   └── src/
│       ├── main.cpp                   入口：SDL2 + ImGui + HTTP 服务
│       ├── core/
│       │   ├── Document.{h,cpp}       L0/L1/L2 数据模型（VGSJ）
│       │   ├── VgmParser.{h,cpp}      VGM → L0 寄存器流
│       │   ├── VgmWriter.{h,cpp}      L0 → VGM（回写）
│       │   ├── Mappers.{h,cpp}        IPitchMapper / IGridMapper / IVolumeMapper
│       │   └── ChipYamaha.{h,cpp}     YM2151/OPM 寄存器语义表
│       ├── ui/
│       │   ├── App.{h,cpp}            ImGui 主窗口
│       │   ├── Sequencer.{h,cpp}      横向音条音序器（核心视图）
│       │   └── Sidebar.{h,cpp}        通道列表 / 芯片信息 / 换算器选择
│       └── api/
│           ├── CommandBus.{h,cpp}     命令总线（UI 与 HTTP 共用）
│           └── HttpApi.{h,cpp}        httplib 路由
│
├── furnace/                      ← 参考实现（GPLv2+，ImGui）
│   ├── src/engine/platform/          81 个芯片 dispatch
│   │   ├── arcade.cpp:1089           getRegisterPool() YM2151 寄存器影子范本
│   │   └── arcade.cpp:25             regCheatSheetOPM[] 寄存器名列
│   ├── src/engine/dispatch.h:469     SharedChannel —— 通道状态结构
│   ├── src/engine/dispatch.h:841     DivRegWrite —— 寄存器写流格式
│   ├── src/engine/vgmOps.cpp:566     saveVGM —— 出 VGM 的完整实现
│   ├── src/engine/fileOps/json.cpp   只导出，无导入
│   ├── src/engine/fileOps/midi.cpp   MIDI 导入
│   └── extern/imgui_patched/         ImGui（同版本，可对照）
│
├── FamiStudio/                   ← 算法读书材料（MIT，C#，本机编不了）
│   └── FamiStudio/Source/IO/VgmFile.cs    寄存器→音符 的完整参考（2538 行）
│       ├── :556 ChannelState          通道状态机
│       ├── :582 GetBestMatchingNote   音高量化
│       ├── :1180 UpdateChannel        每帧差分
│       └── :1657 Load                 VGM 命令主循环
│
├── Gridline/                     ← UI 参考（GPLv3，C++17，纯 ImGui）★ 最贴近
│   ├── src/core/Types.h:63            Note{start,length,pitch,velocity,frac,endFrac,bend}
│   ├── src/core/Timeline.h            网格梯 / 吸附（Snap::None、缩放自适应）
│   ├── src/app/PianoRoll.cpp          横向音符编辑（2318 行）
│   ├── src/app/ChannelRack.cpp        通道机架（1299 行）
│   ├── src/app/AutomationEditor.cpp   自动化 lane（1756 行）→ 对应 VGSJ lanes
│   └── src/core/MidiImport.h          MIDI 导入
│
├── nanotracker/                  ← UI 最小范本（GPLv3，ImGui）
│   ├── src/ui/pattern_view.cpp        875 行 tracker 网格
│   ├── src/ui/piano_roll_view.cpp     776 行钢琴卷帘
│   └── src/midi/                      midi_io / midi_learn / midi_out_thread
│
├── 0x808/                        ← 引擎/UI 分离范例（ImGui + GTK 双前端）
│   └── src/gui/{drum_grid,piano_roll,virtual_keyboard,undo,theme,knobs}.c*
│
├── libvgm/                       ← 验证工具（VGM 命令处理的完整参考）
│   └── player/vgmplayer*.cpp
│
├── libmidi/                      ← ImGui 钢琴键盘 / 事件卷轴 / 百万音符性能
└── tks/                          ← Synergy（❌ 非 ImGui）功能设计参考 + synergy_replay
```

---

## 11. 一句话总结

**寄存器原值是唯一真值，其余（音符、音高、网格、行号）全是可替换的参考视图；界面用横向音条呈现，AI 通过同一条命令总线直接操作运行时。**

先把"寄存器流按通道还原成可看的事件"这件事做对，其余都是工程。
