// FmVoice.cpp —— 音色跨芯片转换链。
//
// 全部数字来自 docs/_vendor/ym-voice-0.3.2/dist（ISC，与 vgm-conv 同源）：
//   OpmToOpn      opm-voice.js:34-48     OPMSlotParam.toOPN()
//   OpnToOpl      opn-voice.js:222-352   OPNVoice.toOPL()
//   OplToOpllVoice opl-voice.js:164-168  OPLVoice.toOPLLVoice()
//   MatchOpllRom  opl-voice.js:170-222   OPLVoice.toOPLLROMVoice()
//   OpllDecode    opll-voice.js:198-238  OPLLVoice.decode()
//   OpllRomBank   opll-voice.js:6-71     三套 ROM 表
//   FreqToOpmNote src/converter/opm_freq.ts:freqToOPMNote
//
// 这些函数是纯数据变换，不认识 VGM 也不认识寄存器流；
// 与 json2json 的接线在 ChipConvert.cpp。
#include "FmVoice.h"

#include "AnalyzerYm2151.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vgmstudio {
namespace {

// opl-voice.js:7 的 _ml_tbl —— ML 对应的实际倍频（注意 10/11 都是 10，12/13 都是 12）
constexpr double kMlTable[16] = {0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};

}  // namespace

const double kMlTableFreq[16] = {0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};

namespace {

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// opn-voice.js:53-72 OPNSlotParam.toOPL 的三个内部函数
int OpnRateToOplRate(int a) {
    if (a == 31) return 15;
    if (a == 0) return 0;
    return Clamp((a * 28) >> 6, 1, 15);
}

// opll-voice.js:116-127 OPLLSlotParam.toOPN 的 _RR
// 目前 OPM→OPLL 单向转换用不到它（我们只做降 op，不做升 op），保留供将来 OPLL→OPM 用。
[[maybe_unused]] int OpllRateToOpnRate(int rate) {
    if (rate == 0) return 0;
    if (rate == 15) return 31;
    return Clamp(static_cast<int>(std::lround((rate + 1.5) * 2)), 0, 31);
}

// opll-voice.js:36-51 OPLLSlotParam.toOPL 的 _klfix
constexpr int kKlFix[4] = {0, 2, 1, 3};

}  // namespace

// ─────────────────────────── OPM 编解码 ───────────────────────────
// opm-voice.js:98-142。文件/寄存器顺序是 M1,M2,C1,C2；内部顺序是 M1,C1,M2,C2。
OpmVoice OpmDecode(const uint8_t d[26]) {
    OpmVoice v;
    OpmSlot tmp[4];
    for (int i = 0; i < 4; i++) {
        tmp[i].dt1 = (d[0 + i] >> 4) & 7;
        tmp[i].ml = d[0 + i] & 15;
        tmp[i].tl = d[4 + i] & 127;
        tmp[i].ks = (d[8 + i] >> 6) & 3;
        tmp[i].ar = d[8 + i] & 31;
        tmp[i].am = (d[12 + i] >> 7) & 1;
        tmp[i].dr = d[12 + i] & 31;
        tmp[i].dt2 = (d[16 + i] >> 6) & 3;
        tmp[i].sr = d[16 + i] & 31;
        tmp[i].sl = (d[20 + i] >> 4) & 15;
        tmp[i].rr = d[20 + i] & 15;
    }
    v.slots[0] = tmp[0];  // M1
    v.slots[1] = tmp[2];  // C1
    v.slots[2] = tmp[1];  // M2
    v.slots[3] = tmp[3];  // C2
    v.fb = (d[24] >> 3) & 7;
    v.con = d[24] & 7;
    v.ams = d[25] & 3;
    v.pms = (d[25] >> 3) & 7;
    return v;
}

