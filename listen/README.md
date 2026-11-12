# 试听文件（存档）

> **新工作一律用 [`out/`](../out/README.md)。** 本目录只保留下面这几个已入库的历史成品，
> 不再新增内容；生成物统一落到 `out/`，避免两处并存、命令对不上。

全部由 `31 Grass Maze.vgm`（Spindizzy II / Sharp X68000）生成，同一首曲子。

| 文件 | 是什么 | 怎么来的 |
|---|---|---|
| `01_original_OPM_YMF2151.wav` | **原曲**：YM2151 直接播放原 VGM | `vgm2wav "31 Grass Maze.vgm"` |
| `02_converted_OPLL_YM2413.wav` | **转换后**：YM2413 播放转换出的 VGM | `opm2opll --vgm=dst.vgm` → `vgm2wav` |
| [`02_converted_OPLL_YM2413.vgm`](02_converted_OPLL_YM2413.vgm) | 转换后的 VGM 本身 | `opm2opll song.json -o song.ym2413.json --vgm=dst.vgm` |
| [`03_original_OPM.mid`](03_original_OPM.mid) | 原曲 JSON 转的标准 MIDI | `json2mid song.json -o src.mid` |
| [`04_converted_OPLL.mid`](04_converted_OPLL.mid) | 目标 JSON 转的标准 MIDI | `json2mid song.ym2413.json -o dst.mid` |

> 两个 `.wav` 每个约 10 MB，**不进仓库**（`.gitignore` 里排除了 `listen/*.wav`）。
> 克隆后按下面的命令重新生成即可，一条命令几秒钟。

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

**不要在这里重新生成。** 上表只是历史存档，重新生成一律写 `out/`，
完整命令见 [`out/README.md`](../out/README.md)。
