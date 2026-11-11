# furnace — 输出引擎（★ 只链 engine 层）+ 寄存器字典 + .fui 音色格式

| 项 | 值 |
|---|---|
| 仓库 | tildearrow/furnace |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\furnace` |
| 版本 | `dev258-16-gd74e7a01a`（2026-11-10） |
| 许可 | **GPL-2.0+**（开 ASIO 升 GPLv3） |
| 语言 | C++14 |
| 构建 | CMake ≥3.0…3.16 |
| 规模 | 6119 文件 / 464MB / `src` 约 30.1 万行（`src/engine` 19.2 万，81 个 `platform/*.cpp`） |
| 本机可编 | ❌ **配置就失败**（6 个空 submodule） |

**用途重定位（v2）**：不是壳（没有 VGM 导入），但是**输出阶段的唯一正确来源**——它有一套经过实战验证的"芯片写序列 → VGM 字节"机制，以及完整的 `.fui` 音色格式和 FM 音色编辑器。

---

## 1. 它是什么

**多芯片 tracker**（编辑器 + 芯片仿真实时回放 + 导出）。81 个芯片平台，是开源世界里支持芯片最多的 tracker。

---

## 2. 关键结论：**它没有 VGM 导入，只有写出**

### 2.1 证据链

**（a）`src` 下所有文件名含 vgm 的文件，全部是写：**

| 文件 | 行数 | 读写 | 证据 |
|---|---|---|---|
| `src/engine/vgmOps.cpp` | **2340** | **纯写** | `grep 'DivEngine::'` 只有 `performVGMWrite`（L27）和 `SafeWriter* DivEngine::saveVGM(...)`（L566）。写符号 640 处，读符号 7 处（局部用法）。L662 `w->write("Vgm ",4)` 写 magic |
| `src/engine/vgmOps_after_surgery.cpp` | 3295 | **纯写，废弃文件** | 含 12 个冲突标记，`performVGMWrite` 定义两次（L30、L809），来自 merge commit `b17947fdc`。**CMakeLists 完全没引用** |

**（b）`DivEngine::load()` 的唯一入口没有 VGM 分支：**

`src/engine/fileOps/fileOpsCommon.cpp`（182 行）是唯一入口（L22）。magic 分派只有 DMF/FTM/DNM/FUR/FC13-14/TFM/IT/MIDI/S3M/XM/mod/TFE。**没有 `"Vgm "`**。结尾 L178 `lastError = "not a compatible song"`。

**（c）GUI 打开过滤器没有 `*.vgm`：** `src/gui/gui.cpp:2205-2210`。

**（d）`.vgm` 只出现在导出路径：** `gui.cpp:2475`（导出对话框）、`main.cpp:644`（CLI `-O/--vgmout`）、`main.cpp:1071`（调 `saveVGM`）。

**（e）它有个命令流播放器，但只吃自家格式：** `src/engine/cmdStream.cpp`(905) + `cmdStreamOps.cpp`(1981)，是 Furnace 自家 ROM/命令流格式的播放器（`DivCSPlayer`），**不能吃 VGM**。

---

## 3. VGM 写入引擎：为什么它"正确出声"（★ 核心价值）

### 3.1 机制：不是翻译乐谱，是**先自己播放一遍**

`DivEngine::saveVGM`（`vgmOps.cpp:566`）的流程：

| 步骤 | 位置 | 说明 |
|---|---|---|
| 1 | `vgmOps.cpp:1369-1371` | `disCont[i].dispatch->toggleRegisterDump(true)` 打开寄存器转储 |
| 2 | `vgmOps.cpp:1997-2003` | `playSub(false)` 起播 |
| 3 | `vgmOps.cpp:2006/2017` | `while(!done) { ... nextTick(false,true); }` 跑完整个歌曲 |
| 4 | `vgmOps.cpp:2161-2174` | 每 tick 从 `dispatch->getRegisterWrites()` 取写，转 `DivDelayedWrite`，`writes.clear()` |
| 5 | `vgmOps.cpp:2188-2221` | 按 `(time, order)` 排序 → `performVGMWrite` |
| 6 | `vgmOps.cpp:2252-2256` | `toggleRegisterDump(false)` |

```cpp
// vgmOps.cpp:2161-2174
for (int i=0; i<song.systemLen; i++) {
  int curDelay=0;
  std::vector<DivRegWrite>& writes=disCont[i].dispatch->getRegisterWrites();
  for (DivRegWrite& j: writes) {
    if (j.addr==0xfffffffe) {   // 伪地址 = 插入延时
      curDelay+=(double)j.val*(44100.0/(double)disCont[i].dispatch->rate);
      if (curDelay>totalWait) curDelay=totalWait-1;
    } else {
      sortedWrites.push_back(std::pair<int,DivDelayedWrite>(i,DivDelayedWrite(curDelay,writeNum++,j.addr,j.val)));
    }
  }
  writes.clear();
}
```

**结论**：VGM 里的每个字节都来自"芯片 dispatch 在真实播放中被逼着产生的写"。它不翻译乐谱、不猜寄存器——**播放路径就是唯一真相源**。

### 3.2 寄存器写记录链

| 组成 | 位置 | 内容 |
|---|---|---|
| `addWrite` 宏 | `dispatch.h:39` | `regWrites.push_back(DivRegWrite(a,v))` |
| `DivRegWrite` | `dispatch.h:841-873` | `unsigned int addr; unsigned int val;` |
| `DivDelayedWrite` | `dispatch.h:877-897` | `time`、`order`、`DivRegWrite write` |
| `regWrites` | `dispatch.h:1272-1273` | `std::vector<DivRegWrite>` |
| `skipRegisterWrites` / `dumpWrites` | `dispatch.h:1275-1276` | seek 时不写；导出时记录 |
| 访问器 | `dispatch.h:1601-1602` | `getRegisterWrites()` |
| 基类实现 | `platform/abstract.cpp:204-218` | `toggleRegisterDump()` 设 `dumpWrites` |

`addr` 的两层伪地址语义（`dispatch.h:843-866`）：

- `0xffffxx00`–`0xffffxx05`：Furnace 专有命令（起播样本/设采样率/停样本/方向/换 bank/设位置）
- `0xfffffffe`：插入延时（`val` = 芯片周期数）

两条写路径（`platform/fmsharedbase.h:97-108`）：

```cpp
inline void rWrite(unsigned short a, short v) {
  if (!skipRegisterWrites) { pendingWrites[a]=v; }   // 延迟到 tick 末尾去重后 flush
}
inline void immWrite(unsigned int a, unsigned short v) {
  if (!skipRegisterWrites) {
    writes.push_back(QueuedWrite(a,v));
    if (dumpWrites) { addWrite(a,v); }                // 立即进 VGM 记录
  }
}
```

`QueuedWrite`（`platform/fmsharedbase.h:78-86`）带 `addrOrVal` 字段——这是"两端口分两步写"的状态机核心。

### 3.3 YM2151 的 addr/data 两端口（三种核各自实现）

| 核 | 位置 | 代码 |
|---|---|---|
| Nuked OPM | `arcade.cpp:58-83` | `OPM_Write(&fm,0,w.addr)` 然后置 `w.addrOrVal=true`，下一轮 `OPM_Write(&fm,1,w.val)` |
| ymfm | `arcade.cpp:116-129` | `fm_ymfm->write(0x0+((w.addr>>8)<<1), w.addr);` / `fm_ymfm->write(0x1+((w.addr>>8)<<1), w.val);` |
| LLE | `arcade.cpp:156-254` | 通过 `fm_lle.input.a0`（0=地址口，1=数据口）+ `wr/rd/cs` 时序 |

VGM 侧靠同样约定（`vgmOps.cpp:281-285` `case DIV_SYSTEM_YM2151`）：

```cpp
w->writeC(4|baseAddr1); w->writeC(write.addr&0xff); w->writeC(write.val);
```

"端口拆分"交给 VGM 播放器，但记录时靠 `order` 字段保证 addr 在前、val 在后。

### 3.4 初始化与静音序列（没有 `postInit()`）

全仓 `grep postInit` = **0 结果**。初始化在 `DivPlatformArcade::reset()`（`arcade.cpp:1116-1178`）：

```cpp
// arcade.cpp:1172-1177
immWrite(0x01,0x02);  // LFO Off
immWrite(0x18,0x00);  // LFO Freq Off
immWrite(0x19,amDepth);
immWrite(0x19,0x80|pmDepth);   // ← 同一寄存器写两次，bit7 区分 AM/PM
```

**静音序列** `DivPlatformArcade::softReset()`（`arcade.cpp:1105-1114`）：

```cpp
void DivPlatformArcade::softReset() {
  for (int i=0; i<8; i++) {
    immWrite(0xe0+i,0xff); immWrite(0xe8+i,0xff);   // SL/RR
    immWrite(0xf0+i,0xff); immWrite(0xf8+i,0xff);
    immWrite(0x08,i);                                // keyoff 该通道
  }
}
```

**只在导出时调用**（`arcade.cpp:1144-1146`，`dumpWrites` 为真时）。实时播放不走。

OPLL 对应 `DivPlatformOPLL::softReset()`（`opll.cpp:1091-1097`）：清 `0x20/0x30/0x10` 三组（含 keyon 位）。

### 3.5 需要延时的场合

`arcade.cpp:529-549` 硬复位：先把 4 个 op 的 SL/RR 写 `0x0f`（最快释放），然后用伪写插延时（`hardResetCycles=127`，`fmsharedbase.h:50`）：

```cpp
if (mustHardReset) {
  immWrite(0xfffffffe, hardResetCycles-hardResetElapsed);
  for (int i=0; i<8; i++) {
    if ((chan[i].keyOn || chan[i].opMaskChanged) && chan[i].hardReset) {
      // restore SL/RR ...
      immWrite(0x08,(chan[i].opMask<<3)|i);
```

### 3.6 "生搬硬套寄存器翻译"必然失败的 5 个点（★ 我们禁止自己拼字节的依据）

| # | 失败点 | 证据 |
|---|---|---|
| 1 | **必须成对/成序写（addr→data）** | `arcade.cpp:69-77` 的 `addrOrVal` 状态机。只发数据字节 = 芯片无反应 |
| 2 | **同一地址多次写、用 bit7 区分** | `arcade.cpp:1174-1176`（`0x19` AM/PM depth）；`arcade.cpp:323-326`（`0x0f` noise bit7） |
| 3 | **必须插延时** | `arcade.cpp:535` hard reset 需 127 周期；`opll.cpp:57-70` Nuked OPLL 对地址写 `delay=3`、数据写 `delay=21` |
| 4 | **必须先发静音序列** | `arcade.cpp:1105-1114` softReset（RR=0xF 强制释放 + keyoff all）。否则从任意时刻切进来旧包络会余音 |
| 5 | **`rWrite` vs `immWrite` 语义差异** | `rWrite` 只写 `pendingWrites[a]=v`，靠 `tick()` 末尾（`arcade.cpp:476-481`）与 `oldWrites` 比较去重后再 `immWrite`。直接用 `immWrite` 会把中间态也导出，VGM 体积与行为都变 |

### 3.7 `poke()` 如何驱动芯片

`engine.cpp:1501-1513` → `dispatch->poke()`。芯片侧**不是直接碰仿真器状态，而是走正常写队列**：

```cpp
// arcade.cpp:1097-1103
void DivPlatformArcade::poke(unsigned int addr, unsigned short val) { immWrite(addr,val); }
void DivPlatformArcade::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) immWrite(i.addr,i.val);
}
```

---

## 4. `.fui` 音色格式（★ 音色编辑器要用）

**没有 `loadFUI`/`saveFUI` 函数名**——统一走 `DivInstrument::save()` / `readInsData()`，用 `fui` 布尔参数分叉。

| 功能 | 位置 |
|---|---|
| 保存入口（fui=true） | `instrument.cpp:4429-4447` `DivInstrument::save()` → `putInsData2(w,true,...)` |
| 写容器 | `instrument.cpp:1749` `putInsData2`；`:1760-1761` 写 `"FINS"` |
| 读容器 | `instrument.cpp:4316-4345` `readInsData`；`:4330` 识别 `"FINS"` → `readInsDataNew(...,fui=true,...)` |
| 读主体 | `instrument.cpp:3405` `readInsDataNew` |
| FM 特征块 | `instrument.cpp:979` `writeFeatureFM` / `:2347` `readFeatureFM` |
| 声明 | `instrument.h:1183`, `:1257`, `:1260-1261` |

### 4.1 容器结构（二进制，不是 JSON）

```
"FINS" | u16 engineVersion | u8 type | u8 0
  [ "NA" | u16 len | 名字 ]
  [ "FM" | u16 len | FM 数据 ]
  [ "MA" "GB" "SM" "O1".."O4" "LD" "SN" "N1" "FD" "WS" "SL" "WL" "MP" "SU" "ES" "X1" "NE" "EF" "PN" "S2" "S3" "KT" ]
  "EN"   ← 结束（仅 fui，instrument.cpp:2278）
```

特征块机制：`instrument.cpp:1-10` 附近的 `FEATURE_BEGIN(x) ... FEATURE_END`，先占位 2 字节长度，最后回填。

### 4.2 FM 块字段（`writeFeatureFM`，`instrument.cpp:979-1025`，逐字节）

| 字节 | 内容 |
|---|---|
| 0 | `op3.enable<<7 \| op2<<6 \| op1<<5 \| op0<<4 \| opCount`（fui 时 OPLL=2、OPL 按 `fm.ops`） |
| 1 | `(alg&7)<<4 \| (fb&7)` |
| 2 | `(tremLFO<<5) \| (ams&3)<<3 \| (fms&7)` |
| 3 | `(fmsLFO<<7) \| (amsLFO<<6) \| (ops==4)<<5 \| (opllPreset&31)` ← **OPLL preset 编号在这里** |
| 4 | `block&15` |
| 5.. | 每 operator 8 字节：`ksr<<7\|dt<<4\|mult`; `sus<<7\|tl`; `rs<<6\|vib<<5\|ar`; `am<<7\|ksl<<5\|dr`; `egt<<7\|kvs<<5\|d2r`; `sl<<4\|rr`; `dvb<<4\|ssgEnv`; `dam<<5\|dt2<<3\|ws` |

注意：`.fui` 的 `opCount` 由芯片类型截断；`.fur` 内嵌 INS2 一律 4 op。

**实现方式**：直接链接 furnace 的 `instrument.cpp` 调 `readInsData()`，**不自己解析**（`RULES.md` F13）。

---

## 5. 音色编辑器（抄布局，不抄代码）

`src/gui/insEdit.cpp` = **520 行**；`src/gui/insEdit/` 共 74 个文件。

| 文件 | 行数 | 作用 |
|---|---|---|
| `insEdit.cpp` | 520 | 窗口框架、类型下拉、按 type 分发 |
| **`insEdit/insTabFM.cpp`** | **3644** | **所有 FM 芯片共用的主编辑器**（含 OPLL/OPM/OPN/OPL/OPZ/ESFM 分支） |
| `insEdit/insMacroEdit.cpp` | 1283 | 宏编辑 |
| `insEdit/insEditCommon.cpp/.h` | 133 / 112 | 公共控件 |
| `insEdit/opm.cpp` | **84** | `insEditOPM()`：调 `insTabFM(ins)` + 每 op 宏页签 |
| `insEdit/opll.cpp` | **83** | `insEditOPLL()`：同上，2 op |
| `insEdit/opn.cpp` / `opl.cpp` | 78 / 77 | 同类 |
| 其余 ~60 个 | 30–900 | 每芯片一文件 |

### 5.1 组织方式（★ 我们要抄的）

**不是"每个 operator 一个页签"，而是一张宽表**：

- 顶部：`InsProp` 表（编号/名字/Open/Save/Type 下拉），`insEdit.cpp:127-235`
- Type 下拉直接改 `ins->type`（`insEdit.cpp:220-228`），**不做任何参数迁移**
- 按类型分发：`insEdit.cpp:243-345`，`DIV_INS_OPM → insEditOPM`（`:343`）
- 主体 `insTabFM()`（`insTabFM.cpp:1380`）：
  - 子页签 `"FM"`（`:1419`）：算法图 + 全局参数
  - **`FMOperators` 表**，每行一个 operator，列是各项参数（AR/DR/SL/D2R/RR/...），`insTabFM.cpp:1739-1830`，列数 12~20 按芯片类型（`:1741-1746`）
  - 多套布局 `settings.fmLayout`（`insTabFM.cpp:1739, 2345, 3110`）
  - operator 显示顺序 `opOrder[]` 重排（`insTabFM.cpp:1206-1207, 1815`）
  - **OPLL 特殊**：preset 下拉（`insTabFM.cpp:1602-1633`），选预置后**只允许改音量**，`willDisplayOps=false`（`:1692`），用 `OPLL_GetPatchROM()` 拉真实 ROM patch 做预览（`:1695-1727`）

---

## 6. `DivInstrument` 结构（音色内存表示）

`src/engine/instrument.h`：

| 项 | 行 | 内容 |
|---|---|---|
| `DivInstrumentFM` | 173 | `alg, fb, fms, ams, ops, opllPreset, block`；`fmsLFO/amsLFO/tremLFO`；`fixedDrums`；`kickFreq/snareHatFreq/tomTopFreq` |
| `DivInstrumentFM::Operator` | 183-214 | 见下 |
| `op[4]` | 216 | 固定 4 个 operator（OPLL 只用前 2） |
| `DivInstrumentPOD` | 1070-1094 | `type` + `fm` + `std` + `gb` + `c64` + `amiga` + ... |
| `DivInstrument` | 1149 | 继承 POD + `String name` + `temp` + undo/redo + 读写方法 |

Operator 字段（`instrument.h:184-186`）：

```cpp
bool enable;
unsigned char am, ar, dr, mult, rr, sl, tl, dt2, rs, dt, d2r, ssgEnv;
unsigned char dam, dvb, egt, ksl, sus, vib, ws, ksr;   // OPL/OPZ/ESFM 用
unsigned char kvs;
```

**没有 operator 级的"算法数"概念**：`alg` 只是 0-7 的整数；`ops`（2 或 4）在 `DivInstrumentFM` 级别。

---

## 7. YM2413（OPLL）patch 处理

| 内容 | 位置 |
|---|---|
| `platform/opll.cpp` | 1287 行；`opll.h:77` 有 `patchSet` 字段 |
| 用户音色（preset 0）的 8 条寄存器序列 | `opll.cpp:448-468`（`0x00`–`0x07`，两组 2-op 参数） |
| **只允许 1 个用户音色共享 9 通道** | `lastCustomMemory`（`opll.cpp:455, 982`）；文档 `doc/4-instrument/fm-opll.md:14-16` 明说 |
| 15 个预置选择 | `0x30+ch` 高 4 位：`rWrite(0x30+ch, (vol&15)\|(opllPreset<<4))`（`opll.cpp:168, 493, 663, 740-741`） |
| Patch 切换命令 | `DIV_CMD_WAVE`（效果 `0x10xx`）：`opll.cpp:737-743`，`state.opllPreset=c.value&15` |
| Drums 模式（preset 16） | `opll.cpp:468-486`、`:1091`；`0x0e/0x16/0x17/0x18/0x26/0x27/0x28` 序列，**同一寄存器连写两次**（`0x16,0x20` / `0x26,0x05` 各重复）—— VRC7/OPLL 兼容鼓必需的 hack |
| Patch ROM 表 | `extern/Nuked-OPLL/opll.c:114/139/164/189`（ds1001 / ym2413 / ymf281 / ym2423）；结构 `opll.h:63-78` |
| patchSet 选择 | `opll.cpp:1099-1120` `reset()` 按 `patchSet` 调 `OPLL_GetPatchROM(opll_type_*)` |
| 预览时把 ROM patch 展开成内部 4-op 结构 | `gui/insEdit/insTabFM.cpp:1695-1727`；`gui/fmPreview.cpp:195-215` |
| 效果 `0x51` | **不是 patch 命令**。`sysDef.cpp:640` 定义为 `DIV_CMD_FM_SL`（Set sustain level），处理在 `opll.cpp:818-830` |

### 7.1 4-op → 2-op 的对应方式（Furnace 的做法）

**它不做自动映射。** 只有两条合法路径：

- `opllPreset != 0`：丢弃 op 参数，只保留 `op[1].tl` 作为音量，其余从 ROM 表读（`insTabFM.cpp:1692`, `:1719-1727`）
- `opllPreset == 0`：取 `op[0]`（modulator）+ `op[1]`（carrier），按 `opll.cpp:448-468` 逐位打包；`alg` 退化成"SUS 位"（`insTabFM.cpp:1560-1570`），`fb` 保留 3 位，`ops` 强制 2

### 7.2 参数位宽差异（跨芯片音色映射的硬约束）

| 参数 | OPM | OPLL | 处理 |
|---|---|---|---|
| `ar` / `dr` | 5 bit（0-31） | 4 bit（0-15） | 右移 1 |
| `rr` / `sl` | 4 bit | 4 bit | 直接搬 |
| `tl` | 7 bit（0-127） | mod 6bit / car 4bit | 重标定 |
| `ksr` | 双义（KSR / TL-ramp） | 1 bit | 取最高位 |
| `dt2` / `d2r` / `rs` / `ssgEnv` | 有 | **无** | 丢弃 |
| `alg` | 3 bit（0-7） | **1 bit（SUS）** | 按载波数折算 |

---

## 8. 芯片能力表（跨芯片转换的依据）

`src/engine/sysDef.h:118-170` 的 `DivSysDef`：

| 字段 | 内容 |
|---|---|
| `channels / minChans / maxChans` | ✔ 通道数有 |
| `isFM / isSTD / isCompound` | 类别布尔 |
| `vgmVersion` | 该芯片最小 VGM 版本 |
| `getChanDef`（`DivChanDefFunc`） | ✔ 每通道定义 |
| `effectHandlers / postEffectHandlers / preEffectHandlers` | ✔ 间接体现能力 |
| **`operators` / `algorithms`** | ❌ **没有**。写死在 GUI（`insTabFM.cpp:1381-1383`）和导出（`instrument.cpp:982-989, 2160-2170`）里 |

`DivChanDef`（`sysDef.h:72-93`）才携带乐器类型能力：

```cpp
struct DivChanDef {
  String name, shortName;
  int type;
  unsigned int flags;
  DivInstrumentType insType[2];   // 0: primary, 1: alternate
};
```

`DivEngine::getPossibleInsTypes()`（`sysDef.cpp:68-70`）→ `song.possibleInsTypes`（`song.cpp:1013-1015` 汇集）—— **这是"某芯片能用哪些乐器类型"的唯一权威表**。

具体注册：

```cpp
// sysDef.cpp:1091-1102  YM2151
sysDefs[DIV_SYSTEM_YM2151]=new DivSysDef(..., 8, 8, 8, true, false, 0x150, ...,
  DivChanDefFunc(fmChanDef<DIV_CH_FM,DIV_INS_OPM>), ...);

// sysDef.cpp:1257-1265  OPLL
sysDefs[DIV_SYSTEM_OPLL]=new DivSysDef(..., 9, 9, 9, true, false, 0x150, ...,
  DivChanDefFunc(fmChanDef<DIV_CH_FM,DIV_INS_OPLL>), ...);
```

`fmChanDef`（`sysDef.cpp:462-471`）给通道贴 `DIV_INS_OPM` / `DIV_INS_OPLL` 标签。

---

## 9. 寄存器影子（导入阶段的语义参考）

| 机制 | 位置 | 说明 |
|---|---|---|
| 每芯片寄存器池 | `dispatch.h:1407 getRegisterPool()` / `:1413 size` / `:1420 depth` | 68 个平台文件实现 |
| 全局寄存器状态 | `dispatch.h:1123 DivGlobalRegState` | `name / val / type / firstChan / lastChan / combine / bits / bitName[32] / bitIcon[32] / effectiveChan / chanVal` |
| 寄存器视图 | `src/gui/regView.cpp` **98 行** | L59 `getRegisterPool(i,size,depth)`；L63 支持 16bit 深度 |
| 全局寄存器叠层 | `src/gui/pattern.cpp:1937-2074` | 把寄存器值画成通道列上的位条 |
| **YM2151 寄存器名字典** | `src/engine/platform/arcade.cpp:26 regCheatSheetOPM[]` | `:51 getRegisterSheet()` |

---

## 10. 构建（本机失败）

**6 个 git submodule 全空：**

```
extern/SDL  extern/fmt  extern/json  extern/adpcm  extern/YM2414-LLE  extern/asio
```

CMakeLists 无条件引用：`add_subdirectory(extern/fmt)` L250、`extern/json` L500、`extern/adpcm/bs_codec.c` L622-626、`extern/YM2414-LLE/opz_lle.c` L648。

→ **不联网 `git submodule update --init --recursive` 配置直接失败。**

即使拉回子模块，本机 MSYS2 也缺 fmt/portaudio/sndfile/freetype/fftw/vorbis/flac/mpg123/lame/opus 的 pkg-config。

已 vendored：`extern/` 下 portaudio-modified(32)、libsndfile-modified(32)、zlib(57)、fftw(45)、freetype(20)、opus(48)、flac(36)、lame(53)、mpg123(35)、libogg/vorbis 齐全、opn/opl/ESFMu/Nuked-*/vgsound_emu、`extern/imgui_patched`。

