# 乐器库 —— 标签、参数、指纹

这个文件是**本地乐器库**：曲目用到的每一个音色的「人读标签 + 完整 FM 参数 + 指纹」。
§4 的数据由 `fuitool lib` 从 `out/2151-2413.fur` 自动生成，§1–§3 是手写说明。

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
下表把每个缩写展开成完整英文名，并给出最接近的 General MIDI 分类
（GM program 是官方 1..128 编号；打击乐在 GM 里是通道 10 的固定音符号，不是 program）。

| 标签 | 完整名 | GM 分类 |
|---|---|---|
| `pad` | Pad（铺底 / 长音） | Pad 1 (new age)，GM 89 |
| `pad sq` | Square Pad（方波铺底） | Lead 1 (square)，GM 81 |
| `pad2` | Pad，第二个 | Pad 2 (warm)，GM 90 |
| `celesta` | Celesta（钢片琴，衰减快、偏亮） | Celesta，GM 9 |
| `trum` | Trumpet（小号，铜管、起音硬） | Trumpet，GM 57 |
| `vib` | Vibraphone（颤音琴，带音高颤动的金属音） | Vibraphone，GM 12 |
| `sanmi` | Shamisen（三味线，拨弦） | Shamisen，GM 107 |
| `acid bass` | Acid Bass（酸性贝斯，共振重、滑音多） | Synth Bass 1，GM 39 |
| `bd` | Bass Drum（低音鼓 / 贝斯鼓） | Bass Drum 1，打击乐音符 36 |
| `hh` | Hi-Hat（踩镲） | Closed Hi-Hat，打击乐音符 42 |
| `sd` | Snare Drum（军鼓） | Acoustic Snare，打击乐音符 38 |
| `ch1ins2` | 第 1 个源通道（YM2151 的 ch1）的第 2 个音色 | — |

序号由 `opm2opll` 按音色首次出现顺序编。

**同一标签在源侧和目标侧成对出现**：`ch1ins1-pad`（YM2151，type 33）是源音色，
`ch1ins1`（YM2413，type 13）是它转换后的目标音色。目标侧名字不带标签，
GM 分类从同名的源侧乐器继承。`opm2opll --fur` 也按同名取目标音色。

---

## 3. 转换映射（源 → 目标 → GM）

`opm2opll` 的匹配结果。`program` 是 YM2413 的 ROM 预置号（0 = 唯一可写的用户槽）。

三列名字各有各的意思，别混：
- **标签 / 完整名**：曲目里这个乐器是什么（按 FM 参数 + 你打的标判断）
- **ROM 预置**：YM2413 芯片上实际播放的那一号音色
- **GM**：那个预置在 General MIDI 里最接近的对应物

| 标签 | 完整名 | 源 alg/fb | program | ROM 预置名 | GM 音色 | 音符数 |
|---|---|---|---|---|---|---|
| `ch1ins1-pad` | Pad | 2 / 6 | 5 | Clarinet | Clarinet (GM 72) | 318 |
| `ch1ins2-celesta` | Celesta | 0 / 0 | 12 | Vibraphone | Vibraphone (GM 12) | 240 |
| `ch2ins1-trum` | Trumpet | 5 / 7 | 7 | Trumpet | Trumpet (GM 57) | 9 |
| `ch3ins1-trum` | Trumpet | 5 / 7 | 3 | Piano | Acoustic Grand Piano (GM 1) | 18 |
| `ch3ins2-sanmi` | Shamisen | 2 / 4 | 15 | Electric Guitar | Electric Guitar (clean) (GM 28) | 487 |
| `ch4ins1-acid bass` | Acid Bass | 0 / 2 | 10 | Synthesizer | Synth Bass 1 (GM 39) | 422 |
| `ch5ins1-bd` | Bass Drum | 5 / 2 | 2 | Guitar | Bass Drum 1 (perc 36) | 348 |
| `ch5ins4-pad2` | Pad | 4 / 2 | 14 | Acoustic Bass | Acoustic Bass (GM 33) | 673 |

其余 11 个音色与上表同 program 的乐器**共用**同一份 ROM 预置
（指纹相同 = 同一音色 = 同一预置）。

