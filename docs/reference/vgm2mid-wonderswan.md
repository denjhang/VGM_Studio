# vgm_ws_to_mid — WonderSwan 单芯片转换（△ 参考价值低）

| 项 | 值 |
|---|---|
| 路径 | `E:\working\vscode-projects\vgm2mid-modizer\vgm2mid_ws_175\vgm_ws_to_mid` |
| 许可 | 见仓库根（未核实） |
| 语言 | C++ |
| 规模 | 有效代码约 **2,400 行**（`main.cpp` + `VgmReader.cpp` + `WonderSwanChip.cpp` + `MidiWriter.cpp`） |
| 支持芯片 | **仅 WonderSwan（WS）4 通道** |
| 开发方式 | AI 辅助（`development_summary.md` 有完整历程） |

---

## 1. 它是什么

从零写的、**只针对 WonderSwan** 的 VGM→MIDI 独立控制台转换器。

它把 modizer 里的 libvgm/VGM 解析结构"手抄"进 `main.cpp`，把 modizer 的 `audio.cpp`（WS 声芯片寄存器语义）逆向成 `WonderSwanChip.cpp`。

`development_summary.md:9-16` 明确写了：核心库来自 modizer 的 `libs/libvgm` 和 `libs/libwonderswan`；**"VGM 中 WonderSwan 寄存器地址相对 `0x80` 偏移"是最关键发现**。

---

## 2. 源文件

| 文件 | 行数 | 职责 |
|---|---|---|
| **`WonderSwanChip.cpp`** | **390** | 核心：WS 芯片状态机、寄存器解析、音符判定、音高/时间换算 |
| `WonderSwanChip.h` | 81 | 通道状态数组 |
| `MidiWriter.cpp` | 260 | MIDI 事件流、delta-time、running status、循环块复制 |
| `main.cpp` | 182 | VGM 命令解析主循环（`wgplayer` 那套 switch 的手抄版） |
| `VgmReader.cpp` | 101 | 只读文件 + 取 header 的 data/loop offset |
| `InstrumentConfig.cpp` | 328 | 波形指纹→MIDI 音色自动匹配（`instruments.ini`） |
| `markdown_to_html.cpp` | 414 | 文档工具（与转换无关） |
| `midi_validator.cpp` / `validator_main.cpp` | 199 / 185 | MIDI 校验 |
| `hex_dumper.cpp` / `simple_hex_dump.cpp` | 63 / 91 | 调试工具 |

构建只链接 4 个文件（`.vscode/tasks.json:8-17`）。**libvgm 完全没有参与编译。**

---

## 3. WonderSwan 的音符判定（与我们无关，但方法可借鉴）

WS 硬件**没有 FM 那种 key-on 寄存器**。`WonderSwanChip.cpp:120-147`：

```cpp
bool is_active = channel_is_active[channel];
bool should_be_on = channel_enabled[channel] &&
                    (channel_volumes_left[channel] > 0 || channel_volumes_right[channel] > 0);
int current_note_pitch = period_to_midi_note(channel_periods[channel]);
if (should_be_on && current_note_pitch == 0) should_be_on = false;
```

- **使能位**：port `0x90` 的 bit0..3（`:252-254`）
- **音量**：`0x88..0x8B`，高半字节 = 左、低半字节 = 右，0..15（`:233-236`）
- **频率**：`0x80..0x87`，每通道 2 字节，取低 11 位；`0x7FF` 特判成 2048（= 静音/最高周期）（`:217-232`）
- **Note Off**：`is_active && !should_be_on`（`:136-142`）
- **Note On**：`!is_active && should_be_on`（`:145-146`）
- PCM（ch1，`0x90` bit5）额外用 `0x94` 的独立 PCM 音量（`:128-131`、`:258-261`）

**注意**：`channel_is_noise`、`noise_type`、`noise_reset` 只被赋值，**从未参与判定**（`:27, 36-37, 249-250`）——噪声通道实际仍按普通 period 出音高，只是 MIDI 音色被换成 127（`:82-92`）。

