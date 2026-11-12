# `out/` —— 全部生成物，一个版本一个子目录

**规则：每次生成都新建一个版本子目录（`v1/`、`v2/`……），绝不在根下堆放产物。**
这样任意两个版本可以并存对比，不会互相覆盖。

```
out/
├── README.md                    ← 本文件
├── 2151-2413.fur                ← Furnace 工程（在 Furnace 里改音色的入口）
├── ch*_ym2151.fui               ← 当前源侧音色（19 个，正在用的那一份）
├── ch*_ym2413.fui               ← 当前目标侧音色（19 个，改这几个）
├── 00_reference/                ← 与版本无关的参照物
│   ├── 01_original_OPM_YMF2151.wav
│   ├── 03_original_OPM.mid
│   └── 04_converted_OPLL.mid
├── v1/                          ← ① 原芯片导入
├── v2/                          ← ② 首次跨芯片转换（自动匹配）
├── v3/ … v4/                    ← 中间修正
├── v5/                          ← 修完 key/音高/移调/循环/GD3
├── v6/                          ← 首次用 --fur 读音色
├── v7/                          ← 首次用新 .fur（预设 + 宏，不用 0 号槽）
├── v8/                          ← 修 baseVol 极性（音量域 vs 衰减域）
├── v9/                          ← 宏只在真实 key-on 启动一次
├── v10/                         ← TL→nibble 改 round(TL/4)，voff 仍是 1
├── v11/                         ← voff 归零，整曲音量与源对齐
└── v12/                         ← ★ 当前版本：乐器名参数 note:/vol:/noteoff:/link: 生效
```

`.fui` 在根下是**当前工作副本**；每个版本目录里另存一份**当时的快照**。

**v6 起改用 `--fur`**：不再逐个导出/覆盖 19 个 `.fui`（容易搞错文件号），
直接在 Furnace 里改 `2151-2413.fur` 一个工程，`opm2opll --fur` 读它。
实测 `--fur` 与 `--use-edited-fui` 两条路径对同一套音色产出**逐字节相同**。

**v7 起 OPLL 侧完全不用 0 号自定义音色槽**：19 个目标音色全落在 ROM 的
15 个预设上（preset 1–15，0 次使用 preset 0），音色丰富度靠 patch 宏
（音符内切预设）+ 音量宏（拉包络）+ arp 宏（做音高坠落）实现。
实测对 0x00–0x07 的写入次数为 **0**。

**v8 修 `baseVol` 极性**：`0x30` 低 4 位是**衰减量**（0=最响，15=静音），
而 `ExpandMacros` 的 `baseVol` 走 Furnace 的 `chan.vol`（15=最响），两者相反。
v7 直接把衰减值当音量传，等于二次反向衰减，多数通道掉到几乎无声。改为
`mi.baseVol = 15 - vol` 后平均寄存器音量从 ~12 回到 3.5–6.6。

**v9 修宏相位**：宏只在**真实 key-on** 启动一次。`NoteSplitter` 因 KC 跳变切出的
legato 块在源里没有 key 事件，此前每块都重新 `Init` 一遍宏，导致 `vol` 宏重吐
第一帧（很多音色第一帧是 0 → 块边界静音 16.7 ms）、`arp` 宏重扫（听感是多余的
颤音/弯音）。v9 用 `skipTicks` 把相位推进到正确位置，整曲 legato 块边界的静音帧
从 371 降到 1，旋律通道的音高轨迹与 v5 逐写入一致。

**v11 修音量台阶比例**：源 `0x60` 载波 TL 每单位 0.7526 dB，目标 `0x30` 低 4 位
每步 3.01 dB，比值**正好 4**（`ym2151.c:1143` 的 `<<3` 与 `ym2413.c:1695` 的
`<<2<<1` 都把值移进同一个 10 bit 包络域）。旧代码用 vgm-conv 给 OPN 的 `tl >> 3`，
把 9.8 dB 的源动态范围压成 2 步。改成 `round(TL / 4)` 并把 `volumeOffset`
基准从 1 改回 0 后，整曲 RMS 从 −16.4 → −19.0 dBFS，与源 `01_original` 的
−19.0 dBFS 对齐。宏音量仍按 `VOL_SCALE_LOG` 与基础音量做 dB 域相加（比例乘法）。

## 各版本做了什么

| 版本 | 内容 | 已知问题 |
|---|---|---|
| `v1/` | `vgm2json --fui`：原 OPM JSON + 19 个 `ym2151.fui` | — |
| `v2/` | 首次 `opm2opll --fui`，用户把 ch1/ch2 的音色号改成 5/12/7 | **音符被切开、颤音被量化成半音阶** |
| `v3/` | 修 key 事件（按源重放，不再每块补 key-on/off） | 未降八度 |
| `v4/` | 修音高轨迹（逐点跟随 KC/KF） | 未降八度 |
| `v5/` | **v3+v4 + `--transpose=-12` + 循环点 + GD3** | — |
| `v6/` | 改用 `--fur=2151-2413.fur` 读全部音色（取代逐个 `.fui`） | 音色尚未重新设计 |
| `v7/` | 新 `.fur`：19 个目标音色改用预设 + 宏，完全不用 0 号槽 | **多通道音量极小（baseVol 极性反了）** |
| `v8/` | 修 `baseVol = 15 - vol`，音量回到源的水平 | **块边界静音一拍、arp 重扫（宏被重启动）** |
| `v9/` | 宏只在真实 key-on 启动一次（`skipTicks` + `releaseAtEnd`） | 音量用 `tl>>3`，动态范围被压扁 |
| `v10/` | TL→nibble 改成 `round(TL/4)` | `volumeOffset` 仍是 1，整曲偏轻 3 dB |
| `v11/` | `volumeOffset` 基准改回 0，整曲 RMS 与源对齐 | — |
| `v12/` | **乐器名参数**：`note:` 锁音高、`vol:` 锁音量、`noteoff:self` 宏播完才 off、`link:` 联动第 9 通道 | 当前版本 |

