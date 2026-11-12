# 数据格式：VGSJ JSON

> 本文件是 JSON 的**权威 schema 定义**。改格式先改这里。
> 设计依据在 `ARCHITECTURE.md`；目标和步骤在 `GOAL.md`。

---

## 1. 三条不可动摇的规则

| # | 规则 | 后果 |
|---|---|---|
| **D1** | **时间单位 = 采样数**（int64，44100 Hz）。全文件不出现 tick / 行 / 拍 / 帧 / BPM。 | 整个软件的时间轴都是采样数 |
| **D2** | **原值显示**：块里存 `(reg, val, atSample)` 原始寄存器值；音高/音名/音量是视图层推测，不进数据层 | 换换算器不改数据 |
| **D3** | **按音符事件分块**：寄存器流切成"一次 key-on 到 key-off"的块，不是一条扁平长链 | 可按音符编辑 |

---

## 2. 顶层结构

```jsonc
{
  "format": "vgm-studio/song",
  "version": 1,

  "meta":       { /* 来源、解析器版本、诊断 */ },
  "timeBase":   { /* 采样率、总长、循环点 */ },
  "chips":      [ /* 芯片实例列表 */ ],
  "globals":    [ /* 全局寄存器块（按时间分段） */ ],
  "channels":   [ /* 通道列表，每个含音符块 */ ],
  "raw":        { /* 原始命令流，位精确往返用 */ }
}
```

---

## 3. `meta`

```jsonc
"meta": {
  "sourceFile": "01 Arsys Logo.vgm",
  "sourceSize": 7228,
  "sourceSha256": "a3f1...",
  "importedAt": "2026-11-11T15:30:00Z",
  "parser": { "lib": "libvgm", "commit": "c5c03a2c", "regStreamMethod": "shadowdev" },
  "structurer": { "name": "ym2151/v1", "splitRule": "modizer-note-merge" },
  "unhandled": [
    { "cmd": "0x93", "atSample": 44100, "note": "DAC Stream 透传，未结构化" }
  ]
}
```

`unhandled` 是诊断账本：它告诉用户"这个文件里有多少东西我没搞懂"，比"导入成功"有用得多。

---

## 4. `timeBase`

```jsonc
"timeBase": {
  "sampleRate": 44100,      // 时间单位
  "totalSamples": 251057,   // 头部声明的总长
  "loopSample": -1,         // 循环点采样位置，-1 = 无循环
  "vgmRate": 60             // 仅影响 0x62/0x63 的换算，不参与任何计算
}
```

**没有 bpm 字段。** 需要网格显示时，BPM 由 `IGridMapper` 在 UI 层假设（例如"假定 120 BPM"），且必须在界面上标出"这是假设值"。

### 4.1 `loopSample` 是「起点」，不是「长度」

VGM 头 `0x20`（`loopTicks`）存的是**循环段长度**，不是循环起点位置。
导入时换算：

```
loopSample = (loopTicks > 0) ? totalSamples - loopTicks : -1
导出时反向：
loopTicks  = totalSamples - loopSample
```

`31 Grass Maze` 实测：`loopTicks = 2393734`、`totalSamples = 2406316`，
所以 `loopSample = 12582`（0.29 秒前奏）。**如果把长度直接当位置存，会得到一个
贴着曲末的假循环点**——这个 bug 曾在 `Importer.cpp` 里存在。

---

## 5. `chips`

```jsonc
"chips": [
  {
    "id": "ym2151.0",
    "type": "YM2151",
    "vgmChipType": 84,        // 0x54
    "clock": 4000000,
    "instances": 1,
    "channelCount": 8,
    "portCount": 2,           // 寄存器地址口 / 数据口
    "capabilities": {
      "operators": 4,
      "algorithms": 8,
      "polyphony": 8
    }
  }
]
```

`capabilities` 是跨芯片转换的依据（见 `GOAL.md` §6）。目标芯片（如 YM2413）会是 `operators: 2, algorithms: 1, polyphony: 9`。

---

## 6. `globals` — 全局寄存器块

全局寄存器不属于任何通道，但影响所有通道（LFO、noise、timer 等）。它们也按时间分段：

```jsonc
"globals": [
  {
    "chip": "ym2151.0",
    "atSample": 0,
    "regs": [
      { "reg": 1,  "val": 2 },      // LFO reset / test
      { "reg": 24, "val": 0 },      // 0x18 LFO 频率
      { "reg": 25, "val": 128 },    // 0x19 AMD-PMD（bit7 = PM）
      { "reg": 27, "val": 0 }       // 0x1B LFO 波形
    ]
  },
  {
    "chip": "ym2151.0",
    "atSample": 88200,
    "regs": [
      { "reg": 24, "val": 12 }      // LFO 频率变了
    ]
  }
]
```

