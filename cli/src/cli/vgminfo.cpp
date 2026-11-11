// vgminfo —— 体检工具：只报账，不结构化。
// 头部信息与命令统计全部来自 libvgm（RULES.md E1/F2）。
#include <cstdio>
#include <cstring>
#include <string>

#include "VgmImportPlayer.h"

namespace {

std::string FormatTime(uint32_t samples) {
    double sec = static_cast<double>(samples) / 44100.0;
    int mins = static_cast<int>(sec / 60.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%05.2f", mins, sec - 60.0 * mins);
    return buf;
}

void Usage() {
    std::printf(
        "usage: vgminfo <input.vgm>\n"
        "\n"
        "打印 VGM 头部信息与命令统计。不解析语义、不写文件。\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0) {
        Usage();
        return argc < 2 ? 1 : 0;
    }

    const std::string path = argv[1];
    vgmstudio::ImportResult r = vgmstudio::ProbeVgm(path);
    if (!r.ok) {
        std::fprintf(stderr, "error: %s (%s)\n", r.error.c_str(), path.c_str());
        return 2;
    }

    std::printf("file          : %s\n", path.c_str());
    std::printf("version       : %x.%02x\n", (r.header.fileVer >> 8) & 0xFF, r.header.fileVer & 0xFF);
    std::printf("dataOffset    : 0x%X\n", r.header.dataOffset);
    std::printf("eofOffset     : 0x%X\n", r.header.eofOffset);
    std::printf("totalSamples  : %u  (%s @44100)\n", r.header.totalSamples,
                FormatTime(r.header.totalSamples).c_str());
    if (r.header.loopOffset != 0) {
        std::printf("loopOffset    : 0x%X\n", r.header.loopOffset);
        std::printf("loopSamples   : %u  (%s)\n", r.header.loopSamples,
                    FormatTime(r.header.loopSamples).c_str());
    } else {
        std::printf("loop          : none\n");
    }
    if (r.header.recordHz != 0) std::printf("recordHz      : %u\n", r.header.recordHz);

    std::printf("chips:\n");
    for (const vgmstudio::ChipInfo& c : r.chips) {
        std::printf("  0x%02X %-10s clock=%u  channels=%u\n", c.vgmChipType, c.name.c_str(),
                    c.clock, c.channelCount);
    }

    std::printf("commands:\n");
    unsigned long long total = 0;
    for (const vgmstudio::CommandStat& s : r.commands) {
        std::printf("  %-26s %8llu\n", s.label.c_str(), static_cast<unsigned long long>(s.count));
        total += s.count;
    }
    std::printf("  %-26s %8llu\n", "(total)", total);

    if (!r.unhandled.empty()) {
        std::printf("unhandled:\n");
        for (const std::string& u : r.unhandled) std::printf("  %s\n", u.c_str());
    }
    return 0;
}
