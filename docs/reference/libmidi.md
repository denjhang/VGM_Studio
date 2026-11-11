# libmidi — UI 控件来源（○ 最容易搬）

| 项 | 值 |
|---|---|
| 仓库 | opencodewin/libmidi |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\libmidi` |
| 许可 | **MIT**（仓库根 LICENSE），ImGui 也是 MIT |
| 语言 | C/C++ |
| 构建 | CMake ≥3.12，`USE_IMGUI=ON` 默认 |
| 规模 | libmidi 内核 19.9 万行 + imgui 5.98 万行 + 应用 1.4 万行 |
| 本机可编 | ⚠️ 需 ncurses / OpenMP / glm / SDL2 / OpenGL |
| 有 build 产物 | ❌ 无 |

---

## 1. 它是什么

**基于 TiMidity 的 SoundFont（SF2）软音源 MIDI 播放器**，带 ImGui GUI（`immidi`）与软件 MIDI 键盘输入。

四个可执行目标：

| 目标 | 入口 | 行号 |
|---|---|---|
| `immidi`（GUI 播放器，**主线**） | `test/immidi.cpp` | `Application_Setup` 3360 / `Midi_Frame` 2757 |
| `timidity`（ncurses/CLI） | `test/timidity.c` | `main` 5122 |
| `sample`（示例） | `test/sample.c` | `main` 37 |
| `bin2c`（资源转换） | `test/bin2c.c` | `main` 48 |

ImGui 应用框架（窗口/后端枚举）在 `imgui/apps/application/`。

---

## 2. 对我们最有价值的部分

**`test/immidi.cpp`（3398 行，421 处 `ImGui::` 调用）里三个函数几乎就是我们要的骨架。**

### 2.1 横向事件卷轴（★ 最重要）

```cpp
static void draw_event(std::vector<MIDI_Event>& events, int index,
                       ImVec2 size, float time, float duration = 5.0f);