**为什么分段而不是一对一点**：全局寄存器的变化频率远低于通道寄存器（一个文件里通常只有几十次），按时间聚成段更省空间也更好读。段内 `regs` 数组的顺序就是原始写入顺序。

---

## 7. `channels` — 通道 + 音符块（核心）

```jsonc
"channels": [
  {
    "chip": "ym2151.0",
    "index": 0,
    "name": "FM1",
    "kind": "FM",

    "notes": [
      {
        "id": 0,
        "startSample": 8820,
        "endSample": 17640,
        "durationSamples": 8820,

        "view": {                            // ← 视图层推测（可丢弃重算）
          "nearestNote": 60,
          "cents": 12.0,
          "exact": false,
          "pitchMapper": "opm-kc-kf/v1"
        },

        "segments": [                        // ← 数据层原值（唯一真值）
          {
            "atSample": 8820,
            "regs": [
              { "reg": 32, "val": 30 },      // 0x20 PAN/FB/ALG
              { "reg": 40, "val": 46 },      // 0x28 KC（音名码）
              { "reg": 48, "val": 26 },      // 0x30 KF（细音高）
              { "reg": 96, "val": 1 },       // 0x60 TL（载波音量）
              { "reg": 8,  "val": 120 }      // 0x08 Key-on：ch3, M1|C2 掩码
            ]
          },
          {
            "atSample": 13230,
            "regs": [
              { "reg": 96, "val": 12 }       // 0x60 TL 衰减
            ]
          },
          {
            "atSample": 17640,
            "regs": [
              { "reg": 8, "val": 0 }         // 0x08 Key-off
            ]
          }
        ],

        "provenance": {                      // 溯源：对应 raw 的哪几条命令
          "cmdRange": [12, 27],
          "confidence": 0.98
        }
      }
    ],

    "orphans": [                             // 不属于任何音符块的寄存器写
      {
        "atSample": 0,
        "regs": [ { "reg": 32, "val": 0xC7 } ],
        "reason": "音符开始前的音色参数初始化"
      }
    ],

    "lanes": [                               // 连续量：不按音符切
      {
        "param": "pms",
        "reg": 56,                           // 0x38
        "points": [ [0, 3], [17640, 0] ]     // [atSample, rawValue]
      },
      {
        "param": "pan",
        "reg": 32,
        "points": [ [0, 64] ]
      }
    ]
  }
]
```

### 7.1 三个层级的区别

| 字段 | 内容 | 权威性 | 可否丢弃 |
|---|---|---|---|
| `notes[].segments[]` | 原始 `(reg, val, atSample)` | ✅ **唯一真值** | ❌ |
| `notes[].view` | 音高/音名推测 | 参考值 | ✅ 随时重算 |
| `channels[].lanes[]` | 连续量（PMS/AMS/pitch bend）的原始值点 | ✅ 原始值 | ❌（但可重算其分段） |
| `orphans[]` | 无法归入音符块的写 | ✅ 原始值 | ❌ |

### 7.2 为什么 `segments` 是一条一条时间点，不是一段连续区间

因为 VGM 里同一音符内的寄存器写**不是均匀分布的**：音符开始瞬间可能一口气写 20 条（所有 operator 参数 + KC/KF + keyon），中间可能只改 1 条 TL，结束只写 1 条 keyoff。

用"时间点 + 该点的写列表"表示，和原始日志同构，且便于重建。

### 7.3 `notes[]` 的排序与唯一性

- `notes[]` 按 `startSample` 升序
- 同通道的 `notes[]` **可以重叠**（YM2151 同通道可以快速重触发，前一个还没结束下一个就开始）。重叠时按 `startSample` 排序，`endSample` 可能大于后一个的 `startSample`
- `id` 在文件内唯一，用于编辑时的引用

### 7.4 `orphans[]` 的意义

不是所有寄存器写都属于某个音符：

- 音色初始化（音符开始前设定 operator 参数）
- 全局 LFO 设置
- 某些曲子在 key-on 之前就先写好了 KC/KF

这些不能丢，但也不属于任何音符块，所以单独列出。**`orphans` 是完整性的保证**：`notes[].segments[]` + `orphans[]` 合起来必须能重建出原始的每一条寄存器写。

