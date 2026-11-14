# `out/` —— 生成物，按 **游戏 / 曲目** 分类

**规则：`out/<游戏名>/<曲目名>/`。** 每首曲子一个独立目录，互不覆盖；
同一首曲子的所有调参版本、探针、参照物都收在它自己目录下。

```
out/
├── README.md                      ← 本文件
└── Spindizzy_II/                  ← 游戏（Sharp X68000）
    ├── 01 Arsys Logo/
    │   ├── song.ym2151.vgm        ← ⓪ 原版 YM2151 VGM（对比用）
    │   ├── song.json              ← ① 原芯片 JSON（YM2151）
    │   ├── song.ym2413.json       ← ② 目标芯片 JSON
    │   ├── song.ym2413.vgm        ← ③ 目标芯片 VGM（试听用这个）
    │   ├── fui/                   ← ④ 本曲全部音色（chXinsY_ym2151 / _ym2413）
    │   │                             ym2413 那份带 opllPreset 与全部宏，
    │   │                             直接拖进 Furnace 就能调
    │   ├── import.log
    │   └── convert.log            ← 含音色库命中/未命中明细
    ├── 02 Zoom Up/
    ├── …
    ├── 33 Oily Way/               ← 第二首在调音色的曲子（自带 Oily Way ins.fur）
    │   ├── song.ym2151.vgm / song.json / song.ym2413.json / song.ym2413.vgm
    │   ├── Oily Way ins.fur       ← 这首自己的 Furnace 工程（33 个乐器）
    │   ├── fui/                   ← 转换时导出的目标音色
    │   ├── ins/                   ← 手工试过的音色 + from_fur/（工程解包）
    │   └── versions/              ← v1 … v5（v5 = 当前 song.ym2413.json）
    ├── 41 Eternal Move/
    └── 31 Grass Maze/             ← ★ 主力调试曲（版本最多，音色都是为它调的）
        ├── song.ym2151.vgm         ← 原版（对比）
        ├── song.json / song.ym2413.json / song.ym2413.vgm
        ├── import.log / convert.log
        ├── 2151-2413.fur          ← Furnace 工程（改音色的唯一入口）
        ├── fui/                   ← ch*_ym2151.fui / ch*_ym2413.fui（38 个）
        │   └── from_fur/          ← 从 2151-2413.fur 解包的 20 个 YM2413 音色，
        │                            文件名就是 Furnace 里的乐器全名（含 note:/vol: 参数）
        ├── versions/              ← v1 … v18，每次调参的快照
        │   ├── v1/  ← ① 原芯片导入
        │   ├── v2/  ← ② 首次跨芯片转换（自动匹配）
        │   ├── v5/  ← 修完 key/音高/移调/循环/GD3
        │   ├── v6/  ← 首次用 --fur 读音色
        │   ├── v7/  ← 首次用新 .fur（预设 + 宏，不用 0 号槽）
        │   ├── v8/  ← 修 baseVol 极性（音量域 vs 衰减域）
        │   ├── v9/  ← 宏只在真实 key-on 启动一次
        │   ├── v10/ ← TL→nibble 改 round(TL/4)
        │   ├── v11/ ← voff 归零，整曲音量与源对齐
        │   ├── v12/ ← 乐器名参数 note:/vol:/noteoff:/link: 生效
        │   ├── v13/ ← link 目标可用 note:，鼓组 vol 调成 13
        │   ├── v14/ ← 鼓组/贝斯微调
        │   ├── v15/ ← 新增 noteoff:N（鼓按 tick 精确收声）
        │   ├── v16/ ← ch6 贝斯音量抬起；FM9 联动层换音色 + note:B3
        │   ├── v17/ ← FM9 vol 13→14
        │   └── v18/ ← ch4 贝斯 vol 宏 12→14
        ├── probes/                ← 音量/通道测量产物（chanprobe, volprobe, ins）
        └── reference/             ← 与版本无关的参照物（OPM/OPLL 的 mid）
```

**为什么 31 Grass Maze 单独有一套**：它是唯一逐版调过的曲子——`.fur` 里的
19 个目标音色、所有宏、`note:/vol:/noteoff:` 参数都是对着它调的。
其余 40 首是批量转换产物，直接复用 `docs/instrument-library.json`。

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
| `v12/` | **乐器名参数**：`note:` 锁音高、`vol:` 锁音量、`noteoff:self` 宏播完才 off、`link:` 联动第 9 通道 | link 目标不能自己有 `note:` |
| `v13/` | `link` 目标支持 `note:X`；鼓组 `vol:15`→`13` | — |
| `v14/` | 鼓组 `vol:15`/`14`，贝斯加 `vol:[12]` + `arp:[-12]` | — |
| `v15/` | 新增 **`noteoff:N`**：宏只走 N 步就 off（BD `6`、SD `9`），鼓按 tick 精确收声 | — |
| `v16/` | FM9 联动层换成 SD2（`preset 14` / `note:B3`）；ch6 贝斯音量抬起 | — |
| `v17/` | FM9 `vol:13`→`14` | — |
| `v18/` | ch4 贝斯 vol 宏 `12`→`14` | 单曲调试的最后一版 |
| `Spindizzy_II/` | **整张专辑批量转换**：41 首，`--library` 跨曲目音色库 | 当前成果 |

### 33 Oily Way 的版本

| 版本 | 内容 | 印象 |
|---|---|---|
| `v1/` | 首次 `opm2opll --fur`（早期 `.fur`，音色还没调） | — |
| `v2/` | fur 换音色 | — |
| `v3/` | fur 再调 | — |
| `v4/` | 方波加 detune、降音量、电吉他改低失真、鼓降音量 | ch6 宏状态机 bug 已修 |
| `v5/` | 当前 `.fur`（2026-11-14 13:40）：ch3/ch6 电吉他换 preset 10、SD2 去掉 vol 参数 | **当前 `song.ym2413.json`** |

