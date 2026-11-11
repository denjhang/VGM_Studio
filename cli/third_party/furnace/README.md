# 第三方：Furnace 的 `.fui` 读写

## 这是什么

从 [Furnace Tracker](https://github.com/tildearrow/furnace)（GPL-2.0+）**原样复制**的一组源码，
只为了让本工程能读写 `.fui` 乐器文件，而不必自己实现一遍 Furnace 的二进制格式。

| 文件 | 来源（furnace 仓库） | 作用 |
|---|---|---|
| `src/engine/instrument.cpp` | `src/engine/instrument.cpp` | **核心**：`DivInstrument::putInsData2` / `readInsData` |
| `src/engine/instrument.h` | `src/engine/instrument.h` | `DivInstrument` / `DivInstrumentFM` / `DivInstrumentMacro` |
| `src/engine/safeWriter.{h,cpp}` | 同名 | 小端二进制写入 |
| `src/engine/safeReader.{h,cpp}` | 同名 | 小端二进制读取 |
| `src/engine/{defines,dataErrors,config,export,sysDef,effect,cmdStream,filePlayer,song,dispatch,sample,wavetable,blip_buf,sfWrapper}.h` | 同名 | 只为了满足 `instrument.h` 的 include 链 |
| `src/{pch,ta-utils,ta-log,fixedQueue,timeutils,fileutils}.h` | 同名 | 同上 |
| `src/audio/taAudio.h` | 同名 | 同上（被 `engine.h` 引入） |
| `stubs.cpp` | **本工程新写** | 补上采样/波表/日志的空实现 |

同步的 Furnace 版本：`dev258-16-gd74e7a01a`（`DIV_ENGINE_VERSION = 258`）。

## 为什么必须用它的代码

`.fui` 的结构是 `"FINS"` 容器 + 若干带长度前缀的特性块（`NA` 名字、`FM` 音色、`MA` 宏…），
每块的字段顺序、位宽、以及「哪些字段在哪个引擎版本之后才存在」都由 `DIV_ENGINE_VERSION` 控制。
自己写解析器 = 每次 Furnace 升级都要重写一遍。直接编译它的 `instrument.cpp` 就没有这个问题。

## `stubs.cpp` 是什么

`instrument.cpp` 里有几处会调用其它子系统的符号（采样 `DivSample`、波表 `DivWavetable`、日志 `writeLog`）。
**FM 音色 + 宏这条路径完全不碰它们**，所以给空实现即可，不改变 `.fui` 的字节布局。
`stubs.cpp` 里的每个空函数都在注释里标了它属于哪个子系统。

## 许可

**GPL-2.0+（传染）**。`cli/CMakeLists.txt` 把 `furnace_fui` 链进 `vgmcore`，
因此**本工程整体应按 GPL-2.0+ 分发**（`docs/RULES.md` 的「许可前提」一节早有记录）。
如果将来要改为宽松许可，唯一的办法是把 `FuiVoice.cpp` 里的编解码换成自己实现的解析器
——那时必须自己跟踪 `DIV_ENGINE_VERSION` 的全部历史差异。

## 我们自己的部分

`cli/src/FuiVoice.{h,cpp}` 只做三件事，都不改 Furnace 的代码：

1. `EncodeFui` / `DecodeFui`：`FuiVoice` 结构体 ⇄ `DivInstrument` 的字段搬运
2. `MacroRunner`：宏的三种模式（序列 / ADSR / LFO）的执行器，
   逐行对应 `furnace/src/engine/macroInt.cpp:83-296` 的 `DivMacroStruct::doMacro`
3. `OpmShadowToFui` / `FuiToOpmShadow`、`OpllUserVoiceToFui` / `FuiToOpllUserVoice`：
   YM2151 / YM2413 影子寄存器 ⇄ `FuiVoice` 的语义映射
   （依据 `arcade.cpp:562-602` 与 `opll.cpp:453-466` 的寄存器打包顺序）

## ★ 宏的速率不在 `.fui` 里

这是最容易误解的一点：

| 存在哪 | 内容 |
|---|---|
| `.fui` 里的宏 | `speed`（每步隔几个 tick）、`delay`（首步额外等几个 tick）、`len/loop/rel`、模式（序列/ADSR/LFO） |
| `.fur` 工程里 | `divSong::hz`（Furnace 里叫 `tickRate`，默认 60）—— **一秒有多少个 tick** |

Furnace 的宏解释器 `DivMacroInt::next()` 由宿主引擎的 tick 推动
（`engine.cpp:2227 divider=curSubSong->hz`），**乐器文件本身不定义中断速率**。

所以在我们的工具里：

- `fuitool dump --tick-rate=N`：`N` 就是那个中断速率，只影响打印出来的毫秒数
- `MacroRunner::Start(m, tickRate)`：一个宏步的绝对时长 = `speed / tickRate` 秒
- 一个宏步「折算成多少个采样」= `tickRate` 与 `timeBase.sampleRate` 的比值
