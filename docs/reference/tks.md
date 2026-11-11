# tks（Synergy）— 不用（✗）

| 项 | 值 |
|---|---|
| 仓库 | bsp2/tks |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\tks` |
| 许可 | TkScript 内核 / tkui / tkmidipipe = **LGPL 2.1**；tksampler / minnie / synergy_replay = MIT；**reSID / libsidplay = GPL v2** |
| 语言 | C++ 内核 + **TkScript 脚本**（UI 逻辑全是 `.tks`） |
| 构建 | 手写 GNU make + MSVC（`makefile.msvc` / `install_msvc.mk`），另有 `tks-source/build_vs/vs2019/tks.sln` |
| 规模 | C++ 3512 文件 / 88,791 行；`.tks` 脚本 1588 文件（Synergy 单目录 377 文件 / 119MB） |
| 本机可编 | ❌ 需 MSVC 2026 + Windows SDK 硬编码路径 |
| 有 build 产物 | ❌ 无（只有两个无关的 i386 测试 exe） |

---

## 1. 它是什么

**TkScript（TKS）脚本语言 VM + 原生插件 SDK**。实际"软件"是脚本写的：

- **Synergy** —— MIDI 音序器（tracker + 步进 + piano roll 三合一）
- **Eureka** —— DAW
- 另有 SID 播放、采样器、矢量渲染器

主入口链：

| 层 | 文件 | 行号 |
|---|---|---|
| C++ VM | `tks-source/main.cpp` → `main` | 1761 / `tks_main` 1454 |
| Synergy 脚本 | `tks-projects/apps/synergy/synergy.tks` → `main.tks` 的 `function main()` | 720 |
| 包清单 | `tks-projects/apps/synergy/synergy.tkp` | — |

---

## 2. 为什么完全不能用

### 2.1 没有 ImGui

**全仓 `grep -rli imgui` = 0 命中。**

它的 UI 是自研 **tkui** 脚本控件库（`tkui/*.tks`，153 个控件）+ **Minnie/shadervg** 矢量渲染（OpenVG 风格），绘制原语是 `UIRenderer.DrawFilledRectangle / DrawLine / DrawRectangle`（`tkui/ui_renderer.tks:30 class UIRenderer`），底层 `sdvg_*`（`minnie/shadervg/shadervg.h`）。

而用户明确要求 **ImGui**（`RULES.md` F6 相关）。

### 2.2 语言/运行时三重错配

UI 逻辑是 TKS 脚本（`.tks`），依赖 TkScript VM + YAC 宿主 + `ying_*` 绑定。C++ 项目**无法直接引用**。

### 2.3 许可最差

- 核心脚本/UI/插件：**LGPL 2.1**（`tks-source/COPYING`、`tkui/COPYING`、`tkmidipipe/*.h` 头注释）
- reSID / libsidplay：**GPL v2**（`tksidplay2/sidplay-libs-2.1.1/resid/COPYING`）

### 2.4 没有 VGM

全库 `grep '\bVGM\b'` = **0 命中**（之前疑似命中的都是字体二进制串）。

---

## 3. 它唯一有价值的东西：reSID（但用不上）

**真正的周期级寄存器仿真**：

| 文件 | 说明 |
|---|---|
| `tksidplay2/sidplay-libs-2.1.1/resid/sid.cc:187` | `void SID::write(reg8 offset, reg8 value)` → 分发到 `voice[].wave.writeFREQ_LO` 等 |
| `resid/sid.h` | 145 行 |
| `resid/voice.cc` / `filter.cc` / `envelope.cc` / `wave.cc` | 147 / 309 / 231 / — |
| `wave6581_*.c` / `wave8580_*.c` | 8 张波形表，各 540 行 |

**但它是 SID（6581/8580），不是 YM2151。** 对我们的 YM2151 场景零用处，且许可是 GPL v2。

---

## 4. 其它可参考的设计（只能读，不能搬）

| 目标 | 位置 | 判断 |
|---|---|---|
| 钢琴键盘（脚本版） | `synergy/PianoWidget.tks:98-264` | 只作**布局参考**：黑键偏移表 `offXTbl` 131-140 行，比 libmidi 的 `has_black()` 更精细（真实键盘黑键错位） |
| 钢琴卷帘/音符事件视图 | `synergy/NSQ_Events.tks:1108-1359`（`onDraw()`） | 参考"按音符行铺背景 + 小节线 + note 块"的画法 |
| 编曲器/时间轴（bar 网格） | `synergy/NSQ_Arranger.tks:3964`（`onDraw()`）、`:4273`（`onDrawOverlays()`） | — |
| DAW 时间轴（clip 轨道） | `synergy/NA_TimelineData.tks:6183`（`onDraw()`）、`:5993`（`onDrawOverlays()`）；数据 `NA_Clip.tks:39-40 start_ticks/end_ticks` | 8578 行，太大了 |
| **SysEx 参数矩阵编辑器**（芯片参数面板范式） | `synergy/syntheditor_generic/GenericSynthEdit.tks`(4078)、`syntheditor_yamaha_fs1r_montage/Yamaha8OpFM.tks`(903)、`syntheditor_korg_radias/KorgRadias.tks`(2285) | **对"VGM 芯片寄存器面板按 op/param 分页"很有参考价值**：Yamaha FS1R/Montage 8-op FM、Korg Radias、Prophet 08/12、Oberheim Matrix1000 的参数矩阵 |
| MIDI 帧容器（按类型分桶 + 双时间戳） | `tkmidipipe/MIDIPipeFrame.h:55-75` | `sUI time_stamp; // millisec`（74 行）+ `sSI time_stamp_tmp; // ticks`（75 行）；事件按类型分 12 个数组（note_on/note_off/cc/rpn/nrpn/pitchbend/sysex...，55-68 行）。**数据模型借鉴价值高** |
| 矢量渲染器 | `minnie/`（**MIT**，含 shadervg 58k 行） | 若不强求 ImGui，minnie 是可用的替代渲染层；但要接 VGM 等于换技术栈 |

---

## 5. MIDI 与合成（与我们无关）

| 能力 | 位置 |
|---|---|
| MIDI 读（脚本层 SMF，仅 PPQ） | `synergy/MIDIFile.tks`（511 行）；`SMPTE division format is not supported` 480 行 |
| MIDI 写（完整 SMF 导出器） | `synergy/replay.tks:345-390`（`smfAppendDeltaTime` 345、`finishSMFExport` 355） |
| 实时 MIDI | `tkmidi/`（WinMM）、`tkmidi_alsa`、`tkmidi_portmidi`、`tkmidipipe`（13,319 行） |
| 合成引擎 | `tksampler/`（MIT，40,054 行，mod matrix 183 源/203 目标）；Cycle 模块化软合成（`tks-projects/libs/cycle/`） |
| SoundFont | **无 SF2 支持** |

---

## 6. 结论

**完全不用。** 四个硬理由：

1. **无 ImGui**（用户明确要求 ImGui）
2. **UI 逻辑是 TKS 脚本**（C++ 无法引用）
3. **许可 LGPL 2.1 / GPL v2**
4. **无 VGM**

唯一值得读的是 `syntheditor_generic/GenericSynthEdit.tks`（4078 行）—— 它展示了"FM 芯片参数按 operator/参数页做成矩阵编辑器"的交互设计。这对我们后续做 YM2151 音色面板有参考价值，但代码是 LGPL 脚本，**只能重写不能搬**。
