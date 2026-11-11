# MDPlayer form/KB — 芯片寄存器查看器（○ 参考：每通道小键盘的绘制方式）

| 项 | 值 |
|---|---|
| 路径 | `E:\working\vscode-projects\yasp11\MDPlayer-STBL449\MDPlayer\MDPlayerx64\form\KB` |
| 许可 | 见仓库根（未核实；只读参考） |
| 语言 | **C# WinForms** |
| 规模 | 48 个非 Designer `.cs` 文件，合计 **18,143 行** |
| 结论 | **不是钢琴卷帘**，是"每通道一行小键盘 + 寄存器参数表" |

---

## 1. 它是什么

**芯片寄存器查看器 + 每通道小键盘显示。** 每个 form 背后是一张位图面板（`ResMng.ImgDic["planeYM2612"]`），通过 `FrameBuffer` 直接写像素（`drawByteArray`），属于 **GDI+ / Bitmap 像素缓冲**，不是 WinForms 控件绘图。

### 1.1 文件清单（按芯片分类）

| 分类 | 文件:行数 |
|---|---|
| 根 | `frmChipBase.cs:68`、`frmMIDI.cs:452`、`frmRegTest.cs:593`、**`frmYM2151.cs:299`**、`frmYMZ280B.cs:186` |
| OPN | `frmYM2203.cs:473`、`frmYM2608.cs:500`、`frmYM2609.cs:608`、`frmYM2610.cs:600`、`frmYM2612.cs:557`、`frmYM2612MIDI.cs:571` |
| OPL | `frmY8950.cs:351`、`frmYM2413.cs:422`、`frmYM3526.cs:322`、`frmYM3812.cs:332`、`frmYMF262.cs:488`、`frmYMF278B.cs:636` |
| OPX | `frmYMF271.cs:249` |
| PSG | `frmAY8910.cs:264`、`frmSAA1099.cs:10`（空壳）、`frmSN76489.cs:383` |
| NES | `frmDMG.cs:337`、`frmFDS.cs:242`、`frmMMC5.cs:234`、`frmN106.cs:244`、`frmNESDMC.cs:333`、`frmS5B.cs:233`、`frmVRC6.cs:212`、`frmVRC7.cs:302` |
| PCM | `frmC140.cs:259`、`frmC352.cs:267`、`frmGA20.cs:227`、`frmK053260.cs:246`、`frmK054539.cs:268`、`frmMegaCD.cs:193`、`frmMpcmX68k.cs:415`、`frmMultiPCM.cs:277`、`frmOKIM6258.cs:212`、`frmOKIM6295.cs:186`、`frmPCM8.cs:206`、`frmPWM.cs:10`（空壳）、`frmQSound.cs:257`、`frmRf5c68.cs:212`、`frmSegaPCM.cs:248` |
| WF | `frmHuC6280.cs:267`、`frmK051649.cs:224` |
| Driver | `frmPPZ8.cs:280` |

### 1.2 三个特殊文件

| 文件 | 内容 |
|---|---|
| `frmRegTest.cs`（593 行） | **纯寄存器网格查看器**。`RegisterManager` 里 `AddChip("YM2151", ...) => Audio.GetYM2151Register(0)`，把 `0x00-0xFF` 逐字节画成十六进制表。**这是我们要做 `vgm_reg_view` 的参考** |
| `frmMIDI.cs`（452 行） | MIDI 模块（XG/GS）的 LCD 面板 + 32 通道 fader。不是键盘 |
| `frmYM2612MIDI.cs`（571 行） | MIDI 键盘输入 → YM2612 的**音符历史条**（`NoteLog[ch][100]` 环形缓冲，显示最近 10 个音名）。**全仓最接近 piano roll 的东西**，但只是序号列表，不是时间轴 |

`frmSAA1099.cs`、`frmPWM.cs` 是空壳（只有构造函数）。

---

## 2. 统一数据通路（与 Denjhang 的架构差异）

```
MDSound 芯片寄存器写
  → ChipRegister.setXxxRegister()
  → 维护 fmRegisterXxx[][] 快照 + fmKeyOnXxx[] / ChipKeyInfo.On[]/Off[]
  → Audio.GetXxxRegister() / GetXxxKeyOn() / GetXxxKeyInfo()
  → frmXxx.screenChangeParams()  算 note
  → frmXxx.screenDrawParams()    → DrawBuff.KeyBoard*()
```

**两段式刷新**（`frmChipBase.cs` 定义虚方法）：

- `screenChangeParams()`：从寄存器快照算 note/volume/inst
- `screenDrawParams()`：只在值变化时重绘对应图元（`if (ot == nt) return;`）