**为什么标签和 ROM 预置对不上**：`pad` 落到 Clarinet、`bd` 落到 Guitar——
YM2413 是 2 算子芯片，没有真正的鼓、没有真正的铺底，只能拿 ROM 里最接近的顶替。
这是芯片能力受限的妥协，不是识别错误。

---

## 4. 参数表与明文清单（自动生成）

生成命令：`cli/build/fuitool.exe lib out/2151-2413.fur --json=docs/instrument-library.json`

<!-- 以下内容由 `fuitool lib out/2151-2413.fur` 自动生成，勿手改 -->

来源 `out/2151-2413.fur`，Furnace 版本 228，hz=60.00，共 38 个乐器。

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
| 19 | `ch1ins1` | Pad 1 (new age) (89) | 5 | 0 | 3 | `b948f06873152e69` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 20 | `ch1ins2` | Celesta (9) | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 21 | `ch2ins1` | Trumpet (57) | 7 | 0 | 0 | `203029e11972d7d3` | op0: ml=3 tl=25 ar=15 dr=2 sl=2 rr=1 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=13 dr=4 sl=2 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 22 | `ch2ins2-vib` | Vibraphone (12) | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 23 | `ch3ins1` | Trumpet (57) | 3 | 0 | 0 | `8a43a9e9b36fbedf` | op0: ml=3 tl=25 ar=15 dr=2 sl=2 rr=1 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=13 dr=4 sl=2 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 24 | `ch3ins2` | Shamisen (107) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 25 | `ch3ins3` | Shamisen (107) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 26 | `ch4ins1` | Synth Bass 1 (39) | 10 | 0 | 0 | `47ed160944acebc3` | op0: ml=1 tl=11 ar=8 dr=5 sl=8 rr=1 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=0 sl=0 rr=7 dt=0 ksr=0 am=0 vib=0 egt=1 ksl=0 sus=0 kvs=2 |
| 27 | `ch4ins2` | Shamisen (107) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 28 | `ch5ins1` | Bass Drum 1 (perc 36) | 2 | 0 | 5 | `c5e05bfe922f346f` | op0: ml=3 tl=26 ar=13 dr=8 sl=2 rr=3 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=7 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 29 | `ch5ins2` | Closed Hi-Hat (perc 42) | 2 | 0 | 5 | `c5e05bfe922f346f` | op0: ml=3 tl=26 ar=13 dr=8 sl=2 rr=3 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=7 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 30 | `ch5ins3` | Acoustic Snare (perc 38) | 2 | 0 | 5 | `c5e05bfe922f346f` | op0: ml=3 tl=26 ar=13 dr=8 sl=2 rr=3 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=7 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 31 | `ch5ins4` | Pad 2 (warm) (90) | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 32 | `ch6ins1` | Synth Bass 1 (39) | 10 | 0 | 0 | `47ed160944acebc3` | op0: ml=1 tl=11 ar=8 dr=5 sl=8 rr=1 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=0 sl=0 rr=7 dt=0 ksr=0 am=0 vib=0 egt=1 ksl=0 sus=0 kvs=2 |
| 33 | `ch6ins2` | Shamisen (107) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 34 | `ch7ins1` | Lead 1 (square) (81) | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 35 | `ch7ins2` | Vibraphone (12) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 36 | `ch8ins1` | Lead 1 (square) (81) | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 37 | `ch8ins2` | Vibraphone (12) | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |

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
[19] ch1ins1          YM2413  Pad                        preset=5  alg=0 fb=3 fms=0 ams=0 ops=2 fp=b948f06873152e69
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[20] ch1ins2          YM2413  Celesta                    preset=12 alg=0 fb=3 fms=0 ams=0 ops=2 fp=e213096f8b505662
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[21] ch2ins1          YM2413  Trumpet                    preset=7  alg=0 fb=0 fms=0 ams=0 ops=2 fp=203029e11972d7d3
     op0 ml=3  tl=25  ar=15 dr=2  sl=2  rr=1  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=13 dr=4  sl=2  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[22] ch2ins2-vib      YM2413  Vibraphone                 preset=12 alg=0 fb=3 fms=0 ams=0 ops=2 fp=e213096f8b505662
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[23] ch3ins1          YM2413  Trumpet                    preset=3  alg=0 fb=0 fms=0 ams=0 ops=2 fp=8a43a9e9b36fbedf
     op0 ml=3  tl=25  ar=15 dr=2  sl=2  rr=1  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=13 dr=4  sl=2  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[24] ch3ins2          YM2413  Shamisen                   preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[25] ch3ins3          YM2413  Shamisen                   preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[26] ch4ins1          YM2413  Acid Bass                  preset=10 alg=0 fb=0 fms=0 ams=0 ops=2 fp=47ed160944acebc3
     op0 ml=1  tl=11  ar=8  dr=5  sl=8  rr=1  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=0  sl=0  rr=7  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=1 ksl=0 ws=1 sus=0 kvs=2