---

## 11. 我们要从 furnace 拿的东西

### 11.1 链接进构建（engine 层）

| 内容 | 位置 | 用途 |
|---|---|---|
| `DivDispatch` 基类 + 各芯片 dispatch | `src/engine/platform/*.cpp` | 芯片写入 |
| `DivRegWrite` / `addWrite` / `regWrites` / `dumpWrites` | `dispatch.h:39, 841, 1272-1276` | 寄存器写记录 |
| `DivInstrument` / `DivInstrumentFM` | `instrument.h:173, 1149` | 音色结构 |
| `instrument.cpp` 的 `.fui` 读写 | `:3405` / `:4429` | 读 FUI |
| `sysDef.cpp` 芯片能力表 | `:1091` / `:1257` | 能力查询 |
| `vgmOps.cpp` 的 VGM 编码机制 | `:566` / `:2161-2221` | 写序列 → 字节 |

**代价：GPL-2.0+ 传染。** 见 `RULES.md` 的"许可前提"。

### 11.2 只读参考（不链）

| 内容 | 位置 |
|---|---|
| `.fui` 二进制布局 | `instrument.cpp:979-1025` |
| FM 音色编辑器布局（operator 宽表 + 算法图 + OPLL preset 下拉） | `gui/insEdit/insTabFM.cpp:1739-1830` |
| YM2151 寄存器名字典 | `platform/arcade.cpp:26 regCheatSheetOPM[]` |
| 寄存器十六进制表布局 | `gui/regView.cpp`（98 行） |
| softReset / hardReset 的寄存器序列 | `arcade.cpp:1105-1114` / `:529-549` |

---

## 12. 结论

**不是壳，但是输出阶段的唯一正确来源。**

它最有价值的资产是：**`saveVGM` 那套"先播放一遍再收集寄存器写"的机制**（保证了输出一定合规、一定出声），以及 `.fui` 音色格式和 FM 编辑器布局。

第一阶段（导入 + 显示 + 回放）不涉及 furnace；到步骤 3（输出）才需要它。届时必须接受 GPL-2.0+，或只手工实现 YM2151/YM2413 两个芯片的写序列。
