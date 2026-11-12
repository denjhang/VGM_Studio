# VGM Studio

把 VGM 当**结构化乐谱**来编辑，而不是当二进制去转换。

**目标流水线**（三步，全部是控制台 CLI，每一步都能用文本工具检查）：

```
原芯片 VGM ──vgm2json──> 原芯片 JSON ──opm2opll──> 目标芯片 JSON ──json2vgm──> 目标 VGM
                              │                          │
                          json2mid                   json2mid
                              ↓                          ↓
                         原曲 MIDI  ←───── 逐音符对比 ─────→  目标 MIDI
```

本仓库当前完成的是 **YM2151(OPM) → YM2413(OPLL)** 这一条链，以及用 MIDI 做音高验证的完整闭环。

---

## 目录

| 路径 | 内容 |
|---|---|
| [`cli/`](cli/) | **本项目自己的代码**：导入、结构化、跨芯片转换、输出、音高验证 |
| [`docs/`](docs/) | 设计文档（每个主题一个文件，见 [`docs/README.md`](docs/README.md)） |
| [`out/`](out/) | **全部生成物**，一个版本一个子目录（`v1/`…`v5/`），见 [`out/README.md`](out/README.md) |
| `0x808/` `libvgm/` `furnace/` 等 | 外部参考工程（各自带 `.git`，不纳入本仓库） |

---

## 快速开始

### 构建

```bash
# 依赖：libvgm（需先编出静态库）、nlohmann/json、CMake + Ninja（MSYS2）
cmake -S libvgm -B libvgm/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_LIBAUDIO=OFF -DBUILD_PLAYER=OFF -DBUILD_VGM2WAV=ON -DUSE_SANITIZERS=OFF
cmake --build libvgm/build --target vgm2wav

cmake -S cli -B cli/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cli/build
```

### 跑通全链路

**所有产物都写到 `out/`**（目录说明见 [`out/README.md`](out/README.md)）。

```bash
B=cli/build
V="vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm"
mkdir -p out

$B/vgm2json.exe  "$V" -o out/song.json              # VGM → JSON
$B/json2mid.exe  out/song.json -o out/03_original_OPM.mid --report
$B/opm2opll.exe  out/song.json -o out/song.ym2413.json \
                 --vgm=out/song.ym2413.vgm           # JSON → JSON + 试听 VGM
$B/json2mid.exe  out/song.ym2413.json -o out/04_converted_OPLL.mid --report
libvgm/build/bin/vgm2wav.exe out/song.ym2413.vgm out/02_converted_OPLL_YM2413.wav
```

### 音色（`.fui`）

每个 JSON 都配一套 Furnace 音色文件，用户直接在 Furnace 里改，改完再重新生成：

```bash
$B/vgm2json.exe  "$V" -o out/song.json --fui              # 出 chXinsY_ym2151.fui
$B/opm2opll.exe  out/song.json -o out/song.ym2413.json --fui  # 出 chXinsY_ym2413.fui
$B/fuitool.exe   dump out/ch1ins1_ym2413.fui --tick-rate=60   # 看一眼
#   ……在 Furnace 里改 out/ch1ins1_ym2413.fui……
$B/opm2opll.exe  out/song.json -o out/song.ym2413.json \
                 --fui --use-edited-fui --vgm=out/song.ym2413.vgm
```

**`.fui` 里不含中断速率**：宏只存「每步几个 tick」，一秒几个 tick 来自 `.fur` 工程
（Furnace 的 `song.hz`，默认 60）。所以 `--tick-rate` 必须显式给。详见 [`docs/VOICE-FUI.md`](docs/VOICE-FUI.md)。

### 验证音高

两份 `--report` 输出按 `(channel, startSample)` 配对，就是逐音符的音高 diff：

```python
import re
from collections import Counter
def load(p):
    d = {}
    for line in open(p):
        m = re.match(r'\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+([+-][\d.]+)\s+(-?[\d.]+)', line)
        if m:
            st, en, ch, note, cents, pitch = m.groups()
            d[(int(ch), int(st))] = float(pitch)
    return d
a, b = load('r_src.txt'), load('r_dst.txt')
delta = [b[k] - a[k] for k in set(a) & set(b) if a[k] > 0 and b[k] > 0]
print(Counter(round(x) for x in delta).most_common(10))
```

`31 Grass Maze` 的实测结果（2514 个音符配对）：**91.3% 完全一致**，其余差 2~4 个半音，
全部来自「OPLL 没有 ML=10 这种非 2 次幂载波倍频」这一芯片能力上限。

---

## 程序清单

| 程序 | 输入 | 输出 | 职责 |
|---|---|---|---|
| `vgminfo` | `.vgm` | stdout | 头部信息 + 命令统计（不解析语义） |
| `vgm2json` | `.vgm` | `.json` | 导入 + 结构化（按 key-on 分块） |
| `jsondump` | `.json` | stdout | 人读摘要 + 完整性校验 |
| `json2vgm` | `.json` | `.vgm` | 输出（raw 直通路径） |
| `vgmroundtrip` | `.vgm` | stdout | `vgm→json→vgm` 字节级往返校验 |
| `opm2opll` | `.json` | `.json` / `.vgm` | YM2151 → YM2413 跨芯片转换 |
| **`json2mid`** | `.json` | `.mid` | **音高验证**：JSON → 标准 MIDI，两份直接对比 |
| **`fuitool`** | `.fui` | stdout / `.fui` | **音色文件**：dump 参数与宏、批量校验、读改写 |

---

## 核心结论

1. **时间轴用采样数，不量化**。JSON 里只存寄存器原值，不存 BPM、不存音高。
2. **音符 = 一次 key-on 边沿到 key-off 边沿**。分块判据由 4 个独立实现交叉验证（Valley Bell / Denjhang / MDPlayer / Furnace）。
3. **YM2151 的音高是"音名"**：`pitch = 25 + (kc − (kc>>2)) + kf/64`，与时钟无关（时钟只影响绝对频率）。
4. **YM2413 的音高是"频率"**：`Hz = fnum × (clock/72) × 2^(block−19)`。
5. **FM 的实际基频 = 音高频率 × 载波 ML**。跨芯片转换必须两端都算，否则会差整数个八度。

这些结论的逐行代码出处（带 `文件:行号`）在 [`docs/PITCH-CONVERSION.md`](docs/PITCH-CONVERSION.md) 与
[`docs/reference/_pitch-*.md`](docs/reference/)。

---

## 文档

从 [`docs/README.md`](docs/README.md) 开始。最该先读的三份：

- [`docs/GOAL.md`](docs/GOAL.md) — 目标与步骤
- [`docs/DATA-MODEL.md`](docs/DATA-MODEL.md) — JSON schema（权威定义）
- [`docs/PITCH-CONVERSION.md`](docs/PITCH-CONVERSION.md) — 音高转换的全部公式与已修 bug

---

## 许可

本仓库自有代码（`cli/`、`docs/`）见 [LICENSE](LICENSE)。
`0x808` 为 MIT，`libvgm` 为其原许可，`furnace` 为 GPL-2.0+（**只作参考，未链接**）。
`cli/third_party/midifile` 为 Craig Sapp `midifile`（从 Denjhang Music Player 复制，原许可随目录）。