## 完整重跑（写进新版本目录）

```bash
cd E:/working/zcode-projects/VGM_Studio
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"
NEW=out/v12     # ← 每次换新名字，不要复用
mkdir -p "$NEW"

# ① 原芯片 VGM → JSON + 源侧音色
$B/vgm2json.exe "$V" -o "$NEW/song.json" --fui

# ② 在 Furnace 里改 out/2151-2413.fur（一次改完 19 个音色存盘）

# ③ JSON → 目标芯片 JSON + VGM（音色来自 .fur，含循环点与 GD3）
$B/opm2opll.exe out/v1/song.json -o "$NEW/song.ym2413.json" \
    --fur=out/2151-2413.fur --transpose=-12 \
    --vgm="$NEW/song.ym2413.vgm"

# ④ 试听
libvgm/build/bin/vgm2wav.exe --loops 1 --fade 3 \
    "$NEW/song.ym2413.vgm" "$NEW/02_converted_OPLL_YM2413.wav"
```

首次导入工程时才需要把 19 个 `.fui` 拖进 Furnace；之后只改 `.fur` 一个文件。

## 关键数据（`31 Grass Maze`，源 YM2151 @4MHz → YM2413 @3.579545MHz）

| 项 | 源 | v5 | v6（`.fur`） | v7（新 `.fur`） |
|---|---|---|---|---|
| 音符块数 | 327（ch1） | 327 | 327 | 327 |
| 真实 key-on 边沿 | 2107 | 2107 | 2107 | **2107** |
| 真实 key-off 边沿 | 2104 | 2104 | 2104 | **2104** |
| 音符内音高写入 | 483 次（ch1 的 KF） | 1513 次 | 1513 次 | 1513 次 |
| 循环起点 | 采样 12582 | 12582 | 12582 | **12582** |
| 循环长度 | 2393734 | 2393734 | 2393734 | **2393734** |
| GD3 | 268 字节 | 逐字节相同 | 逐字节相同 | **逐字节相同** |
| 0x00–0x07 用户槽写入 | — | 0 | 0 | **0** |
| 用到的 ROM 预设 | — | 8 个 | 8 个 | **15 个（1–15 全用到，preset 0 零写入）** |
| 音符内切 preset 的音符 | — | 0 | 0 | **1737** |
| `link:` 联动出的音符（FM9） | — | 0 | 0 | **54（= ch5ins3 的音符数）** |
| `noteoff:self` 音符重叠 | — | — | — | **0** |
| legato 块边界静音帧 | — | 0 | 0 | **371 → v9 降到 1** |
| 源动态范围（载波 TL） | 9.8 dB | — | — | 9.6 dB（v11，旧映射 5.9） |
| 整曲 RMS | −19.0 dBFS | — | — | −19.0（v11）/ −16.6（v12，多一层联动） |
| VGM 字节数 | — | 51224 | 63832 | v9 141023 / v11 143339 / **v12 205157** |

## 四条必须记住的规则

1. **循环点是「起点」，不是「长度」**。VGM 头 `0x20` 存的是循环段长度，
   `loopSample = totalSamples − loopTicks`。直接拿长度当位置存，会得到一个
   贴着曲末的假循环点。
2. **key 事件与音高轨迹必须分开处理**。`NoteSplitter` 在 KC 跳变 >0.5 半音时
   切新块，这些块是 legato 延续（源里没有 `0x08` 写），不能补 key-off/key-on；
   而音符内部的 `0x28`/`0x30`（KC/KF）必须逐点跟随，否则颤音弯音全被压成固定音高。
3. **宏只在真实 key-on 启动一次**。legato 块必须用 `skipTicks` 接续相位，
   否则 `vol` 宏重吐第一帧（很多音色第一帧是 0 → 块边界静音一拍）、
   `arp` 宏重扫（听起来像多余的颤音）。见 [VOICE-FUI.md §8.5.4](../docs/VOICE-FUI.md)。
4. **音量台阶是 4:1，不是 8:1**。源载波 TL 0.7526 dB/单位，目标 `0x30`
   3.01 dB/步，`nibble = round(TL/4)`；配套的 `volumeOffset` 基准是 0。
   见 [VOICE-MAPPING.md §5.1](../docs/VOICE-MAPPING.md)。

## 响度测量脚本（可重跑）

| 脚本 | 作用 |
|---|---|
| `cli/tools/probe_volume.py` | 造最小 VGM 扫两颗芯片的音量台阶，输出 dB/单位表 |
| `cli/tools/probe_channels.py` | 把源/目标的每个通道单独抽成 VGM 渲染，逐通道比 RMS |
