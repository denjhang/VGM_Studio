// fuitool —— Furnace `.fui` 乐器文件的查看 / 校验 / 转换。
//
// 全部编解码走 Furnace 自己的 instrument.cpp（见 cli/third_party/furnace/），
// 所以能跟上游 Furnace 的版本演进保持一致。
//
// ★ 宏的速率
//   .fui 里只存每个宏自己的 speed / delay（每步几个 tick），
//   具体「一秒几个 tick」由 .fur 工程（song.hz）决定，乐器文件里没有。
//   因此 dump 时用 --tick-rate 给出中断速率，才能把宏打印成毫秒。
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>

#include <nlohmann/json.hpp>
#include <fmt/format.h>

#include "FuiVoice.h"
#include "FurFile.h"
#include "InstrumentLibrary.h"

namespace {

void Usage() {
    std::printf(
        "usage: fuitool <command> [args]\n"
        "\n"
        "  fuitool dump <voice.fui> [--tick-rate=60]\n"
        "      打印音色参数与全部宏（含每一步的毫秒时长）\n"
        "\n"
        "  fuitool check <voice.fui> [...]\n"
        "      校验能否被 Furnace 解码；多个文件一起给时批量检查\n"
        "\n"
        "  fuitool fur <project.fur>\n"
        "      列出 Furnace 工程里的全部乐器（与 opm2opll --fur 同一读取路径）\n"
        "\n"
        "  fuitool lib <project.fur> [--json=OUT.json]\n"
        "      导出乐器库：stdout 是 Markdown（表格 + 明文参数清单），\n"
        "      --json=FILE 另存一份机器可读的 JSON\n"
        "\n"
        "  fuitool rewrite <in.fui> <out.fui> [--name=...] [--alg=N] [--fb=N]\n"
        "      读入 → 改几个字段 → 写回（验证编解码往返无损）\n"
        "\n"
        "  fuitool unpack <project.fur> <outdir> [--type=13] [--tagged]\n"
        "      把工程里的乐器逐个解包成 .fui，**文件名 = Furnace 里的乐器全名**。\n"
        "      --type=13  只解 OPLL（YM2413）；33 只解 OPM；不给就全解\n"
        "      --tagged   文件名只保留 chNinsM 前缀（去掉 note:/vol: 等参数），\n"
        "                 即转换时 opm2opll 认的那种名字\n"
        "\n"
        "  --tick-rate=N   宏的中断速率（Hz，Furnace 的 song.hz，默认 60）\n"
        "                  只影响 dump 里的毫秒换算，不改变 fui 内容\n");
}

const char* TypeName(int t) {
    switch (t) {
        case vgmstudio::kFuiInsOpll: return "OPLL(YM2413)";
        case vgmstudio::kFuiInsOpl: return "OPL";
        case vgmstudio::kFuiInsOpm: return "OPM(YM2151)";
        default: return "other";
    }
}

// ── 标签 → GM 标准音色 ────────────────────────────────────────────────
// 乐器名形如 ch1ins1-pad / ch5ins1-bd / ch4ins1-acid bass：取 "chXinsY" 之后
// 的部分当标签，展开成全称并尽量对齐 General MIDI 的官方名字与 program 号。
// GM program 用 1..128 的官方编号（MIDI 线上的 program 是 0..127，差 1）。
// 打击乐在 GM 里是通道 10 的固定音符（note number），不是 program。
struct GmEntry {
    const char* tag;         // 乐器名里的标签（前缀匹配，长的排前面）
    const char* fullName;    // 完整英文名
    int gmProgram;           // 1..128；0 = 不是 GM program
    const char* gmName;      // GM 官方音色名
    int gmPercussionNote;    // GM 打击乐音符号；0 = 不是打击乐
};

const GmEntry kGmTable[] = {
    // 长的写前面，匹配时按前缀
    {"clean guitar2", "Clean Electric Guitar 2", 28, "Electric Guitar (clean)", 0},
    {"clean guitar", "Clean Electric Guitar", 28, "Electric Guitar (clean)", 0},
    {"clean vibraphone", "Clean Vibraphone", 12, "Vibraphone", 0},
    {"light vibraphone", "Light Vibraphone", 12, "Vibraphone", 0},
    {"dark vibraphone", "Dark Vibraphone", 12, "Vibraphone", 0},
    {"acid bass", "Acid Bass", 39, "Synth Bass 1", 0},
    {"synth bass", "Synth Bass", 39, "Synth Bass 1", 0},
    {"syn bass", "Synth Bass", 39, "Synth Bass 1", 0},
    {"pad sq2", "Square Pad 2", 81, "Lead 1 (square)", 0},
    {"pad sq", "Square Pad", 81, "Lead 1 (square)", 0},
    {"pad sine", "Sine Pad", 89, "Pad 1 (new age)", 0},
    {"pad2", "Pad (second)", 90, "Pad 2 (warm)", 0},
    {"pad", "Pad", 89, "Pad 1 (new age)", 0},
    {"celesta", "Celesta", 9, "Celesta", 0},
    {"trumpet", "Trumpet", 57, "Trumpet", 0},
    {"trum", "Trumpet", 57, "Trumpet", 0},
    {"vibraphone", "Vibraphone", 12, "Vibraphone", 0},
    {"vib", "Vibraphone", 12, "Vibraphone", 0},
    {"guitar", "Electric Guitar", 27, "Electric Guitar (jazz)", 0},
    {"sanmi", "Shamisen", 107, "Shamisen", 0},
    {"bd", "Bass Drum", 0, "Bass Drum 1", 36},
    {"hh", "Hi-Hat", 0, "Closed Hi-Hat", 42},
    {"sd", "Snare Drum", 0, "Acoustic Snare", 38},
};

// 从乐器名里抽出标签：去掉开头的 ch<N>ins<M>，剩下 - 或空格分隔的部分。
std::string TagOf(const std::string& name) {
    const size_t p = name.find("ins");
    if (p == std::string::npos) return std::string();
    size_t q = p + 3;
    while (q < name.size() && name[q] >= '0' && name[q] <= '9') q++;
    if (q >= name.size()) return std::string();
    const char sep = name[q];
    if (sep != '-' && sep != '_' && sep != ' ') return std::string();
    return name.substr(q + 1);
}

const GmEntry* LookupGm(const std::string& name) {
    const std::string tag = TagOf(name);
    if (tag.empty()) return nullptr;
    // 前缀匹配可能误中（"pad" 会吃掉 "pad2"），所以先求最长匹配
    const GmEntry* best = nullptr;
    size_t bestLen = 0;
    for (const GmEntry& e : kGmTable) {
        const size_t n = std::strlen(e.tag);
        if (tag.size() >= n && tag.compare(0, n, e.tag) == 0 && n > bestLen) {
            best = &e;
            bestLen = n;
        }
    }
    return best;
}

// 目标侧乐器（ch1ins1）本身不带标签，GM 名继承自同名的源侧乐器
// （ch1ins1-pad）。这样 19 个 OPLL 音色也有可读的 GM 类别。
const GmEntry* LookupGmPaired(const vgmstudio::FurSong& song, const std::string& name) {
    if (const GmEntry* e = LookupGm(name)) return e;
    for (const vgmstudio::FurInstrument& i : song.instruments) {
        if (i.name.size() <= name.size()) continue;
        if (i.name.compare(0, name.size(), name) != 0) continue;
        const char sep = i.name[name.size()];
        if (sep != '-' && sep != '_' && sep != ' ') continue;
        if (const GmEntry* e = LookupGm(i.name)) return e;
    }
    return nullptr;
}

const char* MacroName(int i) {
    static const char* kNames[vgmstudio::kMacroCount] = {
        "vol", "arp", "duty", "wave", "pitch", "ex1", "ex2", "ex3",
        "alg", "fb", "fms", "ams", "panL", "panR", "phaseReset",
        "ex4", "ex5", "ex6", "ex7", "ex8", "ex9", "ex10"};
    return (i >= 0 && i < vgmstudio::kMacroCount) ? kNames[i] : "?";
}

const char* OpMacroName(int i) {
    static const char* kNames[vgmstudio::kOpMacroCount] = {
        "am", "ar", "dr", "mult", "rr", "sl", "tl", "dt2", "rs", "dt",
        "d2r", "ssg", "dam", "dvb", "egt", "ksl", "sus", "vib", "ws", "ksr"};
    return (i >= 0 && i < vgmstudio::kOpMacroCount) ? kNames[i] : "?";
}

int DumpVoice(const vgmstudio::FuiVoice& v, const std::string& label, int tickRate) {
    std::printf("file          : %s\n", label.c_str());
    std::printf("type          : %d (%s)\n", v.type, TypeName(v.type));
    std::printf("name          : %s\n", v.name.c_str());
    std::printf("alg/fb        : %d / %d\n", v.alg, v.fb);
    std::printf("fms/ams       : %d / %d   (ops=%d, opllPreset=%d)\n", v.fms, v.ams, v.ops,
                v.opllPreset);
    std::printf("fingerprint   : %016llx\n",
                (unsigned long long)vgmstudio::VoiceFingerprint(v));
    std::printf("signature     : %s\n", vgmstudio::VoiceSignature(v).c_str());
    std::printf("\n  %-4s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n", "op",
                "mult", "tl", "ar", "dr", "sl", "rr", "dt", "dt2", "ksr", "am", "vib");
    const int shown = v.ops >= 2 ? v.ops : 2;
    for (int i = 0; i < shown; i++) {
        const vgmstudio::FuiOperator& o = v.op[i];
        std::printf("  %-4d %-3d %-3d %-3d %-3d %-3d %-3d %-3d %-3d %-3d %-3d %-3d%s\n", i, o.mult,
                    o.tl, o.ar, o.dr, o.sl, o.rr, o.dt, o.dt2, o.ksr, o.am, o.vib,
                    o.enable ? "" : "   (disabled)");
    }

    std::printf("\n  macros (tickRate=%d Hz, 1 tick = %.4f ms)\n", tickRate,
                1000.0 / (tickRate > 0 ? tickRate : 60));
    int used = 0;
    for (int i = 0; i < vgmstudio::kMacroCount; i++) {
        const vgmstudio::Macro& m = v.macros[i];
        if (m.val.empty()) continue;
        used++;
        const char* modeName = m.type == 0 ? "seq" : (m.type == 1 ? "ADSR" : "LFO");
        std::printf("    %-10s %-4s len=%-3zu speed=%-2d delay=%-2d loop=%-3d rel=%-3d%s\n",
                    MacroName(i), modeName, m.val.size(), m.speed, m.delay, m.loop, m.rel,
                    m.activeRelease ? "  activeRelease" : "");
        std::printf("      step = %.2f ms", 1000.0 * m.speed / (tickRate > 0 ? tickRate : 60));
        if (m.type == 0) {
            std::printf("   values:");
            for (size_t j = 0; j < m.val.size() && j < 32; j++) std::printf(" %d", m.val[j]);
            if (m.val.size() > 32) std::printf(" ... (%zu total)", m.val.size());
            std::printf("   total %.1f ms\n",
                        1000.0 * m.speed * (double)m.val.size() / (tickRate > 0 ? tickRate : 60));
        } else {
            std::printf("   params:");
            for (size_t j = 0; j < m.val.size() && j < 16; j++) std::printf(" %d", m.val[j]);
            std::printf("\n");
        }
    }
    if (used == 0) std::printf("    (none)\n");

    // 算子级宏（O1..O4 块）
    std::printf("\n  operator macros\n");
    int opUsed = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < vgmstudio::kOpMacroCount; j++) {
            const vgmstudio::Macro& m = v.opMacros[i][j];
            if (m.val.empty()) continue;
            opUsed++;
            std::printf("    op%d %-5s seq len=%-3zu speed=%-2d delay=%-2d loop=%-3d rel=%-3d  values:",
                        i, OpMacroName(j), m.val.size(), m.speed, m.delay, m.loop, m.rel);
            for (size_t k = 0; k < m.val.size() && k < 24; k++) std::printf(" %d", m.val[k]);
            std::printf("\n");
        }
    }
    if (opUsed == 0) std::printf("    (none)\n");
    return 0;
}

