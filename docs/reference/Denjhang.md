# Denjhang Music Player v16 — VGM 可视化参考实现（○ 唯一完整的"VGM 寄存器 → 界面"实现）

| 项 | 值 |
|---|---|
| 路径 | `E:\working\vscode-projects\YM2163-Midi\Denjhang_Music_Player_v16\src` |
| 许可 | 见仓库根（未核实；只读参考） |
| 语言 | C++ |
| UI | **Win32 + Direct3D 11 + Dear ImGui**（DockSpace + Viewports） |
| 规模 | `windows/vgm/vgm_window.cpp` **8410 行** |

---

## 1. 它是什么

模块化的 ImGui DockSpace 多窗口播放器，四个 tab：

| Tab | 模块 | 内容 |
|---|---|---|
| YM2163 (DSG) | `windows/ym2163/` | FTDI 硬件芯片控制 |
| YMF262 (OPL3) | `windows/opl3/` | — |
| **libvgm simulation player** | `windows/vgm/` | **VGM 可视化（我们关注的）** |
| SN76489 (DCSG) | `windows/sn76489/` | SPFM 硬件 |

---

## 2. 核心结构：三段式

```
UpdateShadowRegisters()   →  BuildLevelMeters()   →  RenderPianoArea()
     (1238-2167)                (3089-4612)             (5240-5504)
   事件回放到 shadow 寄存器    算每通道的音符/音量      画键盘
```

### 2.1 阶段 A：`UpdateShadowRegisters()`（1238-2167，930 行）

把 VGM 寄存器写事件按当前播放位置**重放**到 shadow 数组，在**边沿**上更新 key-on/key-off。

- 时间：`s_player.GetCurTime(PLAYTIME_LOOP_EXCL) * 44100` → `curSample`，直接当 tick 用（1263-1276）
- 逐事件 `while (events[idx].tick <= curTick)` 回放
- 检测 `curSample` 回退 → `ResetShadowRegisters()`
- **每帧只回放一次**（去抖）：`if (thisFrame == s_shadowUpdatedFrame) return;`（1239-1242）

**各芯片的 key-on 判定**（全部是"寄存器位 + 上一帧状态"的边沿检测）：

| 芯片 | 寄存器 | 条件 | 行号 |
|---|---|---|---|
| **YM2151** | **`0x08`** | `ch = data & 7`；**`new_kon = (data & 0x78) != 0`**（4 个 slot 的 KeyOn 位任一置位） | **1335-1349** |
| YM2612 | port0 `0x28` | `slot = data & 7`；`ch = slot<4 ? slot : slot-1`；`new_kon = (data>>4)&1`；同一通道任一 slot keyon 即通道 keyon；keyoff 时要扫其它 3 个 slot | 1296-1328 |
| YM2413 | `0x20-0x28` | `KEY = bit4` 上升/下降沿；rhythm 用 `0x0E` 的 5 个位做边沿 | 1357-1393 |
| YM2203/2608/2610 | port0 `0x28` | `new_kon = (data & 0xF0) != 0`；YM2610 的 slot4 → ch3，slot5+ 忽略 | 1398-1491 |
| OPL2/OPL3/Y8950/YM3526/YMF278B | `0xB0-0xB8` | `bit5 = KEY`；OPL3 port1 → ch 9-17 | 1494-1600 |

**通道状态结构** `ChVizState`（229-244）：

```cpp
struct ChVizState {
    bool  key_on;
    bool  key_on_event;     // 本帧是否发生了 keyon 上升沿
    float visual_note;      // 平滑后的显示音高
    float target_note;      // 目标音高
    float start_note;       // keyon 时的锚点音高（用于 portamento 显示）
    int   prev_kf;
    float decay;            // 电平条 [0,1]，keyon 时重置为 1
    ...
};
```

### 2.2 阶段 B：`BuildLevelMeters()`（3089-4612，1500+ 行）

