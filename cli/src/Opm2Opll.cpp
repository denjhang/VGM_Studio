// Opm2Opll.cpp —— 见头文件注释。
#include "Opm2Opll.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

#include "AnalyzerYm2151.h"
#include "FmVoice.h"
#include "FuiVoice.h"
#include "FurFile.h"
#include "InstrumentLibrary.h"
#include "MacroEngine.h"

namespace vgmstudio {
namespace {

constexpr int kSrcChannels = 8;

// ── 音量台阶的物理换算（实测，见 cli/tools/probe_volume.py 与 docs/VOICE-FUI.md §8.5.4）──
//   YM2151 载波 TL：7 bit，每单位 0.7526 dB（实测 TL 0→80 共 −60.98 dB，严格线性）
//   YM2413 0x30 低 4 位：0..15 是**衰减量**，每步 3.01 dB（实测 0→12 共 −36.07 dB）
// 两者比值正好 4：`4 × 0.7526 = 3.0104 dB`。这与寄存器位宽也一致——
//   OPM  `op->tl = (v&0x7f)<<3`（ym2151.c:1143）
//   OPLL `SLOT->TL = ((v&0x0f)<<2)<<1`（ym2413.c:1695）
// 都把值左移进同一个 10 bit 包络域，所以 nibble n 恰好等于 TL 4n。
// 旧代码写 `tl >> 3`（÷8）是 vgm-conv 对 OPN 的 0.75 dB/单位 的近似，
// 放到 OPM 上把 4 倍关系压成了 8 倍：本曲 TL 13..26 的 9.8 dB 动态范围
// 被压成 2 步（6 dB），且所有通道整体偏响约 9 dB。
constexpr int kTlPerOpllStep = 4;

constexpr int kDstChannels = 9;

// 跨曲目音色库的「相似」容差。0 = 参数完全一致。
// 同一套曲目里同一件乐器在不同曲子上，OPM 参数常只差几个 EG 值
// （ar/dr/sl/rr 各差 1~3），累加距离落在 10~40；alg 不同会直接返回 1000，
// 那种情况宁可退回 ROM 自动匹配，也不硬套一个结构不同的音色。
constexpr int kLibMaxDistance = 48;

// "ch5ins3-..." → 5（1 基）→ 返回 4（0 基通道号）。解析失败返回 -1。
int ChannelFromInstrumentName(const std::string& name) {
    if (name.size() < 3 || name[0] != 'c' || name[1] != 'h') return -1;
    size_t i = 2;
    int n = 0;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) {
        n = n * 10 + (name[i] - '0');
        i++;
    }
    if (n <= 0) return -1;
    return n - 1;
}


OpllBank ParseBank(const std::string& name) {
    if (name == "vrc7") return OpllBank::Vrc7;
    if (name == "ymf281b") return OpllBank::Ymf281b;
    return OpllBank::Ym2413;
}

// 把 OPM 影子寄存器（0x20..0xFF 的通道部分）读成一个 OPMVoice。
// 索引与 libvgm 一致：op*8 + ch。
OpmVoice SnapshotOpmVoice(const uint8_t shadow[256], int ch) {    OpmVoice v;
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

int TlvToOpllVolume(int carrierTl) {
    const int t = carrierTl < 0 ? 0 : (carrierTl > 127 ? 127 : carrierTl);
    const int v = (t + kTlPerOpllStep / 2) / kTlPerOpllStep;   // 四舍五入
    return v > 15 ? 15 : v;
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

    // .fur 工程：一次读出全部音色（用户在 Furnace 里改完存盘的唯一入口）。
    // 工程的 hz 就是宏的 tick 速率，比命令行猜 60 更准。
    FurSong fur;
    bool haveFur = false;
    int tickRate = opt.tickRate;
    if (!opt.furPath.empty()) {
        std::string ferr;
        if (!LoadFur(opt.furPath, &fur, &ferr)) {
            out.error = ferr;
            return out;
        }
        haveFur = true;
        out.furInstruments = fur.instruments.size();
        if (fur.hz > 0.5f) tickRate = (int)std::lround(fur.hz);
    }
    out.tickRateUsed = tickRate;

    // 跨曲目乐器库：按源侧 YM2151 FM 参数查表（不是指纹）。
    // 这是「利用现有音色成果」的主路径——库里存的是别的曲目已经调好的目标音色、
    // 宏与名字参数（note/vol/noteoff/link），命中就整套搬过来。
    InstrumentLibrary lib;
    bool haveLib = false;
    if (!opt.libraryPath.empty()) {
        std::string lerr;
        if (!LoadInstrumentLibrary(opt.libraryPath, &lib, &lerr)) {
            out.error = lerr;
            return out;
        }
        haveLib = true;
    }

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
    doc.tags = src.tags;          // GD3 标签原样透传（曲名/作者/日期）

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
    // furVoiceSeq：全局递增的目标音色序号，与 .fur 里 OPLL 乐器的排列顺序对应
    // （导出时就是按 ch1ins1, ch1ins2, ch2ins1… 的顺序追加的）。
    // 用户在 Furnace 里给乐器改名后，靠它仍能对上。
    int furVoiceSeq = 0;
    // 每个目标音符用的是哪个乐器基名（chXinsY）。link:X 只在该乐器真正发声的
    // 那些音符上联动，不能把整条通道的音符都算进去（鼓通道里有 4 个音色）。
    std::map<int, std::map<uint32_t, std::string>> noteVoiceOfChannel;
    // 每个 (通道, 音符起点) 对应的**源音色身份键**。link 的跨曲目复用靠它认人：
    // 库里的 link 条目记的是「叠在哪个源音色上」，本曲这个通道上未必叫同一个名字。
    std::map<int, std::map<uint32_t, std::string>> noteKeyOfChannel;
    // 每个 (通道, 音符起点) 在本曲里的目标乐器名，link 复制时按它改写名字。
    std::vector<std::string> linkEntriesApplied;
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
        std::map<uint64_t, InsTags> voiceTags;       // 音色 → 乐器名里的参数标签
        std::map<uint64_t, std::string> voiceFurName; // 音色 → 匹配到的 .fur 乐器全名
        std::map<uint64_t, std::string> voiceSrcKey;  // 音色 → 源侧身份键（不含 TL）
        // 该通道的 key 位当前是否置位。legato 切块（KC 跳变产生，源里没有 key 事件）
        // 必须沿用上一块的状态，否则会凭空产生一次 key-off + key-on。
        bool keyDown = false;
        // 当前这次发声的真实 key-on 时刻（宏从这里启动一次）
        uint32_t keyOnSample = 0;
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
                // 源音色身份键（不含 TL）。link:X 的跨曲目复用靠它认人。
                voiceSrcKey[vh] = OpmTimbreKey(OpmShadowToFui(shadow, ch));

                char baseName[32];
                std::snprintf(baseName, sizeof(baseName), "ch%dins%d", ch + 1, ordinal);

                // ★ 音色覆盖优先级：.fur 工程 > --use-edited-fui 单文件 > 自动匹配。
                //   .fur 里按乐器名 "chXinsY" 精确匹配（同一工程里还存着源侧
                //   OPM 音色 chXinsY-pad 之类，所以限定 type=13 OPLL）。
                //   opllPreset == 0 是合法的：0 号槽是唯一可写的用户音色，
                //   只有它能被算子级宏改动，也正是 patch 切换之外 OPLL 唯一的自由音色。
                bool usedEdited = false;
                FuiVoice editedVoice;
                // ★ 跨曲目乐器库优先于 .fur 的按名字匹配之外的一切：
                //   库里存的是「这套 YM2151 参数 → 已经调好的 YM2413 音色 + 宏 +
                //   note/vol/noteoff 参数」，判据是源侧 FM 参数本身，不是指纹
                //   （指纹混了 type/preset，只适合同曲目去重）。
                //   完全一致直接用，不完全一致取参数最近的。
                if (!usedEdited && haveLib && !lib.Empty()) {
                    const FuiVoice srcVoice = OpmShadowToFui(shadow, ch);
                    const LibraryEntry* le = lib.Find(srcVoice, kLibMaxDistance, ch);
                    if (le != nullptr) {
                        editedVoice = le->target;
                        if (editedVoice.opllPreset >= 0 && editedVoice.opllPreset < 19) {
                            m.program = editedVoice.opllPreset;
                        }
                        usedEdited = true;
                        voiceTags[vh] = le->tags;
                        // 名字里的通道前缀换成本曲目的（ch5ins1-BD3 → ch2ins3-BD3），
                        // 后面的描述与参数原样保留。
                        std::string nm = le->targetName;
                        const std::string base = le->tags.base;
                        if (!base.empty() && nm.compare(0, base.size(), base) == 0) {
                            nm = std::string(baseName) + nm.substr(base.size());
                        } else {
                            nm = std::string(baseName) + "-" + nm;
                        }
                        voiceFurName[vh] = nm;
                        const bool exact = (le->sourceKey == OpmTimbreKey(srcVoice));
                        out.libApplied.push_back(le->sourceName + " -> " + nm);
                        (exact ? out.libExact : out.libApprox).push_back(le->sourceName);
                    } else {
                        const std::string srcKey = OpmTimbreKey(srcVoice);
                        int dist = -1;
                        const LibraryEntry* near = lib.Nearest(srcVoice, &dist);
                        std::string note = std::string(baseName) + " (d=" +
                                           std::to_string(dist);
                        if (near != nullptr) note += ", 最近 " + near->targetName;
                        note += ")";
                        out.libMissing.push_back(note);
                    }
                }
                // .fur 只对「库里没命中」的音色生效：库是跨曲目的成果，
                // 当前工程的按名字匹配只覆盖这首曲子自己的那份。
                if (!usedEdited && haveFur) {
                    // 先按名字找（chXinsY，可带后缀）；找不到再按序号兜底——
                    // 用户在 Furnace 里把乐器改成完全不同的名字时，序号仍然对应。
                    const FurInstrument* fi =
                        FindFurInstrument(fur, baseName, kFuiInsOpll);
                    bool matchedByName = (fi != nullptr);
                    if (fi == nullptr) {
                        fi = FindFurInstrumentByIndex(fur, kFuiInsOpll,
                                                      (size_t)furVoiceSeq);
                    }
                    furVoiceSeq++;
                    if (fi != nullptr) {
                        editedVoice = fi->voice;
                        if (editedVoice.opllPreset >= 0 && editedVoice.opllPreset < 19) {
                            m.program = editedVoice.opllPreset;
                        }
                        usedEdited = true;
                        // ★ 乐器名里可以带参数（note:B2 / vol:15 / noteoff:self /
                        //   link:ch5ins3），与音色一起缓存，后面每个音符都用得上。
                        voiceTags[vh] = ParseInstrumentTags(fi->name);
                        voiceFurName[vh] = fi->name;
                        out.furApplied.push_back(matchedByName
                                                     ? fi->name
                                                     : (fi->name + " (按序号)"));
                    } else {
                        // 工程里既没名字也没序号 → 必须显式报出来。
                        // 悄悄退回自动匹配会让人以为改的音色生效了。
                        out.furMissing.push_back(baseName);
                    }
                }
                if (!usedEdited && opt.useEditedFui) {
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
                    }
                }
                if (usedEdited && m.program == 0 && !haveUserVoice) {
                    userVoice = editedVoice;
                    haveUserVoice = true;
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

            // 乐器名里写在 .fur 上的参数（note:/vol:/noteoff:/link:）
            InsTags tags;
            {
                auto tit = voiceTags.find(vh);
                if (tit != voiceTags.end()) tags = tit->second;
            }
            // 目标音色（含宏）提前取出来：noteoff:self 要按宏的长度算音符时长。
            const FuiVoice* voicePtr = nullptr;
            {
                auto fit = voiceFui.find(vh);
                if (fit != voiceFui.end()) voicePtr = &fit->second;
            }

            const int carrierTl = CarrierMinTL(shadow, ch, opm.con);
            // TL（0.7526 dB/单位）→ OPLL 衰减 nibble（3.01 dB/步）＝ ÷4。
            // m.volumeOffset / volumeOffsetBias 是 nibble 单位，直接叠加。
            // ★ 名称里写了 vol:N 就以它为准，完全不看源音量。
            //   N 走 Furnace 的音量语义（0=静音，15=最响），而 `vol` 这个变量
            //   是 0x30 的衰减 nibble（0=最响），所以要 15-N 转换。
            int vol = tags.hasVol
                          ? 15 - tags.vol
                          : TlvToOpllVolume(carrierTl) + m.volumeOffset +
                                opt.volumeOffsetBias;
            vol = std::max(0, std::min(15, vol));

            // 音高：KC/KF → Hz → OPLL block/fnum
            const uint8_t kc = shadow[0x28 + ch];
            const uint8_t kf = (shadow[0x30 + ch] >> 2) & 0x3F;
            // 整体移调（--transpose=N，单位半音）：直接作用在频率上，
            // 比改 block 精确（block 只能整八度，半音靠 fnum）。
            const double transposeRatio =
                opt.transposeSemitones != 0
                    ? std::pow(2.0, opt.transposeSemitones / 12.0)
                    : 1.0;
            // ★ 名称里写了 note:X 时，X 就是目标芯片上的**最终音高**：
            //   不读源 KC/KF、不做 ML 八度补偿、不叠加 --transpose。
            //   源里的滑音/颤音也不再跟随——那部分由 2413 侧的音色宏负责。
            const double srcFreq =
                tags.hasNote ? tags.noteHz : OpmKcKfToFreq(kc, kf, clockRatio);
            const double freq = tags.hasNote ? srcFreq : srcFreq * transposeRatio;
            const OpllFnum fn = FreqToOpllFnum(freq, opt.targetClock);
            // 八度补偿：源的载波 ML 与目标 ROM 的载波 ML 不同，
            // 实际发声基频就差 log2(ML比) 个八度，必须加回 block。
            // （vgm-conv 的 `floor(log2(ratio)/2)` 少了一个因子 2，见 FmVoice.cpp 注释）
            const int octaveOffset = tags.hasNote
                                         ? 0
                                         : MlRatioToOctave(OpmCarrierMl(shadow, ch),
                                                           OpllCarrierMl(m.program));
            const int blkO = std::min(7, std::max(0, static_cast<int>(fn.block) + octaveOffset));
            const uint8_t dhBase = static_cast<uint8_t>((blkO << 1) | ((fn.fnum >> 8) & 1));

            Channel& dc = doc.channels[ch];
            Note dn;
            dn.id = n.id;
            dn.startSample = n.startSample;
            dn.endSample = n.endSample;

            // ★ key 事件完全按源重放，不再自己合成。
            //   NoteSplitter 会在 KC 跳变 >0.5 半音时切出新块，那些块在源里既没有
            //   key-on 也没有 key-off，是同一个发声过程的延续（legato）。过去每个块
            //   都补一对 key-off/key-on，听感就是「音符被擅自切开」
            //   （实测源 ch1 只有 202 次真实 key-on，327 个块会变成 327 次重触发）。
            //   源里音符内部也可能有真实的 key-on（重触发），这里一并按原时刻重放。
            std::vector<std::pair<uint32_t, bool>> keyEvents;  // (atSample, isOn)
            for (const Segment& s : n.segments)
                for (const RegValue& r : s.regs)
                    if (r.reg == 0x08) keyEvents.push_back({s.atSample, (r.val & 0x78) != 0});
            std::stable_sort(keyEvents.begin(), keyEvents.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            const bool hadKeyOff = !keyEvents.empty() && !keyEvents.back().second;
            (void)hadKeyOff;

            // ★ noteoff:self / noteoff:N —— 不用源的 key-off，让宏播完（或只播 N 步）
            //   再 off。鼓类（BD/HH/SD）就是这种：源的 key-off 时刻会把宏的衰减
            //   尾巴截断。noteoff:N 则给「尾巴到了就该收声」的音色卡一个精确长度。
            uint32_t selfOffSample = 0;
            if (tags.noteOffSelf && voicePtr != nullptr) {
                const int macroTicks = tags.noteOffTicks > 0 ? tags.noteOffTicks
                                                             : MacroActiveTicks(*voicePtr);
                const int rate = doc.timeBase.sampleRate > 0 ? doc.timeBase.sampleRate : 44100;
                const double perTick = static_cast<double>(rate) / (tickRate > 0 ? tickRate : 60);
                selfOffSample = n.startSample +
                                static_cast<uint32_t>(macroTicks * perTick + 0.5);
                // 源的 key-off 全部丢掉，只保留起点那次 key-on
                keyEvents.erase(std::remove_if(keyEvents.begin(), keyEvents.end(),
                                               [](const std::pair<uint32_t, bool>& e) {
                                                   return !e.second;
                                               }),
                                keyEvents.end());
                dn.endSample = std::max(dn.endSample, selfOffSample);
            }

            // 先把落在音符起点及之前的 key 事件结清，起点段才知道 key 位该是什么。
            const bool keyDownBefore = keyDown;
            size_t kev = 0;
            while (kev < keyEvents.size() && keyEvents[kev].first <= n.startSample) {
                keyDown = keyEvents[kev].second;
                kev++;
            }

            // ★ 宏的相位基准：只有 0→1 的真实 key 上升沿才启动一次宏。
            //   NoteSplitter 在 KC 跳变时切出的块没有 key 事件，它们是同一次发声的
            //   延续，宏必须接着走，而不是从这里重新开始。
            if (keyDown && !keyDownBefore) keyOnSample = n.startSample;
            const int blockSkipTicks =
                (keyDown && n.startSample > keyOnSample)
                    ? static_cast<int>(std::lround(
                          (n.startSample - keyOnSample) * tickRate * 1.0 /
                          (doc.timeBase.sampleRate > 0 ? doc.timeBase.sampleRate : 44100)))
                    : 0;
            // 本块末尾若没有真实 key-off（下一个块接着发声），宏不能进 release。
            bool endsWithKeyOff = false;
            for (size_t k = kev; k < keyEvents.size(); k++) {
                if (keyEvents[k].first > n.endSample) break;
                if (!keyEvents[k].second) endsWithKeyOff = true;
            }

            // key-on 段：音高 + 音色 + 音量，全部写原值。
            // legato 延续块只改 fnum/block，key 位沿用上一块的状态。
            Segment ks;
            ks.atSample = n.startSample;
            ks.regs.push_back(Rv(0x10 + ch, fn.fnumLow));
            ks.regs.push_back(Rv(0x20 + ch,
                                 static_cast<uint8_t>(dhBase | (keyDown ? 0x10 : 0x00))));
            ks.regs.push_back(Rv(0x30 + ch, static_cast<uint8_t>((m.program << 4) | vol)));
            dn.segments.push_back(ks);

            // ★ 音符中间的音高轨迹（弯音 / 颤音 / 滑音）必须跟随。
            //   OPM 的 0x28+ch 是 KC（音名），0x30+ch 是 KF（1/64 半音细调）。
            //   源里音符内部可能有上百次 KF 写（ch1 实测 482 次），只写起始 fnum
            //   会把整条轨迹压成一个固定音高，听感就是「被量化成半音阶」。
            //   音高写与 key 事件必须按时间合并成一条时间线，否则 key 位会错。
            struct PitchPoint {
                uint32_t at;
                int fnumLow;
                int dh;
            };
            std::vector<PitchPoint> pitchPoints;
            int curKc = kc;
            int curKf = kf;
            int lastFnumLow = fn.fnumLow;
            int lastFnumHigh = dhBase;
            // ★ note:X 的音高是固定的，源里的滑音/颤音一律不跟（由 2413 的宏替代）。
            for (const Segment& s : n.segments) {
                if (tags.hasNote) break;
                if (s.atSample <= n.startSample) continue;
                bool pitchMoved = false;
                for (const RegValue& r : s.regs) {
                    if (r.reg == 0x28 + ch) {
                        curKc = r.val;
                        pitchMoved = true;
                    } else if (r.reg == 0x30 + ch) {
                        curKf = (r.val >> 2) & 0x3F;
                        pitchMoved = true;
                    }
                }
                if (!pitchMoved) continue;
                const double f2 =
                    OpmKcKfToFreq(static_cast<uint8_t>(curKc),
                                  static_cast<uint8_t>(curKf), clockRatio) *
                    transposeRatio;
                const OpllFnum fn2 = FreqToOpllFnum(f2, opt.targetClock);
                const int b2 =
                    std::min(7, std::max(0, static_cast<int>(fn2.block) + octaveOffset));
                const int dh2 = (b2 << 1) | ((fn2.fnum >> 8) & 1);
                if (fn2.fnumLow == lastFnumLow && dh2 == lastFnumHigh) continue;
                lastFnumLow = fn2.fnumLow;
                lastFnumHigh = dh2;
                pitchPoints.push_back(PitchPoint{s.atSample, fn2.fnumLow, dh2});
            }

            // 合并时间线：同一采样点上，先写 fnum、再写 key 状态。
            size_t pi = 0, ki = kev;
            while (pi < pitchPoints.size() || ki < keyEvents.size()) {
                const bool takePitch =
                    ki >= keyEvents.size() ||
                    (pi < pitchPoints.size() && pitchPoints[pi].at <= keyEvents[ki].first);
                Segment seg;
                if (takePitch) {
                    seg.atSample = pitchPoints[pi].at;
                    seg.regs.push_back(Rv(0x10 + ch,
                                          static_cast<uint8_t>(pitchPoints[pi].fnumLow)));
                    seg.regs.push_back(Rv(0x20 + ch,
                                          static_cast<uint8_t>(pitchPoints[pi].dh |
                                                               (keyDown ? 0x10 : 0x00))));
                    pi++;
                } else {
                    const bool on = keyEvents[ki].second;
                    ki++;
                    if (on == keyDown) continue;
                    keyDown = on;
                    seg.atSample = keyEvents[ki - 1].first;
                    seg.regs.push_back(Rv(0x20 + ch,
                                          static_cast<uint8_t>(dhBase | (on ? 0x10 : 0x00))));
                }
                dn.segments.push_back(seg);
            }

            // 音符中间若源 TL 变化，跟随写 0x30（值变了才写）
            // ★ 名称里写了 vol:N（或 noteoff:self 的音符），音量由名称/宏决定，
            //   不再跟随源 TL。
            int lastVol = vol;
            for (const Segment& s : n.segments) {
                if (tags.hasVol || tags.noteOffSelf) break;
                if (s.atSample <= n.startSample) continue;
                for (const RegValue& r : s.regs) {
                    if (r.reg < 0x60 || r.reg > 0x7F) continue;
                    shadow[r.reg] = r.val;
                    if ((r.reg & 0x07) != ch) continue;
                    const int tl = CarrierMinTL(shadow, ch, shadow[0x20 + ch] & 7);
                    int v2 = TlvToOpllVolume(tl) + m.volumeOffset + opt.volumeOffsetBias;
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
                const FuiVoice* fv = voicePtr;
                if (fv != nullptr && HasAnyMacro(*fv)) {
                    MacroExpandInput mi;
                    mi.ch = ch;
                    mi.baseKc = kc;
                    mi.baseKf = kf;
                    // ★ 极性：`vol` 是 0x30 低 4 位的**衰减值**（0=最响，15=最轻），
                    //   而 baseVol 走 Furnace 的 chan.vol 语义（15=最响）。
                    //   必须转成 15-vol，否则衰减值被当音量用，等于额外加了一次
                    //   反向衰减：vol=2 会变成 15-2=13 的衰减（几乎没声）。
                    mi.baseVol = 15 - vol;
                    mi.startSample = n.startSample;
                    // noteoff:self：宏要完整播完，不能被源的音符终点截断。
                    mi.endSample = tags.noteOffSelf && selfOffSample > n.startSample
                                       ? selfOffSample
                                       : n.endSample;
                    mi.sampleRate = doc.timeBase.sampleRate > 0 ? doc.timeBase.sampleRate : 44100;
                    mi.tickRate = tickRate;
                    // ★ 宏只在真实 key-on 启动一次。legato 块（源里无 key 事件）
                    //   必须沿用上一次发声的相位，否则 vol 宏会重新吐第一帧
                    //   （很多音色第一帧是 0 → 每个块开头静音一拍），
                    //   arp 宏也会从 0 重扫（听感就是块边界处一次多余的弯音）。
                    mi.skipTicks = blockSkipTicks;
                    mi.releaseAtEnd = endsWithKeyOff;
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
                        keyDown = true;
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

            // key-off 不再另行合成：源里真实的 key-off 已经在上面的 keyEvents
            // 重放里按原时刻写进 0x20，重放结束后 keyDown 就是源的真实状态。
            // legato 切块到这里 keyDown 仍为 true（源里从未 key-off），
            // 不补写 —— 补写就会把同一个音切成两段独立起音。
            //
            // ★ noteoff:self：把上面丢掉的源 key-off 换成「宏播完再 off」。
            if (tags.noteOffSelf && selfOffSample > n.startSample) {
                Segment seg;
                seg.atSample = selfOffSample;
                seg.regs.push_back(Rv(0x20 + ch,
                                      static_cast<uint8_t>(dhBase)));  // key 位清零
                dn.segments.push_back(seg);
                keyDown = false;
            }

            dc.notes.push_back(dn);
            // link:X 要知道「哪个音符属于哪个 .fur 乐器」，按 (通道, 起点) 记一份。
            {
                auto nit = voiceFurName.find(vh);
                if (nit != voiceFurName.end())
                    noteVoiceOfChannel[ch][n.startSample] = nit->second;
                auto kit = voiceSrcKey.find(vh);
                if (kit != voiceSrcKey.end())
                    noteKeyOfChannel[ch][n.startSample] = kit->second;
            }
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

        // ★ noteoff:self 把音符拉长到宏播完，可能越过下一个音符的起点。
        //   OPLL 同一通道不能同时发两个音（后一个 key-on 会直接抢走前一个），
        //   所以把前一个音符裁到下个音符起点：宏的尾巴本来就是渐弱，
        //   提前几毫秒 off 听不出来，但能保证每个鼓点都真的重新起音。
        for (size_t i = 0; i + 1 < sc.notes.size(); i++) {
            Note& cur = doc.channels[ch].notes[i];
            const Note& nxt = doc.channels[ch].notes[i + 1];
            if (cur.endSample > nxt.startSample) {
                cur.endSample = nxt.startSample;
                // 该块的收尾 key-off 也要跟着前移（如果它落在裁剪区之外）
                for (Segment& seg : cur.segments) {
                    if (seg.atSample > cur.endSample) seg.atSample = cur.endSample;
                }
                std::stable_sort(cur.segments.begin(), cur.segments.end(),
                                 [](const Segment& a, const Segment& b) {
                                     return a.atSample < b.atSample;
                                 });
            }
        }
    }

    // ── link:X 联动 ───────────────────────────────────────────────────
    // 名字里带 link:chNinsM 的乐器（通常挂在第 9 通道这个空位上）是「联动音色」：
    // 目标乐器每次击发时，它也在同一采样点、同样时长地一起击发。
    //
    // 这里取的不是目标乐器的参数，而是**联动音色自己的**音色/音量/宏——
    // 它的价值就在于叠加一个不同的层次（例如 SD2 底鼓层之上再叠一层 HF）。
    // 音高与 key 时机沿用目标音符（那已经换算成 OPLL 的 fnum/block 了），
    // 音色与包络按联动音色重新展开。
    //
    // ★ 两个来源：
    //   ① .fur 工程里的 link:X（X 是本曲的通道名 "ch5ins3"）
    //   ② 乐器库里的 link 条目（库里记的是**被联动源音色的身份键**，
    //      跨曲目复用时那个通道名可能完全不同，所以按参数认人）
    // 同一个通道号只取一条，先命中的优先（.fur 是本曲专用，优先）。
    struct LinkSpec {
        int dstCh = -1;
        std::string srcName;     // 本曲里的被联动通道名（仅 .fur 用）
        std::string srcKey;      // 源音色身份键（库条目用）
        InsTags tags;
        const FuiVoice* voice = nullptr;
        std::string label;
    };
    std::vector<LinkSpec> links;
    std::map<int, bool> linkChTaken;

    if (haveFur) {
        for (const FurInstrument& fi : fur.instruments) {
            if (fi.type != kFuiInsOpll) continue;
            const InsTags ltags = ParseInstrumentTags(fi.name);
            if (ltags.linkTo.empty()) continue;
            const int dstCh = ChannelFromInstrumentName(fi.name);
            const int srcCh = ChannelFromInstrumentName(ltags.linkTo);
            if (dstCh < 0 || dstCh >= static_cast<int>(doc.channels.size())) continue;
            if (srcCh < 0 || srcCh >= static_cast<int>(doc.channels.size())) continue;
            if (srcCh == dstCh) continue;
            if (linkChTaken[dstCh]) continue;
            linkChTaken[dstCh] = true;
            LinkSpec ls;
            ls.dstCh = dstCh;
            ls.srcName = ltags.linkTo;
            ls.tags = ltags;
            ls.voice = &fi.voice;
            ls.label = fi.name + " -> " + ltags.linkTo;
            links.push_back(ls);
        }
    }
    if (haveLib) {
        for (const LibraryEntry& le : lib.entries) {
            if (!le.isLink) continue;
            const int dstCh = ChannelFromInstrumentName(le.targetName);
            if (dstCh < 0 || dstCh >= static_cast<int>(doc.channels.size())) continue;
            if (linkChTaken[dstCh]) continue;
            // 库里记的是源音色身份键；本曲里存在这个音色才安装这条联动
            bool here = false;
            for (const auto& kv : noteKeyOfChannel)
                for (const auto& nk : kv.second)
                    if (nk.second == le.linkSourceKey) here = true;
            if (!here) continue;
            linkChTaken[dstCh] = true;
            LinkSpec ls;
            ls.dstCh = dstCh;
            ls.srcKey = le.linkSourceKey;
            ls.tags = le.tags;
            ls.voice = &le.target;
            ls.label = le.targetName + " (库, 按源音色参数联动)";
            links.push_back(ls);
        }
    }

    for (const LinkSpec& lk : links) {
        const int dstCh = lk.dstCh;
        const InsTags& ltags = lk.tags;
        {
            Channel& dc = doc.channels[dstCh];
            // 哪些音符该被联动：
            //   .fur  → 本曲里的名字前缀（ch5ins3）
            //   库     → 被联动源音色的身份键（参数一致）
            auto shouldLink = [&](int srcCh, const Note& sn) {
                if (!lk.srcKey.empty()) {
                    auto kit = noteKeyOfChannel.find(srcCh);
                    if (kit == noteKeyOfChannel.end()) return false;
                    auto vIt = kit->second.find(sn.startSample);
                    return vIt != kit->second.end() && vIt->second == lk.srcKey;
                }
                auto nvIt = noteVoiceOfChannel.find(srcCh);
                if (nvIt == noteVoiceOfChannel.end()) return false;
                auto vIt = nvIt->second.find(sn.startSample);
                if (vIt == nvIt->second.end()) return false;
                const InsTags srcTags = ParseInstrumentTags(vIt->second);
                return srcTags.base.compare(0, lk.srcName.size(), lk.srcName) == 0;
            };
            (void)shouldLink;
            for (int srcCh = 0; srcCh < static_cast<int>(doc.channels.size()); srcCh++) {
                if (srcCh == dstCh) continue;
                const std::vector<Note> source = doc.channels[srcCh].notes;
                for (const Note& sn : source) {
                    if (!shouldLink(srcCh, sn)) continue;
                Note n;
                n.id = static_cast<int>(dc.notes.size());
                n.startSample = sn.startSample;
                n.endSample = sn.endSample;

                // ① 音高与 key 轨迹：沿用目标音符的 0x10/0x20，只改通道号。
                for (const Segment& seg : sn.segments) {
                    Segment out;
                    out.atSample = seg.atSample;
                    for (const RegValue& r : seg.regs) {
                        if (r.reg >= 0x10 && r.reg <= 0x18) {
                            out.regs.push_back(Rv(static_cast<uint8_t>(0x10 + dstCh), r.val));
                        } else if (r.reg >= 0x20 && r.reg <= 0x28) {
                            // 高 4 位是 sus/pan 之类的通道位，低 4 位是 block/fnum 高位；
                            // 只换通道地址，值原样保留（含 key 位）。
                            out.regs.push_back(Rv(static_cast<uint8_t>(0x20 + dstCh), r.val));
                        }
                    }
                    if (!out.regs.empty()) n.segments.push_back(out);
                }
                if (n.segments.empty()) continue;

                // ①b 联动音色自己也写了 note:X 时，音高同样由名字决定：
                //    把上面搬来的 fnum/block 全部改写成固定值，key/sus 位保留。
                if (ltags.hasNote) {
                    const OpllFnum lf = FreqToOpllFnum(ltags.noteHz, opt.targetClock);
                    const int lb = std::min(7, std::max(0, static_cast<int>(lf.block)));
                    const uint8_t ldh =
                        static_cast<uint8_t>((lb << 1) | ((lf.fnum >> 8) & 1));
                    for (Segment& seg : n.segments) {
                        for (RegValue& r : seg.regs) {
                            if (r.reg == 0x10 + dstCh) {
                                r.val = lf.fnumLow;
                            } else if (r.reg == 0x20 + dstCh) {
                                r.val = static_cast<uint8_t>((r.val & 0xF0) | ldh);
                            }
                        }
                    }
                }

                // ② 音色与音量：联动音色自己的 preset + vol。
                const int program = lk.voice->opllPreset;
                // 同主循环：vol:N 是 Furnace 音量语义（15=最响），转成 0x30 的衰减 nibble。
                int vol = ltags.hasVol ? 15 - ltags.vol : 0;
                vol = std::max(0, std::min(15, vol));
                // 起点补一条 0x30（音色号 + 音量），与主循环的写法一致。
                {
                    Segment seg;
                    seg.atSample = n.startSample;
                    seg.regs.push_back(
                        Rv(static_cast<uint8_t>(0x30 + dstCh),
                           static_cast<uint8_t>((program << 4) | vol)));
                    n.segments.insert(n.segments.begin(), seg);
                }

                // ③ noteoff:self / noteoff:N：延长到宏播完（或 N 步），再补 key-off。
                uint32_t offAt = 0;
                if (ltags.noteOffSelf) {
                    const int ticks = ltags.noteOffTicks > 0 ? ltags.noteOffTicks
                                                             : MacroActiveTicks(*lk.voice);
                    const int rate = doc.timeBase.sampleRate > 0 ? doc.timeBase.sampleRate : 44100;
                    const double perTick = static_cast<double>(rate) / (tickRate > 0 ? tickRate : 60);
                    offAt = n.startSample + static_cast<uint32_t>(ticks * perTick + 0.5);
                    n.endSample = std::max(n.endSample, offAt);
                }

                // ④ 宏：按联动音色重新展开，时间窗与目标音符相同。
                if (opt.runMacros && HasAnyMacro(*lk.voice)) {
                    MacroExpandInput mi;
                    mi.ch = dstCh;
                    mi.baseVol = 15 - vol;
                    mi.startSample = n.startSample;
                    mi.endSample = ltags.noteOffSelf ? n.endSample : sn.endSample;
                    mi.sampleRate = doc.timeBase.sampleRate > 0 ? doc.timeBase.sampleRate : 44100;
                    mi.tickRate = tickRate;
                    mi.releaseAtEnd = true;
                    mi.opll = true;
                    mi.opllPreset = program;
                    const MacroExpandOutput mo = ExpandMacros(*lk.voice, mi);
                    for (const MacroWrite& w : mo.writes) {
                        Segment seg;
                        seg.atSample = w.atSample;
                        seg.regs.push_back(Rv(w.reg, w.val));
                        n.segments.push_back(seg);
                    }
                    for (const MacroPitch& p : mo.pitches) {
                        if (p.offset128 == 0) continue;
                        // 在联动通道的基准音高上叠偏移：直接改该点的 fnum。
                        // 基准取目标音符起点的 fnum/block。
                        int baseFnumLow = 0, baseDh = 0;
                        for (const Segment& seg : n.segments) {
                            if (seg.atSample > n.startSample) break;
                            for (const RegValue& r : seg.regs) {
                                if (r.reg == 0x10 + dstCh) baseFnumLow = r.val;
                                if (r.reg == 0x20 + dstCh) baseDh = r.val & 0x0F;
                            }
                        }
                        const double semi = p.offset128 / 128.0;
                        const int blk = (baseDh >> 1) & 7;
                        const int fnum = (baseFnumLow | ((baseDh & 1) << 8));
                        const double f = static_cast<double>(fnum) *
                                         std::pow(2.0, blk) * std::pow(2.0, semi / 12.0);
                        int nf = static_cast<int>(f);
                        int nb = 0;
                        while (nf > 511) {
                            nf >>= 1;
                            nb++;
                        }
                        if (nb > 7) { nb = 7; if (nf > 255) nf = 255; }
                        Segment seg;
                        seg.atSample = p.atSample;
                        seg.regs.push_back(Rv(static_cast<uint8_t>(0x10 + dstCh),
                                              static_cast<uint8_t>(nf & 0xFF)));
                        seg.regs.push_back(Rv(static_cast<uint8_t>(0x20 + dstCh),
                                              static_cast<uint8_t>(((nb & 7) << 1) |
                                                                   ((nf >> 8) & 1) | 0x10)));
                        n.segments.push_back(seg);
                    }
                }

                // ⑤ noteoff:self 的收尾 key-off（宏跑完才 off）
                if (offAt > 0) {
                    Segment seg;
                    seg.atSample = offAt;
                    uint8_t dh = 0;
                    for (const Segment& s2 : n.segments) {
                        if (s2.atSample > offAt) break;
                        for (const RegValue& r : s2.regs)
                            if (r.reg == 0x20 + dstCh) dh = r.val & 0x0F;
                    }
                    seg.regs.push_back(Rv(static_cast<uint8_t>(0x20 + dstCh), dh));
                    n.segments.push_back(seg);
                }

                std::stable_sort(n.segments.begin(), n.segments.end(),
                                 [](const Segment& a, const Segment& b) {
                                     return a.atSample < b.atSample;
                                 });
                dc.notes.push_back(n);
                out.notesConverted++;
                }
            }
            out.linkApplied.push_back(lk.label);
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
