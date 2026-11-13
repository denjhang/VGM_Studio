# 乐器库 —— 标签、参数、指纹

这个文件是**本地乐器库**：曲目用到的每一个音色的「人读标签 + 完整 FM 参数 + 指纹」。
§4 的数据由 `fuitool lib` 从 `out/Spindizzy_II/31 Grass Maze/2151-2413.fur` 自动生成，§1–§3 是手写说明。

---

## 1. 三条判据，别搞混

| 判据 | 是什么 | 用在哪 | 能不能当身份 |
|---|---|---|---|
| **参数** | alg / fb / fms / ams / ops + 每个算子的 mult、tl、ar、dr、sl、rr、dt、dt2、ksr、am、vib、egt、ksl、sus、kvs | 判断「这是哪个乐器」 | ✅ **唯一身份** |
| **指纹** | 参数串的 FNV-1a 64 位哈希（`VoiceSignature()` → `VoiceFingerprint()`） | 快速比较 / 表里对齐 | ⚠️ 只是参数的摘要 |
| **寄存器影子指纹** | `0x40/0x60/0x80/0xA0/0xC0/0xE0` 共 256 字节（`VoiceHash`） | **仅曲目内去重** | ❌ 不能当身份 |

**为什么影子不能当身份**：影子寄存器里混着演奏期数据。最典型的是 `0x60` 的 TL——
OPLL 上载波 TL 会随每个音符的音量改写，同一个乐器在不同音符上影子不同；
反过来两个不同乐器在某个瞬间的影子也可能撞上。所以影子只适合回答
「这一时刻的音色和前面出现过的是不是同一个」，不适合回答「这是哪个乐器」。

**指纹的定义**（`cli/src/FuiVoice.cpp` 的 `VoiceSignature`）：

```
t<type>;preset<opllPreset>;alg<alg>;fb<fb>;fms<fms>;ams<ams>;ops<ops>
  |0:mult,tl,ar,dr,sl,rr,dt,dt2,ksr,am,vib,egt,ksl,ws,sus,kvs
  |1:...
```

`preset` **必须**算进去：OPLL 上 `opllPreset≠0` 时芯片用的是 ROM 里那一号音色，
`.fui` 里的 FM 参数只是它的副本、并不生效。参数一模一样但 preset=5 / preset=14
的两个乐器，实际是 Clarinet 和 Acoustic Bass，完全是两个音色。
`block` 不算：它只是 2413 侧的八度偏移量。

---

## 2. 标签怎么读（全部展开成全称 + GM 分类）

标签是你在 Furnace 里手打的，格式 `<通道><序号>[-标签]`。
下表把每个标签展开成完整英文名，并给出最接近的 General MIDI 分类
（GM program 是官方 1..128 编号；打击乐在 GM 里是通道 10 的固定音符号，不是 program）。

| 标签 | 完整名 | GM 分类 |
|---|---|---|
| `pad sq` | Square Pad（方波铺底） | Lead 1 (square)，GM 81 |
| `pad sq2` | Square Pad 2（方波铺底，第二个） | Lead 1 (square)，GM 81 |
| `pad sine` | Sine Pad（正弦铺底） | Pad 1 (new age)，GM 89 |
| `pad` | Pad（铺底 / 长音） | Pad 1 (new age)，GM 89 |
| `pad2` | Pad，第二个 | Pad 2 (warm)，GM 90 |
| `celesta` | Celesta（钢片琴，衰减快、偏亮） | Celesta，GM 9 |
| `trumpet` / `trum` | Trumpet（小号，铜管、起音硬） | Trumpet，GM 57 |
| `vibraphone` / `vib` | Vibraphone（颤音琴） | Vibraphone，GM 12 |
| `clean vibraphone` | Clean Vibraphone（干净、无过载的颤音琴） | Vibraphone，GM 12 |
| `light vibraphone` | Light Vibraphone（轻、亮） | Vibraphone，GM 12 |
| `dark vibraphone` | Dark Vibraphone（暗、闷） | Vibraphone，GM 12 |
| `guitar` | Electric Guitar（电吉他） | Electric Guitar (jazz)，GM 27 |
| `clean guitar` | Clean Electric Guitar（清音电吉他） | Electric Guitar (clean)，GM 28 |
| `clean guitar2` | Clean Electric Guitar 2 | Electric Guitar (clean)，GM 28 |
| `sanmi` | Shamisen（三味线，拨弦） | Shamisen，GM 107 |
| `acid bass` | Acid Bass（酸性贝斯，共振重） | Synth Bass 1，GM 39 |
| `synth bass` / `syn bass` | Synth Bass（合成贝斯） | Synth Bass 1，GM 39 |
| `bd` | Bass Drum（低音鼓 / 贝斯鼓） | Bass Drum 1，打击乐音符 36 |
| `hh` | Hi-Hat（踩镲） | Closed Hi-Hat，打击乐音符 42 |
| `sd` | Snare Drum（军鼓） | Acoustic Snare，打击乐音符 38 |
| `ch1ins2` | 第 1 个源通道（YM2151 的 ch1）的第 2 个音色 | — |

序号由 `opm2opll` 按音色首次出现顺序编。匹配按**最长前缀**，
所以 `pad sq2` 不会被 `pad` 抢先匹配、`clean guitar2` 不会被 `clean guitar` 抢先。

**同一基名在源侧和目标侧成对出现**：`ch1ins1-pad`（YM2151，type 33）是源音色，
`ch1ins1-pad sq`（YM2413，type 13）是它转换后的目标音色。`opm2opll --fur`
按 `chXinsY` 基名取目标音色，后缀随意。

---

## 3. ★ OPLL 侧不用 0 号自定义音色

这一版的目标侧**完全不碰 0x00-0x07 用户音色槽**，实测输出里对 0x00-0x07 的写为 **0 次**。
音色的丰富度靠两件事：

