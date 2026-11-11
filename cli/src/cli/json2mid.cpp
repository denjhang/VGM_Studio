// json2mid —— VGSJ JSON → Standard MIDI File（音高验证工具）
//
// 目的：把"原芯片 JSON"和"目标芯片 JSON"各自转成 MIDI，直接对比音高是否正确。
//
// 音高规则全部来自 docs/PITCH-CONVERSION.md（原始出处见 docs/reference/_pitch-*.md）：
//   YM2151: Module/YM2151.bas:26-53, 171-201  —— KC 查表 + KF/64 半音，不用 Hz
//   YM2413: Modules/YM2413.bas:46-50, 178-192 —— Hz = FNum*fsam*2^(Block-19)
//   时间:   Modules/Midi_f0.bas:493-514, 625-631 —— 绝对采样取整 + 差值（无漂移）
//   弯音:   Prog/VGM2MID.bas:221-243 —— 整数 = note，小数 = pitch bend，4096 步/半音
//
// MIDI 写入用 cli/third_party/midifile（Craig Sapp smf，从 Denjhang 库直接复制）。
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "Document.h"
#include "FmVoice.h"
#include "MidiFile.h"

namespace {

// 运算符倍频表（libvgm ym2413.c:422-426 的 mul_tab，与 opl-voice.js 的 _ml_tbl 一致）
constexpr double kMlTable[16] = {0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};

// ALG → 载波掩码（bit0=OP1 … bit3=OP4），docs/CHIP-YM2151.md 定稿
constexpr uint8_t kAlgCarriers[8] = {0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF};

// 载波的 ML 倍频：FM 的**实际基频 = 音高频率 × 载波 ML**。
// 不乘这一步，两个芯片的音高对比就会差出整数个八度。
double Ym2151CarrierMl(const uint8_t shadow[256], int ch) {
    const int con = shadow[0x20 + ch] & 7;
    const uint8_t mask = kAlgCarriers[con];
    for (int op = 0; op < 4; op++) {
        if ((mask >> op) & 1) {
            const int ml = shadow[0x40 + op * 8 + ch] & 0x0F;
            return kMlTable[ml];
        }
    }
    return 1.0;
}

// YM2413 的载波 ML：program != 0 时来自 ROM 表（slot1 = 载波）；
// program == 0（用户音色）时来自全局寄存器 0x00-0x07。
double Ym2413CarrierMl(uint8_t instVol, const vgmstudio::GlobalSegment* userVoice) {
    const int program = (instVol >> 4) & 0x0F;
    if (program == 0) {
        if (userVoice != nullptr) {
            for (const vgmstudio::RegValue& r : userVoice->regs)
                if (r.reg == 0x01) return kMlTable[r.val & 0x0F];
        }
        return 1.0;
    }
    const std::vector<vgmstudio::OpllVoice>& bank =
        vgmstudio::OpllRomBank(vgmstudio::OpllBank::Ym2413);
    if (program < static_cast<int>(bank.size()))
        return kMlTable[bank[program].slots[1].ml & 0x0F];
    return 1.0;
}

// ── 命令行 ──────────────────────────────────────────────────────────
struct Options {
    std::string input;
    std::string output;
    int ppqn = 480;              // frmOptions.frm 的默认精度
    double bpm = 120.0;          // Valley Bell 默认 120 BPM
    int octaveOffset = 25;       // 物理真值（docs/PITCH-CONVERSION.md §1.4）
    bool pitchBend = true;
    bool report = false;
};

void Usage() {
    std::printf(
        "usage: json2mid <input.json> -o <out.mid> [options]\n"
        "\n"
        "  --ppqn=480             MIDI ticks per quarter note (30|480|2205|22050)\n"
        "  --bpm=120              时间轴 BPM（只影响 tick 换算，不影响音高）\n"
        "  --octave-offset=25     YM2151 的八度基准（物理真值 25；Valley Bell 等价 13）\n"
        "  --no-pitchbend         不输出弯音，只取整到最近的半音\n"
        "  --report               打印逐音符 note/cents\n");
}

std::string DefaultOutput(const std::string& in) {
    const size_t dot = in.find_last_of('.');
    const size_t sep = in.find_last_of("/\\");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep))
        return in.substr(0, dot) + ".mid";
    return in + ".mid";
}

