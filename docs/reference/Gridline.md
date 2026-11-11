# Gridline — 时间模型参考（○ 只读，不链）

| 项 | 值 |
|---|---|
| 仓库 | lolitemaultes/Gridline |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\Gridline` |
| 许可 | **GPL-3.0**（不能链进构建） |
| 语言 | C++17 |
| 构建 | CMake，纯 ImGui 1.92.9b |
| 规模 | 1928 文件 / 120MB / `src` **465,741 行** |
| 本机可编 | ✅ 基本离线（唯一 FetchContent 是 SDL2，默认 OFF） |
| 有 build 产物 | ❌ 无 |

---

## 1. 它是什么

**从零写的通用型 DAW / 音乐工作站**（channel rack + step sequencer + piano roll + playlist + mixer，对标 FL Studio 工作流）。

不是 tracker。这个区别重要：tracker 的范式是纵向表格，Gridline 是横向音条 —— **正是我们要的形态**。

规模分布：`src/app` 92.7k 行（248 文件）、`src/packs` 132.7k（178 个乐器/效果）、`src/core` 91.7k、`src/engine` 34.6k。

---

## 2. 为什么只读不链

1. **体量 46.5 万行** —— 剥离 178 个内建乐器、P2P 协作、5 种插件宿主、ASIO 的成本远超自研一个可视化器。
2. **GPL-3.0** —— 当壳意味着我们的衍生作品也得 GPL-3.0。
3. 我们只需要它 3 个文件里的**思想**。

---

## 3. 唯一真正实现"不量化"的时间模型（★）

**这是全工作区唯一一处把"事件时间不量化"落到数据结构上的实现。**

### 3.1 `struct Note`（`src/core/Types.h:63`）

```cpp
struct Note {
    Tick start;      // int64_t
    Tick length;
    int  pitch;
    int  velocity;
    ...
    float frac;      // ← 起点亚 tick 小数
    float endFrac;   // ← 终点亚 tick 小数
    ...
};
```

`frac` / `endFrac` 在 `Types.h:80-83`，注释明确说是为"录音保留精确采样"。

### 3.2 引擎侧 `NoteEv`（`src/engine/Program.h:38`）

```cpp
struct NoteEv {
    ...
    float onFrac;   // :40-46
    float offFrac;  // 注释明说"引擎按采样调度"
};
```

### 3.3 还有两处亚精度

| 结构 | 位置 | 内容 |
|---|---|---|
| `AutoPoint` | `src/core/Song.h:220-222` | `frac + fine(double)` |
| `StepProps::shift` | `src/core/Song.h:274,281` | 步骤延迟 0–100% |
| swing | `src/core/Song.h:670-672` | — |

### 3.4 TempoMap（tick ↔ 秒 ↔ 采样 三段转换的标准写法）

`src/core/Automation.h:68-101`（105 行头 + 404 行实现）：

```cpp
class TempoMap {
    double secondsAt(Tick) const;
    Tick   tickAtSeconds(double) const;
    Tick   tickAfterSamples(...) const;
    ...
};
```

实现 `src/core/Automation.cpp:350` 的 `samplesBetween()`。**区分 constant / segmented 两条路径**（分段 BPM）。

**对我们的意义**：我们的 `IGridMapper` 需要 `sampleToTick` / `tickToSample`，这个类的写法可以直接照搬思路（但我们的默认实现更简单：固定 BPM 假设即可，因为 VGM 本身没有 BPM）。

---

## 4. 网格与吸附

`src/core/Timeline.h`（71 行）：

- `Snap::None` —— 吸附关闭是默认
- 缩放自适应网格 —— 缩放越小，网格线越稀
- 这是"网格只是视觉层"实现得最干净的开源范例

**对我们的意义**：`DESIGN.md` §7.2 第 3 条"网格线是背景参考，不是约束；吸附默认关闭"就是从这来的。

---

## 5. UI 模块

| 模块 | 文件 | 行数 |
|---|---|---|
| 钢琴卷帘（UI，全部） | `src/app/PianoRoll.cpp` | **2318** |
| 播放列表 / 时间轴 | `src/app/Playlist.cpp`（+ `PlaylistWarp.cpp` 195） | 4437 |
| 通道机架（step 音序） | `src/app/ChannelRack.cpp` | 1299 |
| 自动化编辑器（→ 我们的 lanes） | `src/app/AutomationEditor.cpp` | **1756** |
| 数据模型 | `src/core/Types.h`(215) / `src/core/Song.h`(815) | 1030 |
| TempoMap | `src/core/Automation.h`(105) / `.cpp`(404) | 509 |
| 编译快照（音频线程） | `src/engine/Program.h`(805) / `.cpp`(1689) | 2494 |
| 实时引擎 | `src/engine/AudioEngine.h`(805) / `.cpp`(2751) | 3556 |
| 内建 tracker 采样器 | `src/packs/tracker/{tracker_ui,tracker_dsp,TrackerChannel,TrackerModule}.cpp` | 4120 |

**注意**：钢琴卷帘没有独立 `.h`，state 挂在 `App`/`PlaylistState` 里。

---

## 6. 引擎与音频

- **自带完整合成/采样引擎**：`Generator` 抽象基类，`virtual void render(float* L, float* R, int n) = 0`（`src/engine/Generators.h:215,380`），178 个内建乐器/效果。
- 设备层：SDL2 音频（`src/app/main.cpp:131,298`）+ 自写 ASIO（`src/host/audio/AsioHost.cpp`，490 行）。
- **三条外部音频源入口**：音频 clip / 插件宿主（VST3/CLAP/VST2/AU/LV2，`src/host/**` 6510 行）/ 离线渲染（`setOffline()` / `setFixedBlock()`，`AudioEngine.h:103-109`，CLI `gridline render`）。

**若真要拿它当壳**：把 libvgm 包成 CLAP 插件丢进现有宿主，比写 `Generator` 子类省事。但见 §2 的体量与许可问题。

---

## 7. 可借鉴清单（只读，抄思路不抄代码）

| 想要的东西 | 位置 | 抄什么 |
|---|---|---|
| 亚 tick 音符结构 | `src/core/Types.h:63-97` | `frac` / `endFrac` 双小数的字段设计 |
| 网格/吸附语义 | `src/core/Timeline.h`(71) | `Snap::None` 默认、缩放自适应网格 |
| 横向音条绘制 | `src/app/PianoRoll.cpp`(2318) | 整体布局、音符块的边缘不对齐网格 |
| 通道行组织 | `src/app/ChannelRack.cpp`(1299) | M/S 按钮 + 通道名 + 芯片标记的排布 |
| 自动化 lane | `src/app/AutomationEditor.cpp`(1756) | → VGSJ 的 `lanes` |
| tick↔秒↔采样转换 | `src/core/Automation.h:68-101` + `Automation.cpp:350` | `IGridMapper` 的实现思路 |

---

## 8. 与 VGM 的关系

全库（含 docs、third_party）`grep` `vgm|ym2612|ym2151|sn76489|psg|register write|spc700` = **0 命中**（唯一命中是 rubberband 和 nanosvg 的函数名巧合）。

**没有 VGM、没有芯片仿真。** 纯 UI/DAW 参考。

### 邻近能力（不是芯片仿真，但有参考价值）

| 能力 | 位置 | 说明 |
|---|---|---|
| tracker 单元格模型 | `src/core/TrackerChannel.{h,cpp}`(151+879)、`TrackerStream.h`(245)、`TrackerModule.cpp`(614) | 把 MOD/XM 每个 cell 存进 `Note::tracker`，按 tick 回放通道状态。"一行 = 一组寄存器快照"的语义最接近 VGM |
| CV/Gate | `docs/cv-gate.md`、`src/packs/modulation/cv*.cpp` | 已有"把音符变成模拟电压/时钟脉冲"的插件，证明它有能力做非音频事件出口 |
| MIDI 导出 | `src/core/MidiFile.h:70 writeMidiFile` | 唯一的外部事件出口 |

---

## 9. 结论

**不当壳，当"时间模型 + UI 布局"参考库。**

它证明了"事件时间带亚 tick 小数"在 C++ 里能做得干净，并且给出了 `TempoMap` 的三段转换标准写法。这两点是它对我们唯一的实质贡献。

体量（46.5 万行）与许可（GPL-3.0）决定了代码不能进构建。
