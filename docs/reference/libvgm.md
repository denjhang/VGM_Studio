# libvgm — 引擎（★ 唯一解析与回放来源）

| 项 | 值 |
|---|---|
| 仓库 | valleybell/libvgm（`https://gh-proxy.org/https://github.com/valleybell/libvgm.git`） |
| 本地路径 | `E:\working\zcode-projects\VGM_Studio\libvgm` |
| 许可 | **无顶层 LICENSE，逐文件标注**：MAME 系 GPL-2.0+、Nuked 系 LGPL-2.1+、大量 BSD-3 |
| 语言 | C90（+扩展）+ C++98 |
| 构建 | CMake ≥3.12 |
| 规模 | 272 文件 / 11MB |
| 本机可编 | ✅ 已出 `libvgm-emu.a` / `libvgm-player.a` / `libvgm-utils.a` |

---

## 1. 它是什么

**VGM 播放器 + 60 多种芯片仿真 + 采样级渲染**。VGM/S98/DRO/GYM 四种格式的播放器，加一个完整的芯片仿真层。

我们的用法：**解析和回放全用它，不自己写解析器、不自己写仿真。**

---

## 2. 对外 API 层次

| 层 | 目录 | 主库 | 主要入口 |
|---|---|---|---|
| 数据加载 | `utils/` | `libvgm-utils.a` | `FileLoader_Init()`, `DataLoader_Load()` |
| 芯片仿真 | `emu/` | `libvgm-emu.a` | `SndEmu_Start2()`, `SndEmu_GetDeviceFunc()` |
| 文件播放 | `player/` | `libvgm-player.a` | `PlayerBase`（抽象）, `VGMPlayer`, `PlayerA`（宿主门面） |
| 音频输出 | `audio/` | `libvgm-audio.a`（可不编） | `Audio_Init()`, `Audio_Output()` |
| 应用示例 | 根目录 | exe | `vgm2wav.cpp`（674 行，最小宿主） |

### 2.1 关键函数签名（带文件:行号）

| 动作 | 签名 | 位置 |
|---|---|---|
| 设输出 | `UINT8 PlayerA::SetOutputSettings(UINT32 smplRate, UINT8 channels, UINT8 smplBits, UINT32 smplBufferLen)` | `player/playera.cpp:152`（`channels` 必须为 2） |
| 加载 | `UINT8 PlayerA::LoadFile(DATA_LOADER* dLoad)` | `player/playera.cpp:386` |
| 加载（引擎级） | `UINT8 VGMPlayer::LoadFile(DATA_LOADER *dataLoader)` | `player/vgmplayer.cpp:248` |
| 渲染 | `UINT32 PlayerA::Render(UINT32 bufSize, void* data)` | `player/playera.cpp:546`（`bufSize` 是**字节数**） |
| 渲染（核心） | `UINT32 VGMPlayer::Render(UINT32 smplCnt, WAVE_32BS* data)` | `player/vgmplayer.cpp:2035` |
| **取位置** | `UINT32 VGMPlayer::GetCurPos(UINT8 unit) const` | `player/vgmplayer.cpp:886` |
| 取时间 | `double PlayerA::GetCurTime(UINT8 flags) const` | `player/playera.cpp:304` |
| Seek | `UINT8 PlayerA::Seek(UINT8 unit, UINT32 pos)` | `player/playera.cpp:472` / `vgmplayer.cpp:1976` |
| 起停复位 | `Start() / Stop() / Reset()` | `player/vgmplayer.cpp:963/975/1009` |
| Tick↔Sample | `Tick2Sample(UINT32)` / `Sample2Tick(UINT32)` | `player/vgmplayer.cpp:860/867` |
| 取头信息 | `const VGM_HEADER* GetFileHeader(void) const` | `player/vgmplayer.hpp:151` |

`GetCurPos` 的 `unit` 取值（`player/playerbase.hpp:18-21`）：