void OpmEncode(const OpmVoice& v, uint8_t out[26]) {
    const OpmSlot* s = v.slots;
    out[0] = static_cast<uint8_t>((s[0].dt1 << 4) | s[0].ml);
    out[1] = static_cast<uint8_t>((s[2].dt1 << 4) | s[2].ml);
    out[2] = static_cast<uint8_t>((s[1].dt1 << 4) | s[1].ml);
    out[3] = static_cast<uint8_t>((s[3].dt1 << 4) | s[3].ml);
    out[4] = static_cast<uint8_t>(s[0].tl);
    out[5] = static_cast<uint8_t>(s[2].tl);
    out[6] = static_cast<uint8_t>(s[1].tl);
    out[7] = static_cast<uint8_t>(s[3].tl);
    out[8] = static_cast<uint8_t>(s[0].ar);
    out[9] = static_cast<uint8_t>(s[2].ar);
    out[10] = static_cast<uint8_t>(s[1].ar);
    out[11] = static_cast<uint8_t>(s[3].ar);
    out[12] = static_cast<uint8_t>((s[0].am << 7) | s[0].dr);
    out[13] = static_cast<uint8_t>((s[2].am << 7) | s[2].dr);
    out[14] = static_cast<uint8_t>((s[1].am << 7) | s[1].dr);
    out[15] = static_cast<uint8_t>((s[3].am << 7) | s[3].dr);
    out[16] = static_cast<uint8_t>((s[0].dt2 << 6) | s[0].sr);
    out[17] = static_cast<uint8_t>((s[2].dt2 << 6) | s[2].sr);
    out[18] = static_cast<uint8_t>((s[1].dt2 << 6) | s[1].sr);
    out[19] = static_cast<uint8_t>((s[3].dt2 << 6) | s[3].sr);
    out[20] = static_cast<uint8_t>((s[0].sl << 4) | s[0].rr);
    out[21] = static_cast<uint8_t>((s[2].sl << 4) | s[2].rr);
    out[22] = static_cast<uint8_t>((s[1].sl << 4) | s[1].rr);
    out[23] = static_cast<uint8_t>((s[3].sl << 4) | s[3].rr);
    out[24] = static_cast<uint8_t>((v.fb << 3) | v.con);
    out[25] = static_cast<uint8_t>((v.pms << 3) | v.ams);
}

// ─────────────────────────── OPM → OPN ───────────────────────────
// opm-voice.js:34-48。OPM 与 OPN 的 slot 字段一一对应，只有 dt1→dt。
OpnVoice OpmToOpn(const OpmVoice& v) {
    OpnVoice o;
    o.fb = v.fb;
    o.con = v.con;
    o.ams = v.ams;
    o.pms = v.pms;
    for (int i = 0; i < 4; i++) {
        o.slots[i].dt = v.slots[i].dt1;
        o.slots[i].ml = v.slots[i].ml;
        o.slots[i].tl = v.slots[i].tl;
        o.slots[i].ks = v.slots[i].ks;
        o.slots[i].ar = v.slots[i].ar;
        o.slots[i].am = v.slots[i].am;
        o.slots[i].dr = v.slots[i].dr;
        o.slots[i].sr = v.slots[i].sr;
        o.slots[i].sl = v.slots[i].sl;
        o.slots[i].rr = v.slots[i].rr;
    }
    return o;
}

