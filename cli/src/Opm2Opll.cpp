// Opm2Opll.cpp —— 见头文件注释。
#include "Opm2Opll.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "AnalyzerYm2151.h"
#include "FmVoice.h"
#include "FuiVoice.h"
#include "MacroEngine.h"

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

// 音色哈希：只取定义音色的寄存器（排除 0x60 TL = 每音符音量）。
// 与 Importer.cpp 的 .fui 导出用同一判据，两边序号才能一一对应。
uint64_t VoiceHash(const uint8_t shadow[256], int ch) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint8_t v) { h = (h ^ v) * 1099511628211ull; };
    for (int op = 0; op < 4; op++) {
        const int base = op * 8 + ch;
        mix(shadow[0x40 + base]);
        mix(shadow[0x80 + base]);
        mix(shadow[0xA0 + base]);
        mix(shadow[0xC0 + base]);
        mix(shadow[0xE0 + base]);
    }
    mix(shadow[0x20 + ch] & 0x3F);
    mix(shadow[0x38 + ch]);
    return h;
}

// 是否含任何宏（通道级或算子级）
bool HasAnyMacro(const FuiVoice& v) {
    for (int i = 0; i < kMacroCount; i++)
        if (!v.macros[i].val.empty()) return true;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < kOpMacroCount; j++)
            if (!v.opMacros[i][j].val.empty()) return true;
    return false;
}