// ── 芯片识别 ────────────────────────────────────────────────────────
enum class Chip { Unknown, Ym2151, Ym2413 };

Chip DetectChip(const vgmstudio::Document& doc) {
    for (const vgmstudio::ChipEntry& c : doc.chips) {
        if (c.vgmChipType == 0x03 || c.type == "YM2151") return Chip::Ym2151;
        if (c.vgmChipType == 0x01 || c.type == "YM2413") return Chip::Ym2413;
    }
    return Chip::Unknown;
}

const char* ChipName(Chip c) {
    switch (c) {
        case Chip::Ym2151: return "YM2151";
        case Chip::Ym2413: return "YM2413";
        default: return "?";
    }
}

// ── YM2151 音高 ─────────────────────────────────────────────────────
// YM2151.bas:37-49 / libvgm ym2151.c:1067 —— 半音索引 = kc - (kc>>2)
// KC 低 4 位 0..12 跳过 3/7/11/15，正好等价于 kc-(kc>>2)。
inline int Ym2151Semitone(uint8_t kc) {
    return static_cast<int>(kc) - (static_cast<int>(kc) >> 2);
}

// 浮点音高（半音）：整数 = MIDI 音号，小数 = 半音内偏移。
// YM2151.bas:180 是 61 + ... + (Block-4)*12；物理真值见 PITCH-CONVERSION.md §1.4
// 为 25 + (kc-(kc>>2))，两者是同一公式的不同常数项（25 = 61 - 48 + 12）。
//
// clockRatio：KC 是"音名"，它在芯片上产生的绝对频率与时钟成正比
// （libvgm ym2151.c:600-620 的 scaler = (clock/64)/sampfreq，基准 3579545 Hz）。
// 所以实际听到的音高 = 标准音高 + 12*log2(clock/3579545)。
inline double Ym2151Pitch(uint8_t kc, uint8_t kf, int octaveOffset, double clockRatio) {
    const int frac = (kf >> 2) & 0x3F;  // YM2151.bas:196
    const double base = static_cast<double>(Ym2151Semitone(kc) + octaveOffset) +
                        static_cast<double>(frac) / 64.0;
    return base + clockRatio;
}

// ── YM2413 音高 ─────────────────────────────────────────────────────
// YM2413.bas:46-50:  Hz = FNum * fsam * 2^(Block-19)，fsam = clock/72
inline double Ym2413Hz(uint8_t fnumLow, uint8_t fnumHigh, uint32_t clock) {
    const int fnum = ((fnumHigh & 0x01) << 8) | fnumLow;
    const int block = (fnumHigh >> 1) & 0x07;
    if (fnum == 0) return 0.0;
    const double fsam = static_cast<double>(clock) / 72.0;
    return static_cast<double>(fnum) * fsam * std::pow(2.0, block - 19);
}

// VGM2MID.bas:246-250:  Note = (log2(Hz/440))*12 + 69
inline double HzToNote(double hz) {
    if (hz <= 0.0) return -1.0;
    return std::log2(hz / 440.0) * 12.0 + 69.0;
}

// ── 时间换算（Midi_f0.bas:493-514, 625-631）─────────────────────────
struct TimeMapper {
    double samplesPerTick = 1.0;

    TimeMapper(int ppqn, double bpm) {
        // DeltaFact = 22050 / TickspQuarter * 120 / TEMPO_BPM
        samplesPerTick = 22050.0 / static_cast<double>(ppqn) * 120.0 / bpm;
    }

    int Tick(uint32_t sample) const {
        return static_cast<int>(std::floor(sample / samplesPerTick + 0.5));
    }
};

// ── 事件收集 ────────────────────────────────────────────────────────
struct RawWrite {
    uint32_t at = 0;
    uint8_t reg = 0;
    uint8_t val = 0;
    uint32_t seq = 0;
};

std::vector<RawWrite> CollectChannelWrites(const vgmstudio::Channel& ch) {
    std::vector<RawWrite> out;
    uint32_t seq = 0;
    for (const vgmstudio::Orphan& o : ch.orphans)
        for (const vgmstudio::RegValue& r : o.regs)
            out.push_back(RawWrite{o.atSample, r.reg, r.val, seq++});
    for (const vgmstudio::Note& n : ch.notes)
        for (const vgmstudio::Segment& s : n.segments)
            for (const vgmstudio::RegValue& r : s.regs)
                out.push_back(RawWrite{s.atSample, r.reg, r.val, seq++});
    std::stable_sort(out.begin(), out.end(), [](const RawWrite& a, const RawWrite& b) {
        if (a.at != b.at) return a.at < b.at;
        return a.seq < b.seq;
    });
    return out;
}