```
PLAYPOS_FILEOFS = 0   // 文件偏移（字节）
PLAYPOS_TICK    = 1   // tick（内部 tick 率）
PLAYPOS_SAMPLE  = 2   // **采样号（渲染采样率）← G3 进度用这个**
PLAYPOS_COMMAND = 3   // 内部命令 ID
```

### 2.2 最小宿主调用链

参考 `vgm2wav.cpp`（674 行，`main()` 在 `:98`）：

```
FileLoader_Init → DataLoader_Load → SetOutputSettings
→ LoadFile → Start → 循环 Render() → Stop → UnloadFile
```

渲染循环在 `vgm2wav.cpp:356-394`：`player.Render(curFrames*((bit_depth/8)*2), packed)`。

---

## 3. 时间模型（采样级，无量化）

`VGMPlayer::ParseFile(UINT32 ticks)`（`player/vgmplayer.cpp:2089`）以 **44100 为基准**累加：

| 命令 | 推进 |
|---|---|
| `0x61 nn nn` | + 字面采样数 |
| `0x62` | + 735（NTSC 1/60 秒） |
| `0x63` | + 882（PAL 1/50 秒） |
| `0x70..0x7F` | + (N-0x6F)，即 1..16 |

再经 `_tsMult / _tsDiv` 换算到输出采样率（`vgmplayer.cpp:834-858`）。

**→ 完全符合我们"不量化"的要求。**

---

## 4. 命令表（我们不重写解析器的依据）

`static const COMMAND_INFO VGMPlayer::_CMD_INFO[0x100]`，定义在 `player/vgmplayer_cmdhandler.cpp:20`。

结构：

```c
struct COMMAND_INFO {
    UINT8 chipType;    // 0xFF = 非芯片命令
    UINT32 cmdLen;     // 命令字节长度
    COMMAND_FUNC func; // 成员函数指针
};
```

关键条目（对我们 YM2151 场景）：

| 命令 | chipType | 长度 | 处理函数 | 含义 |
|---|---|---|---|---|
| `0x54` | 0x03（YM2151） | 3 | `Cmd_Reg8_Data8` | **YM2151 寄存器写** |
| `0xA4` | 0x03 | 3 | `Cmd_Reg8_Data8` | YM2151 第二实例 |
| `0x61` | 0xFF | 3 | `Cmd_DelaySamples2B` | 等 N 采样 |
| `0x62` | 0xFF | 1 | `Cmd_Delay60Hz` | 等 735 |
| `0x63` | 0xFF | 1 | `Cmd_Delay50Hz` | 等 882 |
| `0x70-0x7F` | 0xFF | 1 | `Cmd_DelaySamplesN1` | 等 N+1 |
| `0x66` | 0xFF | 0 | `Cmd_EndOfData` | 结束 |
| `0x67` | 0xFF | 0（特殊） | `Cmd_DataBlock` | 数据块 |

命令处理主循环只有 4 行（`vgmplayer.cpp:2095-2101`）：

```cpp
while(_filePos < _fileHdr.dataEnd && _fileTick <= _playTick && !(_playState & PLAYSTATE_END)) {
    UINT8 curCmd = _fileData[_filePos];
    COMMAND_FUNC func = _CMD_INFO[curCmd].func;
    (this->*func)();
    _filePos += _CMD_INFO[curCmd].cmdLen;
}
```

---

## 5. 寄存器流怎么取（关键难点）

**libvgm 不对外暴露寄存器写流。** 证据：

- 所有 `Cmd_*` 是 `protected` 且**非 virtual**（`player/vgmplayer.hpp:191-276`）
- `_CMD_INFO`（`vgmplayer.hpp:348`）、`_devices` / `CHIP_DEVICE`（`:360`）、`_fileData` / `_filePos` / `_fileTick`（`:363-372`）全是 `protected`
- 公开事件回调只有 `PLAYER_EVENT_CB`，事件仅 `PLREVT_START/STOP/LOOP/END`（`player/playerbase.hpp:25-30`），**不含寄存器数据**