// ─────────────────────────── OPN → OPL ───────────────────────────
// opn-voice.js:53-72 + 222-352。
// 单个 slot：ar/dr 用 (a*28)>>6 压到 4bit；rr 为 0 时取 1（避免"永不释放"）。
// 组合：按 con 决定两个 OPL 通道；我们只取第 0 个（单通道）。
OplVoice OpnToOpl(const OpnVoice& v, bool keyOn) {
    OplSlot ss[4];
    for (int i = 0; i < 4; i++) {
        const OpnSlot& s = v.slots[i];
        OplSlot& o = ss[i];
        o.am = s.am;
        o.pm = 0;
        o.eg = keyOn ? 0 : 1;
        o.kr = s.ks >> 1;
        o.ml = s.ml;
        o.kl = 0;
        o.tl = (s.tl < 63) ? s.tl : 63;
        o.ar = OpnRateToOplRate(s.ar);
        o.dr = OpnRateToOplRate(s.dr);
        o.sl = s.sl;
        o.rr = keyOn ? OpnRateToOplRate(s.sr) : (s.rr == 0 ? 1 : s.rr);
        o.ws = 0;
    }

    OplVoice out;
    switch (v.con & 7) {
        case 0:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[3];
            out.slots[1].ml = ss[1].ml;
            out.slots[1].tl = Clamp(Clamp(ss[1].tl - 2, 0, 63) + ss[3].tl, 0, 63);
            break;
        case 1:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[3];
            out.slots[1].ml = ss[2].ml;
            out.slots[1].tl = Clamp(Clamp(ss[2].tl - 2, 0, 63) + ss[3].tl, 0, 63);
            break;
        case 2:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[3];
            break;
        case 3:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[3];
            out.slots[1].ml = ss[1].ml;
            out.slots[1].tl = Clamp(Clamp(ss[1].tl - 2, 0, 63) + ss[3].tl, 0, 63);
            break;
        case 4:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[1];
            break;
        case 5:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[1];
            break;
        case 6:
            out.fb = v.fb;
            out.con = 0;
            out.slots[0] = ss[0];
            out.slots[1] = ss[1];
            break;
        default:  // 7
            out.fb = v.fb;
            out.con = 1;
            out.slots[0] = ss[0];
            out.slots[1] = ss[1];
            break;
    }
    return out;
}

// ─────────────────────────── OPL → OPLL 用户音色 ───────────────────────────
// opl-voice.js:36-51 + 164-168。con==0 时 slot0 参与，否则 slot0 置空。
OpllVoice OplToOpllVoice(const OplVoice& v) {
    auto convSlot = [](const OplSlot& s) {
        OpllSlot o;
        o.am = s.am;
        o.pm = s.pm;
        o.eg = s.eg;
        o.ml = s.ml;
        o.kr = s.kr;
        o.kl = kKlFix[s.kl & 3];
        o.tl = s.tl & 0x3f;
        o.ar = s.ar;
        o.dr = s.dr;
        o.sl = s.sl;
        o.rr = s.rr;
        o.ws = (s.ws > 0) ? 1 : 0;
        return o;
    };
    OpllVoice out;
    out.fb = v.fb;
    if (v.con == 0) {
        out.slots[0] = convSlot(v.slots[0]);
    }
    out.slots[1] = convSlot(v.slots[1]);
    return out;
}

// ─────────────────────────── OPLL 编解码 ───────────────────────────
// opll-voice.js:198-238
OpllVoice OpllDecode(const uint8_t d[8]) {
    OpllVoice v;
    v.fb = d[3] & 7;
    v.slots[0].am = (d[0] >> 7) & 1;
    v.slots[0].pm = (d[0] >> 6) & 1;
    v.slots[0].eg = (d[0] >> 5) & 1;
    v.slots[0].kr = (d[0] >> 4) & 1;
    v.slots[0].ml = d[0] & 0xf;
    v.slots[0].kl = (d[2] >> 6) & 3;
    v.slots[0].tl = d[2] & 0x3f;
    v.slots[0].ar = (d[4] >> 4) & 0xf;
    v.slots[0].dr = d[4] & 0xf;
    v.slots[0].sl = (d[6] >> 4) & 0xf;
    v.slots[0].rr = d[6] & 0xf;
    v.slots[0].ws = (d[3] >> 3) & 1;

    v.slots[1].am = (d[1] >> 7) & 1;
    v.slots[1].pm = (d[1] >> 6) & 1;
    v.slots[1].eg = (d[1] >> 5) & 1;
    v.slots[1].kr = (d[1] >> 4) & 1;
    v.slots[1].ml = d[1] & 0xf;
    v.slots[1].kl = (d[3] >> 6) & 3;
    v.slots[1].tl = 0;  // 载波 TL 由 0x30+ch 的低 4 位在运行时给
    v.slots[1].ar = (d[5] >> 4) & 0xf;
    v.slots[1].dr = d[5] & 0xf;
    v.slots[1].sl = (d[7] >> 4) & 0xf;
    v.slots[1].rr = d[7] & 0xf;
    v.slots[1].ws = (d[3] >> 4) & 1;
    return v;
}

