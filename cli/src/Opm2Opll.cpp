// Opm2Opll.cpp —— 见头文件注释。
#include "Opm2Opll.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "AnalyzerYm2151.h"
#include "FmVoice.h"

namespace vgmstudio {
namespace {

constexpr int kSrcChannels = 8;
constexpr int kDstChannels = 9;

OpllBank ParseBank(const std::string& name) {
    if (name == "vrc7") return OpllBank::Vrc7;
    if (name == "ymf281b") return OpllBank::Ymf281b;
    return OpllBank::Ym2413;
}

// 把 OPM 影子寄存器（0x20..0xFF 的通道部分）读成一个 OPMVoice。
// 索引与 libvgm 一致：op*8 + ch。
OpmVoice SnapshotOpmVoice(const uint8_t shadow[256], int ch) {
    OpmVoice v;
    v.fb = (shadow[0x20 + ch] >> 3) & 7;
    v.con = shadow[0x20 + ch] & 7;
    v.pms = (shadow[0x38 + ch] >> 4) & 7;
    v.ams = shadow[0x38 + ch] & 3;
    for (int op = 0; op < 4; op++) {
        const int base = op * 8 + ch;
        OpmSlot& s = v.slots[op];
        s.dt1 = (shadow[0x40 + base] >> 4) & 7;
        s.ml = shadow[0x40 + base] & 15;
        s.tl = shadow[0x60 + base] & 127;
        s.ks = (shadow[0x80 + base] >> 6) & 3;
        s.ar = shadow[0x80 + base] & 31;
        s.am = (shadow[0xA0 + base] >> 7) & 1;
        s.dr = shadow[0xA0 + base] & 31;
        s.dt2 = (shadow[0xC0 + base] >> 6) & 3;
        s.sr = shadow[0xC0 + base] & 31;
        s.sl = (shadow[0xE0 + base] >> 4) & 15;
        s.rr = shadow[0xE0 + base] & 15;
    }
    return v;
}

RegValue Rv(uint8_t reg, uint8_t val) { return RegValue{reg, val}; }

}  // namespace

int CarrierTlOfOpmVoice(const uint8_t shadow[256], int ch, int con) {
    return CarrierMinTL(shadow, ch, con);
}

ConvertResult ConvertOpmToOpll(const Document& src, const ConvertOptions& opt) {
    ConvertResult out;
    const OpllBank bank = ParseBank(opt.bank);
    // 源时钟从 chips 里取（OPM 的 clockRatio = 3579545 / 源时钟）
    uint32_t srcClock = 4000000;
    for (const ChipEntry& c : src.chips) {
        if (c.vgmChipType == 0x03) {
            srcClock = c.clock;
            break;
        }
    }
    const double clockRatio = 3579545.0 / static_cast<double>(srcClock);

    Document& doc = out.doc;
    doc.format = "vgm-studio/song";
    doc.version = 1;
    doc.meta = src.meta;
    doc.meta.structurer = "opm2opll/v1";
    doc.meta.splitRule = src.meta.splitRule;
    doc.meta.unhandled.clear();

    doc.timeBase = src.timeBase;  // 采样数原样搬运，不量化

    ChipEntry chip;
    chip.id = "ym2413.0";
    chip.type = "YM2413";
    chip.vgmChipType = 0x01;
    chip.clock = opt.targetClock;
    chip.instances = 1;
    chip.channelCount = kDstChannels;
    chip.portCount = 1;
    chip.capabilities.operators = 2;
    chip.capabilities.algorithms = 2;  // FYM：调制+载波 / 全加法
    chip.capabilities.polyphony = kDstChannels;
    doc.chips.push_back(chip);

    // 目标芯片只有 0x0E（节奏）一个全局寄存器；源侧全局量不搬。
    doc.globals.clear();

    doc.channels.resize(kDstChannels);
    for (int ch = 0; ch < kDstChannels; ch++) {
        Channel& c = doc.channels[ch];
        c.chipType = 0x01;
        c.index = ch;
        char name[16];
        std::snprintf(name, sizeof(name), "FM%d", ch + 1);
        c.name = name;
        c.kind = "FM";
    }

    // 逐通道重放源寄存器，得到每个音符 key-on 时刻的音色与音高
    for (int ch = 0; ch < kSrcChannels && ch < static_cast<int>(src.channels.size()); ch++) {
        const Channel& sc = src.channels[ch];
        uint8_t shadow[256] = {0};

        // 把所有写按时间排好（orphans + 各音符的 segments）
        struct Entry {
            uint32_t at;
            uint8_t reg;
            uint8_t val;
        };
        std::vector<Entry> stream;
        for (const Orphan& o : sc.orphans)
            for (const RegValue& r : o.regs) stream.push_back(Entry{o.atSample, r.reg, r.val});
        for (const Note& n : sc.notes)
            for (const Segment& s : n.segments)
                for (const RegValue& r : s.regs) stream.push_back(Entry{s.atSample, r.reg, r.val});
        std::stable_sort(stream.begin(), stream.end(),
                         [](const Entry& a, const Entry& b) { return a.at < b.at; });

        size_t cur = 0;
        for (const Note& n : sc.notes) {
            // 重放到音符起始
            while (cur < stream.size() && stream[cur].at <= n.startSample) {
                shadow[stream[cur].reg] = stream[cur].val;
                cur++;
            }

            const OpmVoice opm = SnapshotOpmVoice(shadow, ch);
            const OpnVoice opn = OpmToOpn(opm);
            const OplVoice opl = OpnToOpl(opn, /*keyOn=*/true);
            const OpllMatch m = MatchOpllRom(opl, bank);

            const int carrierTl = CarrierMinTL(shadow, ch, opm.con);
            int vol = (carrierTl >> 3) + m.volumeOffset + opt.volumeOffsetBias;
            vol = std::max(0, std::min(15, vol));

            // 音高：KC/KF → Hz → OPLL block/fnum
            const uint8_t kc = shadow[0x28 + ch];
            const uint8_t kf = (shadow[0x30 + ch] >> 2) & 0x3F;
            const double freq = OpmKcKfToFreq(kc, kf, clockRatio);
            const OpllFnum fn = FreqToOpllFnum(freq, opt.targetClock);
            // 八度补偿：源的载波 ML 与目标 ROM 的载波 ML 不同，
            // 实际发声基频就差 log2(ML比) 个八度，必须加回 block。
            // （vgm-conv 的 `floor(log2(ratio)/2)` 少了一个因子 2，见 FmVoice.cpp 注释）
            const int octaveOffset = MlRatioToOctave(OpmCarrierMl(shadow, ch),
                                                     OpllCarrierMl(m.program));
            const int blkO = std::min(7, std::max(0, static_cast<int>(fn.block) + octaveOffset));
            const uint8_t dhBase = static_cast<uint8_t>((blkO << 1) | ((fn.fnum >> 8) & 1));

            Channel& dc = doc.channels[ch];
            Note dn;
            dn.id = n.id;
            dn.startSample = n.startSample;
            dn.endSample = n.endSample;

            // key-on 段：音高 + 音色 + 音量，全部写原值
            Segment ks;
            ks.atSample = n.startSample;
            ks.regs.push_back(Rv(0x10 + ch, fn.fnumLow));
            ks.regs.push_back(Rv(0x20 + ch, static_cast<uint8_t>(dhBase | 0x10)));  // key on
            ks.regs.push_back(Rv(0x30 + ch, static_cast<uint8_t>((m.program << 4) | vol)));
            dn.segments.push_back(ks);

            // 音符中间若源 TL 变化，跟随写 0x30（值变了才写）
            int lastVol = vol;
            for (const Segment& s : n.segments) {
                if (s.atSample <= n.startSample) continue;
                for (const RegValue& r : s.regs) {
                    if (r.reg < 0x60 || r.reg > 0x7F) continue;
                    shadow[r.reg] = r.val;
                    if ((r.reg & 0x07) != ch) continue;
                    const int tl = CarrierMinTL(shadow, ch, shadow[0x20 + ch] & 7);
                    int v2 = (tl >> 3) + m.volumeOffset + opt.volumeOffsetBias;
                    v2 = std::max(0, std::min(15, v2));
                    if (v2 == lastVol) continue;
                    lastVol = v2;
                    if (!dn.segments.empty() && dn.segments.back().atSample == s.atSample) {
                        dn.segments.back().regs.push_back(
                            Rv(0x30 + ch, static_cast<uint8_t>((m.program << 4) | v2)));
                    } else {
                        Segment seg;
                        seg.atSample = s.atSample;
                        seg.regs.push_back(
                            Rv(0x30 + ch, static_cast<uint8_t>((m.program << 4) | v2)));
                        dn.segments.push_back(seg);
                    }
                }
            }

            // key-off 段：0x20 去掉 bit4
            Segment ko;
            ko.atSample = n.endSample;
            ko.regs.push_back(Rv(0x20 + ch, dhBase));  // key off
            dn.segments.push_back(ko);

            dc.notes.push_back(dn);
            out.notesConverted++;
            NoteConvertReport rep;
            rep.sourceChannel = ch;
            rep.targetChannel = ch;
            rep.program = m.program;
            rep.volumeOffset = m.volumeOffset;
            rep.octaveOffset = octaveOffset;
            rep.srcCarrierTl = carrierTl;
            rep.kc = kc;
            rep.kf = kf;
            rep.freqHz = freq;
            out.reports.push_back(rep);
        }
    }

    // 用户音色槽（可选）：把最后一个音色写成 0x00-0x07（仅单音色曲目合理）
    if (opt.useUserVoice && !src.channels.empty()) {
        uint8_t shadow[256] = {0};
        for (const Orphan& o : src.channels[0].orphans)
            for (const RegValue& r : o.regs) shadow[r.reg] = r.val;
        const OpmVoice opm = SnapshotOpmVoice(shadow, 0);
        const OpllVoice oll = OplToOpllVoice(OpnToOpl(OpmToOpn(opm), true));
        uint8_t bytes[8];
        OpllEncode(oll, bytes);
        GlobalSegment gs;
        gs.atSample = 0;
        for (int i = 0; i < 8; i++) gs.regs.push_back(Rv(static_cast<uint8_t>(i), bytes[i]));
        doc.globals.push_back(gs);
        doc.meta.unhandled.push_back("userVoice: 0x00-0x07 由 opm2opll 写入（仅单音色合理）");
    }

    // 目标文档不做位精确往返，raw 留空
    doc.raw = RawData{};
    out.ok = true;
    return out;
}

}  // namespace vgmstudio