1. **预设乐器**：19 个目标音色全部落在 ROM 的 15 个预置上（preset 1..15，无 0）
2. **patch 宏 + 音量宏**：同一个音符内部切换 preset、拉伸音量包络，
   造出 ROM 预置之外的听感

实测 patch 宏生效的证据（每个音符内部出现多个不同 preset）：

| 通道 | 音符数 | 音符内切换 preset 的音符数 |
|---|---|---|
| FM1 | 327 | 0（单音色，未加 patch 宏） |
| FM2 | 240 | 231 |
| FM3 | 327 | 309 |
| FM4 | 291 | 80 |
| FM5 | 388 | 388（鼓组，全部） |
| FM6 | 291 | 80 |
| FM7 | 326 | 325 |
| FM8 | 325 | 324 |
| FM9 | 0 | — |

鼓组（FM5）是这套做法的典型：`ch5ins1-bd` preset=14 + 音量宏
`0 15 15 15 15 15 15 15 13 10 8 0 …`（快速衰减）+ arp 宏
`0 -5 -10 -14 … -41`（音高坠落）+ wave 宏 `10 4 6`（快速换 preset）。
三者叠在一起才做出鼓的"砰"，单靠 ROM 预设做不到。

### 3.1 预设使用分布（全曲 0x30 高 4 位统计）

| preset | ROM 音色名 | 写入次数 |
|---|---|---|
| 1 | Violin | 18 |
| 2 | Guitar | 1108 |
| 3 | Piano | 240 |
| 4 | Flute | 1711 |
| 5 | Clarinet | 8597 |
| 6 | Oboe | 757 |
| 7 | Trumpet | 27 |
| 8 | Organ | 108 |
| 9 | Horn | 18 |
| 10 | Synthesizer | 680 |
| 11 | Harpsichord | 3378 |
| 12 | Vibraphone | 1901 |
| 13 | Synth Bass | 224 |
| 14 | Acoustic Bass | 1804 |
| 15 | Electric Guitar | 188 |
| **0** | **User（自定义）** | **0（未使用）** |

（名称取自 `FmVoice.cpp:395` 的 `kYm2413Names`，16–18 是节奏音色，本曲未用。）

### 3.1a 宏的两个硬约束（v7–v9 踩过的坑）

**① `baseVol` 与 `0x30` 低 4 位极性相反。** `0x30` 低 4 位写的是**衰减量**
（0=最响，15=静音），而 `ExpandMacros` 的 `baseVol` 走 Furnace 的 `chan.vol`
（15=最响）。必须 `mi.baseVol = 15 - vol`，否则音量宏反向工作，通道几乎无声。

**② 宏只在真实 key-on 启动一次。** 音量宏常见的写法是第一帧 0、第二帧 15
（先用静音占一拍再起音）。`NoteSplitter` 因 KC 跳变切出的 legato 块在源里
没有 key 事件，如果每块都重新 `Init` 宏，就会在块边界重放第一帧——听感是
「每个音中间被掐掉一拍」。正确处理是用 `skipTicks` 接续相位（见
[VOICE-FUI.md §8.5.4](VOICE-FUI.md)）。

**③ 基础音量本身要按 4:1 换算，宏才是在正确的基线上做比例。**
宏的 `baseVol` 不是满量程常数，它来自转换器算出的音符音量，所以 TL→nibble
的换算精度直接决定宏的比例是否成立。两端台阶是 **4:1**（YM2151 载波 TL
0.7526 dB/单位 vs YM2413 `0x30` 3.01 dB/步），即 `nibble = round(TL/4)`，
不是 vgm-conv 给 OPN 的 `TL>>3`。整曲 RMS：`>>3` 时 −16.4 dBFS，
`÷4` 且 `volumeOffset=0` 时 −19.0 dBFS，与源一致。详见
[VOICE-MAPPING.md §5.1](VOICE-MAPPING.md)。

**④ 逐通道仍有 ±7 dB 的剩余偏差，与 nibble 无关。**
整曲电平对齐后，把每个通道单独抽出来渲染会发现：FM4/FM6 约 +0.9/+0.5 dB，
而 FM2/FM3/FM8 约 −6.6 ~ −7.0 dB、FM5（鼓）−11.5 dB。原因是同一个 nibble
在不同 ROM 预置上的绝对响度不同（各预置的载波 TL 与波形不同），加上 OPM
的 4 算子频谱密度 OPLL 本来就还原不了。要逐通道拉平，得在 `MatchOpllRom`
里加一张"预置实测响度"补偿表，而不是继续调 nibble。测量方法见
`cli/tools/probe_channels.py`。

---

### 3.1b ★ 乐器名里的参数标签

用户在 Furnace 里直接把参数写进乐器名，用 `-` 分隔。`opm2opll --fur` 会解析
并执行它们（`FurFile.cpp` 的 `ParseInstrumentTags`）。

| 标签 | 语义 | 关掉了什么 |
|---|---|---|
| `note:X` | 音高由名字指定。**X 就是目标芯片上的最终音高**（科学音高记法，A4=440，`C4`=60） | 不读源 KC/KF、**不做 ML 八度补偿**、不叠加 `--transpose`，也**不跟随源里的滑音/颤音**——那些由 2413 侧的宏替代 |
| `vol:N` | 基础音量由名字指定（0..15，Furnace 音量语义，**15 = 最响**） | 不看载波 TL；宏仍在这个基线上做比例 |
| `noteoff:self` | 不使用源的 key-off，音符延长到宏完整播完 | 源里的所有 key-off 被丢弃，改为宏长度 × tick 时长后补一条 |
| `noteoff:N` | 不使用源的 key-off，宏**只走 N 步**就立刻 off（N 是 tick 数，含 delay 之后算起） | 同上，但长度取 N 而不是整条宏。给「宏尾巴到了就该收声」的鼓卡精确长度 |
| `link:X` | 联动音色：X 是另一乐器的基名（如 `ch5ins3`） | 目标乐器每次击发时，本乐器在同一起点、同样时长地一起响 |