管道是**读寄存器快照**，不是"重放事件流"。与 Denjhang 的 `UpdateShadowRegisters` 思路一致，但 MDPlayer 的寄存器快照来自 MDSound 的实时写入，不是 VGM 事件重放。

---

## 3. YM2151（OPM）支持

**有，`form/KB/frmYM2151.cs`（299 行）**，直立在 KB 根目录。

### 3.1 key-on 判定（`ChipRegister.cs:1252-1270`）

```csharp
if (dAddr == 0x08) {
    int ch = dData & 0x7;
    if ((dData & 0x78) != 0) {
        fmKeyOn[ch] = (dData & 0x78) | 1;     // &1 = 通道级 keyon，高 4 位保留 slot 掩码
    } else {
        fmKeyOn[ch] &= 0xfe;                   // 清 bit0
    }
}
```

与 Denjhang 的 `new_kon = (data & 0x78) != 0` 一致。

### 3.2 音高换算（`frmYM2151.cs:218-227, 254`）

```csharp
// KC 查半音
int note = reg[0x28 + ch] & 0xF;
note = note < 3 ? note : note < 7 ? note - 1 : note < 11 ? note - 2 : note - 3;
int oct = (reg[0x28 + ch] >> 4) & 7;
note = (fmKey & 1) != 0 ? oct * 12 + note + hosei : -1;
// KF 不参与音高，只单独显示
int kf = (reg[0x30 + ch] & 0xFC) >> 2;
```

**注意**：MDPlayer **KF 不参与音高计算**，只单独显示。Denjhang 则把 KF 加进去了（`+ KF/64`）。**Denjhang 的做法更正确**（KF 就是分数半音）。

### 3.3 音量（`frmYM2151.cs:228-242`）

CARRIER 最小 TL：

```csharp
// md[8] = {0x40,0x40,0x40,0x40,0x50,0x70,0x70,0x78}
int md = ...;  // 按 alg 取的掩码
```

注意 MDPlayer 的掩码是 **7 位格式**（`0x40/0x50/0x70/0x78`），等价于 Denjhang 的 4 位格式 `0x8/0xC/0xE/0xF` 左移 3 位。**同一个结论，不同编码。**

### 3.4 其它芯片的音高（都用"查表找最近"）

| 芯片 | 公式 | 行号 |
|---|---|---|
| YM2612 | `ff = freq / ((2<<20)/(masterClock/(24*fmDiv))) * (2<<(octav+2)); ff /= 1038f;` 然后 `Common.searchYM2608Adpcm(ff)-1` | `frmYM2612.cs:180-190` |
| YM2203 | 同上，`fmDivTbl[divInd]`（reg 0x2D 选 6/3/2），`12*fmDiv` | `frmYM2203.cs:167-176` |
| YM2608/2609/2610 | 同上（`24*fmDiv`） | `frmYM2608.cs:197-203`、`frmYM2610.cs:349-355` |
| YM2413 | `nyc.note = Common.searchSegaPCMNote(freq/172.0) + (oct-4)*12`，`freq = reg[0x10+ch] + ((reg[0x20+ch]&1)<<8)` | `frmYM2413.cs:108-113` |
| YM3812/3526/Y8950 | `fmus = FNUM/(1<<19) * (mClock/72) * (1<<BL); note = searchSegaPCMNote(fmus/523.3)`（523.3 = C4） | `frmYM3812.cs:166-171` |
| YMF262 | `nt = searchSegaPCMNote(inst[12]/344.0) + (inst[11]-4)*12` | `frmYMF262.cs:253` |
| YMF271 | `nrc.note = searchSSGNote(fns) + (((block+8)&0xF)-11)*12 - 7` | `frmYMF271.cs:179` |
| SSG 通道 | `ftone = masterClock/(64*tp)` → `searchSSGNote` | `frmYM2608.cs:288-310` |

查找表：
- `Common.searchYM2608Adpcm` — `common.cs:425-448`，遍历 96 项 `Tables.pcmMulTbl[i%12+12] * 2^(i/12-3)` 取最近频率，返回 1-based
- `Common.searchSSGNote` — `common.cs:374-390`，遍历 96 项 `Tables.freqTbl`（C4=261.63 的 1/8..×4 共 8 个八度）
- `Common.searchSegaPCMNote` — `common.cs:392-406`，同表但 `2^(i/12-4)`

**这跟 Denjhang 的 `log2` 公式等效**，但用查表避免了浮点运算（C# WinForms 时代的性能考虑）。我们用 `log2` 更简单。

---

## 4. 键盘绘制（`drawBuff.cs:1464-1499`，`KeyBoardOPNM`）

