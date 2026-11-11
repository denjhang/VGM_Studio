# modizer — 音符分块判据的唯一实战参考（○ 只借判据）

| 项 | 值 |
|---|---|
| 仓库 | yoyofr/modizer（iOS 音乐播放器） |
| 本地路径 | `E:\working\vscode-projects\Reference_Project\vgm_libs\modizer-master-20260408\modizer-master` |
| 许可 | 见仓库根（未核实；仅读逻辑，不搬代码） |
| 语言 | Objective-C++ / C（iOS） |
| 关键结论 | **没有"VGM → 音符表"的离线转换器**；音符条是在**渲染时实时合并**出来的 |

---

## 1. 它实际在做什么

```
VGM 文件
 → libvgm 魔改版（vgmplayer_cmdhandler.cpp）
 → 芯片核的 write 回调
 → 同一 update_one() 里比较"输出波形是否变化"
 → 写全局数组 vgm_last_note[ch] / vgm_last_vol[ch] / vgm_last_instr[ch]
 → ModizMusicPlayer.mm:6878 把频率换成 MIDI 音号
 → tim_notes[64][256]（每 1024 采样一块）
 → RenderUtils.mm:5841 UpdateDataMidiFX() 推入 256 行环形缓冲
 → RenderUtils.mm:5891 DrawMidiFX() 把连续同音行合并成 bar
```

**关键点**：音符对象 `{start, pitch, duration, channel}` **在解析阶段根本不存在**。真正的音符段是渲染时现场合并出来的。

---

## 2. 两个它做对的事

### 2.1 音符嗅探不用"查 key 位"，而用"输出是否变化"

YM2612（`emu/cores/fmopn.c:4979-5019`）：

```c
if (!(cch[ii]->Muted)) {
    if (cch[ii]->block_fnum == 0) {
        if ((out_fm[ii] != old_out_fm[ii])) { ... vol=2 }
    } else {
        if ((out_fm[ii] != old_out_fm[ii])) {      // ← 输出变化才更新
            int freq = (cch[ii]->block_fnum) & 0x7FF;
            int octave = ((cch[ii]->block_fnum) >> 11) & 0x7;
            vgm_last_note[ii+m_voice_ofs] = (freq<<octave)*110.0f/1081.0f;
            int newvol = cch[ii]->keyonff_triggered+1;   // 1 或 2
            cch[ii]->keyonff_triggered = 0;
            if (vgm_last_vol[...] < newvol) vgm_last_vol[...] = newvol;
        }
    }
}
```

"寄存器写入 → 输出波形确实改变"双重条件，**避免静音通道刷出音符**。

YM2151（`ym2151.c:1988-2003`）条件更严：还要求 `pan != 0`：

```c
if (!Muted && (pan[2*ii]|pan[2*ii+1]) && (chanout[ii]!=last_chanout[ii])) {
    if (oper[ii*4+3].freq == 0) { /* nothing */ }
    else {
        int freq = PSG->oper[ii*4+3].freq;
        vgm_last_note[ii+m_voice_ofs] = freq/604.0f/2;
        vgm_last_vol[ii+m_voice_ofs] = PSG->oper[ii*4].key+1;   // 用 operator key 位
    }
}
```

### 2.2 音符块合并判据（★ 这是我们要抄的唯一东西）

`utils/GLES3/RenderUtils.mm:5970-6015`（`DrawMidiFX`），约 50 行：

```c
if (data_midifx_note[j][i]) {
    if (vol && (st & VOICE_ON)) {           // 该行有音
        bar.startidx = j - (MIDIFX_LEN - data_midifx_len);
        bar.note = note;  bar.instr = instr;
        bar.size = 0;  bar.played = 0;
        bar.frameidx = st>>4;
        while ( data_midifx_instr[j][i] == instr &&   // ① 同通道
                data_midifx_note[j][i]  == note  &&   // ② 同音高
                data_midifx_vol[j][i]   <= vol   &&   // ③ 音量不回升
                (data_midifx_st[j][i] & VOICE_ON) ) { // ④ 仍在发声
            bar.size++;
            data_midifx_st[j][i] = st;
            vol = data_midifx_vol[j][i];
            j++;
        }
        bar_count++;
    }
}
```

**切分规则表**：

| 条件 | 动作 | 含义 |
|---|---|---|
| 音高改变（②） | 断开，新 bar | — |
| 通道/instr 改变（①） | 断开，新 bar | — |
| **音量上升**（③ 的 `<=` 被违反） | 断开 → 新 bar | **这就是 key-on 重触发** |
| `note == 0` 或 `VOICE_ON` 清零（④） | 断开 → key-off/静音 | — |
| **音量下降** | **不断开**，继续延长 | 长音衰减不断条 |

"音量下降是合法延长"这条是关键：`vol <= vol` 允许递减，而 `vol` 只在 keyonff 触发时变成 2 —— **重触发 = 新 bar，衰减 = 同 bar**。

---

## 3. 它做错的两件事（我们不能抄）

### 3.1 时间单位是 1/60 秒帧行，不是采样

- 底层音频块：`SOUND_BUFFER_SIZE_SAMPLE = 1024` @ 44100 → 23.2 ms/块
- **但 bar 的长度单位是"FX 行"**：`UpdateDataMidiFX` 每显示帧推 1 行，`DetailViewControllerIphone.mm:9750-9754` 每帧调 `frameToUpdate` 次（30 或 60 fps）
- 所以 1 行 ≈ 1/30 或 1/60 秒

