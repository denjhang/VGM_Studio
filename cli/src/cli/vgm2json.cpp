// vgm2json —— VGM → VGSJ JSON（导入 + 结构化 + 完整性校验）。
#include <cstdio>
#include <cstring>
#include <string>

#include "Document.h"
#include "Importer.h"

namespace {

void Usage() {
    std::printf(
        "usage: vgm2json <input.vgm> [-o output.json] [--no-raw] [--compact]\n"
        "                [--pitch-jump=<semitone>]\n"
        "\n"
        "  -o              输出路径（默认与输入同名 .json）\n"
        "  --no-raw        不写 raw（文件更小，但失去位精确往返）\n"
        "  --compact       紧凑输出（默认缩进）\n"
        "  --pitch-jump    KC 音高跳变切块阈值（半音，默认 0.5）\n"
        "\n"
        "退出码：0 成功 / 1 参数错误 / 2 文件读失败 / 3 VGM 解析失败 / 4 完整性校验失败\n");
}

std::string DefaultOutput(const std::string& in) {
    size_t dot = in.find_last_of('.');
    size_t sep = in.find_last_of("/\\");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
        return in.substr(0, dot) + ".json";
    }
    return in + ".json";
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    vgmstudio::ImportOptions opt;

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
        } else if (std::strcmp(a, "--no-raw") == 0) {
            opt.includeRaw = false;
        } else if (std::strcmp(a, "--compact") == 0) {
            opt.pretty = false;
        } else if (std::strncmp(a, "--pitch-jump=", 13) == 0) {
            opt.split.pitchJumpSemitones = static_cast<float>(std::atof(a + 13));
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
    if (output.empty()) output = DefaultOutput(input);

    vgmstudio::ImportOutcome r = vgmstudio::BuildDocument(input, opt);
    if (!r.ok) {
        std::fprintf(stderr, "error: %s\n", r.error.c_str());
        return (r.error.find("not a VGM") != std::string::npos ||
                r.error.find("load") != std::string::npos)
                   ? 3
                   : 2;
    }

    // ── 完整性校验：结构化条数 == 文件里的寄存器写条数 ──
    const size_t structured = r.doc.CountStructuredWrites();
    bool integrityOk = (structured == r.totalWrites);
    if (!integrityOk) {
        std::fprintf(stderr,
                     "error: 完整性校验失败：结构化 %zu 条，寄存器流 %zu 条\n",
                     structured, r.totalWrites);
        return 4;
    }

    std::string err;
    if (!r.doc.SaveJson(output, opt.pretty, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    std::printf("written       : %s\n", output.c_str());
    std::printf("source        : %s (%u samples)\n", r.doc.meta.sourceFile.c_str(),
                r.doc.timeBase.totalSamples);
    std::printf("regWrites     : file=%zu  stream=%zu  structured=%zu\n",
                r.registerWritesInFile, r.totalWrites, structured);
    std::printf("integrity     : %s\n", integrityOk ? "OK" : "FAIL");
    size_t notes = 0;
    for (const auto& c : r.doc.channels) notes += c.notes.size();
    std::printf("notes         : %zu\n", notes);
    std::printf("globals       : %zu segments\n", r.doc.globals.size());
    return 0;
}
