# VGM Studio 文档索引

> 2026-11-11。所有文档都是**分开的独立文件**，改哪一块只动那个文件。

## 必读顺序

| # | 文件 | 内容 |
|---|---|---|
| 1 | [`GOAL.md`](GOAL.md) | **目标 + 步骤**：数据流全图、结构化五规则、三件事、跨芯片等价性分析 |
| 2 | [`CLI.md`](CLI.md) | **命令行设计（当前主线）**：7 个程序、完整链路、映射表格式、预设鼓声、实现顺序 |
| 3 | [`ARCHITECTURE.md`](ARCHITECTURE.md) | **架构**：四个角色的分工、四条数据流、分块规则、代码落点、引擎边界 |
| 4 | [`DATA-MODEL.md`](DATA-MODEL.md) | **JSON schema**（权威定义）：采样数时间轴、按音符分块、完整性不变量 |
| 5 | [`DESIGN.md`](DESIGN.md) | **界面设计**：布局、交互、音色编辑器、转换面板、可搬的代码 |
| 6 | [`RULES.md`](RULES.md) | 硬约束 D1–D5 / E1–E5 + 禁止 F1–F15 |
| 7 | [`MILESTONES.md`](MILESTONES.md) | 验收标准（每条都可实测） |
| 8 | [`CHIP-YM2151.md`](CHIP-YM2151.md) | **YM2151 语义表**（三方交叉验证，已定稿）：key-on、KC/KF、ALG 载波 |
| 9 | [`VOICE-MAPPING.md`](VOICE-MAPPING.md) | **乐器指纹 + OPLL 15 个 ROM 音色表 + 最近邻匹配 + 音量 4:1 台阶换算**（vgm-conv/ym-voice 实证 + 本机实测） |
| 10 | [`IMPLEMENTATION-CLI.md`](IMPLEMENTATION-CLI.md) | **CLI 实施记录**：`cli/` 工程结构、影子设备接法、实测结果、7 个坑、json2mid 音高验证 |
| 11 | [`PITCH-CONVERSION.md`](PITCH-CONVERSION.md) | **★ 音高转换权威汇总**：YM2151/YM2413 在 5 个参考实现里的全部公式（带行号）+ 物理真值推导 + 4 个已修 bug |
| 12 | [`VOICE-FUI.md`](VOICE-FUI.md) | **★ 音色文件与宏**：`.fui` 命名规则、FM/MA 块布局、22 条宏、**宏速率不在 fui 里**、宏展开实测 |
| 13 | [`INSTRUMENT-LIBRARY.md`](INSTRUMENT-LIBRARY.md) | **★ 本地乐器库**：标签缩写对照、参数/指纹/影子三条判据的区别、源→目标映射表、38 个乐器的完整参数（由 `fuitool lib` 生成） |

## 参考库档案（一库一文件）

### 核心三件套

| 文件 | 库 | 结论 |
|---|---|---|
| [`reference/0x808.md`](reference/0x808.md) | averagenative/0x808 | **★ 壳**。MIT，本机可编（一处链接待修） |
| [`reference/libvgm.md`](reference/libvgm.md) | valleybell/libvgm | **★ 解析与回放引擎**。已编出 3 个 `.a` |
| [`reference/furnace.md`](reference/furnace.md) | tildearrow/furnace | **★ 输出引擎**（只链 engine 层）+ 寄存器字典 + `.fui` 音色格式 |

### 音符分块与音高换算（决定显示准确性）

| 文件 | 库 | 结论 |
|---|---|---|
| [`reference/vgm2mid-valleybell.md`](reference/vgm2mid-valleybell.md) | Valley Bell vgm2mid（VB6，9.5k 行） | **★★ 最权威的音符分块参考**。含 YM2151，9+ 芯片一张表 |
| [`reference/_pitch-valleybell.md`](reference/_pitch-valleybell.md) | 同上（专项） | **★★ 音高代码逐行摘录**：KC 查表、KF/64、Hz→Note、弯音拆分、无漂移时间换算 |
| [`reference/_pitch-denjhang-mdplayer-modizer.md`](reference/_pitch-denjhang-mdplayer-modizer.md) | Denjhang + MDPlayer + modizer | **★★ 三家音高对照**：key-on 判定、KC 查表、KF 是否计入、YM2413 公式 |
| [`reference/_pitch-vgmconv.md`](reference/_pitch-vgmconv.md) | vgm-conv + ym-voice | **★★ 频率↔KC/KF/fnum 全部公式**，含 octaveOffset 与时钟换算 |
| [`reference/_pitch-furnace.md`](reference/_pitch-furnace.md) | Furnace（反向） | **★★ 音符→寄存器**：hScale/KC、KF 1/64、OPLL fnum/block，正好与解析互逆 |
| [`reference/Denjhang.md`](reference/Denjhang.md) | Denjhang Music Player v16 | **○ 唯一完整的"VGM→ImGui 界面"实现**，同栈 |
| [`reference/MDPlayer.md`](reference/MDPlayer.md) | MDPlayer form/KB | ○ 交叉验证：key-on/KC 查表/ALG 掩码与本项目一致 |
| [`reference/modizer.md`](reference/modizer.md) | yoyofr/modizer | ○ 音符合并判据的实战来源 |

### 跨芯片转换

| 文件 | 库 | 结论 |
|---|---|---|
| [`reference/vgm-conv.md`](reference/vgm-conv.md) | digital-sound-antiques/vgm-conv | **○ 4-op→2-op 音色映射范本**（无 OPM→OPLL） |
| [`reference/vgmtransmute.md`](reference/vgmtransmute.md) | grauw/vgmtransmute | △ OPNA→OPNB 寄存器重映射（BSD-2，1.3k 行） |
| [`reference/vgm2mid-wonderswan.md`](reference/vgm2mid-wonderswan.md) | vgm2mid_ws_175 | △ 仅 WonderSwan；可取"无 key-on 芯片"判定法 |

### UI 与时间模型

| 文件 | 库 | 结论 |
|---|---|---|
| [`reference/libmidi.md`](reference/libmidi.md) | opencodewin/libmidi | ○ UI 控件最容易搬（横向事件卷轴 58 行，MIT） |
| [`reference/Gridline.md`](reference/Gridline.md) | lolitemaultes/Gridline | ○ 唯一实现"不量化"的时间模型 |
| [`reference/nanotracker.md`](reference/nanotracker.md) | savannah-i-g/nanotracker | △ 最小钢琴卷帘范本；本机需 MSVC |

### 不用

| 文件 | 库 | 结论 |
|---|---|---|
| [`reference/FamiStudio.md`](reference/FamiStudio.md) | BleuBleu/FamiStudio | × **无 YM2151**，只读状态机形状 |
| [`reference/tks.md`](reference/tks.md) | bsp2/tks（Synergy） | × 自研 tkui，无 ImGui |

## 历史

| 文件 | 说明 |
|---|---|
| [`_recovered/DESIGN_v1_recovered.md`](_recovered/DESIGN_v1_recovered.md) | 2026-11-11 首版设计全文（675 行）留档 |

## 第三方参考源码（只读，不进构建）

| 目录 | 内容 | 许可 |
|---|---|---|
| [`_vendor/ym-voice-0.3.2/`](_vendor/ym-voice-0.3.2/) | `ym-voice` 0.3.2 完整 dist（OPM/OPN/OPL/OPLL 音色编解码 + 15 个 ROM 表 + 最近邻匹配） | ISC |