**解析规则**：每个标签是 `key:value`，值到下一个空格或「`-` + 已知 key + `:`」为止。
`-` 同时是 Furnace 音符记法的占位符（`C-4` 就是 `C4`），所以只在后面紧跟已知
参数名时才当分隔符。`Bb3`/`Cb4` 里的 `b` 是降号。

**实现要点**

- `note:X` 直接走 `FreqToOpllFnum(440·2^((X-69)/12))`，`octaveOffset = 0`。
  例：`note:B2` → MIDI 47 → 123.47 Hz → block 2 / fnum 325。
- `vol:N` 要转换极性：`N` 是音量域（15=最响），而 `0x30` 低 4 位是衰减域
  （0=最响），所以内部用 `15 - N`。写成 `vol:15` 才是最响，`vol:0` 是静音。
- `noteoff:self` 的时长取 `MacroActiveTicks(voice)`（音量宏最后一个非零帧之前
  的步数 × speed）。拉长后若越过下一个音符起点，会被裁到下个音符起点——
  OPLL 同通道不能同时发两个音，不裁就会丢鼓点。
- `noteoff:N` 走同一条路径，只是时长直接取 `N × (sampleRate / tickRate)`，
  不查宏长度。`noteoff:N` 也会置上 `noteOffSelf`，所以「丢弃源 key-off」的行为一致。
  宏本身仍按 `N` 个 tick 的窗口展开（`mi.endSample` 就是这条 off 点），
  超出窗口的宏帧自然不吐。
- `link:X` 只联动「确实用了 X 这个乐器」的音符：鼓通道上同时有 BD/hh/SD/pad2，
  按 `(通道, 起点) → 乐器名` 筛一遍，否则整条通道都会被复制过去。

**当前 .fur 里的实例**（`31 Grass Maze`，v15）

| 乐器 | 参数 | 效果 |
|---|---|---|
| `ch5ins1-BD3` | `note:B2` `vol:15` `noteoff:6` | 底鼓锁在 B2，宏走 6 步（4410 采样 = 100 ms）立刻 off |
| `ch5ins2-hh` | `note:C8` `noteoff:self` | 镲锁在 C8（block 7 / fnum 344），宏放完才 off |
| `ch5ins3-SD2` | `note:B3` `noteoff:9` `vol:14` | 军鼓锁在 B3，宏走 9 步（6615 采样 = 150 ms）立刻 off |
| `ch9ins1-SD2-HF` | `link:ch5ins3` `vol:13` `noteoff:self` `note:C8` | FM9 叠一层 SD2 的 HF 层，只在 ch5ins3 击发时跟着响，音高锁 C8 |

实测（v15）：FM9 有 **54 个**音符（正好等于 ch5ins3 的音符数），
每个的基音高都落在 block 7 / fnum 344（= C8，随后由它自己的 arp 宏继续加偏移）；
FM5 全部 388 个音符 **0 处重叠**；音符长度直方图里
BD 类整齐落在 6.00 tick（207 个）、SD 类 9.00 tick（48 个），
`noteoff:N` 的长度是按 tick 精确落地的，不受源 key-off 影响。

**`link` 目标也支持 `note:`**：联动方自己写了 `note:X` 时，搬运过来的
fnum/block 会被改写成 X 的固定值（key/sus 位保留）。这条是 v13 加的——
之前 link 只能沿用被联动方的音高。

---

### 3.1c ★ 跨曲目复用：库怎么认音色

`docs/instrument-library.json` 除了 `instruments`（当前 .fur 的明文清单），
还有一段 **`pairs`**：源音色 → 目标音色的复用表。批量转换时 `opm2opll --library=`
读的就是它。

**判据是 YM2151 的 FM 参数本身，不是指纹。**

指纹（`VoiceFingerprint`）是把签名串整个哈希一遍，签名里含 `type` 与 `opllPreset`，
只适合**同一曲目内去重**。跨曲目比的是源侧那 4 个算子的参数：

```
opm;alg<A>;fb<F>;fms<F>;ams<A>|mult,dt,dt2,ar,dr,sl,rr,rs,am,ksr,vib,egt ×4
```

- **不含 TL**：`0x60` 是每个音符自己的音量，同一音色在不同音符上 TL 不同，
  算进身份会把一套音色拆成几十条。
- **含 alg**：4 算子连接方式不同就是另一种乐器，`alg` 不同直接判为不匹配
  （距离返回 1000）。

匹配顺序（`InstrumentLibrary::Find`）：

1. **参数完全一致** → 直接搬用。同一键在库里可能有多条（例如 ch1/ch7/ch8
   三个 pad 的 OPM 参数完全相同，但分别配了 Clarinet / Square Pad / Sine Pad），
   此时优先取**通道号相同**的那条——通道号在整套曲目里代表同一种角色。
2. **参数最近**（距离 ≤ 48）→ 取最近的。距离是加权和：`mult` 差一档 ×2、
   `fb`/`fms`/`ams` 直接加、四个算子的 EG 值（ar/dr/sl/rr）直接加。
3. 都没命中 → 退回 `.fur` 按名字匹配，再退回 ROM 自动匹配。

**联动音色也进库**。`link:X` 的目标侧没有自己的源音色，库里给它记的是
**被联动那件源音色的身份键**（`linkSourceKey`）。这样换一首曲子，
即使通道名完全不同，只要那件源音色的参数还在，联动就能装回去。

### 3.1d 批量转换实测（Spindizzy II 全 41 首）

| 项 | 值 |
|---|---|
| 曲目数 | 41 |
| 音符总数 | 110 180 |
| 库精确命中 | 19（全都来自 `31 Grass Maze`） |
| 库"最近"命中 | 41 |
| 未命中（退回 ROM 自动匹配） | 748 |
| 全专辑不同的 YM2151 音色数 | **363**，其中 **354 种只出现在一首曲子里** |