每帧从 shadow 寄存器算出 `LevelMeterEntry`（2618-2633）：

```cpp
struct LevelMeterEntry {
    // label / chip_abbrev / level / color
    // keyon / keyoff / note(0-127) / pitch_offset / tremolo / voice_ch
};
```

#### 音高换算（★ 关键）

**YM2151 用 KC/KF，不用 F-Number**（3400-3407）：

```cpp
int KC = s_shadowYM2151[0x28 + ch];
int oct = (KC >> 4) & 7;
int idx = KC & 0xF;
// 半音查表：OPM 的 KC 低 4 位跳过每八度的 4 个"空缺"位
int semi = idx < 3 ? idx : idx < 7 ? idx - 1 : idx < 11 ? idx - 2 : idx - 3;
int KF = (s_shadowYM2151[0x30 + ch] >> 2) & 0x3F;
float target_note = oct * 12 + semi + 24 /* + 24: 对齐到 MIDI C1 */ + KF / 64.0f;
```

**其它芯片用频率公式**：

| 芯片 | 公式 | 行号 |
|---|---|---|
| OPN (YM2612/2203/2608/2610) | `freq = clock/72 * fnum / 2^(21-block)` | 3262-3270 |
| OPL (YM3812/3526/8950/YMF262) | `freq = fnum * clock / (72 * 2^(19-block))` | 3273-3280 |
| YM2413 | `freq = fnum * clk / (72 * 2^(19-blk))`，`fnum = ((hi&1)<<8) \| lo`，`blk = (hi>>1)&7` | 3460-3463 |
| AY8910 | `freq = clock / (16 * period)` | 4067 |
| SN76489 | `freq = clock / (32 * period)` | 3797-3800 |
| NES APU | pulse `clk/(16*(period+1))`，tri `clk/(32*(period+1))` | 4157-4174 |
| GB DMG | `freq = 524288 / (2048 - period)` | 4214 |

通用频率→音符（`libvgm-modizer/chip_params.h:155`）：

```cpp
inline int FrequencyToMIDINote(float frequency) {
    float note = 12.0f * log2f(frequency / 440.0f) + 69.0f;
    return (int)(note + 0.5f);
}
```

#### 音量：ALG 载波的最小 TL（★ 关键，与我们的推测一致）

YM2151（3394-3399）：

```cpp
static const UINT8 kCarrierMask[8] = {0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF};
// bit0=OP1 … bit3=OP4，与我们的 kAlgCarriers 完全一致
for (int op = 0; op < 4; op++) {
    if ((cmask >> (3 - op)) & 1) {
        UINT8 tl = s_shadowYM2151[0x60 + op*8 + ch] & 0x7F;
        min_tl = std::min(min_tl, tl);
    }
}
```

OPN 族同样表（3304-3305），TL 偏移 `{0x90,0x94,0x98,0x9C}`。

**这是独立第三方对我们 `kAlgCarriers` 表的确认。** 我们的表：

```cpp
static const uint8_t kAlgCarriers[8] = {
  0b1000, 0b1000, 0b1000, 0b1000, 0b1100, 0b1110, 0b1110, 0b1111
};
// = {0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF}
```

完全一致。**这张表现在可以定稿了**（之前标了"待核对"）。

#### "新音锚点"判定（防抖，3200-3232）

```cpp
} else if (fabsf(fnt - vs.target_note) > 0.5f) {
    // 音高跳变 > 半个半音：视为新音锚点
    vs.start_note  = fnt;
    vs.target_note = fnt;
    vs.decay       = 1.0f;
}
```

即：半音以上跳变 = 重新起音；否则当滑音/颤音（`poff = visual_note - start_note`，0.5 指数平滑）。

**这条对我们有用**：音符块内如果音高跳变超过半个半音，应该考虑切块（即使 key-on 位没变）。

### 2.3 阶段 C：`RenderPianoArea()`（5240-5504，265 行）