### 3.1 音高（无音符表，纯公式）

`WonderSwanChip.cpp:340-350`：

```cpp
double WonderSwanChip::period_to_freq(int period) {
    if (period >= 2048) return 0.0;
    return (3072000.0 / (2048.0 - period)) / 32.0;   // 3072000/32 = 96000
}
int WonderSwanChip::period_to_midi_note(int period) {
    double freq = period_to_freq(period);
    if (freq <= 0) return 0;
    int note = static_cast<int>(round(69 + 12 * log2(freq / 440.0)));
    return note > 127 ? 127 : note;
}
```

`development_summary.md:58-60` 记录了"音高过高"的修复：先 /2 再 **/4（降两个八度）**，即现在公式里的 `/32.0`。

---

## 4. 时间与复音

### 4.1 时间：绝对采样，无漂移

- 单位：**samples @44100**。`current_sample_time`（uint64）在 `advance_time()` 末尾累加（`:71`）
- 换算：`SAMPLES_TO_TICKS = (480.0*120.0)/(44100.0*60.0)`（`:9`），即 480 PPQN、tempo 固定 500000 µs（`main.cpp:20`）
- tick：`current_tick = (uint32_t)(current_sample_time * SAMPLES_TO_TICKS)`（`:75`, `:353`）
- 每个事件 `delta = current_tick - channel_last_tick_time[ch]`（`:133`, `:114`, `:322`）

**这是"绝对时间差"算法，不是"余数保留"算法。与 Valley Bell 的做法一致。**

### 4.2 复音：每通道严格单音

- 4 个 MIDI 通道 + 4 条 MIDI track（`:41-58`）
- 发声中被改音高：偏差 > ±2 半音 → 先 Note Off 再 Note On（`:180-185`）；≤ 2 半音 → 只发 Pitch Bend（`:186-197`，RPN 设 ±2 半音，`:53-57`）
- 没有 voice stealing

**这个"±2 半音内用 pitch bend，超出就重触发"的判据值得记。**

---

## 5. 自带的 libvgm 是 modizer 分支

全库 **587 处** `//TODO: MODIZER changes start / YOYOFR` 标记：

- `libvgm/emu/cores/ws_audio.c:13-15, 219-254, 371-381`
- `libvgm/emu/cores/fmopn.c:169-171, 2402-2421, 2499-2514, 3131-3148`
- `libvgm/emu/cores/adlibemu_opl_inc.c:42-44, 1180-1276`

改动是**给 iOS/modizer 做"每 voice 示波器采样缓冲"**（`m_voice_buff[...]`、`MODIZER_OSCILLO_OFFSET_FIXEDPOINT`、`m_voice_ofs`），属于**音频域**，**不是寄存器流回调**。

`player/vgmplayer.cpp` 仍是标准 `SndEmu_*` 接口（`:1411-1435`），**没有暴露 reg-write 钩子**。

**这个仓库也没链接 libvgm**，所以对我们只有"VGM 命令表结构"这一份参考价值。

---

## 6. 对我们的价值

| 拿 | 说明 |
|---|---|
| "无 key-on 寄存器芯片"的判定方法 | 使能位 + 音量组合 → 这是我们将来做 PSG/WS 时的范式 |
| "±2 半音内用 pitch bend，超出重触发" | 音高变化的判定阈值参考 |
| 音高过高时降八度的经验 | `development_summary.md:58-60` |

| 不拿 | 原因 |
|---|---|
| 只支持 WonderSwan | 我们做 YM2151 |
| 单文件流式处理 | 我们要持久化音符段 |
| 手抄 modizer 的 VGM 解析 | 我们直接用 libvgm |

---

## 7. 结论

**参考价值低。** 唯一有用的是"无 key-on 芯片怎么判定音符"这个范式（使能位 + 音量），以及"音高偏差阈值"的经验。

**不要参考它的架构**：它把 modizer 的 VGM 解析代码手抄了一遍放进 `main.cpp`，这正是我们 `RULES.md` F2/F15 禁止的做法。