### 三条取法（选一条，不要自己写解析器）

#### 方案 A（推荐）：注入影子核

```cpp
// 1. 自定义一个 DEV_DECL，rwFuncs 里放写回调
//    {RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, my_write}
// 2. Update() 返回静音，不消耗算力
// 3. PlayerBase::SetUserDevices(const DEV_DECL**, UINT8)   // player/playerbase.cpp:79
// 4. 用这个影子核实例化一个 VGMPlayer，专门做导入（不出声）
```

工作机制：`VGMPlayer` 初始化时会把 `chipDev.write8` 指向注册的设备函数（`vgmplayer.cpp:1503-1644` 每个分支 `SndEmu_GetDeviceFunc(..., DEVRW_A8D8, ...)`）。

**坑**：YM 系写入被拆成两次 `write8`（`SendYMCommand`，`vgmplayer_cmdhandler.cpp:638-643`）：

```cpp
cDev->write8(..., (port << 1) | 0, reg);
cDev->write8(..., (port << 1) | 1, data);
```

要按 `(port<<1)|0 = reg`、`(port<<1)|1 = data` 配对还原。

**另一个坑**：`my_write` 里**拿不到时间戳**。需要在外部按 `Render()` 的采样块累加，或用 `GetCurPos(PLAYPOS_SAMPLE)` 在每次 `Render` 后记录窗口。

#### 方案 B：继承复刻

`_fileData/_filePos/_fileTick/_fileHdr/_CMD_INFO/_devices` 全是 `protected`，子类可读。把 `ParseFile()` 的 4 行主循环复制出来自己解码。

**优点**：最直接，一个类搞定。
**缺点**：依赖库内部结构，libvgm 升级会断；`ParseFile` 非 virtual，只能绕过不能 override。

#### 方案 C：抄 `vgmtest.c` 的独立 mini-parser

`vgmtest.c`（1656 行）里有现成的四元组提取器：

| 符号 | 行号 | 作用 |
|---|---|---|
| `SendChipCommand_RegData8(UINT8 chipID, UINT8 chipNum, UINT8 port, UINT8 reg, UINT8 data)` | `:958` | **就是我们要的 `(chip, port, reg, data)` 回调** |
| `DoVgmCommand()` | `:1218` | 命令分派 |
| `VGMSmplPos` | `:188` | 已维护的采样位置 |
| 主体 | `:1084-1510` | 约 500 行 |

**优点**：时间戳天然带，无 GPL 依赖（可只抄这段到我们代码里）。
**缺点**：等于在 libvgm 之外再跑一份解析器，违背"只用一个解析源"。

### 实施建议

导入用 A（影子核 + 静音 VGMPlayer 实例），回放用另一个 VGMPlayer 实例挂真实核。两个实例中间只过 L0 JSON。

若 A 实测接线太绕，退到 C。**只用一个，不要两份同时跑。**

---

## 6. 芯片核与许可

`emu/SoundDevs.h` 列 48 个设备 ID；注册表在 `emu/SoundEmu.c:198` 的 `sndEmu_Devices[]`。

| 许可 | 代表核 |
|---|---|
| **BSD-3-Clause** | `ay8910.c`, `sn76496.c`, `segapcm.c`, `scsp.c`, `x1_010.c`, `ymf271.c`, `ymz280b.c`, `ymfm/*` 等 30+ 个 |
| **GPL-2.0+** | `fmopl.c`, `fmopn.c`, **`ym2151.c`(MAME OPM)**, `ym2413.c`, `nes_apu.c`, `ymf262.c`, `nukedopll.c` |
| **LGPL-2.1+** | `nukedopl3.c`, **`nukedopm.c`(Nuked OPM)**, `ym3438.c` |
| **MIT** | `mikey.c`, `sameboy_apu.c` |

### YM2151 有两个核

`emu/cores/2151intf.c:34` 的 `sndDev_YM2151` 挂两个：

