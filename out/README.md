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
└── v5/                          ← ★ 当前正确版本
```

`.fui` 在根下是**当前工作副本**（在 Furnace 里改的就是这几个）；
每个版本目录里另存一份**当时的快照**，便于回溯。改完 `.fui` 重新生成时，
把根下的 `.fui` 拷进新的版本目录即可（见下）。

## 各版本做了什么

| 版本 | 内容 | 已知问题 |
|---|---|---|
| `v1/` | `vgm2json --fui`：原 OPM JSON + 19 个 `ym2151.fui` | — |
| `v2/` | 首次 `opm2opll --fui`，用户把 ch1/ch2 的音色号改成 5/12/7 | **音符被切开、颤音被量化成半音阶** |
| `v3/` | 修 key 事件（按源重放，不再每块补 key-on/off） | 未降八度 |
| `v4/` | 修音高轨迹（逐点跟随 KC/KF） | 未降八度 |
| `v5/` | **v3+v4 + `--transpose=-12` + 循环点 + GD3** | 当前版本 |

## 完整重跑（写进新版本目录）

```bash
cd E:/working/zcode-projects/VGM_Studio
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"
NEW=out/v6      # ← 每次换新名字，不要复用
mkdir -p "$NEW"

# ① 原芯片 VGM → JSON + 源侧音色
$B/vgm2json.exe "$V" -o "$NEW/song.json" --fui

# ② 原芯片 JSON → 目标芯片 JSON + 目标侧音色
$B/opm2opll.exe "$NEW/song.json" -o "$NEW/song.ym2413.json" \
    --fui-dir="$NEW" --tick-rate=60

# ③ 在 Furnace 里改 "$NEW"/ch*_ym2413.fui，然后重生成（含循环点与 GD3）
$B/opm2opll.exe "$NEW/song.json" -o "$NEW/song.ym2413.v2.json" \
    --fui-dir="$NEW" --use-edited-fui --tick-rate=60 \
    --vgm="$NEW/song.ym2413.v2.vgm"

# ④ 试听
libvgm/build/bin/vgm2wav.exe --loops 1 --fade 3 \
    "$NEW/song.ym2413.v2.vgm" "$NEW/02_converted_OPLL_YM2413.wav"
```

## 关键数据（`31 Grass Maze`，源 YM2151 @4MHz → YM2413 @3.579545MHz）

| 项 | 源 | v5 |
|---|---|---|
| 音符块数 | 327（ch1） | 327 |
| 真实 key-on 边沿 | 2107 | **2107** |
| 真实 key-off 边沿 | 2104 | **2104** |
| 音符内音高写入 | 483 次（ch1 的 KF） | 1513 次（全曲 fnum） |
| 循环起点 | 采样 12582 | 采样 12582 |
| 循环长度 | 2393734 | 2393734 |
| GD3 | 268 字节 | **逐字节相同** |

## 两条必须记住的规则

1. **循环点是「起点」，不是「长度」**。VGM 头 `0x20` 存的是循环段长度，
   `loopSample = totalSamples − loopTicks`。直接拿长度当位置存，会得到一个
   贴着曲末的假循环点。
2. **key 事件与音高轨迹必须分开处理**。`NoteSplitter` 在 KC 跳变 >0.5 半音时
   切新块，这些块是 legato 延续（源里没有 `0x08` 写），不能补 key-off/key-on；
   而音符内部的 `0x28`/`0x30`（KC/KF）必须逐点跟随，否则颤音弯音全被压成固定音高。