// 乐器库 JSON：机器可读、可 diff、可被别的工具直接吃。
// 结构：每条 = 标签 + 类型 + preset + 全部 FM 参数（明文数字）+ 指纹 + 签名。
bool WriteLibraryJson(const vgmstudio::FurSong& song, const std::string& source,
                      const std::string& path, std::string* err) {
    nlohmann::json ins = nlohmann::json::array();
    for (size_t i = 0; i < song.instruments.size(); i++) {
        const vgmstudio::FurInstrument& fi = song.instruments[i];
        const vgmstudio::FuiVoice& v = fi.voice;
        nlohmann::json ops = nlohmann::json::array();
        const int shown = v.ops >= 2 ? v.ops : 2;
        for (int o = 0; o < shown; o++) {
            const vgmstudio::FuiOperator& op = v.op[o];
            ops.push_back({
                {"index", o},
                {"mult", op.mult}, {"tl", op.tl}, {"ar", op.ar}, {"dr", op.dr},
                {"sl", op.sl}, {"rr", op.rr}, {"dt", op.dt}, {"dt2", op.dt2},
                {"ksr", op.ksr}, {"am", op.am}, {"vib", op.vib}, {"egt", op.egt},
                {"ksl", op.ksl}, {"ws", op.ws}, {"sus", op.sus}, {"kvs", op.kvs},
                {"enable", op.enable},
            });
        }
        // 有宏的乐器把宏也带上（OPLL 的 0 号槽 + patch 宏是音色的一部分）
        nlohmann::json macros = nlohmann::json::array();
        for (int m = 0; m < vgmstudio::kMacroCount; m++) {
            const vgmstudio::Macro& mc = v.macros[m];
            if (mc.val.empty()) continue;
            macros.push_back({{"macro", m},
                              {"type", mc.type},
                              {"values", mc.val},
                              {"speed", mc.speed},
                              {"delay", mc.delay},
                              {"loop", mc.loop},
                              {"rel", mc.rel}});
        }
        nlohmann::json gm = nullptr;
        if (const GmEntry* e = LookupGmPaired(song, fi.name)) {
            gm = {{"fullName", e->fullName}, {"gmName", e->gmName}};
            if (e->gmPercussionNote) {
                gm["gmPercussionNote"] = e->gmPercussionNote;
            } else {
                gm["gmProgram"] = e->gmProgram;
            }
        }
        ins.push_back({
            {"index", i},
            {"name", fi.name},
            {"gm", gm},
            {"type", fi.type},
            {"chip", fi.type == vgmstudio::kFuiInsOpm
                         ? "YM2151"
                         : (fi.type == vgmstudio::kFuiInsOpll ? "YM2413" : "other")},
            {"opllPreset", v.opllPreset},
            {"alg", v.alg}, {"fb", v.fb}, {"fms", v.fms}, {"ams", v.ams},
            {"ops", v.ops},
            {"operators", ops},
            {"macros", macros},
            {"fingerprint", fmt::format("{:016x}", vgmstudio::VoiceFingerprint(v))},
            {"signature", vgmstudio::VoiceSignature(v)},
        });
    }
    nlohmann::json root = {
        {"format", "vgm-studio/instrument-library"},
        {"version", 2},
        {"source", source},
        {"furVersion", song.version},
        {"hz", song.hz},
        {"songName", song.name},
        {"songAuthor", song.author},
        {"note",
         "身份靠 YM2151 的 FM 参数（operators，不含 TL）；指纹只用于同一曲目内去重。"
         "pairs 段是 源音色 → 目标音色 的跨曲目复用表。"},
        {"instruments", ins},
        // ★ 跨曲目复用表：源侧 OPM 参数 + 目标侧 OPLL 音色/宏/名字参数。
        //   批量转换时按源侧参数查表（InstrumentLibrary::Find），
        //   完全一致直接用、不一致取最近的，都不会退回 ROM 自动匹配。
        {"pairs", vgmstudio::LibraryPairsToJson(vgmstudio::BuildLibraryPairs(song))},
    };
    std::ofstream f(path);
    if (!f) {
        *err = "cannot write " + path;
        return false;
    }
    f << root.dump(2) << "\n";
    return f.good();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        Usage();
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help") {
        Usage();
        return 0;
    }

    int tickRate = 60;
    int unpackType = -1;
    bool unpackTagged = false;
    std::vector<std::string> args;
    std::string newName;
    std::string jsonOut;   // --json=OUT.json（仅 lib 用）
    int newAlg = -1, newFb = -1;
    for (int i = 2; i < argc; i++) {
        const char* a = argv[i];
        if (std::strncmp(a, "--tick-rate=", 12) == 0) {
            tickRate = std::atoi(a + 12);
        } else if (std::strncmp(a, "--name=", 7) == 0) {
            newName = a + 7;
        } else if (std::strncmp(a, "--alg=", 6) == 0) {
            newAlg = std::atoi(a + 6);
        } else if (std::strncmp(a, "--fb=", 5) == 0) {
            newFb = std::atoi(a + 5);
        } else if (std::strncmp(a, "--json=", 7) == 0) {
            jsonOut = a + 7;
        } else if (std::strncmp(a, "--type=", 7) == 0) {
            unpackType = std::atoi(a + 7);
        } else if (std::strcmp(a, "--tagged") == 0) {
            unpackTagged = true;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            return 1;
        } else {
            args.push_back(a);
        }
    }
    if (tickRate < 1) tickRate = 60;

    if (cmd == "dump") {
        if (args.size() != 1) {
            Usage();
            return 1;
        }
        vgmstudio::FuiVoice v;
        std::string err;
        if (!vgmstudio::LoadFui(args[0], &v, &err)) {
            std::fprintf(stderr, "error: %s: %s\n", args[0].c_str(), err.c_str());
            return 2;
        }
        return DumpVoice(v, args[0], tickRate);
    }

    if (cmd == "lib") {
        // 导出乐器库：参数（唯一身份）+ 指纹（快速比较）+ 标签名（人读）
        if (args.size() != 1) {
            Usage();
            return 1;
        }
        vgmstudio::FurSong song;
        std::string err;
        if (!vgmstudio::LoadFur(args[0], &song, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 2;
        }
        if (!jsonOut.empty()) {
            std::string jerr;
            if (!WriteLibraryJson(song, args[0], jsonOut, &jerr)) {
                std::fprintf(stderr, "error: %s\n", jerr.c_str());
                return 2;
            }
            std::fprintf(stderr, "json written: %s\n", jsonOut.c_str());
        }
        std::printf("<!-- 以下内容由 `fuitool lib %s` 自动生成，勿手改 -->\n\n",
                    args[0].c_str());
        std::printf("来源 `%s`，Furnace 版本 %d，hz=%.2f，共 %zu 个乐器。\n\n",
                    args[0].c_str(), song.version, song.hz, song.instruments.size());
        std::printf("**身份靠参数，不靠指纹。** 指纹只是参数串的哈希，用于快速比较；\n");
        std::printf("参数相同 = 同一音色（哪怕标签名不同）。反之参数不同就是不同音色。\n\n");

        for (int wantType : {vgmstudio::kFuiInsOpm, vgmstudio::kFuiInsOpll}) {
            std::printf("## %s（type %d）\n\n",
                        wantType == vgmstudio::kFuiInsOpm ? "YM2151 源侧"
                                                          : "YM2413 目标侧",
                        wantType);
            std::printf("| # | 标签 | GM 音色 | preset | alg | fb | 指纹 | 参数 |\n");
            std::printf("|---|---|---|---|---|---|---|---|\n");
            for (size_t i = 0; i < song.instruments.size(); i++) {
                const vgmstudio::FurInstrument& fi = song.instruments[i];
                if (fi.type != wantType) continue;
                std::string ops;
                const int shown = fi.voice.ops >= 2 ? fi.voice.ops : 2;
                for (int o = 0; o < shown; o++) {
                    const vgmstudio::FuiOperator& op = fi.voice.op[o];
                    char b[192];
                    std::snprintf(b, sizeof(b),
                                  "op%d: ml=%d tl=%d ar=%d dr=%d sl=%d rr=%d dt=%d ksr=%d "
                                  "am=%d vib=%d egt=%d ksl=%d sus=%d kvs=%d",
                                  o, op.mult, op.tl, op.ar, op.dr, op.sl, op.rr, op.dt, op.ksr,
                                  op.am, op.vib, op.egt, op.ksl, op.sus, op.kvs);
                    if (o) ops += "<br>";
                    ops += b;
                }
                char gm[64] = "—";
                if (const GmEntry* e = LookupGmPaired(song, fi.name)) {
                    if (e->gmPercussionNote) {
                        std::snprintf(gm, sizeof(gm), "%s (perc %d)", e->gmName,
                                      e->gmPercussionNote);
                    } else {
                        std::snprintf(gm, sizeof(gm), "%s (%d)", e->gmName, e->gmProgram);
                    }
                }
                std::printf("| %zu | `%s` | %s | %d | %d | %d | `%016llx` | %s |\n", i,
                            fi.name.c_str(), gm, fi.voice.opllPreset, fi.voice.alg,
                            fi.voice.fb,
                            (unsigned long long)vgmstudio::VoiceFingerprint(fi.voice),
                            ops.c_str());
            }
            std::printf("\n");
        }

        // 明文清单：表格里的 <br> 不好复制，这里给每件乐器一段等宽文本
        std::printf("## 明文参数清单\n\n");
        std::printf("```text\n");
        for (size_t i = 0; i < song.instruments.size(); i++) {
            const vgmstudio::FurInstrument& fi = song.instruments[i];
            const vgmstudio::FuiVoice& v = fi.voice;
            const GmEntry* e = LookupGmPaired(song, fi.name);
            char gmtxt[64] = "-";
            if (e != nullptr) {
                if (e->gmPercussionNote) {
                    std::snprintf(gmtxt, sizeof(gmtxt), "%s/perc%d", e->gmName,
                                  e->gmPercussionNote);
                } else {
                    std::snprintf(gmtxt, sizeof(gmtxt), "%s/%d", e->gmName, e->gmProgram);
                }
            }
            std::printf("[%02zu] %-16s %-7s %-26s preset=%-2d alg=%d fb=%d fms=%d ams=%d "
                        "ops=%d fp=%016llx\n",
                        i, fi.name.c_str(),
                        fi.type == vgmstudio::kFuiInsOpm
                            ? "YM2151"
                            : (fi.type == vgmstudio::kFuiInsOpll ? "YM2413" : "other"),
                        e != nullptr ? e->fullName : "-",
                        v.opllPreset, v.alg, v.fb, v.fms, v.ams, v.ops,
                        (unsigned long long)vgmstudio::VoiceFingerprint(v));
            const int shown = v.ops >= 2 ? v.ops : 2;
            for (int o = 0; o < shown; o++) {
                const vgmstudio::FuiOperator& op = v.op[o];
                std::printf("     op%d ml=%-2d tl=%-3d ar=%-2d dr=%-2d sl=%-2d rr=%-2d dt=%-2d "
                            "dt2=%-2d ksr=%d am=%d vib=%d egt=%d ksl=%d ws=%d sus=%d kvs=%d\n",
                            o, op.mult, op.tl, op.ar, op.dr, op.sl, op.rr, op.dt, op.dt2,
                            op.ksr, op.am, op.vib, op.egt, op.ksl, op.ws, op.sus, op.kvs);
            }
            // 宏：OPLL 不用 0 号自定义音色，靠 patch(音色号)/音量宏把预设
            // 拼出更多变化，所以宏必须一起写进库
            for (int mk = 0; mk < vgmstudio::kMacroCount; mk++) {
                const vgmstudio::Macro& mc = v.macros[mk];
                if (mc.val.empty()) continue;
                std::printf("     %-6s speed=%-2d delay=%-2d loop=%-3d rel=%-3d vals:",
                            MacroName(mk), mc.speed, mc.delay, mc.loop, mc.rel);
                for (size_t k = 0; k < mc.val.size() && k < 32; k++)
                    std::printf(" %d", mc.val[k]);
                if (mc.val.size() > 32) std::printf(" ...(%zu)", mc.val.size());
                std::printf("\n");
            }
            for (int o = 0; o < 4; o++) {
                for (int j = 0; j < vgmstudio::kOpMacroCount; j++) {
                    const vgmstudio::Macro& mc = v.opMacros[o][j];
                    if (mc.val.empty()) continue;
                    std::printf("     op%d %-5s speed=%-2d delay=%-2d vals:", o,
                                OpMacroName(j), mc.speed, mc.delay);
                    for (size_t k = 0; k < mc.val.size() && k < 32; k++)
                        std::printf(" %d", mc.val[k]);
                    std::printf("\n");
                }
            }
        }
        std::printf("```\n");
        return 0;
    }

    if (cmd == "fur") {
        // 把整个 .fur 工程里的乐器列出来（读法与 opm2opll --fur 完全一致）
        if (args.size() != 1) {
            Usage();
            return 1;
        }
        vgmstudio::FurSong song;
        std::string err;
        if (!vgmstudio::LoadFur(args[0], &song, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 2;
        }
        std::printf("file          : %s\n", args[0].c_str());
        std::printf("version       : %d\n", song.version);
        std::printf("hz            : %.2f (宏 tick 速率)\n", song.hz);
        std::printf("name          : %s\n", song.name.c_str());
        std::printf("author        : %s\n", song.author.c_str());
        std::printf("instruments   : %zu\n\n", song.instruments.size());
        std::printf("  %-3s %-5s %-6s %-10s %-22s %s\n", "#", "type", "preset", "fingerprint",
                    "name", "macros");
        for (size_t i = 0; i < song.instruments.size(); i++) {
            const vgmstudio::FurInstrument& fi = song.instruments[i];
            int macros = 0;
            for (int k = 0; k < vgmstudio::kMacroCount; k++)
                if (!fi.voice.macros[k].val.empty()) macros++;
            for (int k = 0; k < 4; k++)
                for (int j = 0; j < vgmstudio::kOpMacroCount; j++)
                    if (!fi.voice.opMacros[k][j].val.empty()) macros++;
            std::printf("  %-3zu %-5d %-6d %016llx %-22s %d\n", i, fi.type,
                        fi.voice.opllPreset,
                        (unsigned long long)vgmstudio::VoiceFingerprint(fi.voice),
                        fi.name.c_str(), macros);
        }
        return 0;
    }

    if (cmd == "unpack") {
        // 工程 → 一堆 .fui，文件名用 Furnace 里的乐器全名（含 note:/vol: 等参数）
        if (args.size() != 2) {
            Usage();
            return 1;
        }
        vgmstudio::FurSong song;
        std::string err;
        if (!vgmstudio::LoadFur(args[0], &song, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 2;
        }
        const std::string outDir = args[1];
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);

        int written = 0;
        for (const vgmstudio::FurInstrument& fi : song.instruments) {
            if (unpackType >= 0 && fi.type != unpackType) continue;
            // 文件名 = 乐器全名；Windows 不允许的字符换成 '_'
            std::string base = fi.name;
            if (unpackTagged) base = vgmstudio::ParseInstrumentTags(fi.name).base;
            if (base.empty()) base = "instrument";
            for (char& c : base) {
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                    c == '<' || c == '>' || c == '|')
                    c = '_';
            }
            const std::string path = outDir + "/" + base + ".fui";
            if (!vgmstudio::SaveFui(fi.voice, path, &err)) {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                continue;
            }
            std::printf("%-5s preset=%-2d  %s\n",
                        fi.type == vgmstudio::kFuiInsOpll   ? "OPLL"
                        : fi.type == vgmstudio::kFuiInsOpm ? "OPM "
                                                           : "other",
                        fi.voice.opllPreset, path.c_str());
            written++;
        }
        std::printf("\n%d instruments -> %s\n", written, outDir.c_str());
        return 0;
    }

    if (cmd == "check") {
        if (args.empty()) {
            Usage();
            return 1;
        }
        int bad = 0;
        for (const std::string& p : args) {
            vgmstudio::FuiVoice v;
            std::string err;
            if (vgmstudio::LoadFui(p, &v, &err)) {
                int macros = 0;
                for (int i = 0; i < vgmstudio::kMacroCount; i++)
                    if (!v.macros[i].val.empty()) macros++;
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < vgmstudio::kOpMacroCount; j++)
                        if (!v.opMacros[i][j].val.empty()) macros++;
                std::printf("OK    %-40s type=%-3d %-14s macros=%d\n", p.c_str(), v.type,
                            TypeName(v.type), macros);
            } else {
                std::printf("FAIL  %-40s %s\n", p.c_str(), err.c_str());
                bad++;
            }
        }
        return bad == 0 ? 0 : 3;
    }

    if (cmd == "rewrite") {
        if (args.size() != 2) {
            Usage();
            return 1;
        }
        vgmstudio::FuiVoice v;
        std::string err;
        if (!vgmstudio::LoadFui(args[0], &v, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 2;
        }
        if (!newName.empty()) v.name = newName;
        if (newAlg >= 0) v.alg = newAlg & 7;
        if (newFb >= 0) v.fb = newFb & 7;
        if (!vgmstudio::SaveFui(v, args[1], &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 2;
        }
        std::printf("read  : %s\n", args[0].c_str());
        std::printf("write : %s\n", args[1].c_str());
        return 0;
    }

    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    Usage();
    return 1;
}