void OpllEncode(const OpllVoice& v, uint8_t out[8]) {
    const OpllSlot* s = v.slots;
    out[0] = static_cast<uint8_t>((s[0].am << 7) | (s[0].pm << 6) | (s[0].eg << 5) |
                                  (s[0].kr << 4) | s[0].ml);
    out[1] = static_cast<uint8_t>((s[1].am << 7) | (s[1].pm << 6) | (s[1].eg << 5) |
                                  (s[1].kr << 4) | s[1].ml);
    out[2] = static_cast<uint8_t>((s[0].kl << 6) | s[0].tl);
    out[3] = static_cast<uint8_t>((s[1].kl << 6) | (s[1].ws << 4) | (s[0].ws << 3) | v.fb);
    out[4] = static_cast<uint8_t>((s[0].ar << 4) | s[0].dr);
    out[5] = static_cast<uint8_t>((s[1].ar << 4) | s[1].dr);
    out[6] = static_cast<uint8_t>((s[0].sl << 4) | s[0].rr);
    out[7] = static_cast<uint8_t>((s[1].sl << 4) | s[1].rr);
}

// ─────────────────────────── OPLL ROM 表 ───────────────────────────
// opll-voice.js:6-71，三套表各 19 项（0 = 用户槽，16-18 = 节奏）。
namespace {
const uint8_t kYm2413[19][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 0: User
    {0x71, 0x61, 0x1e, 0x17, 0xd0, 0x78, 0x00, 0x17},  // 1: Violin
    {0x13, 0x41, 0x1a, 0x0d, 0xd8, 0xf7, 0x23, 0x13},  // 2: Guitar
    {0x13, 0x01, 0x99, 0x00, 0xf2, 0xd4, 0x21, 0x23},  // 3: Piano
    {0x11, 0x61, 0x0e, 0x07, 0x8d, 0x64, 0x70, 0x27},  // 4: Flute
    {0x32, 0x21, 0x1e, 0x06, 0xe1, 0x76, 0x01, 0x28},  // 5: Clarinet
    {0x31, 0x22, 0x16, 0x05, 0xe0, 0x71, 0x00, 0x18},  // 6: Oboe
    {0x21, 0x61, 0x1d, 0x07, 0x82, 0x81, 0x11, 0x07},  // 7: Trumpet
    {0x33, 0x21, 0x2d, 0x13, 0xb0, 0x70, 0x00, 0x07},  // 8: Organ
    {0x61, 0x61, 0x1b, 0x06, 0x64, 0x65, 0x10, 0x17},  // 9: Horn
    {0x41, 0x61, 0x0b, 0x18, 0x85, 0xf0, 0x81, 0x07},  // A: Synthesizer
    {0x33, 0x01, 0x83, 0x11, 0xea, 0xef, 0x10, 0x04},  // B: Harpsichord
    {0x17, 0xc1, 0x24, 0x07, 0xf8, 0xf8, 0x22, 0x12},  // C: Vibraphone
    {0x61, 0x50, 0x0c, 0x05, 0xd2, 0xf5, 0x40, 0x42},  // D: Synth Bass
    {0x01, 0x01, 0x55, 0x03, 0xe4, 0x90, 0x03, 0x02},  // E: Acoustic Bass
    {0x41, 0x41, 0x89, 0x03, 0xf1, 0xe4, 0xc0, 0x13},  // F: Electric Guitar
    {0x01, 0x01, 0x18, 0x0f, 0xdf, 0xf8, 0x6a, 0x6d},  // R: Bass Drum
    {0x01, 0x01, 0x00, 0x00, 0xc8, 0xd8, 0xa7, 0x68},  // R: Hi-Hat / Snare
    {0x05, 0x01, 0x00, 0x00, 0xf8, 0xaa, 0x59, 0x55},  // R: Tom / Top Cymbal
};
const uint8_t kVrc7[19][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x03, 0x21, 0x05, 0x06, 0xe8, 0x81, 0x42, 0x27},
    {0x13, 0x41, 0x14, 0x0d, 0xd8, 0xf6, 0x23, 0x12},
    {0x11, 0x11, 0x08, 0x08, 0xfa, 0xb2, 0x20, 0x12},
    {0x31, 0x61, 0x0c, 0x07, 0xa8, 0x64, 0x61, 0x27},
    {0x32, 0x21, 0x1e, 0x06, 0xe1, 0x76, 0x01, 0x28},
    {0x02, 0x01, 0x06, 0x00, 0xa3, 0xe2, 0xf4, 0xf4},
    {0x21, 0x61, 0x1d, 0x07, 0x82, 0x81, 0x11, 0x07},
    {0x23, 0x21, 0x22, 0x17, 0xa2, 0x72, 0x01, 0x17},
    {0x35, 0x11, 0x25, 0x00, 0x40, 0x73, 0x72, 0x01},
    {0xb5, 0x01, 0x0f, 0x0f, 0xa8, 0xa5, 0x51, 0x02},
    {0x17, 0xc1, 0x24, 0x07, 0xf8, 0xf8, 0x22, 0x12},
    {0x71, 0x23, 0x11, 0x06, 0x65, 0x74, 0x18, 0x16},
    {0x01, 0x02, 0xd3, 0x05, 0xc9, 0x95, 0x03, 0x02},
    {0x61, 0x63, 0x0c, 0x00, 0x94, 0xc0, 0x33, 0xf6},
    {0x21, 0x72, 0x0d, 0x00, 0xc1, 0xd5, 0x56, 0x06},
    {0x01, 0x01, 0x18, 0x0f, 0xdf, 0xf8, 0x6a, 0x6d},
    {0x01, 0x01, 0x00, 0x00, 0xc8, 0xd8, 0xa7, 0x68},
    {0x05, 0x01, 0x00, 0x00, 0xf8, 0xaa, 0x59, 0x55},
};
const uint8_t kYmf281b[19][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x62, 0x21, 0x1a, 0x07, 0xf0, 0x6f, 0x00, 0x16},
    {0x40, 0x10, 0x45, 0x00, 0xf6, 0x83, 0x73, 0x63},
    {0x13, 0x01, 0x99, 0x00, 0xf2, 0xc3, 0x21, 0x23},
    {0x01, 0x61, 0x0b, 0x0f, 0xf9, 0x64, 0x70, 0x17},
    {0x32, 0x21, 0x1e, 0x06, 0xe1, 0x76, 0x01, 0x28},
    {0x60, 0x01, 0x82, 0x0e, 0xf9, 0x61, 0x20, 0x27},
    {0x21, 0x61, 0x1c, 0x07, 0x84, 0x81, 0x11, 0x07},
    {0x37, 0x32, 0xc9, 0x01, 0x66, 0x64, 0x40, 0x28},
    {0x01, 0x21, 0x07, 0x03, 0xa5, 0x71, 0x51, 0x07},
    {0x06, 0x01, 0x5e, 0x07, 0xf3, 0xf3, 0xf6, 0x13},
    {0x00, 0x00, 0x18, 0x06, 0xf5, 0xf3, 0x20, 0x23},
    {0x17, 0xc1, 0x24, 0x07, 0xf8, 0xf8, 0x22, 0x12},
    {0x35, 0x64, 0x00, 0x00, 0xff, 0xf3, 0x77, 0xf5},
    {0x11, 0x31, 0x00, 0x07, 0xdd, 0xf3, 0xff, 0xfb},
    {0x3a, 0x21, 0x00, 0x07, 0x80, 0x84, 0x0f, 0xf5},
    {0x01, 0x01, 0x18, 0x0f, 0xdf, 0xf8, 0x6a, 0x6d},
    {0x01, 0x01, 0x00, 0x00, 0xc8, 0xd8, 0xa7, 0x68},
    {0x05, 0x01, 0x00, 0x00, 0xf8, 0xaa, 0x59, 0x55},
};

