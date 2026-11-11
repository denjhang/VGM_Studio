// jsondump —— 人读摘要（通道数、音符数、寄存器写数、完整性校验结果）。
#include <cstdio>
#include <cstring>
#include <string>

#include "Document.h"

namespace {

void Usage() {
    std::printf(
        "usage: jsondump <input.json> [--notes] [--regs]\n"
        "\n"
        "  --notes   逐条打印音符（含起止采样与音名）\n"
        "  --regs    打印前若干条寄存器原文\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    bool showNotes = false;
    bool showRegs = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            Usage();
            return 0;
        }
        if (std::strcmp(a, "--notes") == 0) showNotes = true;
        else if (std::strcmp(a, "--regs") == 0) showRegs = true;
        else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            return 1;
        } else if (input.empty()) input = a;
    }
    if (input.empty()) {
        Usage();
        return 1;
    }

    vgmstudio::Document doc;
    std::string err;
    if (!vgmstudio::Document::LoadJson(input, &doc, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    std::printf("file          : %s\n", input.c_str());
    std::printf("format        : %s v%d\n", doc.format.c_str(), doc.version);
    std::printf("source        : %s (%llu bytes, %u samples)\n", doc.meta.sourceFile.c_str(),
                static_cast<unsigned long long>(doc.meta.sourceSize), doc.timeBase.totalSamples);
    std::printf("parser        : %s (%s)\n", doc.meta.parserLib.c_str(),
                doc.meta.regStreamMethod.c_str());
    std::printf("structurer    : %s (%s)\n", doc.meta.structurer.c_str(),
                doc.meta.splitRule.c_str());

    std::printf("chips         :");
    for (const vgmstudio::ChipEntry& c : doc.chips) {
        std::printf(" %s %s @%u (%d ch)", c.id.c_str(), c.type.c_str(), c.clock, c.channelCount);
    }
    std::printf("\n");

    size_t totalNotes = 0;
    size_t regsInNotes = 0;
    size_t regsInOrphans = 0;
    size_t regsInGlobals = 0;
    size_t lanePoints = 0;
    for (const vgmstudio::Channel& c : doc.channels) {
        totalNotes += c.notes.size();
        for (const vgmstudio::Note& n : c.notes)
            for (const vgmstudio::Segment& s : n.segments) regsInNotes += s.regs.size();
        for (const vgmstudio::Orphan& o : c.orphans) regsInOrphans += o.regs.size();
        for (const vgmstudio::Lane& l : c.lanes) lanePoints += l.points.size();
    }
    for (const vgmstudio::GlobalSegment& g : doc.globals) regsInGlobals += g.regs.size();

    const size_t total = regsInNotes + regsInOrphans + regsInGlobals;
    std::printf("channels      : %zu\n", doc.channels.size());
    std::printf("notes         : %zu\n", totalNotes);
    std::printf("regStream     : %zu writes  (notes %zu / orphans %zu / globals %zu)\n", total,
                regsInNotes, regsInOrphans, regsInGlobals);
    std::printf("globals       : %zu segments\n", doc.globals.size());
    std::printf("lanes         : %zu points\n", lanePoints);
    std::printf("integrity     : %s  (%zu == %zu)\n", total == doc.CountStructuredWrites() ? "OK" : "MISMATCH",
                total, doc.CountStructuredWrites());
    if (!doc.meta.unhandled.empty()) {
        std::printf("unhandled     : %zu\n", doc.meta.unhandled.size());
        for (const std::string& u : doc.meta.unhandled) std::printf("  %s\n", u.c_str());
    }
    if (doc.timeBase.loopSample >= 0) {
        std::printf("loop          : sample %lld\n", static_cast<long long>(doc.timeBase.loopSample));
    }

    for (const vgmstudio::Channel& c : doc.channels) {
        std::printf("  ch%d %-6s notes=%-5zu orphans=%-4zu lanes=%zu\n", c.index, c.name.c_str(),
                    c.notes.size(), c.orphans.size(), c.lanes.size());
    }

    if (showNotes) {
        for (const vgmstudio::Channel& c : doc.channels) {
            for (const vgmstudio::Note& n : c.notes) {
                std::printf("note ch%d id=%d %u..%u (dur %u) segments=%zu\n", c.index, n.id,
                            n.startSample, n.endSample,
                            n.endSample > n.startSample ? n.endSample - n.startSample : 0,
                            n.segments.size());
            }
        }
    }

    if (showRegs) {
        size_t shown = 0;
        for (const vgmstudio::Channel& c : doc.channels) {
            for (const vgmstudio::Note& n : c.notes) {
                for (const vgmstudio::Segment& s : n.segments) {
                    if (shown++ >= 40) return 0;
                    std::printf("@%u ch%d:", s.atSample, c.index);
                    for (const vgmstudio::RegValue& r : s.regs)
                        std::printf(" %02X=%02X", r.reg, r.val);
                    std::printf("\n");
                }
            }
        }
    }
    return 0;
}
