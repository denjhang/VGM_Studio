// FmVoice —— 音色在芯片间转换。
//
// 全部逻辑移植自 docs/_vendor/ym-voice-0.3.2/dist（ISC），与 vgm-conv 用的是同一份：
//   opm-voice.js  OPMSlotParam/OPMVoice      decode/encode/toOPN
//   opn-voice.js  OPNSlotParam/OPNVoice      toOPL(keyOn) / toOPM
//   opl-voice.js  OPLSlotParam/OPLVoice      toOPLLVoice() / toOPLLROMVoice()
//   opll-voice.js OPLLSlotParam/OPLLVoice    decode/encode + 3 套 ROM 表
//
// 不做"自己设计的映射"：所有系数、权重、阈值都是原文数字。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vgmstudio {

// ── OPM（YM2151，4-op）───────────────────────────────────────────────
struct OpmSlot {
    int dt1 = 0, ml = 0, tl = 0, ks = 0, ar = 0, am = 0;
    int dt2 = 0, dr = 0, sr = 0, sl = 0, rr = 0;
};
struct OpmVoice {
    int fb = 0, con = 0, ams = 0, pms = 0;
    OpmSlot slots[4];  // slots[0..3] = M1, C1, M2, C2（不是 1,3,2,4）
};

// ── OPN（YM2612/2203/2608，4-op）────────────────────────────────────
struct OpnSlot {
    int dt = 0, ml = 0, tl = 0, ks = 0, ar = 0, am = 0;
    int dr = 0, sr = 0, sl = 0, rr = 0, ssg = 0;
};
struct OpnVoice {
    int fb = 0, con = 0, ams = 0, pms = 0;
    OpnSlot slots[4];
};

// ── OPL（2-op）──────────────────────────────────────────────────────
struct OplSlot {
    int am = 0, pm = 0, eg = 0, ml = 0, kr = 0, kl = 0;
    int tl = 0, ar = 0, dr = 0, sl = 0, rr = 0, ws = 0;
};
struct OplVoice {
    int fb = 0, con = 0;
    OplSlot slots[2];
};

// ── OPLL（YM2413，2-op + ROM 预置）──────────────────────────────────
struct OpllSlot {
    int am = 0, pm = 0, eg = 0, ml = 0, kr = 0, kl = 0;
    int tl = 0, ar = 0, dr = 0, sl = 0, rr = 0, ws = 0;
};
struct OpllVoice {
    int fb = 0;
    OpllSlot slots[2];
};

// ── 编解码：寄存器字节 ⇄ 结构体（opm-voice.js / opll-voice.js 的原文）──
OpmVoice OpmDecode(const uint8_t d[26]);
void OpmEncode(const OpmVoice& v, uint8_t out[26]);

OpllVoice OpllDecode(const uint8_t d[8]);
void OpllEncode(const OpllVoice& v, uint8_t out[8]);

// ── 转换链 ──────────────────────────────────────────────────────────
OpnVoice OpmToOpn(const OpmVoice& v);

// 4-op → 2×OPL（opn-voice.js:toOPL 的 8 个 con 分支；只用第 0 个 = 单 OPL 通道）
OplVoice OpnToOpl(const OpnVoice& v, bool keyOn = true);

// OPL → OPLL 用户音色（opl-voice.js:toOPLLVoice）
OpllVoice OplToOpllVoice(const OplVoice& v);

// ── OPLL ROM 表（opll-voice.js:_OPLL_ROM_PATCHES / VRC7 / YMF281B）───
enum class OpllBank { Ym2413, Vrc7, Ymf281b };
const std::vector<OpllVoice>& OpllRomBank(OpllBank bank);
const char* OpllProgramName(OpllBank bank, int program);

// ── 最近邻匹配（opl-voice.js:toOPLLROMVoice，原文逐行）──────────────
struct OpllMatch {
    int program = 0;
    int volumeOffset = 1;
    int octaveOffset = 0;
};
OpllMatch MatchOpllRom(const OplVoice& v, OpllBank bank);

// ── 载波 ML（决定实际发声基频 = 音高频率 × ML）────────────────────────
// 两端必须用同一个函数算，八度补偿才能互相抵消。
//   源：shrink 后的 OPM 影子寄存器（0x40 + op*8 + ch 的低 4 位）
//   目标：ROM/用户音色的 slot1（载波）ml
extern const double kMlTableFreq[16];
double OpmCarrierMl(const uint8_t shadow[256], int ch);
double OpllCarrierMl(int program);
// 载波 ML 比值 → 八度补偿（四舍五入，因为 OPLL 无法表达非 2 次幂倍频）
int MlRatioToOctave(double mlSrc, double mlDst);

// ── 频率 → OPM 的 KC/KF（opm_freq.ts:freqToOPMNote）──────────────────
struct OpcNote {
    uint8_t kc = 0;
    uint8_t kf = 0;
};
// clockRatio = 3579545 / (输出 OPM 时钟)
OpcNote FreqToOpmNote(double freq, double clockRatio);

// ── OPLL 的 F-Number（libvgm emu/cores/ym2413.c:1586-1660 的 block_fnum）──
struct OpllFnum {
    uint8_t fnumLow = 0;   // 0x10 + ch
    uint8_t fnumHigh = 0;  // 0x20 + ch：(block<<1) | fnum bit8
    uint8_t block = 0;
    uint16_t fnum = 0;
};
OpllFnum FreqToOpllFnum(double freq, uint32_t clock);

// ── KC/KF → 频率（FreqToOpmNote 的逆运算，同一张 KEY_TO_NOTE 表）──────
// freq = 277.2 * 2^((oct*12 + semitone + kf/64 - 60) / 12) / clockRatio
// clockRatio = 3579545 / 源 OPM 时钟
double OpmKcKfToFreq(uint8_t kc, uint8_t kf, double clockRatio);

}  // namespace vgmstudio