结论：**同一套音色原样跨曲目出现的情况很少**，配器参数每首都在变。
精确匹配只对「库里有这首曲子的原音色」生效，所以只有 31 Grass Maze 全中。
要把复用率提上去，只能逐首确认、把目标音色补齐进库；批量转换的价值在于
**把 41 首的结构都转换好了**，人工只需要调音色，不用再碰音符/时序。

---

### 3.2 转换映射（源 → 目标 → GM）

`opm2opll` 的匹配结果。三列名字各有各的意思，别混：
- **源标签**：曲目里这个乐器原本是什么
- **目标 preset**：YM2413 芯片上实际播放的 ROM 预置
- **GM**：那个预置在 General MIDI 里最接近的对应物

| 源标签 | 目标乐器名 | preset | ROM 预置 | GM 音色 | 宏 |
|---|---|---|---|---|---|
| `ch1ins1-pad` | `ch1ins1-pad sq` | 5 | Clarinet | Clarinet (GM 72) | 1 |
| `ch1ins2-celesta` | `ch1ins2-vibraphone` | 12 | Vibraphone | Vibraphone (GM 12) | 0 |
| `ch2ins1-trum` | `ch2ins1-trumpet` | 7 | Trumpet | Trumpet (GM 57) | 0 |
| `ch2ins2-vib` | `ch2ins2-clean vibraphone` | 12 | Vibraphone | Vibraphone (GM 12) | 2 |
| `ch3ins1-trum` | `ch3ins1-trum` | 7 | Trumpet | Trumpet (GM 57) | 0 |
| `ch3ins2-sanmi` | `ch3ins2-clean guitar` | 2 | Guitar | Electric Guitar (clean) (GM 28) | 2 |
| `ch3ins3-sanmi2` | `ch3ins3-guitar` | 15 | Electric Guitar | Electric Guitar (jazz) (GM 27) | 2 |
| `ch4ins1-acid bass` | `ch4ins1-synth bass` | 10 | Synthesizer | Synth Bass 1 (GM 39) | 0 |
| `ch4ins2-sanmi` | `ch4ins2-clean guitar2` | 15 | Electric Guitar | Electric Guitar (clean) (GM 28) | 2 |
| `ch5ins1-bd` | `ch5ins1-bd` | 14 | Acoustic Bass | Bass Drum 1 (perc 36) | 3 |
| `ch5ins2-hh` | `ch5ins2-hh` | 2 | Guitar | Closed Hi-Hat (perc 42) | 2 |
| `ch5ins3-sd` | `ch5ins3-sd` | 14 | Acoustic Bass | Acoustic Snare (perc 38) | 3 |
| `ch5ins4-pad2` | `ch5ins4-pad2` | 14 | Acoustic Bass | Pad 2 (GM 90) | 2 |
| `ch6ins1-acid bass` | `ch6ins1-syn bass` | 10 | Synthesizer | Synth Bass 1 (GM 39) | 0 |
| `ch6ins2-sanmi` | `ch6ins2-clean guitar` | 2 | Guitar | Electric Guitar (clean) (GM 28) | 2 |
| `ch7ins1-pad sq` | `ch7ins1-pad sine` | 5 | Clarinet | Sine Pad (GM 89) | 2 |
| `ch7ins2-vib` | `ch7ins2-light vibraphone` | 12 | Vibraphone | Light Vibraphone (GM 12) | 2 |
| `ch8ins1-pad sq` | `ch8ins1-pad sq2` | 5 | Clarinet | Square Pad 2 (GM 81) | 2 |
| `ch8ins2-vib` | `ch8ins2-dark vibraphone` | 12 | Vibraphone | Dark Vibraphone (GM 12) | 2 |

**为什么标签和 ROM 预置对不上**：`bd` 落到 Acoustic Bass、`pad` 落到 Clarinet——
YM2413 是 2 算子芯片，没有真正的鼓、没有真正的铺底。真正的差距由宏补：
鼓靠音量宏 + arp 宏做衰减和音高坠落，铺底靠 patch 宏在音符内换预置。

**同名乐器共用预置**：指纹相同即同一音色，共用同一份 ROM 预置
（如 `ch1ins1-pad sq` / `ch7ins1-pad sine` / `ch8ins1-pad sq2` 都是 preset 5）。

---

## 4. 参数表与明文清单（自动生成）

生成命令：`cli/build/fuitool.exe lib out/Spindizzy_II/31 Grass Maze/2151-2413.fur --json=docs/instrument-library.json`

<!-- 以下内容由 `fuitool lib out/Spindizzy_II/31 Grass Maze/2151-2413.fur` 自动生成，勿手改 -->

来源 `out/Spindizzy_II/31 Grass Maze/2151-2413.fur`，Furnace 版本 228，hz=60.00，共 38 个乐器。

**身份靠参数，不靠指纹。** 指纹只是参数串的哈希，用于快速比较；
参数相同 = 同一音色（哪怕标签名不同）。反之参数不同就是不同音色。

## YM2151 源侧（type 33）

