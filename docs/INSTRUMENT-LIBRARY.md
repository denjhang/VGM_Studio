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

## 2. 标签怎么读

标签是在 Furnace 里手打的，格式 `<通道><序号>[-缩写]`：

| 片段 | 含义 |
|---|---|
| `ch1ins2` | 第 1 个源通道（YM2151 的 ch1）的第 2 个音色，序号由 `opm2opll` 按首次出现顺序编 |
| `pad` | 铺底 / 长音（Pad） |
| `celesta` | 钢片琴类，衰减快、偏亮 |
| `trum` | 小号（Trumpet），铜管、起音硬 |
| `vib` | 颤音琴（Vibraphone），带音高颤动的金属音 |
| `sanmi` | ⊙ 待确认（见 §5） |
| `acid bass` | 酸性贝斯（Acid Bass），共振重、滑音多 |
| `bd` / `hh` / `sd` | 贝斯鼓 / 踩镲 / 军鼓（Bass Drum / Hi-Hat / Snare Drum） |
| `pad2` | 第二个铺底音色 |
| `pad sq` | 方波铺底（Square 音色底） |

**同一标签在源侧和目标侧成对出现**：`ch1ins1-pad`（YM2151，type 33）是源音色，
`ch1ins1`（YM2413，type 13）是它转换后的目标音色。`opm2opll --fur` 按同名取目标音色。

---

## 3. 转换映射（源 → 目标）

`opm2opll` 的匹配结果。`program` 是 YM2413 的 ROM 预置号（0 = 唯一可写的用户槽）。

| 源标签 | 源 alg/fb | → program | 目标名 | 音符数 |
|---|---|---|---|---|
| `ch1ins1-pad` | 2 / 6 | 5 | Clarinet | 318 |
| `ch1ins2-celesta` | 0 / 0 | 12 | Vibraphone | 240 |
| `ch2ins1-trum` | 5 / 7 | 7 | Trumpet | 9 |
| `ch3ins1-trum` | 5 / 7 | 3 | Piano | 18 |
| `ch3ins2-sanmi` | 2 / 4 | 15 | Electric Guitar | 487 |
| `ch4ins1-acid bass` | 0 / 2 | 10 | Synthesizer | 422 |
| `ch5ins1-bd` | 5 / 2 | 2 | Guitar | 348 |
| `ch5ins4-pad2` | 4 / 2 | 14 | Acoustic Bass | 673 |
| 其余 11 个 | — | 2/10/14/15 | Guitar / Synthesizer / Acoustic Bass / Electric Guitar | 与同 program 的乐器共用 |

鼓组（bd/hh/sd）在 YM2413 上没有真正的鼓，只能拿预置音色顶替，
音色差距是芯片能力问题，不是映射 bug。

---

## 4. 参数表（自动生成）

<!-- 以下表格由 `fuitool lib out/2151-2413.fur` 自动生成，勿手改 -->

来源 `out/2151-2413.fur`，Furnace 版本 228，hz=60.00，共 38 个乐器。

**身份靠参数，不靠指纹。** 指纹只是参数串的哈希，用于快速比较；
参数相同 = 同一音色（哪怕标签名不同）。反之参数不同就是不同音色。

## YM2151 源侧（type 33）

