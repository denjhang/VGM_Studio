// AnalyzerYm2151 —— YM2151 的语义推算（视图层推测，不写回原值，RULES.md D3）。
//
// 本文件的每一条规则都**照抄 libvgm 的 OPM 核**，不自己推导：
//   音高   ← libvgm/emu/cores/ym2151.c:1066-1069  (case 0x08 Key Code)
//   载波   ← libvgm/emu/cores/ym2151.c:743-830    (set_connect, ALG 接线)
//   寄存器 ← libvgm/emu/cores/ym2151.c:1049-1200  (write() 的 case 分支)
#pragma once

#include <cstdint>

namespace vgmstudio {

// ── ALG → 载波掩码（bit0 = OP1 … bit3 = OP4）──────────────────────────
// 依据 libvgm set_connect()（ym2151.c:743-830）里各 ALG 分支谁接到
// PSG->chanout[cha]：
//   ALG 0-3 : 只有 C2(OP4)          → 0x8
//   ALG 4   : M2(OP3) + C2(OP4)     → 0xC
//   ALG 5/6 : C1(OP2) + M2(OP3) + C2(OP4) → 0xE
//   ALG 7   : 全部四个              → 0xF
constexpr uint8_t kAlgCarriers[8] = {0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF};

// ── KC + KF → 音高 ───────────────────────────────────────────────────
// libvgm 把 KC 直接换算成 768 单位的音高索引（768 = 12 半音 × 64 份）：
//     kc_channel = (kc - (kc >> 2)) * 64 + 768;   // ym2151.c:1066-1068
//     kc_channel |= (kf >> 2) & 63;               // ym2151.c:1106
// 所以半音数 = (kc - (kc >> 2))，分数 = (kf>>2)&63，每份 1/64 半音。
inline int Ym2151Semitone(uint8_t kc) {
    return static_cast<int>(kc) - (static_cast<int>(kc) >> 2);
}

// 浮点 MIDI 音号：整数 = 音名，小数 = 半音内偏移。
// 物理真值（docs/PITCH-CONVERSION.md §1.4）：
//   MIDI = 25 + (kc - (kc>>2)) + kf/64
// 25 = libvgm phaseinc_rom 基准八度（C#1, 34.65 Hz）对应的 MIDI 音号。
constexpr int kDefaultOctaveOffset = 25;

inline float Ym2151Pitch(uint8_t kc, uint8_t kf, int octaveOffset = kDefaultOctaveOffset) {
    const int frac = (kf >> 2) & 0x3F;
    return static_cast<float>(Ym2151Semitone(kc) + octaveOffset) +
           static_cast<float>(frac) / 64.0f;
}

// 音量参考值：载波里最小的 TL（0 = 最响）。
// 影子寄存器按 libvgm 的寻址 op*8 + ch 排布（ym2151.c:1049 的 oper[(r&7)*4]）。
inline int CarrierMinTL(const uint8_t shadow[256], int ch, int alg) {
    int minTl = 127;
    for (int op = 0; op < 4; op++) {
        if ((kAlgCarriers[alg & 7] >> op) & 1) {
            const int tl = shadow[0x60 + op * 8 + ch] & 0x7F;
            if (tl < minTl) minTl = tl;
        }
    }
    return minTl;
}

// ── 寄存器分类（照 libvgm 的 write() switch 分支划）──────────────────
// ym2151.c 的 `case <base>:` 分支就是全部合法地址：
//   0x20 / 0x28 / 0x30 / 0x38  → 每组 8 个通道（低 3 位 = ch）
//   0x40 / 0x60 / 0x80 / 0xA0 / 0xC0 / 0xE0 → 每组 32 个（op*8 + ch）
// 0x08 key-on 也是通道级，但通道号在数据位里而不是地址里。
inline bool IsChannelReg(uint8_t reg) {
    return reg == 0x08 || reg >= 0x20;
}

// 其余（0x00-0x07、0x09-0x1F）是全局量：test/LFO/noise/timer/清寄存器序列。
inline bool IsGlobalReg(uint8_t reg) {
    return reg < 0x20 && reg != 0x08;
}

}  // namespace vgmstudio