| # | 标签 | GM 音色 | preset | alg | fb | 指纹 | 参数 |
|---|---|---|---|---|---|---|---|
| 0 | `ch1ins1-pad` | Pad 1 (new age) (89) | 0 | 2 | 6 | `2de2a59c97e16bbe` | op0: ml=4 tl=29 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=31 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=13 ar=24 dr=4 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 1 | `ch1ins2-celesta` | Celesta (9) | 0 | 0 | 0 | `b0136ef80269e73d` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=13 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 2 | `ch2ins1-trum` | Trumpet (57) | 0 | 5 | 7 | `123eba3e75aad50f` | op0: ml=4 tl=27 ar=15 dr=8 sl=2 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=21 ar=28 dr=11 sl=15 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=21 ar=31 dr=7 sl=15 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=8 tl=19 ar=31 dr=7 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 3 | `ch2ins2-vib` | Vibraphone (12) | 0 | 0 | 0 | `b0136ef80269e73d` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=13 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 4 | `ch3ins1-trum` | Trumpet (57) | 0 | 5 | 7 | `123eba3e75aad50f` | op0: ml=4 tl=27 ar=15 dr=8 sl=2 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=21 ar=28 dr=11 sl=15 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=21 ar=31 dr=7 sl=15 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=8 tl=19 ar=31 dr=7 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 5 | `ch3ins2-sanmi` | Shamisen (107) | 0 | 2 | 4 | `2cb947fcefb07d7d` | op0: ml=12 tl=25 ar=24 dr=14 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=2 tl=35 ar=31 dr=9 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=10 sl=1 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=15 ar=31 dr=10 sl=12 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 6 | `ch3ins3-sanmi2` | Shamisen (107) | 0 | 0 | 2 | `e54dd50c2ee4b0fc` | op0: ml=8 tl=30 ar=31 dr=10 sl=10 rr=12 dt=2 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=34 ar=31 dr=15 sl=12 rr=12 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=8 tl=31 ar=31 dr=12 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=4 tl=13 ar=31 dr=12 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 7 | `ch4ins1-acid bass` | Synth Bass 1 (39) | 0 | 0 | 2 | `04d2e94f6a4f0e50` | op0: ml=0 tl=19 ar=22 dr=12 sl=12 rr=15 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=0 tl=16 ar=25 dr=10 sl=15 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=2 tl=33 ar=26 dr=13 sl=14 rr=15 dt=1 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=1 tl=16 ar=31 dr=3 sl=8 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 8 | `ch4ins2-sanmi` | Shamisen (107) | 0 | 2 | 4 | `bfbc9d008b9579fe` | op0: ml=12 tl=25 ar=24 dr=14 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=2 tl=35 ar=31 dr=9 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=10 sl=1 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=18 ar=31 dr=10 sl=12 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 9 | `ch5ins1-bd` | Bass Drum 1 (perc 36) | 0 | 5 | 2 | `cb6f7eec392ac96f` | op0: ml=1 tl=11 ar=25 dr=16 sl=3 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=17 ar=31 dr=15 sl=3 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=1 tl=21 ar=31 dr=14 sl=10 rr=15 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=1 tl=17 ar=31 dr=15 sl=2 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 10 | `ch5ins2-hh` | Closed Hi-Hat (perc 42) | 0 | 3 | 6 | `056a53811ae50a12` | op0: ml=14 tl=15 ar=28 dr=0 sl=5 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=8 tl=16 ar=29 dr=16 sl=9 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=14 ar=28 dr=13 sl=15 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=10 tl=13 ar=31 dr=19 sl=6 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 11 | `ch5ins3-sd` | Acoustic Snare (perc 38) | 0 | 5 | 6 | `5eb092e9362bb342` | op0: ml=15 tl=8 ar=25 dr=13 sl=5 rr=12 dt=2 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=9 tl=18 ar=31 dr=14 sl=1 rr=12 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=9 tl=21 ar=29 dr=14 sl=1 rr=12 dt=1 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=9 tl=13 ar=31 dr=13 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 12 | `ch5ins4-pad2` | Pad 2 (warm) (90) | 0 | 4 | 2 | `89894146583c5475` | op0: ml=14 tl=32 ar=22 dr=13 sl=10 rr=12 dt=2 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=32 ar=22 dr=13 sl=12 rr=12 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=17 ar=31 dr=15 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=4 tl=13 ar=31 dr=15 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 13 | `ch6ins1-acid bass` | Synth Bass 1 (39) | 0 | 0 | 2 | `2a690374eaff04a5` | op0: ml=0 tl=19 ar=22 dr=12 sl=12 rr=15 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=0 tl=16 ar=25 dr=10 sl=15 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=2 tl=33 ar=26 dr=13 sl=14 rr=15 dt=1 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=1 tl=24 ar=31 dr=3 sl=8 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 14 | `ch6ins2-sanmi` | Shamisen (107) | 0 | 2 | 4 | `978971a12b3076b7` | op0: ml=12 tl=25 ar=24 dr=14 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=2 tl=35 ar=31 dr=9 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=10 sl=1 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=26 ar=31 dr=10 sl=12 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 15 | `ch7ins1-pad sq` | Lead 1 (square) (81) | 0 | 2 | 6 | `0d061b136bc2eefd` | op0: ml=4 tl=29 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=31 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=24 dr=4 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 16 | `ch7ins2-vib` | Vibraphone (12) | 0 | 0 | 0 | `3f6ea3537308f9f4` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 17 | `ch8ins1-pad sq` | Lead 1 (square) (81) | 0 | 2 | 6 | `0d061b136bc2eefd` | op0: ml=4 tl=29 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=31 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=24 dr=4 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 18 | `ch8ins2-vib` | Vibraphone (12) | 0 | 0 | 0 | `3f6ea3537308f9f4` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |

## YM2413 目标侧（type 13）

