// vgmroundtrip —— vgm → json → vgm 字节级往返校验（M1 的硬门禁）。
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Document.h"
#include "Importer.h"
#include "VgmRebuild.h"

namespace {

void Usage() {
    std::printf(
        "usage: vgmroundtrip <input.vgm> [--keep]\n"
        "\n"
        "跑 vgm2json → json2vgm 全链路并逐字节比对。\n"
        "  --keep  保留中间文件（<input>.rt.json / <input>.rt.vgm）\n"
        "退出码：0 PASS / 1 参数错误 / 2 读失败 / 3 结构化失败 / 4 字节不一致\n");
}

std::string StemOf(const std::string& in) {
    size_t dot = in.find_last_of('.');
    size_t sep = in.find_last_of("/\\");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) return in.substr(0, dot);
    return in;
}

void HexByte(char* out, uint8_t v) { std::snprintf(out, 3, "%02X", v); }

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    bool keep = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            Usage();
            return 0;
        }
        if (std::strcmp(a, "--keep") == 0) keep = true;
        else if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            return 1;
        } else if (input.empty()) input = a;
    }
    if (input.empty()) {
        Usage();
        return 1;
    }

    std::vector<uint8_t> original;
    std::string err;
    if (!vgmstudio::ReadWholeFile(input, &original, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    vgmstudio::ImportOptions opt;
    opt.pretty = false;
    vgmstudio::ImportOutcome built = vgmstudio::BuildDocument(input, opt);
    if (!built.ok) {
        std::fprintf(stderr, "error: import failed: %s\n", built.error.c_str());
        return 3;
    }
    if (built.doc.CountStructuredWrites() != built.totalWrites) {
        std::fprintf(stderr, "error: integrity mismatch %zu != %zu\n",
                     built.doc.CountStructuredWrites(), built.totalWrites);
        return 3;
    }

    const std::string stem = StemOf(input);
    const std::string jsonPath = stem + ".rt.json";
    const std::string vgmPath = stem + ".rt.vgm";

    if (!built.doc.SaveJson(jsonPath, false, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 3;
    }

    vgmstudio::Document reloaded;
    if (!vgmstudio::Document::LoadJson(jsonPath, &reloaded, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 3;
    }

    vgmstudio::RebuildStats stats;
    std::vector<uint8_t> rebuilt = vgmstudio::RebuildFromRaw(reloaded, &stats, &err);
    if (rebuilt.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 3;
    }

    {
        std::ofstream f(vgmPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(rebuilt.data()),
                static_cast<std::streamsize>(rebuilt.size()));
    }

    bool same = (original.size() == rebuilt.size()) &&
                std::memcmp(original.data(), rebuilt.data(), original.size()) == 0;

    std::printf("%s\n", same ? "PASS" : "FAIL");
    std::printf("原文件        %zu bytes\n", original.size());
    std::printf("重建文件      %zu bytes\n", rebuilt.size());
    std::printf("字节一致      %s\n", same ? "YES" : "NO");
    if (!same) {
        size_t n = std::min(original.size(), rebuilt.size());
        size_t first = n;
        for (size_t i = 0; i < n; i++) {
            if (original[i] != rebuilt[i]) {
                first = i;
                break;
            }
        }
        if (first == n && original.size() != rebuilt.size()) first = n;
        char a[3] = {0}, b[3] = {0};
        if (first < original.size()) HexByte(a, original[first]); else std::strcpy(a, "--");
        if (first < rebuilt.size()) HexByte(b, rebuilt[first]); else std::strcpy(b, "--");
        std::printf("首个差异偏移  0x%zX  (原 %s / 新 %s)\n", first, a, b);
    }

    if (!keep) {
        std::remove(jsonPath.c_str());
        std::remove(vgmPath.c_str());
    }
    return same ? 0 : 4;
}