```csharp
public static void KeyBoardOPNM(FrameBuffer screen, int y, ref int ot, int nt, int tp)
{
    if (ot == nt) return;                       // 只在变化时重绘（diff 渲染）
    if (ot >= 0 && ot < 12 * 8) {               // 擦掉旧键
        kx = Tables.kbl[(ot % 12) * 2] + ot / 12 * 28;
        kt = Tables.kbl[(ot % 12) * 2 + 1];
        drawKbn(screen, 33 + kx, y, kt, tp);
    }
    if (nt >= 0 && nt < 12 * 8) {               // 画新键（kt+4 = 按下态精灵）
        kx = Tables.kbl[(nt % 12) * 2] + nt / 12 * 28;
        drawKbn(screen, 33 + kx, y, Tables.kbl[(nt % 12) * 2 + 1] + 4, tp);
    }
    drawFont8(screen, 329, y, 1, Tables.kbn[nt % 12]);   // 音名
    ot = nt;
}
```

- 音域只有 **96 音**（`ot < 12*8`）
- 键位表 `Tables.kbl`（`Tables.cs:196`）：12 项 × {x 偏移, 键型}
- 音名 `Tables.kbn`（197 行）
- **diff 渲染**：`if (ot == nt) return;` —— 只有音符变化才重绘

**这个"只在变化时重绘"的思路对我们的音条渲染有参考价值**（虽然 ImGui 是 immediate mode，不需要 diff，但在 40 分钟 × 8 通道 × 数千音符的规模下，需要做视口裁剪）。

---

## 5. 音量与参数显示

| 项 | 有无 | 位置 |
|---|---|---|
| 音量条（0-19 级） | **有**，且是**每帧递减的假包络**：`nyc.volumeL--; if (<0) =0;` | `frmYM2612.cs:305`、`frmYM3812.cs:184` |
| 音量来源 | CARRIER 最小 TL | `frmYM2612.cs:200-214`、`frmYM2151.cs:228-242` |
| 技能（音色参数） | **有**，每算子 11 字段数组：`AR/DR/SR/RR/SL/TL/KS/ML/DT/AM/SG`；YM2151 额外 `DT2`；`inst[44-47] = AL/FB/AMS/PMS` | `frmYM2612.cs:162-176`、`frmYM2151.cs:198-211`、`MDChipParams.cs:69` |
| 包络曲线图 | **未找到** | — |
| LFO 实时 | 有数值：YM2151 的 `ne/lfrq/pmd/amd/waveform/lfosync` | `frmYM2151.cs:246-252` |
| keyon slot 指示灯 | 有，`DrawBuff.Slot` 画 4 个 slot 的 on/off 方块 | `frmYM2612.cs:359` |

---

## 6. 我们能拿什么

| 拿 | 位置 | 用途 |
|---|---|---|
| **YM2151 key-on 判定与 Denjhang 一致** | `ChipRegister.cs:1252-1270` | 交叉验证（两个独立实现同结论） |
| **KC 半音查表**：`idx<3?idx : idx<7?idx-1 : idx<11?idx-2 : idx-3` | `frmYM2151.cs:218-227` | 与我们一致 |
| **ALG 载波掩码的 7 位编码**：`{0x40,0x40,0x40,0x40,0x50,0x70,0x70,0x78}` | `frmYM2151.cs:228-242` | 与 4 位格式 `{0x8,0xC,0xE,0xF}` 等价 |
| **寄存器十六进制表布局** | `frmRegTest.cs`（593 行） | `vgm_reg_view` 的参考 |
| 每算子 11 字段参数数组（AR/DR/SR/RR/SL/TL/KS/ML/DT/AM/SG + DT2） | `frmYM2151.cs:198-211` | 音色编辑器的字段清单 |
| diff 渲染思路（只在变化时重绘） | `drawBuff.cs:1464` | 大规模音符渲染的优化参考 |

## 7. 不能拿什么

| 不拿 | 原因 |
|---|---|
| GDI+ Bitmap 像素缓冲渲染 | 我们用 ImGui |
| KF 不参与音高 | 这是它的缺陷；Denjhang 的 `+ KF/64` 更正确 |
| 假包络（每帧递减） | 那是没有真实音频数据时的近似；我们有 libvgm 可以算真实 RMS |
| 96 音音域限制 | 我们要画全 MIDI 音域（虽然 YM2151 实际不会超出） |

---

## 8. 结论

**它是"每通道小键盘 + 寄存器参数表"的参考，不是 piano roll。**

最大的价值是**交叉验证**：两个完全独立的实现（C++ ImGui 的 Denjhang 和 C# WinForms 的 MDPlayer）在 YM2151 的 key-on 判定（`0x08` 且 `data & 0x78 != 0`）、KC 半音查表、ALG 载波掩码上给出了**完全一致的答案**。

这意味着我们可以把这几条规则定稿，不再标"待核对"。