| # | 标签 | GM 音色 | preset | alg | fb | 指纹 | 参数 |
|---|---|---|---|---|---|---|---|
| 19 | `ch1ins1-pad sq` | Lead 1 (square) (81) | 5 | 0 | 3 | `b948f06873152e69` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 20 | `ch1ins2-vibraphone` | Vibraphone (12) | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 21 | `ch2ins1-trumpet` | Trumpet (57) | 7 | 0 | 0 | `203029e11972d7d3` | op0: ml=3 tl=25 ar=15 dr=2 sl=2 rr=1 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=13 dr=4 sl=2 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 22 | `ch2ins2-clean vibraphone` | Vibraphone (12) | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 23 | `ch3ins1-trum` | Trumpet (57) | 7 | 0 | 0 | `203029e11972d7d3` | op0: ml=3 tl=25 ar=15 dr=2 sl=2 rr=1 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=13 dr=4 sl=2 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 24 | `ch3ins2-clean guitar` | Electric Guitar (clean) (28) | 2 | 0 | 3 | `10239b9037cc9a83` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 25 | `ch3ins3-guitar` | Electric Guitar (jazz) (27) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 26 | `ch4ins1-synth bass` | Synth Bass 1 (39) | 10 | 0 | 0 | `47ed160944acebc3` | op0: ml=1 tl=11 ar=8 dr=5 sl=8 rr=1 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=0 sl=0 rr=7 dt=0 ksr=0 am=0 vib=0 egt=1 ksl=0 sus=0 kvs=2 |
| 27 | `ch4ins2-clean guitar2` | Electric Guitar (clean) (28) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 28 | `ch5ins1-bd` | Bass Drum 1 (perc 36) | 14 | 0 | 7 | `fbeb787c1c42118d` | op0: ml=1 tl=27 ar=15 dr=3 sl=15 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=8 dr=3 sl=15 rr=15 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 29 | `ch5ins2-hh` | Closed Hi-Hat (perc 42) | 2 | 0 | 7 | `8a233a658b948e1c` | op0: ml=1 tl=27 ar=15 dr=3 sl=15 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=3 sl=15 rr=15 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 30 | `ch5ins3-sd` | Acoustic Snare (perc 38) | 14 | 0 | 7 | `fbeb787c1c42118d` | op0: ml=1 tl=27 ar=15 dr=3 sl=15 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=8 dr=3 sl=15 rr=15 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 31 | `ch5ins4-pad2` | Pad 2 (warm) (90) | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 32 | `ch6ins1-syn bass` | Synth Bass 1 (39) | 10 | 0 | 0 | `47ed160944acebc3` | op0: ml=1 tl=11 ar=8 dr=5 sl=8 rr=1 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=0 sl=0 rr=7 dt=0 ksr=0 am=0 vib=0 egt=1 ksl=0 sus=0 kvs=2 |
| 33 | `ch6ins2-clean guitar` | Electric Guitar (clean) (28) | 2 | 0 | 3 | `10239b9037cc9a83` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 34 | `ch7ins1-pad sine` | Pad 1 (new age) (89) | 5 | 0 | 3 | `b948f06873152e69` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 35 | `ch7ins2-light vibraphone` | Vibraphone (12) | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 36 | `ch8ins1-pad sq2` | Lead 1 (square) (81) | 5 | 0 | 3 | `b948f06873152e69` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 37 | `ch8ins2-dark vibraphone` | Vibraphone (12) | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |

## 明文参数清单