std::vector<OpllVoice> DecodeBank(const uint8_t src[19][8]) {
    std::vector<OpllVoice> out;
    out.reserve(19);
    for (int i = 0; i < 19; i++) out.push_back(OpllDecode(src[i]));
    return out;
}

std::vector<OpllVoice> g_bankYm2413;
std::vector<OpllVoice> g_bankVrc7;
std::vector<OpllVoice> g_bankYmf281b;
bool g_banksInit = false;

void InitBanks() {
    if (g_banksInit) return;
    g_bankYm2413 = DecodeBank(kYm2413);
    g_bankVrc7 = DecodeBank(kVrc7);
    g_bankYmf281b = DecodeBank(kYmf281b);
    g_banksInit = true;
}
}  // namespace

const std::vector<OpllVoice>& OpllRomBank(OpllBank bank) {
    InitBanks();
    switch (bank) {
        case OpllBank::Vrc7: return g_bankVrc7;
        case OpllBank::Ymf281b: return g_bankYmf281b;
        case OpllBank::Ym2413:
        default: return g_bankYm2413;
    }
}

const char* OpllProgramName(OpllBank bank, int program) {
    static const char* kYm2413Names[19] = {
        "User", "Violin", "Guitar", "Piano", "Flute", "Clarinet", "Oboe", "Trumpet",
        "Organ", "Horn", "Synthesizer", "Harpsichord", "Vibraphone", "Synth Bass",
        "Acoustic Bass", "Electric Guitar", "Bass Drum", "Hi-Hat/Snare", "Tom/Cymbal"};
    static const char* kVrc7Names[19] = {
        "User", "Buzzy", "Reed", "Bell", "Bass", "Clarinet", "Flute", "Trumpet",
        "Organ", "Strings", "Piano", "Vibraphone", "Synth", "Bass2", "Guitar", "Brass",
        "Bass Drum", "Hi-Hat/Snare", "Tom/Cymbal"};
    static const char* kYmfNames[19] = {
        "User", "Electric Strings", "Bow Wow", "Electric Guitar", "Organ", "Clarinet",
        "Saxophone", "Trumpet", "Street Organ", "Synth Brass", "Electric Piano", "Bass",
        "Vibraphone", "Chimes", "Tom Tom II", "Noise", "Bass Drum", "Hi-Hat/Snare",
        "Tom/Cymbal"};
    if (program < 0 || program > 18) return "?";
    switch (bank) {
        case OpllBank::Vrc7: return kVrc7Names[program];
        case OpllBank::Ymf281b: return kYmfNames[program];
        case OpllBank::Ym2413:
        default: return kYm2413Names[program];
    }
}