| # | 标签 | preset | alg | fb | 指纹 | 参数 |
|---|---|---|---|---|---|---|
| 0 | `ch1ins1-pad` | 0 | 2 | 6 | `2de2a59c97e16bbe` | op0: ml=4 tl=29 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=31 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=13 ar=24 dr=4 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 1 | `ch1ins2-celesta` | 0 | 0 | 0 | `b0136ef80269e73d` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=13 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 2 | `ch2ins1-trum` | 0 | 5 | 7 | `123eba3e75aad50f` | op0: ml=4 tl=27 ar=15 dr=8 sl=2 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=21 ar=28 dr=11 sl=15 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=21 ar=31 dr=7 sl=15 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=8 tl=19 ar=31 dr=7 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 3 | `ch2ins2-vib` | 0 | 0 | 0 | `b0136ef80269e73d` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=13 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 4 | `ch3ins1-trum` | 0 | 5 | 7 | `123eba3e75aad50f` | op0: ml=4 tl=27 ar=15 dr=8 sl=2 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=21 ar=28 dr=11 sl=15 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=21 ar=31 dr=7 sl=15 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=8 tl=19 ar=31 dr=7 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 5 | `ch3ins2-sanmi` | 0 | 2 | 4 | `2cb947fcefb07d7d` | op0: ml=12 tl=25 ar=24 dr=14 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=2 tl=35 ar=31 dr=9 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=10 sl=1 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=15 ar=31 dr=10 sl=12 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 6 | `ch3ins3-sanmi2` | 0 | 0 | 2 | `e54dd50c2ee4b0fc` | op0: ml=8 tl=30 ar=31 dr=10 sl=10 rr=12 dt=2 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=34 ar=31 dr=15 sl=12 rr=12 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=8 tl=31 ar=31 dr=12 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=4 tl=13 ar=31 dr=12 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 7 | `ch4ins1-acid bass` | 0 | 0 | 2 | `04d2e94f6a4f0e50` | op0: ml=0 tl=19 ar=22 dr=12 sl=12 rr=15 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=0 tl=16 ar=25 dr=10 sl=15 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=2 tl=33 ar=26 dr=13 sl=14 rr=15 dt=1 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=1 tl=16 ar=31 dr=3 sl=8 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 8 | `ch4ins2-sanmi` | 0 | 2 | 4 | `bfbc9d008b9579fe` | op0: ml=12 tl=25 ar=24 dr=14 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=2 tl=35 ar=31 dr=9 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=10 sl=1 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=18 ar=31 dr=10 sl=12 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 9 | `ch5ins1-bd` | 0 | 5 | 2 | `cb6f7eec392ac96f` | op0: ml=1 tl=11 ar=25 dr=16 sl=3 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=17 ar=31 dr=15 sl=3 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=1 tl=21 ar=31 dr=14 sl=10 rr=15 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=1 tl=17 ar=31 dr=15 sl=2 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 10 | `ch5ins2-hh` | 0 | 3 | 6 | `056a53811ae50a12` | op0: ml=14 tl=15 ar=28 dr=0 sl=5 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=8 tl=16 ar=29 dr=16 sl=9 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=14 ar=28 dr=13 sl=15 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=10 tl=13 ar=31 dr=19 sl=6 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 11 | `ch5ins3-sd` | 0 | 5 | 6 | `5eb092e9362bb342` | op0: ml=15 tl=8 ar=25 dr=13 sl=5 rr=12 dt=2 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=9 tl=18 ar=31 dr=14 sl=1 rr=12 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=9 tl=21 ar=29 dr=14 sl=1 rr=12 dt=1 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=9 tl=13 ar=31 dr=13 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 12 | `ch5ins4-pad2` | 0 | 4 | 2 | `89894146583c5475` | op0: ml=14 tl=32 ar=22 dr=13 sl=10 rr=12 dt=2 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=32 ar=22 dr=13 sl=12 rr=12 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=17 ar=31 dr=15 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=4 tl=13 ar=31 dr=15 sl=1 rr=12 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 13 | `ch6ins1-acid bass` | 0 | 0 | 2 | `2a690374eaff04a5` | op0: ml=0 tl=19 ar=22 dr=12 sl=12 rr=15 dt=5 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=0 tl=16 ar=25 dr=10 sl=15 rr=12 dt=6 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=2 tl=33 ar=26 dr=13 sl=14 rr=15 dt=1 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=1 tl=24 ar=31 dr=3 sl=8 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 14 | `ch6ins2-sanmi` | 0 | 2 | 4 | `978971a12b3076b7` | op0: ml=12 tl=25 ar=24 dr=14 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=2 tl=35 ar=31 dr=9 sl=12 rr=10 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=10 sl=1 rr=10 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=26 ar=31 dr=10 sl=12 rr=15 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 15 | `ch7ins1-pad sq` | 0 | 2 | 6 | `0d061b136bc2eefd` | op0: ml=4 tl=29 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=31 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=24 dr=4 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 16 | `ch7ins2-vib` | 0 | 0 | 0 | `3f6ea3537308f9f4` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 17 | `ch8ins1-pad sq` | 0 | 2 | 6 | `0d061b136bc2eefd` | op0: ml=4 tl=29 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=4 tl=31 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=4 tl=32 ar=24 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=24 dr=4 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 18 | `ch8ins2-vib` | 0 | 0 | 0 | `3f6ea3537308f9f4` | op0: ml=14 tl=31 ar=22 dr=8 sl=15 rr=15 dt=4 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=14 tl=39 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op2: ml=14 tl=30 ar=31 dr=3 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op3: ml=2 tl=21 ar=31 dr=12 sl=15 rr=15 dt=3 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |

