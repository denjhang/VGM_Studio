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
#include <string>
#include <vector>
#include <cstdlib>

#include "FuiVoice.h"

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
        "  fuitool rewrite <in.fui> <out.fui> [--name=...] [--alg=N] [--fb=N]\n"
        "      读入 → 改几个字段 → 写回（验证编解码往返无损）\n"
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

const char* MacroName(int i) {
    static const char* kNames[vgmstudio::kMacroCount] = {
        "vol", "arp", "duty", "wave", "pitch", "ex1", "ex2", "ex3",
        "alg", "fb", "fms", "ams", "panL", "panR", "phaseReset",
        "ex4", "ex5", "ex6", "ex7", "ex8", "ex9", "ex10"};
    return (i >= 0 && i < vgmstudio::kMacroCount) ? kNames[i] : "?";
}

int DumpOne(const std::string& path, int tickRate) {
    vgmstudio::FuiVoice v;
    std::string err;
    if (!vgmstudio::LoadFui(path, &v, &err)) {
        std::fprintf(stderr, "error: %s: %s\n", path.c_str(), err.c_str());
        return 2;
    }

    std::printf("file          : %s\n", path.c_str());
    std::printf("type          : %d (%s)\n", v.type, TypeName(v.type));
    std::printf("name          : %s\n", v.name.c_str());
    std::printf("alg/fb        : %d / %d\n", v.alg, v.fb);
    std::printf("fms/ams       : %d / %d   (ops=%d, opllPreset=%d)\n", v.fms, v.ams, v.ops,
                v.opllPreset);
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
    return 0;
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
    std::vector<std::string> args;
    std::string newName;
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
        return DumpOne(args[0], tickRate);
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