// 把匹配到的 OPLL ROM 音色展开成一个 FuiVoice（供用户审计/修改）
FuiVoice OpllRomToFui(int program, const char* name) {
    FuiVoice v;
    v.type = kFuiInsOpll;
    v.ops = 2;
    v.name = name != nullptr ? name : "opll";
    v.opllPreset = program;
    if (program <= 0 || program >= 19) return v;

    const std::vector<OpllVoice>& rom = OpllRomBank(OpllBank::Ym2413);
    const OpllVoice& o = rom[program];
    v.fb = o.fb;
    for (int i = 0; i < 2; i++) {
        const OpllSlot& s = o.slots[i];
        FuiOperator& d = v.op[i];
        d.am = s.am;
        d.ksr = s.kr;
        d.mult = s.ml;
        d.ksl = s.kl;
        d.tl = s.tl;
        d.ar = s.ar;
        d.dr = s.dr;
        d.sl = s.sl;
        d.rr = s.rr;
        d.egt = s.eg;
        d.ws = s.ws;
    }
    return v;
}



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

    // 0 号预设（唯一可写的用户音色）的内容：只有用户把某个 .fui 的 opllPreset
    // 改成 0 时才有值，此时把它的算子参数写进 0x00-0x07 全局段。
    FuiVoice userVoice;
    bool haveUserVoice = false;

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
        std::map<uint64_t, int> voiceOrdinal;
        std::map<uint64_t, OpllMatch> voiceProgram;  // 音色 → 首次匹配结果（含 program/volume/octave）
        std::map<uint64_t, FuiVoice> voiceFui;       // 音色 → 用户改过的 .fui（含宏）
        for (const Note& n : sc.notes) {
            // 重放到音符起始
            while (cur < stream.size() && stream[cur].at <= n.startSample) {
                shadow[stream[cur].reg] = stream[cur].val;
                cur++;
            }

            const OpmVoice opm = SnapshotOpmVoice(shadow, ch);
            const OpnVoice opn = OpmToOpn(opm);
            const OplVoice opl = OpnToOpl(opn, /*keyOn=*/true);

            // 音色按哈希去重编号（与 vgm2json --fui 的 chXinsY 一一对应）。
            // 匹配与 .fui 写出只在音色首次出现时做一次；
            // 否则 2515 个音符会写出 2515 份 .fui（实测 fui=2534 files）。
            const uint64_t vh = VoiceHash(shadow, ch);
            int ordinal = 0;
            auto oi = voiceOrdinal.find(vh);
            OpllMatch m;
            // 同一音色的匹配结果在首次出现时确定，后续音符必须沿用
            // （否则用了 --use-edited-fui 时，之后的音符会退回自动匹配）
            auto pm = voiceProgram.find(vh);
            if (pm != voiceProgram.end()) {
                ordinal = oi->second;
                m = pm->second;
                goto have_match;
            }
            if (oi == voiceOrdinal.end()) {
                ordinal = static_cast<int>(voiceOrdinal.size()) + 1;
                voiceOrdinal[vh] = ordinal;
                m = MatchOpllRom(opl, bank);   // ← 自动匹配只在这里做一次

                char baseName[32];
                std::snprintf(baseName, sizeof(baseName), "ch%dins%d", ch + 1, ordinal);

                // ★ 用户改过的 .fui 优先：同名 ym2413.fui 存在且开了 useEditedFui 时，
                //   用它的 program（音色号）覆盖自动匹配结果。
                //   opllPreset == 0 是合法的：0 号槽是唯一可写的用户音色，
                //   只有它能被算子级宏改动，也正是 patch 切换之外 OPLL 唯一的自由音色。
                bool usedEdited = false;
                FuiVoice editedVoice;
                if (opt.useEditedFui) {
                    char fn[64];
                    std::snprintf(fn, sizeof(fn), "ch%dins%d_ym2413.fui", ch + 1, ordinal);
                    const std::string path = opt.fuiDir.empty() ? fn : (opt.fuiDir + "/" + fn);
                    std::string ferr;
                    if (LoadFui(path, &editedVoice, &ferr)) {
                        if (editedVoice.opllPreset >= 0 && editedVoice.opllPreset < 19) {
                            m.program = editedVoice.opllPreset;
                        }
                        usedEdited = true;
                        out.fuiApplied.push_back(path);
                        if (m.program == 0 && !haveUserVoice) {
                            userVoice = editedVoice;
                            haveUserVoice = true;
                        }
                    }
                }

                voiceProgram[vh] = m;
                if (usedEdited) voiceFui[vh] = editedVoice;
                if (opt.exportFui) {
                    auto save = [&](const FuiVoice& v, const char* suffix) {
                        char fn[64];
                        std::snprintf(fn, sizeof(fn), "ch%dins%d_%s.fui", ch + 1, ordinal, suffix);
                        const std::string path = opt.fuiDir.empty() ? fn : (opt.fuiDir + "/" + fn);
                        std::string ferr;
                        if (SaveFui(v, path, &ferr)) {
                            out.fuiFiles.push_back(path);
                        } else {
                            out.doc.meta.unhandled.push_back("fui 写出失败: " + ferr);
                        }
                    };
                    FuiVoice srcVoice = OpmShadowToFui(shadow, ch);
                    srcVoice.name = baseName;
                    save(srcVoice, "ym2151");
                    if (!usedEdited) {
                        FuiVoice dstVoice = OpllRomToFui(m.program, baseName);
                        save(dstVoice, "ym2413");
                    }
                }
            } else {
                ordinal = oi->second;
            }
        have_match:


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


            // ── 宏展开（全部 22 条通道级 + 4×20 条算子级宏）──
            // 时间基准：一个宏步 = speed 个 tick，1 tick = 1/tickRate 秒。
            // ★ .fui 里没有 tickRate，它来自调用方的 --tick-rate（Furnace 的 song.hz）。
            if (opt.runMacros) {
                // 只认目标侧（ym2413.fui）的宏：宏的语义是芯片相关的，
                // 源侧 OPM 的宏不能套到 OPLL 上。
                const FuiVoice* fv = nullptr;
                auto fit = voiceFui.find(vh);
                if (fit != voiceFui.end()) fv = &fit->second;
                if (fv != nullptr && HasAnyMacro(*fv)) {
                    MacroExpandInput mi;
                    mi.ch = ch;
                    mi.baseKc = kc;
                    mi.baseKf = kf;
                    // OPLL 的通道音量满量程是 15（opll.cpp:1144 chan[i].vol=15），
                    // 把已算好的目标音量 vol 直接当 baseVol 传进去，音量宏才是
                    // 在「当前音符音量」上做相对衰减，而不是从满量程重算。
                    mi.baseVol = vol;
                    mi.startSample = n.startSample;
                    mi.endSample = n.endSample;
                    mi.sampleRate = doc.timeBase.sampleRate > 0 ? doc.timeBase.sampleRate : 44100;
                    mi.tickRate = opt.tickRate;
                    mi.opll = true;
                    mi.opllPreset = m.program;
                    const MacroExpandOutput mo = ExpandMacros(*fv, mi);

                    for (const MacroWrite& w : mo.writes) {
                        Segment seg;
                        seg.atSample = w.atSample;
                        // 宏输出里 0x30 的 val 已含 inst 高 4 位；其余直接照抄
                        seg.regs.push_back(Rv(w.reg, w.val));
                        dn.segments.push_back(seg);
                    }
                    // phaseReset / ex4：重触发。
                    // ★ 真机行为：YM2413 在 key 位已置位时再写 key-on 不会重触发
                    //   （libvgm ym2413.c 的 KEY_ON 注释 "do NOT restart Phase Generator
                    //    (verified on real YM2413)"，只有 !SLOT->key 时才回到 EG_DMP）。
                    //   所以必须先 key-off 再 key-on，两条写落在同一采样点、按顺序执行
                    //   （这正是 Furnace arcade.cpp:484-533 的做法：先 immWrite(0x08,i)，
                    //    随后 immWrite(0x08,(opMask<<3)|i)）。
                    // OPLL 的 alg 宏 = 0x20 的 bit5（SUS / EG-S），与 preset 无关：
                    // 它总在**下一次**写 0x20 时随 fnum 高位一起吐出（opll.cpp:258）。
                    // 只有用户音色（preset==0）Furnace 才额外置 freqChanged → 立即重写。
                    // 所以这里按时间顺序取「截至该采样点最新的 sus 值」，并只对
                    // immediate 的那些点额外补一次独立写。
                    auto susAt = [&mo](uint32_t at) {
                        int s = 0;
                        for (const MacroSus& p : mo.susPoints) {
                            if (p.atSample > at) break;
                            s = p.sus;
                        }
                        return s != 0 ? 0x20 : 0x00;
                    };

                    for (const MacroRetrigger& rt : mo.retriggers) {
                        Segment seg;
                        seg.atSample = rt.atSample;
                        const uint8_t sus = static_cast<uint8_t>(susAt(rt.atSample));
                        seg.regs.push_back(Rv(0x20 + ch,
                                              static_cast<uint8_t>(dhBase | sus)));  // key off
                        seg.regs.push_back(Rv(0x20 + ch,
                                              static_cast<uint8_t>(dhBase | sus | 0x10))); // key on
                        dn.segments.push_back(seg);
                    }

                    // arp / pitch 宏：按偏移重算 fnum，落在变化点
                    for (const MacroPitch& p : mo.pitches) {
                        if (p.offset128 == 0) continue;
                        const double semi = p.offset128 / 128.0;
                        const double f2 = freq * std::pow(2.0, semi / 12.0);
                        const OpllFnum fn2 = FreqToOpllFnum(f2, opt.targetClock);
                        const int bO = std::min(7, std::max(0, static_cast<int>(fn2.block) + octaveOffset));
                        Segment seg;
                        seg.atSample = p.atSample;
                        seg.regs.push_back(Rv(0x10 + ch, fn2.fnumLow));
                        seg.regs.push_back(Rv(0x20 + ch,
                                              static_cast<uint8_t>((bO << 1) |
                                                                   ((fn2.fnum >> 8) & 1) | 0x10 |
                                                                   susAt(p.atSample))));
                        dn.segments.push_back(seg);
                    }
                    // immediate 的 sus 变化点（用户音色）：单独写一次 0x20，
                    // 保住 fnum 与 key 位。ROM 预置下 Furnace 不立即写，等下次 key。
                    for (const MacroSus& p : mo.susPoints) {
                        if (!p.immediate) continue;
                        Segment seg;
                        seg.atSample = p.atSample;
                        seg.regs.push_back(Rv(0x20 + ch,
                                              static_cast<uint8_t>(dhBase | 0x10 |
                                                                   (p.sus ? 0x20 : 0x00))));
                        dn.segments.push_back(seg);
                    }
                    std::stable_sort(dn.segments.begin(), dn.segments.end(),
                                     [](const Segment& a, const Segment& b) {
                                         return a.atSample < b.atSample;
                                     });
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

    // 用户音色槽（0 号预设）
    //   ① 用户把某个 .fui 的 opllPreset 改成 0 → 用它那份算子参数（最高优先级）
    //   ② 否则沿用 --user-voice，把源通道 0 的音色写进 0x00-0x07
    if (haveUserVoice) {
        uint8_t uv[256] = {0};
        FuiToOpllUserVoice(userVoice, uv);
        GlobalSegment gs;
        gs.atSample = 0;
        for (int i = 0; i < 8; i++) gs.regs.push_back(Rv(static_cast<uint8_t>(i), uv[i]));
        doc.globals.push_back(gs);
        doc.meta.unhandled.push_back(
            "userVoice: 0x00-0x07 取自用户编辑的 .fui（opllPreset=0）");
    } else if (opt.useUserVoice && !src.channels.empty()) {
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