- 先折叠成 `noteActive[128] / noteColor[128] / noteLevel[128] / noteKeyoff[128]`（5257-5288）
- 同音多通道：取 level 最大的通道颜色，其余写成 `noteEntries[note]` 标签列表
- 音域 `kMinNote=24 (C1)` ~ `kMaxNote=107 (B7)`，7 个八度
- 白键等分宽度、黑键 0.65 宽 / 0.62 高（5335-5345）
- 3 个 pass：白键 → 黑键叠加 → LFO 指示（PMS 水平抖动块 + AMS 垂直脉冲块）
- keyoff 状态用混色系数 0.35-0.55 表现"浅色残留"（5310-5326）

---

## 3. 它做不到的事（我们要补的）

**这是 keyboard，不是 piano roll。**

- `grep -i "piano roll"` 全仓无命中
- **没有音符"条"、没有持续时间字段、没有 history/segment 数组**
- 只有"当前帧是否发声"的瞬时状态
- 时间只存在于事件游标（按 tick 回放）和 `decay` 每帧衰减

**要出"钢琴卷帘条"，需要自己补事件流**：

```
在 UpdateShadowRegisters 里：
  keyon 上升沿  → push {note, startTick=curTick, chip, ch, ...}
  keyoff / decay 归零 → 关闭该 note（填 endTick）
```

时间直接用 `events[i].tick`（已确认是 44100 Hz 采样数）。

**它也不做同通道多音拆分**：YM2612 CH3 的 4 个独立频率（`0xA8/0xA9/0xAC/0xAD`）在 VGM 窗口里未单独建模（只在 `windows/ym2608/ym2608_window.cpp:579-610` 的 `s_fmKeyOnCh3Slot` 位掩码里有痕迹）。

---

## 4. 直接可用的知识点

| 知识点 | 位置 | 用途 |
|---|---|---|
| **YM2151 key-on = `0x08` 且 `(data & 0x78) != 0`** | 1335-1349 | 我们的切块边界 |
| **YM2151 音高 = KC/KF，不用 F-Number** | 3400-3407 | `IPitchMapper` 的默认实现 |
| **KC 半音查表**：`idx<3?idx : idx<7?idx-1 : idx<11?idx-2 : idx-3` | 3402-3403 | 同上 |
| **KF 精度**：`(reg 0x30 >> 2) & 0x3F`，`/64` 半音 | 3404-3405 | 同上 |
| **ALG 载波表 `{0x8,0x8,0x8,0x8,0xC,0xE,0xE,0xF}`** | 3394 | **确认我们的表** |
| **音量 = 载波最小 TL** | 3396-3399 | 通道音量提取 |
| **音高跳变 > 0.5 半音 = 新锚点** | 3200-3232 | 块内切分依据 |
| 逐芯片合成公式（OPN/OPL/PSG/NES/GB） | 3262-4290 | 多芯片扩展时的参考 |
| `ChVizState` 的 `start_note` / `decay` 字段设计 | 229-244 | 参考 |

---

## 5. 结论

**它是唯一一个"完整的 VGM 寄存器 → 界面"参考实现**，而且用的是 ImGui（与我们同栈）。

我们能拿的：

1. **YM2151 的 key-on 与音高换算规则**（这是最直接的，直接照搬）
2. **ALG 载波表的第三方确认**（我们的表定稿）
3. **"音高跳变 > 0.5 半音算新音"这个防抖判据**
4. **shadow 寄存器 + 边沿检测的架构**（与我们的 `RegStream → NoteSplitter` 一致）

我们不能拿的：

1. 它是 keyboard 不是 roll，**没有音符段概念**——音符段的构造要我们自己写
2. 同通道多音不拆分

**关键差异**：它把时间当作"当前帧"来画，我们把时间当作"事件的采样坐标"来存。这是本质区别——我们是数据层，它是渲染层。
