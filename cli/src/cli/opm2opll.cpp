// opm2opll —— YM2151(OPM) JSON → YM2413(OPLL) JSON。
//
// 时域原样搬、通道一一对应、音色走 OPM→OPN→OPL→OPLL ROM 匹配链。
// 输出 JSON 里仍然只有寄存器原值，不含任何音高/音量/时序量化。
#include <cstdio>
#include <fstream>
#include <cstring>
#include <string>

#include "Opm2Opll.h"
#include "Document.h"
#include "FmVoice.h"
#include "PreviewVgm.h"

namespace {

void Usage() {
    std::printf(
        "usage: json2json <source.json> -o <target.json> [options]\n"
        "\n"
        "  --target=ym2413        目标芯片（目前只支持 ym2413）\n"
        "  --bank=ym2413          音色库：ym2413 | vrc7 | ymf281b\n"
        "  --clock=3579545        YM2413 时钟\n"
        "  --volume-bias=0        音量偏移叠加（-15..15）\n"
        "  --transpose=0          整体移调（半音，降八度用 -12）\n"
        "  --user-voice           额外把音色写进 0x00-0x07 用户槽（仅单音色曲目合理）\n"
        "  --report               打印逐音符匹配报告\n"
        "  --fui                  为每个源音色写 chXinsY_ym2413.fui\n"
        "  --fui-dir=DIR          .fui 输出目录（隐含 --fui）\n"
        "  --use-edited-fui       读同目录下改过的 .fui 覆盖自动匹配\n"
        "  --fur=FILE.fur         从 Furnace 工程读全部音色（按乐器名 chXinsY 匹配），\n"
        "                         并用工程的 hz 作为宏 tick 速率\n"
        "  --tick-rate=60         宏的 tick 速率（Furnace 的 song.hz）\n"
        "  --no-macros            不执行 .fui 里的宏\n"
        "  --vgm=OUT.vgm          同时输出试听 VGM\n");
}

std::string DefaultOutput(const std::string& in) {
    size_t dot = in.find_last_of('.');
    size_t sep = in.find_last_of("/\\");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
        return in.substr(0, dot) + ".ym2413.json";
    }
    return in + ".ym2413.json";
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    std::string target = "ym2413";
    bool report = false;
    std::string previewPath;
    vgmstudio::ConvertOptions opt;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            Usage();
            return 0;
        }
        if (std::strcmp(a, "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strncmp(a, "-o", 2) == 0 && a[2] != '\0') {
            output = a + 2;
        } else if (std::strncmp(a, "--target=", 9) == 0) {
            target = a + 9;
        } else if (std::strncmp(a, "--bank=", 7) == 0) {
            opt.bank = a + 7;
        } else if (std::strncmp(a, "--clock=", 8) == 0) {
            opt.targetClock = static_cast<uint32_t>(std::strtoul(a + 8, nullptr, 10));
        } else if (std::strncmp(a, "--volume-bias=", 14) == 0) {
            opt.volumeOffsetBias = std::atoi(a + 14);
        } else if (std::strncmp(a, "--transpose=", 12) == 0) {
            opt.transposeSemitones = std::atoi(a + 12);
        } else if (std::strcmp(a, "--user-voice") == 0) {
            opt.useUserVoice = true;
        } else if (std::strcmp(a, "--report") == 0) {
            report = true;
        } else if (std::strcmp(a, "--fui") == 0) {
            opt.exportFui = true;
        } else if (std::strncmp(a, "--fui-dir=", 10) == 0) {
            opt.fuiDir = a + 10;
            opt.exportFui = true;
        } else if (std::strncmp(a, "--tick-rate=", 12) == 0) {
            opt.tickRate = std::atoi(a + 12);
        } else if (std::strcmp(a, "--no-macros") == 0) {
            opt.runMacros = false;
        } else if (std::strcmp(a, "--use-edited-fui") == 0) {
            opt.useEditedFui = true;
            opt.exportFui = true;
        } else if (std::strncmp(a, "--fur=", 6) == 0) {
            opt.furPath = a + 6;
        } else if (std::strcmp(a, "--fur") == 0 && i + 1 < argc) {
            opt.furPath = argv[++i];
        } else if (std::strncmp(a, "--vgm=", 6) == 0) {
            previewPath = a + 6;
        } else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            Usage();
            return 1;
        } else if (input.empty()) {
            input = a;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", a);
            return 1;
        }
    }
    if (input.empty()) {
        Usage();
        return 1;
    }
    if (target != "ym2413") {
        std::fprintf(stderr, "error: unsupported target chip '%s' (only ym2413)\n", target.c_str());
        return 1;
    }
    if (output.empty()) output = DefaultOutput(input);
    if (opt.exportFui && opt.fuiDir.empty()) {
        const size_t sep = output.find_last_of("/\\");
        opt.fuiDir = (sep == std::string::npos) ? std::string(".") : output.substr(0, sep);
    }

    vgmstudio::Document src;
    std::string err;
    if (!vgmstudio::Document::LoadJson(input, &src, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    vgmstudio::ConvertResult r = vgmstudio::ConvertOpmToOpll(src, opt);
    if (!r.ok) {
        std::fprintf(stderr, "error: %s\n", r.error.c_str());
        return 3;
    }
    if (!r.doc.SaveJson(output, true, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    std::printf("written       : %s\n", output.c_str());
    std::printf("target        : YM2413 @%u bank=%s\n", opt.targetClock, opt.bank.c_str());
    std::printf("notes         : %zu converted\n", r.notesConverted);
    if (!r.fuiFiles.empty()) std::printf("fui           : %zu files\n", r.fuiFiles.size());
    if (!r.fuiApplied.empty())
        std::printf("fui applied   : %zu edited voices\n", r.fuiApplied.size());
    if (r.furInstruments != 0 || !opt.furPath.empty()) {
        std::printf("fur           : %zu instruments, %zu matched (%d tick/s)\n",
                    r.furInstruments, r.furApplied.size(), r.tickRateUsed);
        for (const std::string& n : r.furApplied) std::printf("                %s\n", n.c_str());
    }

    // 有损报告：按 (program, volumeOffset) 聚合
    if (!r.reports.empty()) {
        struct Agg {
            int program = 0;
            int volOff = 0;
            int count = 0;
            int minTl = 127;
            int maxTl = 0;
            int minOct = 0;
            int maxOct = 0;
        };
        std::vector<Agg> aggs;
        for (const auto& rep : r.reports) {
            bool found = false;
            for (Agg& a : aggs) {
                if (a.program == rep.program && a.volOff == rep.volumeOffset) {
                    a.count++;
                    a.minTl = std::min(a.minTl, rep.srcCarrierTl);
                    a.maxTl = std::max(a.maxTl, rep.srcCarrierTl);
                    a.minOct = std::min(a.minOct, rep.octaveOffset);
                    a.maxOct = std::max(a.maxOct, rep.octaveOffset);
                    found = true;
                    break;
                }
            }
            if (!found) {
                Agg a;
                a.program = rep.program;
                a.volOff = rep.volumeOffset;
                a.count = 1;
                a.minTl = a.maxTl = rep.srcCarrierTl;
                a.minOct = a.maxOct = rep.octaveOffset;
                aggs.push_back(a);
            }
        }
        std::printf("\nlossy report  :\n");
        std::printf("  %-4s %-20s %-6s %-8s %-10s %s\n", "prog", "name", "notes", "volOff",
                    "srcTL", "octOff");
        for (const Agg& a : aggs) {
            std::printf("  %-4d %-20s %-6d %-8d %3d..%-3d  %d..%d\n", a.program,
                        vgmstudio::OpllProgramName(vgmstudio::OpllBank::Ym2413, a.program),
                        a.count, a.volOff, a.minTl, a.maxTl, a.minOct, a.maxOct);
        }
        std::printf("  dropped: dt2 / d2r / ks(3bit) / op3 / op4 / ams / pms\n");
        std::printf("  degraded: alg(8) -> sus(1), ar/dr 31 -> 15, tl 127 -> 4bit vol\n");
    }

    if (!previewPath.empty()) {
        std::vector<uint8_t> bytes = vgmstudio::EmitPreviewVgm(r.doc, &err);
        if (bytes.empty()) {
            std::fprintf(stderr, "error: preview vgm: %s\n", err.c_str());
            return 3;
        }
        std::ofstream f(previewPath, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "error: cannot write %s\n", previewPath.c_str());
            return 2;
        }
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        std::printf("preview vgm   : %s (%zu bytes)\n", previewPath.c_str(), bytes.size());
    }

    if (report) {
        std::printf("\nper-note:\n");
        for (const auto& rep : r.reports) {
            std::printf("  ch%d -> ch%d prog=%-2d volOff=%+-2d oct=%+-2d kc=%02X kf=%02X %.2fHz\n",
                        rep.sourceChannel, rep.targetChannel, rep.program, rep.volumeOffset,
                        rep.octaveOffset, rep.kc, rep.kf, rep.freqHz);
        }
    }
    return 0;
}
