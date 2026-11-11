// json2vgm —— JSON → VGM。
//
// 当前只做位精确往返（raw 三段拼回）。结构化重排命令的输出必须走 furnace
// dispatch（RULES.md E3/F1），等步骤 3 接上输出引擎后在这里加 --from-data。
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "Document.h"
#include "VgmRebuild.h"

namespace {

void Usage() {
    std::printf(
        "usage: json2vgm <input.json> [-o output.vgm]\n"
        "\n"
        "  -o      输出路径（默认与输入同名 .vgm）\n"
        "\n"
        "当前只支持位精确往返（用 raw 三段原样拼回）。\n"
        "从编辑后的数据重建 VGM 需要 furnace dispatch（尚未接入）。\n");
}

std::string DefaultOutput(const std::string& in) {
    size_t dot = in.find_last_of('.');
    size_t sep = in.find_last_of("/\\");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
        return in.substr(0, dot) + ".vgm";
    }
    return in + ".vgm";
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            Usage();
            return 0;
        }
        if (std::strcmp(a, "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (std::strncmp(a, "-o", 2) == 0 && a[2] != '\0') output = a + 2;
        else if (a[0] == '-') {
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
    if (output.empty()) output = DefaultOutput(input);

    vgmstudio::Document doc;
    std::string err;
    if (!vgmstudio::Document::LoadJson(input, &doc, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    vgmstudio::RebuildStats stats;
    std::vector<uint8_t> bytes = vgmstudio::RebuildFromRaw(doc, &stats, &err);
    if (bytes.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 3;
    }

    std::ofstream f(output, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "error: cannot write %s\n", output.c_str());
        return 2;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));

    std::printf("written       : %s (%zu bytes)\n", output.c_str(), bytes.size());
    std::printf("segments      : header %zu + commands %zu + tail %zu\n", stats.headerBytes,
                stats.commandBytes, stats.tailBytes);
    return 0;
}