### 7.5 完整性校验不变量

```
∀ (chip, port, reg, val, atSample) ∈ 原始寄存器流：
    它必须出现在以下之一：
      · 某个 notes[].segments[] 里
      · 某个 orphans[] 里
      · 某个 globals[] 里
```

这是 `MILESTONES.md` M1-2 的验收依据。

---

## 8. `raw` — 原始命令流

```jsonc
"raw": {
  "dataOffset": 128,
  "eofOffset": 77910,
  "header":   "base64:...",   // [0, dataOffset)
  "commands": "base64:...",   // [dataOffset, eofOffset)
  "tail":     "base64:..."    // [eofOffset, 文件末尾)
},
"tags": {
  "gd3": "base64:..."         // 可选，GD3 标签的完整原始字节（含 "Gd3 " 头和长度字段）
}
```

**用途**：位精确往返。不编辑任何东西时，直接输出 `raw.commands` 就能得到字节级相同的 VGM。

**不是** "结构化失败后的兜底"——它是**位精确的合同**。结构化再完美，重建出的 VGM 也可能因为寄存器写顺序微调而字节不同（例如 furnace 的 `rWrite` 去重机制），`raw` 保证了不改的时候一定一样。

### 8.1 `tags.gd3` 必须跨芯片透传

GD3 是曲名 / 作者 / 日期 / 备注（UTF-16LE 字符串表）。它不是音频数据，
跨芯片转换（`opm2opll`）与重新写出 VGM 时必须**逐字节原样搬运**：

- 导入：从 VGM 头 `0x14`（相对 0x14 的偏移）定位 `"Gd3 "` 块，取 `12 + length` 字节
- 转换：`doc.tags = src.tags`（`Opm2Opll.cpp`）
- 导出：把它追加在 `0x66`（EOF 命令）之后，并把头 `0x14` 写成
  「相对 0x14 的偏移」。注意 `0x04`（EOF 偏移）仍指向 `0x66` 之后、GD3 之前。

`31 Grass Maze` 实测：源 268 字节，输出也是 268 字节，`cmp` 完全一致，
曲名 "Grass Maze"、机种 "Sharp X68000" 等中文/日文字段全部保留。

---

## 9. 编辑如何落地（步骤 3）

用户在 UI 上拖动音条 → **改 back 到 `segments` 的原值**，不改 `view`：

| 操作 | 落点 |
|---|---|
| 改音符起始时间 | 移动整个第一个 `segment` 的 `atSample`，以及后续所有 `segments` 的 `atSample`（保持相对距离） |
| 改音长 | 移动最后一个 `segment`（key-off）的 `atSample` |
| 改音高 | 改 KC/KF 那条 `{reg:40}` / `{reg:48}` 的 `val` |
| 改力度/音量 | 改载波 TL 那条 `{reg:96}` 的 `val` |
| 改音色 | 改 operator 参数那几条（`0x40/0x60/0x80/0xA0/0xC0/0xE0`）的 `val` |
| 改网格 BPM | **只改 UI 的 `IGridMapper` 参数，不碰 JSON** |
| 改换算器 | **只重算 `view`，不碰 `segments`** |

最后两条是"换尺子不改物"的具体落实。

---

## 10. 版本演进

| version | 变化 |
|---|---|
| 1 | 初版：`meta / timeBase / chips / globals / channels / raw` |

改 schema 时：
1. 先改本文件；
2. 加 `version` 号；
3. 在 `meta` 里记录 `formatVersion`；
4. 写迁移函数。

---

## 11. 已排除的字段（明确不设计）

| 字段 | 为什么不要 |
|---|---|
| `bpm` | VGM 是日志格式，没有 BPM。存在数据层就是造假 |
| `tick` / `row` / `beat` / `frame` | 全部是显示层的派生量 |
| `nearestNote` 在 `segments` 里 | 音高是推测，必须放 `view` |
| `instrumentId` 引用 | 音色由 operator 参数原值决定，不做"音色对象"抽象（第一版）。见下方注 |
| `grid` | 网格是 UI 状态，不是歌曲数据 |

> **关于"音色对象"**：v1 不做 instrument 去重表。理由：去重需要指纹算法，而指纹算法会随版本变化——一旦变化，整个文件的所有引用都要重算。第一版让每个音符块自己带完整的 operator 参数（重复就重复，VGM 里本来也是重复写的）。等文件大到不可接受时再引入去重表，且必须带 `hashAlgorithmVersion`。
