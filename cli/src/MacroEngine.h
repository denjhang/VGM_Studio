// MacroEngine —— 把 FuiVoice 里的全部宏展开成寄存器写。
//
// 逐行对应 Furnace 的两个平台 tick()：
//   OPM : furnace/src/engine/platform/arcade.cpp:292-480   DivPlatformArcade::tick()
//   OPLL: furnace/src/engine/platform/opll.cpp:150-340     DivPlatformOPLL::tick()
//
// 宏的集合与 Furnace 完全一致：
//   22 条通道级宏（MA 块，instrument.cpp:1221-1242）
//   4 个算子 × 20 条算子级宏（O1..O4 块，instrument.cpp:1341-1360 / readFeatureOx）
//
// ★ 中断速率
//   .fui 只存 speed/delay（每步几个 tick），一秒几个 tick 来自 .fur 的 song.hz。
//   本引擎必须显式接收 tickRate，见 docs/VOICE-FUI.md §5。
#pragma once

#include <cstdint>
#include <vector>

#include "FuiVoice.h"

namespace vgmstudio {

// 一条寄存器写（reg 已含通道号）
struct MacroWrite {
    uint32_t atSample = 0;
    uint8_t reg = 0;
    uint8_t val = 0;
};

// 音高变化点（arp / pitch 宏的输出），单位 1/128 半音，与 Furnace 的 pitch2 一致
struct MacroPitch {
    uint32_t atSample = 0;
    int offset128 = 0;
};

struct MacroExpandInput {
    int ch = 0;                 // 通道号（0..7 OPM / 0..8 OPLL）
    // 基础音高：源寄存器原值（OPM 的 KC/KF），引擎只用来算 arp 的基准 note
    uint8_t baseKc = 0;
    uint8_t baseKf = 0;
    // 基础音量：OPM 是通道音量（0..127，127=最响）；OPLL 是 0..15
    int baseVol = 127;
    uint32_t startSample = 0;
    uint32_t endSample = 0;
    int sampleRate = 44100;
    int tickRate = 60;          // ← 一秒多少 tick（Furnace 的 song.hz）
    // ★ 宏的相位。宏只在真实 key-on 上启动一次；同一次发声被 NoteSplitter
    //   切成多个块（legato 块）时，后续块必须**接续**，不能重新启动。
    //   skipTicks = 从起始 key-on 到本块起点已经过去的 tick 数，
    //   引擎会把这几拍空跑一遍（只推进状态、不输出写）。
    int skipTicks = 0;
    // 本块末尾是否真的结束发声（真实 key-off）。legato 块的末尾不能 release，
    // 否则 ADSR / 带 release 点的序列宏会在半路掉到释放值。
    bool releaseAtEnd = true;
    bool opll = false;          // false = OPM(4-op)，true = OPLL(2-op)
    int opllPreset = 0;         // OPLL：0 = 用户音色（0x00-0x07），≠0 = ROM
    int chVolL = 1, chVolR = 1;
    int alg = 0, fb = 0, fms = 0, ams = 0;
};

// 重触发点（phaseReset 宏 = 1，或 OPM 的 ex4 改了算子掩码）
struct MacroRetrigger {
    uint32_t atSample = 0;
    int opMask = 0x0F;   // OPM 的 0x08 数据（slot 掩码）；OPLL 忽略，只重写 key 位
};

// OPLL 专用：alg 宏在 OPLL 上没有 ALG 寄存器，它驱动的是 0x20 的 bit5（SUS / EG-S），
// 该位在每次写 0x20（key-on / 频率变化）时随 fnum 高位一起吐出（opll.cpp:258）。
// Furnace 只在 preset==0 时额外置 freqChanged（opll.cpp:207-209），也就是「立刻重写」；
// ROM 预置下不立刻写，要等下一个 key-on / freq 变化才生效。
struct MacroSus {
    uint32_t atSample = 0;
    int sus = 0;            // 0 = 清 bit5，1 = 置 bit5
    bool immediate = false; // true = 立刻写 0x20（Furnace 的 freqChanged）
};

struct MacroExpandOutput {
    std::vector<MacroWrite> writes;
    std::vector<MacroPitch> pitches;        // arp / pitch 宏的音高偏移
    std::vector<MacroSus> susPoints;        // OPLL 的 alg→SUS
    std::vector<MacroRetrigger> retriggers; // phaseReset / ex4
};

// 执行全部宏。不含 key-on（0x08 / 0x20 bit4）与 key-off —— 那些由调用方按音符边界写。
MacroExpandOutput ExpandMacros(const FuiVoice& voice, const MacroExpandInput& in);

// 这套宏「从头放到尾」需要多少 tick（含 delay，按各宏自己的 speed 折算）。
// 给 noteoff:self 用：那种乐器的音符长度不取源，而是取这个值。
//   音量宏若最终停在 0（鼓类常见写法），按「最后一次非 0 值」计；
//   若完全没有音量宏，取所有通道级宏里最长的那个。
int MacroActiveTicks(const FuiVoice& voice);


}  // namespace vgmstudio
