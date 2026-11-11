# nanotracker — 最小钢琴卷帘范本（△ 本机编不了）

| 项 | 值 |
|---|---|
| 仓库 | savannah-i-g/nanotracker |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\nanotracker` |
| 版本 | 1.2.0 |
| 许可 | **GPLv3** |
| 语言 | C++20 |
| 构建 | CMake，全部依赖走 FetchContent |
| 规模 | 208 文件 / 6.1MB / `src`+`include` **40,168 行**（ui 6.4k, app 4.7k, io 4.5k, plugins 3.7k, audio 2.9k, api 2.6k, engine 0.9k） |
| 本机可编 | ❌ 需 MSVC 2022（VST3 SDK 不支持 MinGW）+ 18 个联网依赖 |
| 有 build 产物 | ❌ 无 |

---

## 1. 它是什么

**原生 tracker**（pattern 网格编曲 + 钢琴卷帘层 + 可插线的音频/CV/gate/MIDI 模块图）。

不是 DAW。范式是纵向 tracker 表格，**与我们要的横向音条形态不同**。

---

## 2. 为什么本机编不了

### 2.1 全部依赖走 FetchContent（18 个，首次 configure 必须联网）

`CMakeLists.txt` 从第 27 行起：

| 仓库 | 用途 |
|---|---|
| glfw/glfw (3.4) | 窗口 |
| Dav1dde/glad (v2.0.8) | GL loader（需 python3 + jinja2） |
| ocornut/imgui (v1.92.8-docking) | UI |
| nlohmann/json | 清单/设置 |
| kcat/openal-soft | 音频设备（Windows） |
| libopenmpt_win（URL zip 0.8.3） | 模块播放（Windows） |
| mackron/dr_libs | WAV/MP3 解码 |
| nothings/stb | vorbis / image |
| richgel999/miniz | 插件 zip |
| jiixyj/libebur128 | LUFS |
| marton78/pffft | 卷积 FFT |
| xiph/ogg, xiph/vorbis | ogg 编码 |
| thestk/rtmidi | MIDI |
| machinezone/IXWebSocket | 本地 WebSocket API |
| free-audio/clap | CLAP 头 |
| **steinbergmedia/vst3sdk** | VST3 宿主 |
| libsndfile/libsamplerate | 重采样 |

（Catch2 在 `Docs/DEPENDENCIES.md` 里写了，但顶层 CMakeLists grep 不到，应在 `tests/CMakeLists.txt`。）

### 2.2 Windows 必须 MSVC 2022

**VST3 SDK 不支持 MinGW。** 本机只有 MSYS2 mingw64 → 编不了。

### 2.3 无 build 产物

全库扫描：无 `build/`、无 CMakeCache、无 exe/dll/o/a。`tools/build_libopenmpt.sh` 只是脚本，未跑过。

---

## 3. 时间模型（精度不足，不能直接抄）

| 项 | 值 |
|---|---|
| 驱动 | **tick / BPM（Amiga 式）** |
| tick 间隔 | `2500 / bpm` ms（`tracker_engine.h:35-39`） |
| `speed` | 每行 tick 数（`tracker_types.h:147-148`） |
| BPM 变化 | **单一常量** `project.bpm`，行内 Fxx 可改 speed/BPM（`tracker_engine.cpp:262-265`），**无 tempo 曲线** |
| 亚 tick | **没有**。`SequenceNote::start_tick` 是 `int`（`tracker_types.h:82-85`），最细一个 tick |
| 量化 | `quantize()` 有 `strength_percent`（`sequence_ops.h:30-36`）但结果**仍落整 tick** |
| 播放位置 | `tracker_types.h:222` 是 int tick |
| 采样精确 | **否**。tick 边界按帧累加（`audio_engine.cpp:573-604`：`frames_until_tick_ += rate*2.5/bpm`），tick 内部事件用 `frame_offset` |

**对我们的意义**：它的时间精度只到整 tick，**正是我们要避免的**。不要抄它的时间模型。

---

## 4. 音序模块（行数）

| 模块 | 文件 | 行数 |
|---|---|---|
| tracker pattern 网格 UI | `src/ui/pattern_view.cpp` / `.h` | 875 / 126 |
| **钢琴卷帘 UI** | `src/ui/piano_roll_view.cpp` / `.h` | **776** / 115 |
| 音序器状态机（纯逻辑，无 IO/无音频） | `src/engine/tracker_engine.cpp` / `.h` | 658 / 46 |
| 音序数据模型 | `src/engine/tracker_types.h` | 252 |
| 钢琴卷帘变换（量化/humanize/琶音） | `src/engine/sequence_ops.cpp` / `.h` | 272 / 79 |
| 实时渲染 | `src/audio/audio_engine.cpp` / `.h` | 832 / 337 |
| 补丁图模型 / block 求值 | `src/graph/graph_model.h` (214) / `src/audio/graph_runner.cpp` (529) | 743 |
| workspace 视图 / 连线 | `src/ui/workspace_view.cpp` (463) / `cable_overlay.cpp` (411) | 874 |
| 离线导出 | `src/io/export_render.cpp` | 693 |

**注意**：nanotracker **没有"播放列表/时间轴"**。编排只有 `TrackerProject::order_list`（`tracker_types.h:153`，pattern id 的播放顺序），没有时间轴视图。

---

## 5. 关键数据结构

| 结构 | 位置 |
|---|---|
| `TrackerCell` / `Pattern` / `Project` | `tracker_types.h:25 / 34 / 145` |
| `SequenceNote` | `tracker_types.h:81`（`start_tick` 是 **int**） |
| `TrackerPlayState` | `tracker_types.h:216` |
| `EngineSnapshot::tick_frame_remainder` | `audio_engine.h:77-83`（只用来回推录音锚点，不用于播放精度） |

---

## 6. 音频与引擎分层（这部分设计不错）

- **引擎是纯状态机**：`tracker_engine.h` 明确写 "No audio, no I/O, standard library only"。这一层的干净程度值得学。
- **自带采样器**：`SampleBuffer`（`audio/sample_buffer.h`，设备采样率、交错立体声 float）。
- 设备层：**OpenAL Soft 回调**（`audio/audio_device_openal.cpp:46-76`，`alBufferCallbackSOFT`），固定 `kBlockFrames = 128`（`audio_engine.h:24`）。
- **补丁图是天然注入点**：`NodeKind { kTrackerBus, kMasterIn, kModulePlayer, kUtilitySum, kPlugin, kExtMidiIn, kExtMidiOut }`（`src/graph/graph_model.h:35-43`）。加一个 `kVgmPlayer` 节点跟 `kModulePlayer`（libopenmpt 包装，`src/modplay/module_player.cpp` 75 行）完全同构。
- 离线渲染：`start_offline(rate)` / `render_offline()`（`audio_engine.h:171-173`）。

---

## 7. 若拿它当壳要改什么（结论：不划算）

| 改动 | 位置 | 工作量 |
|---|---|---|
| 新增 `kVgmPlayer` 图节点 + `VgmRunner`（仿 `modplay/module_player.cpp`） | `graph/graph_model.h:35`、`modplay/` | 24–40h |
| 新增 "event lane" 数据结构（无对应物，需新写） | 新文件 + `tracker_types.h` | 16–24h |
| 替换/旁路音序器：`advance_tick` 不该跑，新增 `PlaybackMode::Vgm` | `audio/audio_engine.cpp:573-604`、`tracker_engine.cpp` | 12–20h |
| 钢琴卷帘改造：`kTickWidth = 3.0F` 是常量（`ui/piano_roll_view.cpp:19`），坐标全是 `int start_tick` | `ui/piano_roll_view.cpp` 776 行 | 20–32h |
| **时间轴/编排视图从零写**（现在只有 `order_list`） | 新文件 | 30–50h |
| 亚 tick：`start_tick` int → double，影响 `sequence_ops.cpp` 全部 8 个变换函数 | `tracker_types.h:81-86`、`sequence_ops.cpp` 272 行 | 12–20h |
| 去掉不需要的（libopenmpt / VST3 / NTP / 导出 / bridge / local API，约 12k 行） | 删代码 + 修 CMake | 8–16h |
| 首次构建打通（MSVC 2022 + 18 个联网依赖） | — | 4–8h（网络好）/ 1–2 天（被墙） |

**总估 120–210 小时。**

---

## 8. 我们应该从它拿什么

| 拿 | 位置 | 为什么 |
|---|---|---|
| 钢琴卷帘的最小写法 | `src/ui/piano_roll_view.cpp`（776 行） | 全工作区最小的横向音符编辑器实现，读起来快 |
| "引擎是纯状态机"的分层 | `src/engine/tracker_engine.h` | 头注释明确 "No audio, no I/O, standard library only" |
| 数据驱动的节点图思路 | `src/graph/graph_model.h:35-43` | 若要支持多芯片，这个 NodeKind 枚举的设计可参考 |

| 不拿 | 原因 |
|---|---|
| 时间模型（tick/BPM，只到整 tick） | 与我们"采样级不量化"直接冲突 |
| 纵向 tracker 表格范式（`pattern_view.cpp`） | 用户明确否定 |
| 整个构建系统 | MSVC-only + 18 个联网依赖 |

---

## 9. 与 VGM 的关系

全库 `grep` `vgm|ym2151|ym2612|sn76489|opl|psg` = **0 命中**。

**没有任何 VGM / 芯片寄存器内容。** `module_player.cpp`（75 行）只是 libopenmpt 的薄包装，没有寄存器层。

---

## 10. 结论

**它是"最小钢琴卷帘范本"，不是壳候选。**

776 行的 `piano_roll_view.cpp` 值得读一遍，理解横向音符网格最简怎么做。但它的时间精度（整 tick）、范式（纵向表格）、构建要求（MSVC + 18 个联网依赖）三重不合格，不能当壳。

许可 GPLv3，代码不能进构建。