// ── 一个待写出的音符 ────────────────────────────────────────────────
struct RenderedNote {
    uint32_t startSample = 0;
    uint32_t endSample = 0;
    int channel = 0;
    int note = 60;
    double cents = 0.0;          // 相对整数音号的偏移（-50..+50）
    double pitch = 60.0;         // 连续音高
    std::vector<std::pair<uint32_t, double>> bends;  // (sample, 连续音高)
};

void SplitPitch(double pitch, int* note, double* fracSemitone) {
    // VGM2MID.bas:223-228：小数 >= 0.5 就进一位，弯音变负
    double n = std::floor(pitch);
    double frac = pitch - n;
    if (frac >= 0.5) {
        n += 1.0;
        frac -= 1.0;
    }
    if (n < 0.0) { n = 0.0; frac = 0.0; }
    if (n > 127.0) { n = 127.0; frac = 0.0; }
    *note = static_cast<int>(n);
    *fracSemitone = frac;
}

std::vector<RenderedNote> RenderYm2151(const vgmstudio::Document& doc, const Options& opt) {
    // 实际时钟对绝对频率的影响：libvgm ym2151.c:600-612 的 scaler = (clock/64)/rate，
    // 基准 3579545 Hz（COLOR_NTSC）。clock=4 MHz 时整曲高 12*log2(4e6/3579545) = 1.92 半音。
    uint32_t clock = 3579545;
    for (const vgmstudio::ChipEntry& c : doc.chips)
        if (c.clock != 0) { clock = c.clock; break; }
    const double clockRatio = 12.0 * std::log2(static_cast<double>(clock) / 3579545.0);

    std::vector<RenderedNote> out;
    for (size_t ci = 0; ci < doc.channels.size(); ci++) {
        const vgmstudio::Channel& chn = doc.channels[ci];
        if (chn.index < 0 || chn.index > 7) continue;
        const std::vector<RawWrite> stream = CollectChannelWrites(chn);
        uint8_t shadow[256] = {0};
        size_t cur = 0;

        for (const vgmstudio::Note& n : chn.notes) {
            // 重放到音符起点，取此刻的 KC/KF
            while (cur < stream.size() && stream[cur].at <= n.startSample) {
                shadow[stream[cur].reg] = stream[cur].val;
                cur++;
            }
            uint8_t kc = shadow[0x28 + chn.index];
            uint8_t kf = shadow[0x30 + chn.index];
            // 实际发声基频 = KC/KF 音高 + 12*log2(载波 ML)
            const double mlSemis = 12.0 * std::log2(Ym2151CarrierMl(shadow, chn.index));
            const double p0 = Ym2151Pitch(kc, kf, opt.octaveOffset, clockRatio) + mlSemis;

            RenderedNote rn;
            rn.startSample = n.startSample;
            rn.endSample = n.endSample > n.startSample ? n.endSample : n.startSample + 1;
            rn.channel = chn.index;
            rn.pitch = p0;
            int note = 60;
            double frac = 0.0;
            SplitPitch(p0, &note, &frac);
            rn.note = note;
            rn.cents = frac * 100.0;

            // 音符内部的音高变化（0x28 / 0x30），转成弯音
            uint8_t inner[256];
            std::memcpy(inner, shadow, sizeof(inner));
            size_t ic = cur;
            bool pitchDirty = false;
            while (ic < stream.size() && stream[ic].at < rn.endSample) {
                const RawWrite& w = stream[ic];
                inner[w.reg] = w.val;
                if (w.reg == 0x28 + chn.index || w.reg == 0x30 + chn.index) pitchDirty = true;
                if (pitchDirty && stream[ic].at > n.startSample) {
                    rn.bends.emplace_back(w.at,
                                          Ym2151Pitch(inner[0x28 + chn.index],
                                                      inner[0x30 + chn.index], opt.octaveOffset,
                                                      clockRatio));
                    pitchDirty = false;
                }
                ic++;
            }
            out.push_back(std::move(rn));
        }
    }
    return out;
}