## YM2413 目标侧（type 13）

| # | 标签 | preset | alg | fb | 指纹 | 参数 |
|---|---|---|---|---|---|---|
| 19 | `ch1ins1` | 5 | 0 | 3 | `b948f06873152e69` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 20 | `ch1ins2` | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 21 | `ch2ins1` | 7 | 0 | 0 | `203029e11972d7d3` | op0: ml=3 tl=25 ar=15 dr=2 sl=2 rr=1 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=13 dr=4 sl=2 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 22 | `ch2ins2-vib` | 12 | 0 | 3 | `e213096f8b505662` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 23 | `ch3ins1` | 3 | 0 | 0 | `8a43a9e9b36fbedf` | op0: ml=3 tl=25 ar=15 dr=2 sl=2 rr=1 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=13 dr=4 sl=2 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 24 | `ch3ins2` | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 25 | `ch3ins3` | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 26 | `ch4ins1` | 10 | 0 | 0 | `47ed160944acebc3` | op0: ml=1 tl=11 ar=8 dr=5 sl=8 rr=1 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=0 sl=0 rr=7 dt=0 ksr=0 am=0 vib=0 egt=1 ksl=0 sus=0 kvs=2 |
| 27 | `ch4ins2` | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 28 | `ch5ins1` | 2 | 0 | 5 | `c5e05bfe922f346f` | op0: ml=3 tl=26 ar=13 dr=8 sl=2 rr=3 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=7 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 29 | `ch5ins2` | 2 | 0 | 5 | `c5e05bfe922f346f` | op0: ml=3 tl=26 ar=13 dr=8 sl=2 rr=3 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=7 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 30 | `ch5ins3` | 2 | 0 | 5 | `c5e05bfe922f346f` | op0: ml=3 tl=26 ar=13 dr=8 sl=2 rr=3 dt=0 ksr=1 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=7 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 31 | `ch5ins4` | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 32 | `ch6ins1` | 10 | 0 | 0 | `47ed160944acebc3` | op0: ml=1 tl=11 ar=8 dr=5 sl=8 rr=1 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=15 dr=0 sl=0 rr=7 dt=0 ksr=0 am=0 vib=0 egt=1 ksl=0 sus=0 kvs=2 |
| 33 | `ch6ins2` | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 34 | `ch7ins1` | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 35 | `ch7ins2` | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 36 | `ch8ins1` | 14 | 0 | 3 | `bd0232348f1c8cc9` | op0: ml=1 tl=21 ar=14 dr=4 sl=0 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=1 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=9 dr=0 sl=0 rr=2 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |
| 37 | `ch8ins2` | 15 | 0 | 3 | `91795c1a5f0eccbd` | op0: ml=1 tl=9 ar=15 dr=1 sl=12 rr=0 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=2 sus=0 kvs=2<br>op1: ml=1 tl=0 ar=14 dr=4 sl=1 rr=3 dt=0 ksr=0 am=0 vib=0 egt=0 ksl=0 sus=0 kvs=2 |


---

## 5. 待确认的缩写

`sanmi` 按「三味线（拨弦）」理解：三个变体（ch3ins2 / ch4ins2 / ch6ins2）
alg=2 fb=4 互为近亲，目标侧统一映射到 program=15（Electric Guitar），拨弦音色对得上。
若理解有误请在 §2 表格里直接改。

`pad sq` 按「方波铺底（Square）」理解：与 `pad` 同 alg/fb=2/6，
只有 op3 的 tl/dt 不同，是同一音色的方波变体。

---

## 6. 这份库怎么更新

```bash
# 改完 out/2151-2413.fur 之后
cli/build/fuitool.exe lib out/2151-2413.fur    # 看 stdout，或替换 §4

# 只想看工程里有什么（含指纹）
cli/build/fuitool.exe fur out/2151-2413.fur

# 看单个音色的完整参数 + 宏 + 指纹
cli/build/fuitool.exe dump out/ch1ins1_ym2413.fui
```