v5 相对 v4 的寄存器变化（同一个 `song.json`，只换 `.fur`）：

| 通道 | `0x30` 写入 | 变化 |
|---|---|---|
| ch3 | 2937 → 1356 | 电吉他改用 preset 8/10（v4 是 11/4/15 三种来回切）；音量统一 nibble 5，不再有 6/7 的零碎台阶 |
| ch6 | 950 → 950 | preset 15/4 → 10/5（两帧宏 `[5,10]`，低失真电吉他） |
| ch5 | 4020 → 4020 | 鼓的 preset 一样，但 `ch5ins3-SD2` 去掉了 `vol:13`，音量参数改由 `TL` 决定 |
| 其余 | — | 逐条相同 |

总写入 41914 → 40333（少 1581 条），VGM 171321 → 165442 字节。

## 批量转换结果（`out/Spindizzy_II/`）

| 项 | 值 |
|---|---|
| 曲目数 | **41**（`01 Arsys Logo` … `41 Eternal Move`） |
| 音符总数 | **110 180** |
| 音色库精确命中 | 19 个（来自 `31 Grass Maze` 的 19 个源音色） |
| 音色库"最近"命中 | 41 个（距离 ≤ 48） |
| 未命中（退回 ROM 自动匹配） | 748 个 |
| VGM 总字节 | 3 181 853 |

**为什么大部分曲目没能复用音色**：库目前只有 `31 Grass Maze` 一首的 19 套音色。
全专辑实测有 **363 种不同的 YM2151 音色**，其中 354 种只出现在一首曲子里——
每首曲子的配器参数都不完全一样。精确匹配只对「同一套音色原样出现」生效，
所以只有 31 Grass Maze 自己全部命中。

要把复用率提上去，正确做法是**逐首人工确认音色**：在 Furnace 里把每首都调一遍，
把 `2151-2413.fur` 扩成多曲目工程（或把每首的目标音色都并进库），
之后相似度匹配才有素材。批量转换已经把这 41 首的结构化 JSON/VGM 都产出了，
人工调整只需要改音色。

## 完整重跑：整张专辑批量转换

```bash
cd E:/working/zcode-projects/VGM_Studio
# 一次跑完 Spindizzy II 全部 41 首，每首一个独立目录
cli/tools/batch_convert.sh
#   → out/Spindizzy_II/<曲名>/
#       song.ym2151.vgm   原版（直接复制，方便 A/B）
#       song.json         源 JSON
#       song.ym2413.json  目标 JSON
#       song.ym2413.vgm   目标 VGM
#       fui/              该曲全部 .fui（ym2413 那份可直接改进 Furnace）
#       import.log / convert.log
```

音色来源优先级（`opm2opll` 内部）：
1. **`--library=docs/instrument-library.json`** —— 跨曲目乐器库，按 **YM2151 的
   FM 参数**匹配（不是指纹；指纹只用于同曲目内去重）
2. `--fur` 本曲工程按 `chXinsY` 名字匹配
3. ROM 自动匹配

单曲调试用（以 31 Grass Maze 为例）：

```bash
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"
D="out/Spindizzy_II/31 Grass Maze"
FUR="$D/2151-2413.fur"

cp -f "$V" "$D/song.ym2151.vgm"                      # ⓪ 原版，对比用
$B/vgm2json.exe "$V" -o "$D/song.json" --fui         # ① 源
# ② 在 Furnace 里改 "$FUR"
NEW="$D/versions/v19"; mkdir -p "$NEW"                # ③ 新版本目录
$B/opm2opll.exe "$D/song.json" -o "$NEW/song.ym2413.json" \
    --library=docs/instrument-library.json --fur="$FUR" \
    --transpose=-12 --vgm="$NEW/song.ym2413.vgm"      # ④ 目标

# 试听：直接用播放器打开 $NEW/song.ym2413.vgm（不再生成 wav）
```

从 `.fur` 解包成单独 `.fui`（文件名 = Furnace 里的乐器全名）：

```bash
# 只解 YM2413（OPLL），文件名保留 note:/vol:/noteoff: 参数（Windows 不允许 ':'，写成 '_'）
cli/build/fuitool.exe unpack "$FUR" "$D/fui/from_fur" --type=13

# 只要 chNinsM 前缀（opm2opll 认的那种名字）就加 --tagged
cli/build/fuitool.exe unpack "$FUR" "$D/fui/from_fur_tagged" --type=13 --tagged
```

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
| `link` 目标的 `note:` 覆盖 | — | — | — | v15 全落 C8；**v16 起改 `note:B3` → block 3 / fnum 325** |
| `noteoff:self` 音符重叠 | — | — | — | **0** |
| `noteoff:N` 音符长度 | — | — | — | **BD 6.00 tick × 207 个、SD 9.00 tick × 48 个（按 tick 精确落地）** |
| legato 块边界静音帧 | — | 0 | 0 | **371 → v9 降到 1** |
| 源动态范围（载波 TL） | 9.8 dB | — | — | 9.6 dB（v11，旧映射 5.9） |
| 整曲 RMS | −19.0 dBFS | — | — | v11 −19.0 / v12 −16.6 / v13 −18.3 / v14 −18.8 / v17 −19.7 / **v18 −18.9** |
| VGM 字节数 | — | 51224 | 63832 | v12 205157 / v13 158937 / v14 158521 / v17 171019 / **v18 171019** |

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