```

**位置：`test/immidi.cpp:2559-2616`（58 行）**

做什么：

- 每通道一行横向卷轴
- `second_length = size.x / duration`（默认 5 秒窗口）
- `note_height = size.y / 128`
- 音符起点画绿圈、终点红圈、当前位置品红实心圈
- 线宽用力度 `event.vel * 2`

**为什么这是最重要的**：它的逻辑就是"start/end 时间 + lane index → 矩形/线"。**把"音符"换成"寄存器写"，把 `vel` 换成 `value` 的灰度/颜色，就是我们的通道视图。** 58 行，纯 `ImDrawList`，无业务耦合。

### 2.2 钢琴键盘

```cpp
class Piano {                     // 191-418
    void draw_keyboard(ImVec2 pos, bool input);  // 197-417
};
```

- `KEY_NUM 58`（186 行）
- 黑白键判定 `has_black()`（187-190）
- 支持电脑键盘映射输入（Q/2/W/3… 24 键 4 组）与鼠标点击
- `key_states[256]` 存 A/D 状态（-1=延音, -2=待抬起）
- 纯 `ImDrawList::AddRectFilled / AddRect / AddText` 手绘

**用法**：可以当"哪些通道正在发声"的指示器。`input` 分支（鼠标+键盘）与可视化无关，可裁掉。

### 2.3 通道表

**位置：`test/immidi.cpp:3153-3310`**

`ImGui::BeginTable` 11 列，行高 `channel_hight = 32`（3153）。列含 ProgressBar / Panning / PitchBend 指示。

**用法**：改成"每芯片 × 每通道"行。

### 2.4 其它可直接借的控件

| 控件 | 位置 | 用途 |
|---|---|---|
| `UvMeter()` | 646-736（两个重载） | 寄存器活动热度 / 音量 |
| `Knob()` | 816-930 | 寄存器值就地编辑（下一阶段） |
| `PlotEx` | 420 | 迷你折线图 |
| `PlotLinesEx` | 571 | 寄存器值随时间变化曲线 |
| `Splitter` | 597 | 分割条 |
| `draw_arc` / `draw_tick` | 768-815 | 弧线 / 刻度 |
| `ShowMediaScopeView()` | 2074-2557（484 行） | 波形/FFT/频谱/瀑布图（依赖 `ImGui::ImMat`） |

### 2.5 ImGui 扩展

| 文件 | 行数 | 内容 |
|---|---|---|
| `imgui/immat.cpp/h` | 5908 | ImMat 图像/张量容器 |
| `imgui/imgui_helper.cpp` | 3096 | helper（时间、路径、纹理） |
| `imgui/imgui_texture.cpp` | 1308 | 纹理管理 |
| `test/ImGuiFileDialog.cpp` | 5522 | 文件选择对话框 |
| `imgui/apps/application/` | — | 跨平台窗口/后端（DX9/11/GL2/GL3/Vulkan/SDL2/GLFW） |

ImGui 版本：**1.91.1**（`imgui/imgui.h:31`）。

---

## 3. 时间模型

**tick → 采样，双重。**

- 磁盘 SMF：`divisions`（PPQ 或 SMPTE），`readmidi.c:4466-4478` 解析
- **加载期一次性换算成采样数**：`groom_list()`（`readmidi.c:4051`），公式在 `readmidi.c:4729`：
  ```
  st += tempo * rate / 1e6 / divisions * (Δtick)
  ```
- 运行期：`current_sample`（`playmidi.c:229`）推进
- 事件结构（`libmidi/playmidi.h:4-9`）：
  ```c
  typedef struct { int32 time; uint8 type, channel, a, b; } MidiEvent;
  ```
  `time` 单位 = **采样点**

**对我们的意义**：它已经证明"把事件时间统一成采样点"这条路可走（和我们的 S0 时间基一致）。但它的 `type` 是 TiMidity 扩展枚举（`ME_NOTEON/ME_TEMPO/...`），不是原始 MIDI 字节。

---

## 4. 内核能力（我们**不用**）

| 能力 | 文件 | 说明 |
|---|---|---|
| MIDI 读 | `readmidi.c:4439 read_smf_file` | SMF/RCP/MFI/m2m/smplfile |
| MIDI 写 | `midi_a.c:175` | 写 MThd/MTrk |
| SoundFont | `sffile.c:141 load_soundfont` | SF2 解析器 + 内置 GM 音色 `sgm.c`（135,487 行） |
| 软件合成 | `mix.c`(2304), `playmidi.c`(9911), `resample.c`(1363), `reverb.c`(3850) | voice/mix/resample/reverb/chorus/低通 |
| 输出 | `wave_a.c`, `sdl_a.c`, `ncurs_c.c`, `midi_a.c` | WAV/SDL/ncurses/MIDI-out |

---

## 5. 许可风险（重要）

仓库根 `LICENSE` 声明 **MIT**（CodeWin 2014-2023），`imgui/LICENSE.txt` 也是 MIT。

**但** `libmidi/` 内核是 **TiMidity 血统**（README 明说 "MIDI player base on timidity"，`playmidi.h` 含 `TiMidity` 字样）。TiMidity 原版是 **GPL**。

### 结论

- ✅ **只用 `test/immidi.cpp` 里的 ImGui 绘制代码**（`Piano`、`draw_event`、`UvMeter`、`Knob`、`PlotEx` 等）—— 这部分是 CodeWin 自写、MIT 覆盖，风险低。
- ❌ **不搬 `libmidi/` 内核**（TiMidity/GPL 血统 + 与 VGM 无关）。

---

## 6. 与 VGM 的关系

全库 `grep` `VGM / YM2612 / SN76489 / OPL / OPN2` = **0 命中**。

**没有任何 VGM / 芯片寄存器 / 复古芯片相关内容。** 它的价值纯粹在 UI 绘制。

---

## 7. 结论

**它是本次盘点里唯一"框架完全相符"的可复用来源**：C++ / CMake / ImGui / MIT，且它画的正是"横向事件卷轴 + 钢琴键盘 + 通道表"，与我们要的形态一致。

**用法**：搬 `draw_event()` 改成 `draw_register_lane()`；搬 `Piano::draw_keyboard()` 做通道活动指示；搬 `UvMeter`/`Knob`/`PlotLinesEx` 做寄存器值控件。

**不搬**：`libmidi/` 内核（TiMidity 血统）。