**→ 与我们的 D1（时间单位 = 采样数）直接冲突。必须换成采样。**

### 3.2 音符提取散落在 26 个芯片核的热循环里

`grep -c vgm_last_note` 统计：

| 核心 | 写入点 | 芯片 |
|---|---|---|
| `fmopn.c` | 16 | YM2203/2608/2610/2612 |
| `fmopl.c` | 7 | YM3812/3526/8950 |
| `nesintf.c` | 3 | NES APU |
| `ymf262.c` / `ym3438.c` | 2 each | OPL3 / YM3438 |
| `x1_010.c` / `qsound_ctr.c` / `pwm.c` / `okim6258.c` / `emu2149.c` / `adlibemu_opl_inc.c` | 2 each | 各自 |
| **`ym2151.c`** | **1** | **YM2151** |
| 其余 26 个核 | 1 each | 各自 |

且 47 个核文件都 `#include "../../../../../src/ModizerVoicesData.h"` —— **反向依赖 App 的全局变量**。上游 libvgm 绝无此设计。

**→ 这不是"可移植的模块"，是把 libvgm 当"带音符嗅探的音频后端"深度耦合进 iOS App。不能复刻这套架构。**

---

## 4. 可复用清单

| 优先级 | 内容 | 位置 | 可移植性 |
|---|---|---|---|
| ★★★ | **bar 合并判据** | `RenderUtils.mm:5965-6015`（MidiFX）、`7320-7375`（Synthesia） | 逻辑独立，~50 行，把 `data_midifx_*` 换成自定结构即可 |
| ★★★ | 频率 → MIDI 音号 | `ModizMusicPlayer.mm:922-952` | 10 行纯数学，零依赖 |
| ★★☆ | 芯片 → 通道数能力表 | `ModizMusicPlayer.mm:974-1136` | 纯 switch，可当"哪些芯片支持音符提取"的白名单 |
| ★★☆ | YM2151 嗅探条件（`pan!=0` + 输出变化） | `ym2151.c:1988-2003` | 作为"何时算有音"的判断依据参考 |
| ★☆☆ | 帧推入与位域打包 | `RenderUtils.mm:5841-5888` | 环形缓冲 + 位域，可改造成 note event 队列 |
| ☆ | `DrawPianoRollFX` / `DrawPianoRollSynthesiaFX` | `RenderUtils.mm:6796` / `7214` | 与 ImGui+GLES 强耦合，不搬 |

### 4.1 频率 → MIDI 音号（10 行，直接抄）

`ModizMusicPlayer.mm:922-952`：

```c
static int vgm_getNoteFromFreq(double freq) {
    const double LOG2_440 = 8.7813597135246596040696824762152;
    const double LOG_2 = 0.69314718055994530941723212145818;
    if (freq > 1.0) return round(12 * (log(freq)/LOG_2 - LOG2_440) + 69);
    else return 0;
}
```

`vgm_getSubNoteFromFreq`（`:934`）额外算 1/8 音分，编码成 0-15。

**注意**：它各芯片的"频率"是近似值（YM2612 用 `110/1081` 这种缩放；YM2151 用 `freq/604/2`），只够画钢琴条，不够做精确 MIDI 导出。我们要自己用正确的 F-Number 公式。

---

## 5. 我们该怎么做（结论）

### 5.1 不复刻它的架构

正确做法是：**写一个纯函数式的 VGM 命令遍历器**（用 libvgm 的命令表 `vgmplayer_cmdhandler.cpp:28-224` 的 `VGM_CMDS` 表），遍历时直接在 `0x28`（YM2612）/ `0x08`（YM2151）上判 key-on/key-off，把 `(sample, chip, ch, KC, KF)` 记成事件，再做音符合并。

时间单位是**精确的 atSample**，不是 1/60 秒。

### 5.2 抄它的合并判据

`ARCHITECTURE.md` §3 的分块规则表就是它的判据（同音高 + 同通道 + 音量不回升 + VOICE_ON），只是把"断行"换成"断采样"。

### 5.3 抄它的"输出变化"思想

判断"这个通道现在有没有音"，不要只查 key-on 位（因为有些曲子会先 key-on 再写参数），要结合：
- key-on 位（`0x08` 的 slot 掩码）
- 音高寄存器非零（KC/KF）
- pan 非零（YM2151 的 `0x20+ch` 低 2 位）

modizer 的 `pan != 0` 条件值得借鉴——它过滤掉了"寄存器写了参数但没打算发声"的通道。

---

## 6. 与标准 libvgm 的区别

| 项 | 本仓库 |
|---|---|
| 标记 | `MODIZER changes` 334 处 / 47 文件；`YOYOFR` 627 处 / 52 文件 |
| README | 无（上游也没有） |
| 反向依赖 | 47 个核 `.c` 文件 `#include "../../../../../src/ModizerVoicesData.h"` |
| 新增字段 | `FM_CH.keyonff_triggered`（`fmopn.c:626`） |
| 新增全局 | `vgm_last_note/vol/instr`、`m_voice_ChipID`、`m_voice_buff[]` |
| 音频核改造 | 每个 `*_update_one()` 里塞 `m_voice_buff[...] = LIMIT8(out_fm...)` 分轨波形捕获 |
| 设备遍历 | `vgmplayer.cpp:1807-1826` 每设备设 `m_voice_current_system / systemSub / systemPairedOfs` |
| `.git` | 无，无法比对 commit |

**结论**：不是"为移植而改"的轻量 fork，是深度耦合进 iOS App 的改造版。