// ─────────────────────────── 最近邻匹配 ───────────────────────────
// opl-voice.js:170-222，逐行照搬（含 6 个设计：ML 比值、反馈、AR/DR、
// SL+TL 加权、持续/打击音硬分流、八度/音量补偿）。
OpllMatch MatchOpllRom(const OplVoice& v, OpllBank bank) {
    const std::vector<OpllVoice>& roms = OpllRomBank(bank);
    OpllMatch best;
    double diff = 1e18;
    int program = 0;

    for (int i = 1; i < 16; i++) {
        if (i == 13) continue;  // Synth Bass：避免误匹配
        const OpllVoice& o = roms[i];
        double d = 0.0;

        // ① 调制器/载波频率比（不是绝对 ML）
        const double mlA = static_cast<double>(v.slots[1].ml) / kMlTable[v.slots[0].ml & 15];
        const double mlB = static_cast<double>(o.slots[1].ml) / kMlTable[o.slots[0].ml & 15];
        d += static_cast<int>(std::fabs(mlA - mlB)) << 1;  // 原文 x<<1
        // ② 反馈
        d += static_cast<int>(std::fabs(static_cast<double>(v.fb - o.fb))) >> 1;
        // ③④ AR / DR（两个 slot）
        d += std::abs(v.slots[0].ar - o.slots[0].ar);
        d += std::abs(v.slots[1].ar - o.slots[1].ar);
        d += std::abs(v.slots[0].dr - o.slots[0].dr);
        d += std::abs(v.slots[1].dr - o.slots[1].dr);
        // ⑤ SL 加权 4 倍 + TL（有波形时补 8）
        {
            const int tlB = o.slots[0].tl + ((o.slots[0].ws != 0) ? 8 : 0);
            int inner = 4 * std::abs(v.slots[0].sl - o.slots[0].sl) + std::abs(v.slots[0].tl - tlB);
            if (inner > 63) inner = 63;
            d += inner >> 3;
        }
        // ⑥ 持续音 / 打击音是硬约束，不是软评分
        if (v.slots[1].rr == 0) {
            if (o.slots[1].eg == 0) continue;  // 目标是打击音，跳过
            d += std::abs(v.slots[1].sl - o.slots[1].sl);
        } else {
            if (o.slots[1].eg == 1) continue;  // 目标是持续音，跳过
            d += std::abs(v.slots[1].rr - o.slots[1].rr);
        }

        if (d < diff) {
            program = i;
            diff = d;
        }
    }

    const OpllVoice& o = roms[program];
    // ⑦ 八度补偿：按载波 ML 的实际倍频比
    //
    // ★ vgm-conv（opl-voice.js:214）写的是 `floor(log2(ratio)/2)`，那个 `/2` 是错的：
    //   载波 ML 翻倍 = 基频翻倍 = **正好 1 个八度 = block +1**，不需要再除 2。
    //   实测（31 Grass Maze）：源载波 ML=2、目标 ROM 载波 ML=1 时 ratio=2，
    //   正确补偿是 block+1，vgm-conv 的公式给 0，于是整轨低 12 个半音；
    //   源 ML=10 时 vgm-conv 给 +1（应 +3），整轨低 28 个半音。
    //   这里改用 round(log2(ratio))，残差始终 < 半个八度（ML 非 2 的幂时无法完全补偿）。
    best.octaveOffset = static_cast<int>(
        std::lround(std::log2(kMlTable[v.slots[1].ml & 15] / kMlTable[o.slots[1].ml & 15])));
    // ⑧ 音量补偿：目标有波形时响度不同
    //
    // ★ 基准值从 1 改成 0。ym-voice 的 voff=1 是为了补它自己用 `tl>>3` 粗移位
    //   带来的系统性偏差（÷8 比 ÷4 多衰减 3 dB/步）；Opm2Opll 现在按物理比例
    //   ÷4 换算（OPM TL 0.7526 dB/单位，OPLL 0x30 3.01 dB/步，正好 4 倍），
    //   再叠加 voff=1 就会整体多衰减约 3 dB。实测整曲 RMS：
    //   voff=1 → −22.0 dBFS，voff=0 → −19.0 dBFS，源 −19.0 dBFS。
    //   ws 的 ±2 步补偿与映射精度无关，原样保留。
    int voff = 0;
    if (o.slots[1].ws) {
        voff -= 2;
        if (o.slots[0].ws) voff -= 2;
    }
    best.program = program;
    best.volumeOffset = voff;
    return best;
}