```text
[00] ch1ins1-pad      YM2151  Pad                        preset=0  alg=2 fb=6 fms=0 ams=0 ops=4 fp=2de2a59c97e16bbe
     op0 ml=4  tl=29  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=4  tl=31  ar=24 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=32  ar=24 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=13  ar=24 dr=4  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[01] ch1ins2-celesta  YM2151  Celesta                    preset=0  alg=0 fb=0 fms=0 ams=0 ops=4 fp=b0136ef80269e73d
     op0 ml=14 tl=31  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=14 tl=39  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=14 tl=30  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=13  ar=31 dr=12 sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[02] ch2ins1-trum     YM2151  Trumpet                    preset=0  alg=5 fb=7 fms=0 ams=0 ops=4 fp=123eba3e75aad50f
     op0 ml=4  tl=27  ar=15 dr=8  sl=2  rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=4  tl=21  ar=28 dr=11 sl=15 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=21  ar=31 dr=7  sl=15 rr=10 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=8  tl=19  ar=31 dr=7  sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[03] ch2ins2-vib      YM2151  Vibraphone                 preset=0  alg=0 fb=0 fms=0 ams=0 ops=4 fp=b0136ef80269e73d
     op0 ml=14 tl=31  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=14 tl=39  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=14 tl=30  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=13  ar=31 dr=12 sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[04] ch3ins1-trum     YM2151  Trumpet                    preset=0  alg=5 fb=7 fms=0 ams=0 ops=4 fp=123eba3e75aad50f
     op0 ml=4  tl=27  ar=15 dr=8  sl=2  rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=4  tl=21  ar=28 dr=11 sl=15 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=21  ar=31 dr=7  sl=15 rr=10 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=8  tl=19  ar=31 dr=7  sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[05] ch3ins2-sanmi    YM2151  Shamisen                   preset=0  alg=2 fb=4 fms=0 ams=0 ops=4 fp=2cb947fcefb07d7d
     op0 ml=12 tl=25  ar=24 dr=14 sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=2  tl=35  ar=31 dr=9  sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=32  ar=24 dr=10 sl=1  rr=10 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=15  ar=31 dr=10 sl=12 rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[06] ch3ins3-sanmi2   YM2151  Shamisen                   preset=0  alg=0 fb=2 fms=0 ams=0 ops=4 fp=e54dd50c2ee4b0fc
     op0 ml=8  tl=30  ar=31 dr=10 sl=10 rr=12 dt=2  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=4  tl=34  ar=31 dr=15 sl=12 rr=12 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=8  tl=31  ar=31 dr=12 sl=1  rr=12 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=4  tl=13  ar=31 dr=12 sl=1  rr=12 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[07] ch4ins1-acid bass YM2151  Acid Bass                  preset=0  alg=0 fb=2 fms=0 ams=0 ops=4 fp=04d2e94f6a4f0e50
     op0 ml=0  tl=19  ar=22 dr=12 sl=12 rr=15 dt=5  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=0  tl=16  ar=25 dr=10 sl=15 rr=12 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=2  tl=33  ar=26 dr=13 sl=14 rr=15 dt=1  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=1  tl=16  ar=31 dr=3  sl=8  rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[08] ch4ins2-sanmi    YM2151  Shamisen                   preset=0  alg=2 fb=4 fms=0 ams=0 ops=4 fp=bfbc9d008b9579fe
     op0 ml=12 tl=25  ar=24 dr=14 sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=2  tl=35  ar=31 dr=9  sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=32  ar=24 dr=10 sl=1  rr=10 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=18  ar=31 dr=10 sl=12 rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[09] ch5ins1-bd       YM2151  Bass Drum                  preset=0  alg=5 fb=2 fms=0 ams=0 ops=4 fp=cb6f7eec392ac96f
     op0 ml=1  tl=11  ar=25 dr=16 sl=3  rr=15 dt=3  dt2=1  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=1  tl=17  ar=31 dr=15 sl=3  rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=1  tl=21  ar=31 dr=14 sl=10 rr=15 dt=5  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=1  tl=17  ar=31 dr=15 sl=2  rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[10] ch5ins2-hh       YM2151  Hi-Hat                     preset=0  alg=3 fb=6 fms=0 ams=0 ops=4 fp=056a53811ae50a12
     op0 ml=14 tl=15  ar=28 dr=0  sl=5  rr=12 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=8  tl=16  ar=29 dr=16 sl=9  rr=12 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=14 tl=14  ar=28 dr=13 sl=15 rr=12 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=10 tl=13  ar=31 dr=19 sl=6  rr=12 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[11] ch5ins3-sd       YM2151  Snare Drum                 preset=0  alg=5 fb=6 fms=0 ams=0 ops=4 fp=5eb092e9362bb342
     op0 ml=15 tl=8   ar=25 dr=13 sl=5  rr=12 dt=2  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=9  tl=18  ar=31 dr=14 sl=1  rr=12 dt=5  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=9  tl=21  ar=29 dr=14 sl=1  rr=12 dt=1  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=9  tl=13  ar=31 dr=13 sl=1  rr=12 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[12] ch5ins4-pad2     YM2151  Pad (second)               preset=0  alg=4 fb=2 fms=0 ams=0 ops=4 fp=89894146583c5475
     op0 ml=14 tl=32  ar=22 dr=13 sl=10 rr=12 dt=2  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=14 tl=32  ar=22 dr=13 sl=12 rr=12 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=17  ar=31 dr=15 sl=1  rr=12 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=4  tl=13  ar=31 dr=15 sl=1  rr=12 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[13] ch6ins1-acid bass YM2151  Acid Bass                  preset=0  alg=0 fb=2 fms=0 ams=0 ops=4 fp=2a690374eaff04a5
     op0 ml=0  tl=19  ar=22 dr=12 sl=12 rr=15 dt=5  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=0  tl=16  ar=25 dr=10 sl=15 rr=12 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=2  tl=33  ar=26 dr=13 sl=14 rr=15 dt=1  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=1  tl=24  ar=31 dr=3  sl=8  rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[14] ch6ins2-sanmi    YM2151  Shamisen                   preset=0  alg=2 fb=4 fms=0 ams=0 ops=4 fp=978971a12b3076b7
     op0 ml=12 tl=25  ar=24 dr=14 sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=2  tl=35  ar=31 dr=9  sl=12 rr=10 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=32  ar=24 dr=10 sl=1  rr=10 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=26  ar=31 dr=10 sl=12 rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[15] ch7ins1-pad sq   YM2151  Square Pad                 preset=0  alg=2 fb=6 fms=0 ams=0 ops=4 fp=0d061b136bc2eefd
     op0 ml=4  tl=29  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=4  tl=31  ar=24 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=32  ar=24 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=21  ar=24 dr=4  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[16] ch7ins2-vib      YM2151  Vibraphone                 preset=0  alg=0 fb=0 fms=0 ams=0 ops=4 fp=3f6ea3537308f9f4
     op0 ml=14 tl=31  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=14 tl=39  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=14 tl=30  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=21  ar=31 dr=12 sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[17] ch8ins1-pad sq   YM2151  Square Pad                 preset=0  alg=2 fb=6 fms=0 ams=0 ops=4 fp=0d061b136bc2eefd
     op0 ml=4  tl=29  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=4  tl=31  ar=24 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=4  tl=32  ar=24 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=21  ar=24 dr=4  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[18] ch8ins2-vib      YM2151  Vibraphone                 preset=0  alg=0 fb=0 fms=0 ams=0 ops=4 fp=3f6ea3537308f9f4
     op0 ml=14 tl=31  ar=22 dr=8  sl=15 rr=15 dt=4  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=14 tl=39  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op2 ml=14 tl=30  ar=31 dr=3  sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op3 ml=2  tl=21  ar=31 dr=12 sl=15 rr=15 dt=3  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[19] ch1ins1-pad sq   YM2413  Square Pad                 preset=5  alg=0 fb=3 fms=0 ams=0 ops=2 fp=b948f06873152e69
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
[20] ch1ins2-vibraphone YM2413  Vibraphone                 preset=12 alg=0 fb=3 fms=0 ams=0 ops=2 fp=e213096f8b505662
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[21] ch2ins1-trumpet  YM2413  Trumpet                    preset=7  alg=0 fb=0 fms=0 ams=0 ops=2 fp=203029e11972d7d3
     op0 ml=3  tl=25  ar=15 dr=2  sl=2  rr=1  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=13 dr=4  sl=2  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[22] ch2ins2-clean vibraphone YM2413  Clean Vibraphone           preset=12 alg=0 fb=3 fms=0 ams=0 ops=2 fp=e213096f8b505662
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 5 12
[23] ch3ins1-trum     YM2413  Trumpet                    preset=7  alg=0 fb=0 fms=0 ams=0 ops=2 fp=203029e11972d7d3
     op0 ml=3  tl=25  ar=15 dr=2  sl=2  rr=1  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=13 dr=4  sl=2  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[24] ch3ins2-clean guitar YM2413  Clean Electric Guitar      preset=2  alg=0 fb=3 fms=0 ams=0 ops=2 fp=10239b9037cc9a83
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 14 11
[25] ch3ins3-guitar   YM2413  Electric Guitar            preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 2 11
[26] ch4ins1-synth bass YM2413  Synth Bass                 preset=10 alg=0 fb=0 fms=0 ams=0 ops=2 fp=47ed160944acebc3
     op0 ml=1  tl=11  ar=8  dr=5  sl=8  rr=1  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=0  sl=0  rr=7  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=1 ksl=0 ws=1 sus=0 kvs=2
[27] ch4ins2-clean guitar2 YM2413  Clean Electric Guitar 2    preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 3 11
[28] ch5ins1-bd       YM2413  Bass Drum                  preset=14 alg=0 fb=7 fms=0 ams=0 ops=2 fp=fbeb787c1c42118d
     op0 ml=1  tl=27  ar=15 dr=3  sl=15 rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=8  dr=3  sl=15 rr=15 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15 15 15 15 15 15 15 13 10 8 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
     arp    speed=1  delay=0  loop=255 rel=255 vals: 0 -5 -10 -14 -17 -20 -23 -26 -28 -30 -32 -34 -36 -37 -38 -39 -41
     wave   speed=1  delay=0  loop=255 rel=255 vals: 10 4 6
[29] ch5ins2-hh       YM2413  Hi-Hat                     preset=2  alg=0 fb=7 fms=0 ams=0 ops=2 fp=8a233a658b948e1c
     op0 ml=1  tl=27  ar=15 dr=3  sl=15 rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=3  sl=15 rr=15 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 10 8 5 3 2 0
     wave   speed=1  delay=0  loop=255 rel=255 vals: 11 4
[30] ch5ins3-sd       YM2413  Snare Drum                 preset=14 alg=0 fb=7 fms=0 ams=0 ops=2 fp=fbeb787c1c42118d
     op0 ml=1  tl=27  ar=15 dr=3  sl=15 rr=15 dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=8  dr=3  sl=15 rr=15 dt=6  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 12 15 14 12 9 6 3 2 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
     arp    speed=1  delay=0  loop=255 rel=255 vals: 0 -5 -10 -14 -17 -20 -23 -26 -28 -30 -32 -34 -36 -37 -38 -39 -41
     wave   speed=1  delay=0  loop=255 rel=255 vals: 8 4 4 4
[31] ch5ins4-pad2     YM2413  Pad (second)               preset=14 alg=0 fb=3 fms=0 ams=0 ops=2 fp=bd0232348f1c8cc9
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 3 13
[32] ch6ins1-syn bass YM2413  Synth Bass                 preset=10 alg=0 fb=0 fms=0 ams=0 ops=2 fp=47ed160944acebc3
     op0 ml=1  tl=11  ar=8  dr=5  sl=8  rr=1  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=0  sl=0  rr=7  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=1 ksl=0 ws=1 sus=0 kvs=2
[33] ch6ins2-clean guitar YM2413  Clean Electric Guitar      preset=2  alg=0 fb=3 fms=0 ams=0 ops=2 fp=10239b9037cc9a83
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 14 11
[34] ch7ins1-pad sine YM2413  Sine Pad                   preset=5  alg=0 fb=3 fms=0 ams=0 ops=2 fp=b948f06873152e69
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 4 5
[35] ch7ins2-light vibraphone YM2413  Light Vibraphone           preset=12 alg=0 fb=3 fms=0 ams=0 ops=2 fp=e213096f8b505662
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 1 12
[36] ch8ins1-pad sq2  YM2413  Square Pad 2               preset=5  alg=0 fb=3 fms=0 ams=0 ops=2 fp=b948f06873152e69
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 14 5
[37] ch8ins2-dark vibraphone YM2413  Dark Vibraphone            preset=12 alg=0 fb=3 fms=0 ams=0 ops=2 fp=e213096f8b505662
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
     vol    speed=1  delay=0  loop=255 rel=255 vals: 0 15
     wave   speed=1  delay=0  loop=255 rel=255 vals: 9 12
```