- `devDef_YM2151_MAME`（`EC_YM2151_MAME`，**默认，带 `*`**）→ **GPL-2.0+**
- `devDef_YM2151_Nuked`（`EC_YM2151_NUKED`）→ **LGPL-2.1+**

→ **要宽松许可就显式选 `FCC_NUKE`**（`emu/EmuCores.h`）。

---

## 7. 构建（已实测）

### 7.1 正确的编译命令

```bash
cmake -S libvgm -B libvgm/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_LIBAUDIO=OFF -DBUILD_PLAYER=OFF -DBUILD_VGM2WAV=OFF \
      -DUSE_SANITIZERS=OFF \
      -DSNDEMU__ALL=OFF -DSNDEMU_YM2151_NUKED=ON
```

产物：`libvgm/build/bin/libvgm-{emu,player,utils}.a`

### 7.2 坑（全部实测）

| # | 坑 | 说明 |
|---|---|---|
| 1 | `BUILD_LIBAUDIO=OFF` 但 `BUILD_PLAYER/VGM2WAV=ON` | `player.cpp` / `vgm2wav.cpp` 报 `WavWrt_SetFileName was not declared` 链接失败。**必须关掉这两个应用** |
| 2 | `SNDEMU__ALL` 默认 **ON** | 把 GPL/LGPL 核全编进同一个 `.a`，**整包许可被污染**。要精选就 `-DSNDEMU__ALL=OFF` 再逐个开 |
| 3 | `USE_SANITIZERS` 默认 **ON** | 会给每个 lib 加 sanitizer；嵌入宿主必须 `-DUSE_SANITIZERS=OFF` |
| 4 | 语言标准老 | C90 + 扩展、C++98。`REALLY_NEED_CXX14` 开关存在 |
| 5 | MinGW `LINK_STATIC_LIBS` 默认 ON | 带 `-static-libgcc -static-libstdc++`，与宿主 CRT 冲突时要关 |
| 6 | 链接顺序 | `vgm-player` private 依赖 `vgm-emu` + `vgm-utils`；POSIX 还要 `-lm` |
| 7 | `BUILD_TESTS` 才需要 ZLIB | 只编库不需要 zlib |

### 7.3 许可结论

**自用无碍。对外分发前必须：**
1. 用 `-DSNDEMU__ALL=OFF` 精选核；
2. YM2151 用 Nuked（LGPL-2.1+）而非 MAME（GPL-2.0+）；
3. 逐核核对，列一份清单。

---

## 8. 我们需要的 API（对接清单）

| 我们要做的事 | libvgm API | 位置 |
|---|---|---|
| 导入：拿寄存器流 | 方案 A/B/C（§5） | — |
| 回放：出声 | `PlayerA::Render(bufSizeBytes, void*)` | `playera.cpp:546` |
| 进度：当前采样位置 | `VGMPlayer::GetCurPos(PLAYPOS_SAMPLE)` | `vgmplayer.cpp:886` |
| 总长 | `GetTotalTicks()` / `GetFileHeader()->numTicks` | `vgmplayer.hpp` |
| 循环点 | `GetFileHeader()->loopOfs` / `loopTicks` | `vgmplayer.hpp:151` |
| 跳转 | `Seek(PLAYPOS_SAMPLE, n)` | `playera.cpp:472` |
| 采样率 | `SetOutputSettings(44100, 2, 16, len)` | `playera.cpp:152` |
| 循环通知 | `SetEventCallback`（`PLREVT_LOOP`） | `playerbase.hpp:25-30` |

---

## 9. 结论

**它是唯一的解析与回放来源。** 用户说"解析的方法直接参考内部 VGM 源码"，落点就是这里：`vgmplayer_cmdhandler.cpp` 的 `_CMD_INFO[0x100]` 和 `vgmplayer.cpp` 的 `ParseFile()`。

唯一需要我们自己解决的是"把寄存器写接出来"（§5 三选一）。
