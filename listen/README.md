# 试听文件

全部由 `31 Grass Maze.vgm`（Spindizzy II / Sharp X68000）生成，同一首曲子。

| 文件 | 是什么 | 怎么来的 |
|---|---|---|
| [`01_original_OPM_YMF2151.wav`](01_original_OPM_YMF2151.wav) | **原曲**：YM2151 直接播放原 VGM | `vgm2wav "31 Grass Maze.vgm"` |
| [`02_converted_OPLL_YM2413.wav`](02_converted_OPLL_YM2413.wav) | **转换后**：YM2413 播放转换出的 VGM | `opm2opll --vgm=dst.vgm` → `vgm2wav` |
| [`02_converted_OPLL_YM2413.vgm`](02_converted_OPLL_YM2413.vgm) | 转换后的 VGM 本身 | `opm2opll song.json -o song.ym2413.json --vgm=dst.vgm` |
| [`03_original_OPM.mid`](03_original_OPM.mid) | 原曲 JSON 转的标准 MIDI | `json2mid song.json -o src.mid` |
| [`04_converted_OPLL.mid`](04_converted_OPLL.mid) | 目标 JSON 转的标准 MIDI | `json2mid song.ym2413.json -o dst.mid` |

## 怎么听

直接双击 `.wav` / `.mid` 即可（Windows 媒体播放器 / foobar / 任意 MIDI 播放器）。

**对比听什么**：

1. **先听 01 和 02** —— 这是最直接的判断。应该能听出旋律一致（音高正确），差别只在音色质感（OPLL 只有 2 个 operator，OPM 有 4 个）。
2. **再听 03 和 04** —— 这是纯音高验证，没有音色干扰。两条旋律应该完全对齐。
   - 如果两条听不出差别 → 音高正确。
   - 如果 04 整体比 03 高或低一个固定音程 → 是八度基准问题，用 `--octave-offset` 调。
   - 如果 04 有零星的音飘了 → 是 OPLL 无法表达该音色的载波倍频（ML=10 之类），已知限制。

## 已知的量化结果

2500+ 个音符配对，**91.3% 的音高完全相同**，其余 8.7% 差 2~4 个半音，全部落在 ch4（源载波 ML=10，OPLL 没有这个倍频）。
听感上表现为该声部的个别音略有偏差，不是整体走调。

## 重新生成

```bash
cd E:/working/zcode-projects/VGM_Studio
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"

# 1) 原曲
libvgm/build/bin/vgm2wav.exe --loops 1 --fade 3 "$V" listen/01_original_OPM_YMF2151.wav

# 2) 转换链
$B/vgm2json.exe "$V" -o listen/song.json
$B/opm2opll.exe listen/song.json -o listen/song.ym2413.json --vgm=listen/02_converted_OPLL_YM2413.vgm
$B/json2mid.exe listen/song.json -o listen/03_original_OPM.mid
$B/json2mid.exe listen/song.ym2413.json -o listen/04_converted_OPLL.mid

# 3) 转换后的 WAV
libvgm/build/bin/vgm2wav.exe --loops 1 --fade 3 listen/02_converted_OPLL_YM2413.vgm listen/02_converted_OPLL_YM2413.wav
```