[27] ch4ins2          YM2413  Shamisen                   preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[28] ch5ins1          YM2413  Bass Drum                  preset=2  alg=0 fb=5 fms=0 ams=0 ops=2 fp=c5e05bfe922f346f
     op0 ml=3  tl=26  ar=13 dr=8  sl=2  rr=3  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=7  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[29] ch5ins2          YM2413  Hi-Hat                     preset=2  alg=0 fb=5 fms=0 ams=0 ops=2 fp=c5e05bfe922f346f
     op0 ml=3  tl=26  ar=13 dr=8  sl=2  rr=3  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=7  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[30] ch5ins3          YM2413  Snare Drum                 preset=2  alg=0 fb=5 fms=0 ams=0 ops=2 fp=c5e05bfe922f346f
     op0 ml=3  tl=26  ar=13 dr=8  sl=2  rr=3  dt=0  dt2=0  ksr=1 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=7  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[31] ch5ins4          YM2413  Pad (second)               preset=14 alg=0 fb=3 fms=0 ams=0 ops=2 fp=bd0232348f1c8cc9
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[32] ch6ins1          YM2413  Acid Bass                  preset=10 alg=0 fb=0 fms=0 ams=0 ops=2 fp=47ed160944acebc3
     op0 ml=1  tl=11  ar=8  dr=5  sl=8  rr=1  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=1 sus=0 kvs=2
     op1 ml=1  tl=0   ar=15 dr=0  sl=0  rr=7  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=1 ksl=0 ws=1 sus=0 kvs=2
[33] ch6ins2          YM2413  Shamisen                   preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[34] ch7ins1          YM2413  Square Pad                 preset=14 alg=0 fb=3 fms=0 ams=0 ops=2 fp=bd0232348f1c8cc9
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[35] ch7ins2          YM2413  Vibraphone                 preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[36] ch8ins1          YM2413  Square Pad                 preset=14 alg=0 fb=3 fms=0 ams=0 ops=2 fp=bd0232348f1c8cc9
     op0 ml=1  tl=21  ar=14 dr=4  sl=0  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=1 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=9  dr=0  sl=0  rr=2  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
[37] ch8ins2          YM2413  Vibraphone                 preset=15 alg=0 fb=3 fms=0 ams=0 ops=2 fp=91795c1a5f0eccbd
     op0 ml=1  tl=9   ar=15 dr=1  sl=12 rr=0  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=2 ws=0 sus=0 kvs=2
     op1 ml=1  tl=0   ar=14 dr=4  sl=1  rr=3  dt=0  dt2=0  ksr=0 am=0 vib=0 egt=0 ksl=0 ws=0 sus=0 kvs=2
```

---

## 5. 这份库怎么更新

```bash
# 改完 out/2151-2413.fur 之后，同时刷新文档与 JSON
cli/build/fuitool.exe lib out/2151-2413.fur --json=docs/instrument-library.json
#   stdout  = 本文件 §4 的 Markdown（表格 + 明文参数清单）
#   --json  = docs/instrument-library.json（机器可读、可 diff）

# 只想看工程里有什么（含指纹）
cli/build/fuitool.exe fur out/2151-2413.fur

# 看单个音色的完整参数 + 宏 + 指纹
cli/build/fuitool.exe dump out/ch1ins1_ym2413.fui
```

`docs/instrument-library.json` 的结构：

```jsonc
{
  "format": "vgm-studio/instrument-library",
  "version": 1,
  "source": "out/2151-2413.fur",
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
