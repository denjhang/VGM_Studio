# 产物目录（`out/`）

**所有生成物一律落在这里，不再散落到 `listen/`。** 一条命令一条链，可反复重跑覆盖。

`listen/` 只保留历史上那 4 个已入库的成品文件（2 个 MIDI + 1 个 VGM），
不再新增内容；新工作全部用 `out/`。

## 目录内容

| 文件 | 是什么 |
|---|---|
| `ch*ins*_ym2151.fui` | 源侧音色（19 个，来自 YM2151） |
| `ch*ins*_ym2413.fui` | 目标侧音色（19 个，ROM 匹配结果），**改这个** |
| `song.json` | ① 原芯片结构化 JSON（YM2151） |
| `song.ym2413.json` | ② 目标芯片结构化 JSON（YM2413） |
| `song.ym2413.vgm` | ③ 目标 VGM（用于试听/校验） |
| `01_original_OPM_YMF2151.wav` | 原曲渲染，YM2151 直接播原 VGM |
| `02_converted_OPLL_YM2413.wav` | 转换后渲染，YM2413 播 ③ |
| `03_original_OPM.mid` | 原曲 JSON → MIDI（纯音高验证） |
| `04_converted_OPLL.mid` | 目标 JSON → MIDI（纯音高验证） |

`.wav` 每个约 10 MB，已被 `.gitignore` 的 `out/` 规则整体排除，不进仓库。

## 完整重跑（从零到全部产物）

```bash
cd E:/working/zcode-projects/VGM_Studio
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"
mkdir -p out

# ① 原芯片 VGM → JSON + 源侧音色
$B/vgm2json.exe "$V" -o out/song.json --fui

# ② 原芯片 JSON → 目标芯片 JSON + 目标侧音色（19 个 ym2413.fui）
$B/opm2opll.exe out/song.json -o out/song.ym2413.json --fui

# ③ 用改过的音色重新生成目标 JSON + 目标 VGM
#    （没改过 .fui 也可以照样加 --use-edited-fui，等于原样读回）
$B/opm2opll.exe out/song.json -o out/song.ym2413.json \
    --fui --use-edited-fui --vgm=out/song.ym2413.vgm

# ④ 两条 MIDI（纯音高验证）
$B/json2mid.exe out/song.json        -o out/03_original_OPM.mid
$B/json2mid.exe out/song.ym2413.json -o out/04_converted_OPLL.mid

# ⑤ 两个 WAV
libvgm/build/bin/vgm2wav.exe --loops 1 --fade 3 "$V" out/01_original_OPM_YMF2151.wav
libvgm/build/bin/vgm2wav.exe --loops 1 --fade 3 out/song.ym2413.vgm out/02_converted_OPLL_YM2413.wav
```

## 改音色的工作流

```bash
# 看一眼某个目标音色（含宏，一眼就能确认 patch/vol/phaseReset 是否存在）
$B/fuitool.exe dump out/ch1ins1_ym2413.fui --tick-rate=60

# 在 Furnace 里改完 .fui，再用它重新生成（③ 那条命令原样重跑即可）
```

宏速率不写在 `.fui` 里，它来自 `.fur` 的 `song.hz`，所以 `--tick-rate` 必须显式给，
且要和转换时用的值一致（默认 60）。

## 已知的量化结果

2500+ 个音符配对，**91.3% 音高完全相同**，其余 8.7% 差 2~4 个半音，全部落在
ch4（源载波 ML=10，OPLL 无法表达非 2 次幂倍频）。听感上是该声部个别音略有偏差，
不是整体走调。详见 `docs/PITCH-CONVERSION.md`。