// ─────────────────────────── 载波 ML 与八度补偿 ───────────────────────────
// FM 的实际发声基频 = 音高频率 × 载波 ML。源与目标 ML 不同就必须补八度，
// 否则两边的绝对音高差 log2(ML比) 个八度。

double OpmCarrierMl(const uint8_t shadow[256], int ch) {
    const int con = shadow[0x20 + ch] & 7;
    const uint8_t mask = kAlgCarriers[con];
    for (int op = 0; op < 4; op++) {
        if ((mask >> op) & 1) {
            return kMlTable[shadow[0x40 + op * 8 + ch] & 0x0F];
        }
    }
    return 1.0;
}

double OpllCarrierMl(int program) {
    if (program <= 0 || program >= 19) return 1.0;
    const std::vector<OpllVoice>& bank = OpllRomBank(OpllBank::Ym2413);
    if (program >= static_cast<int>(bank.size())) return 1.0;
    return kMlTable[bank[program].slots[1].ml & 0x0F];
}

int MlRatioToOctave(double mlSrc, double mlDst) {
    if (mlSrc <= 0.0 || mlDst <= 0.0) return 0;
    return static_cast<int>(std::lround(std::log2(mlSrc / mlDst)));
}

// ─────────────────────────── 频率 → OPM / OPLL ───────────────────────────
// src/converter/opm_freq.ts（原文），BASE_FREQ = C#4 = 277.2Hz，key 60 = C#4。
OpcNote FreqToOpmNote(double freq, double clockRatio) {
    static const int kKeyToNote[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    OpcNote out;
    if (freq <= 0.0 || !std::isfinite(freq)) return out;
    double key = 60.0 + std::log2((freq * clockRatio) / 277.2) * 12.0;
    if (key < 0.0) key = 0.0;
    const double frac = key - std::floor(key);
    const int kf = static_cast<int>(std::floor(frac * 64.0));
    const int note = kKeyToNote[static_cast<int>(std::floor(key)) % 12];
    int oct = static_cast<int>(std::floor(key / 12.0));
    if (oct > 7) oct = 7;
    out.kc = static_cast<uint8_t>((oct << 4) | note);
    out.kf = static_cast<uint8_t>(kf);
    return out;
}

// OPLL 的 fnum/block。
//   fnum = floor((freq << 19) / (clock / 72))     ← op/opl 通用式
//   while (fnum > 511) { fnum >>= 1; blk++; }     ← ★ OPLL 只有 9 位 fnum
//   if (blk > 7) blk = 7;
//
// ★ 这里原来写的是 `> 1023`（OPL 的 10 位 fnum）。对 OPLL 是错的：
//   寄存器 0x20 只放 fnum 的 bit8，所以 fnum 只能是 0..511。
//   用 1023 会算出 fnum ≥ 512，写进寄存器时 bit8 溢出被截掉，
//   音高直接掉到 1~2 Hz（json2mid 实测有 183 个音符因此无效）。
//   依据：Valley Bell Modules/YM2413.bas:186 `FNum_MSB * 256 + FNum_LSB`，
//        FNum_MSB 只有 1 位。
OpllFnum FreqToOpllFnum(double freq, uint32_t clock) {
    OpllFnum out;
    if (freq <= 0.0 || clock == 0) return out;
    double f = std::floor((freq * 524288.0) / (static_cast<double>(clock) / 72.0));  // <<19
    if (f < 0.0) f = 0.0;
    int fnum = static_cast<int>(f);
    int blk = 0;
    while (fnum > 511) {
        fnum >>= 1;
        blk++;
    }
    if (blk > 7) {
        blk = 7;
        if (fnum > 255) fnum = 255;  // blk=7 + fnum 上限，避免 (blk<<1)|bit8 溢出
    }
    out.fnumLow = static_cast<uint8_t>(fnum & 0xFF);
    out.fnumHigh = static_cast<uint8_t>(((blk & 7) << 1) | ((fnum >> 8) & 1));
    out.block = static_cast<uint8_t>(blk);
    out.fnum = static_cast<uint16_t>(fnum);
    return out;
}

// KC/KF → 频率。
//
// ★ 这里原来写成 `277.2 * 2^((keyf-60)/12)`，基准错了 24 个半音（2 个八度）。
//   推导（docs/PITCH-CONVERSION.md §1.4）：
//     OPM 的相位增量表 phaseinc_rom[768] 覆盖 1 个八度（768 = 12 半音 × 64），
//     基准八度给出 69.29 Hz = C#1 = MIDI 25，所以 MIDI = 25 + (kc - (kc>>2)) + kf/64。
//     277.2 Hz = C#4 对应 kc = 0x30，此时 (kc-(kc>>2)) = 36 —— 不是 60。
//   因此基准常数是 36（换成 MIDI 音号写法就是 69 与 25 的组合）：
//     freq = 277.2 * 2^((keyf - 36) / 12)
//   keyf = (kc - (kc>>2)) + kf/64，已包含八度（kc 是完整 7 位 KC）。
double OpmKcKfToFreq(uint8_t kc, uint8_t kf, double clockRatio) {
    // Ym2151Semitone(kc) = kc - (kc>>2)，与 AnalyzerYm2151.h 同一公式。
    const int keyf = static_cast<int>(kc) - (static_cast<int>(kc) >> 2);
    const double hz = 277.2 * std::pow(2.0, (keyf + (kf & 0x3F) / 64.0 - 36.0) / 12.0);
    return hz / clockRatio;
}

}  // namespace vgmstudio