std::vector<RenderedNote> RenderYm2413(const vgmstudio::Document& doc, const Options& opt) {
    uint32_t clock = 3579545;
    for (const vgmstudio::ChipEntry& c : doc.chips)
        if (c.clock != 0) { clock = c.clock; break; }

    std::vector<RenderedNote> out;
    for (size_t ci = 0; ci < doc.channels.size(); ci++) {
        const vgmstudio::Channel& chn = doc.channels[ci];
        if (chn.index < 0 || chn.index > 8) continue;
        const std::vector<RawWrite> stream = CollectChannelWrites(chn);
        uint8_t shadow[256] = {0};
        size_t cur = 0;

        for (const vgmstudio::Note& n : chn.notes) {
            while (cur < stream.size() && stream[cur].at <= n.startSample) {
                shadow[stream[cur].reg] = stream[cur].val;
                cur++;
            }
            const double hz = Ym2413Hz(shadow[0x10 + chn.index], shadow[0x20 + chn.index], clock);
            if (hz <= 0.0) continue;
            // 同样要乘载波 ML：OPLL 的音高寄存器只管 fnum，实际基频还要看音色的载波倍频
            const vgmstudio::GlobalSegment* userVoice =
                doc.globals.empty() ? nullptr : &doc.globals.front();
            const double mlSemis = 12.0 * std::log2(
                Ym2413CarrierMl(shadow[0x30 + chn.index], userVoice));
            const double p0 = HzToNote(hz) + mlSemis;

            RenderedNote rn;
            rn.startSample = n.startSample;
            rn.endSample = n.endSample > n.startSample ? n.endSample : n.startSample + 1;
            rn.channel = chn.index;
            rn.pitch = p0;
            int note = 60;
            double frac = 0.0;
            SplitPitch(p0, &note, &frac);
            rn.note = note;
            rn.cents = frac * 100.0;

            uint8_t inner[256];
            std::memcpy(inner, shadow, sizeof(inner));
            size_t ic = cur;
            bool dirty = false;
            while (ic < stream.size() && stream[ic].at < rn.endSample) {
                const RawWrite& w = stream[ic];
                inner[w.reg] = w.val;
                if (w.reg == 0x10 + chn.index || w.reg == 0x20 + chn.index) dirty = true;
                if (dirty && stream[ic].at > n.startSample) {
                    const double h = Ym2413Hz(inner[0x10 + chn.index],
                                              inner[0x20 + chn.index], clock);
                    if (h > 0.0) rn.bends.emplace_back(w.at, HzToNote(h) + mlSemis);
                    dirty = false;
                }
                ic++;
            }
            out.push_back(std::move(rn));
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            Usage();
            return 0;
        }
        if (std::strcmp(a, "-o") == 0 && i + 1 < argc) {
            opt.output = argv[++i];
        } else if (std::strncmp(a, "--ppqn=", 7) == 0) {
            opt.ppqn = std::atoi(a + 7);
        } else if (std::strncmp(a, "--bpm=", 6) == 0) {
            opt.bpm = std::atof(a + 6);
        } else if (std::strncmp(a, "--octave-offset=", 16) == 0) {
            opt.octaveOffset = std::atoi(a + 16);
        } else if (std::strcmp(a, "--no-pitchbend") == 0) {
            opt.pitchBend = false;
        } else if (std::strcmp(a, "--report") == 0) {
            opt.report = true;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            Usage();
            return 1;
        } else if (opt.input.empty()) {
            opt.input = a;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", a);
            return 1;
        }
    }
    if (opt.input.empty()) {
        Usage();
        return 1;
    }
    if (opt.output.empty()) opt.output = DefaultOutput(opt.input);
    if (opt.ppqn <= 0) opt.ppqn = 480;
    if (opt.bpm <= 0.0) opt.bpm = 120.0;

    vgmstudio::Document doc;
    std::string err;
    if (!vgmstudio::Document::LoadJson(opt.input, &doc, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    const Chip chip = DetectChip(doc);
    if (chip == Chip::Unknown) {
        std::fprintf(stderr, "error: unsupported chip (need YM2151 or YM2413)\n");
        return 3;
    }

    std::vector<RenderedNote> notes =
        (chip == Chip::Ym2151) ? RenderYm2151(doc, opt) : RenderYm2413(doc, opt);
    if (notes.empty()) {
        std::fprintf(stderr, "error: no notes to render\n");
        return 3;
    }
    std::sort(notes.begin(), notes.end(), [](const RenderedNote& a, const RenderedNote& b) {
        return a.startSample < b.startSample;
    });

    const TimeMapper tm(opt.ppqn, opt.bpm);

    bool usedChans[16] = {false};

    smf::MidiFile mf;
    // MidiFile 构造时已自带 1 个 track，不要再 addTrack（会多出空 track，
    // write() 里对空 track 调 back() 会崩）。
    mf.setTicksPerQuarterNote(opt.ppqn);
    mf.addTempo(0, 0, opt.bpm);
    // 弯音范围 ±2 半音（VGM2MID.bas:19 的 PITCHWHEEL_SENSITIVITY_DEFAULT）。
    // 只对实际用到的通道发 RPN，避免污染全部 16 通道。
    for (const RenderedNote& n : notes) usedChans[n.channel] = true;
    for (int ch = 0; ch < 16; ch++)
        if (usedChans[ch]) mf.setPitchBendRange(0, 0, ch, 2.0);

    // 同一 tick 内：先 note-off 再 note-on（避免同音互相掐断）
    struct Pending {
        int tick;
        int prio;   // 0 = note off, 1 = bend, 2 = note on
        uint32_t seq;
        int kind;
        int ch, note;
        double amount;
    };
    std::vector<Pending> ev;
    uint32_t seq = 0;
    for (const RenderedNote& n : notes) {
        ev.push_back(Pending{tm.Tick(n.startSample), 2, seq++, 2, n.channel, n.note, 0.0});
        ev.push_back(Pending{tm.Tick(n.endSample), 0, seq++, 0, n.channel, n.note, 0.0});
        if (opt.pitchBend) {
            int startNote = 0;
            double frac0 = 0.0;
            SplitPitch(n.pitch, &startNote, &frac0);
            if (std::fabs(frac0) > 1e-9)
                ev.push_back(Pending{tm.Tick(n.startSample), 1, seq++, 1, n.channel, startNote,
                                     frac0 / 2.0});
            for (const auto& b : n.bends) {
                int bn = 0;
                double bf = 0.0;
                SplitPitch(b.second, &bn, &bf);
                if (bn != n.note) continue;  // 跨半音的音高变化不在此处理
                if (std::fabs(bf) <= 1e-9) continue;
                ev.push_back(Pending{tm.Tick(b.first), 1, seq++, 1, n.channel, 0, bf / 2.0});
            }
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Pending& a, const Pending& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.prio != b.prio) return a.prio < b.prio;
        return a.seq < b.seq;
    });
    for (const Pending& e : ev) {
        if (e.kind == 0) {
            mf.addNoteOff(0, e.tick, e.ch, e.note, 64);
        } else if (e.kind == 1) {
            mf.addPitchBend(0, e.tick, e.ch, e.amount);
        } else {
            mf.addNoteOn(0, e.tick, e.ch, e.note, 100);
        }
    }
    mf.sortTracks();

    if (!mf.write(opt.output)) {
        std::fprintf(stderr, "error: cannot write %s\n", opt.output.c_str());
        return 2;
    }

    std::printf("chip          : %s\n", ChipName(chip));
    std::printf("notes         : %zu\n", notes.size());
    std::printf("ppqn/bpm      : %d / %.1f  (samples per tick = %.4f)\n", opt.ppqn, opt.bpm,
                tm.samplesPerTick);
    if (chip == Chip::Ym2151)
        std::printf("octave offset : %d (physical truth 25)\n", opt.octaveOffset);
    std::printf("written       : %s\n", opt.output.c_str());

    if (opt.report) {
        std::printf("\n  %-8s %-8s %-3s %-4s %-5s %s\n", "startSmp", "endSmp", "ch", "note",
                    "cents", "pitch");
        for (const RenderedNote& n : notes) {
            std::printf("  %-8u %-8u %-3d %-4d %+-5.1f %.3f\n", n.startSample, n.endSample,
                        n.channel, n.note, n.cents, n.pitch);
        }
    }
    return 0;
}