---

## 5. 这份库怎么更新

```bash
# 改完 out/Spindizzy_II/31 Grass Maze/2151-2413.fur 之后，同时刷新文档与 JSON
cli/build/fuitool.exe lib out/Spindizzy_II/31 Grass Maze/2151-2413.fur --json=docs/instrument-library.json
#   stdout  = 本文件 §4 的 Markdown（表格 + 明文参数清单）
#   --json  = docs/instrument-library.json（机器可读、可 diff）

# 只想看工程里有什么（含指纹）
cli/build/fuitool.exe fur out/Spindizzy_II/31 Grass Maze/2151-2413.fur

# 看单个音色的完整参数 + 宏 + 指纹
cli/build/fuitool.exe dump out/Spindizzy_II/31 Grass Maze/fui/ch1ins1_ym2413.fui

# 把工程里的音色逐个解包成 .fui（**文件名 = Furnace 里的乐器全名**）
cli/build/fuitool.exe unpack out/Spindizzy_II/31 Grass Maze/2151-2413.fur \
    out/Spindizzy_II/31 Grass Maze/fui/from_fur --type=13
#   --type=13 只解 OPLL（YM2413），33 = OPM，不给就全解
#   --tagged  文件名只留 chNinsM 前缀（去掉 note:/vol: 等参数）
#   文件名里的 ':' 会换成 '_'（Windows 不允许）
```

`docs/instrument-library.json` 的结构：

```jsonc
{
  "format": "vgm-studio/instrument-library",
  "version": 2,
  "source": "out/Spindizzy_II/31 Grass Maze/2151-2413.fur",
  "furVersion": 228, "hz": 60.0,
  "instruments": [
    {
      "index": 19, "name": "ch1ins1", "type": 13, "chip": "YM2413",
      "opllPreset": 5, "alg": 0, "fb": 3, "fms": 0, "ams": 0, "ops": 2,
      "operators": [ {"index":0,"mult":1,"tl":21,"ar":14,...}, ... ],
      "macros":    [ {"macro":0,"type":0,"values":[0,15],"speed":1,...} ],
      "fingerprint": "b948f06873152e69",
      "signature":   "t13;preset5;alg0;fb3;fms0;ams0;ops2|0:1,21,14,..."
    }
  ]
}
```

`operators` 里是**明文数字**，任何工具都能直接读，不必解析 `.fui` 二进制。
