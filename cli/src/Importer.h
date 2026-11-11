// Importer —— 把 VGM 文件变成 VGSJ Document（vgm2json 的全部业务）。
#pragma once

#include <string>

#include "Document.h"
#include "NoteSplitter.h"

namespace vgmstudio {

struct ImportOptions {
    SplitOptions split;
    bool includeRaw = true;   // 关掉可减小文件，但失去位精确往返
    bool pretty = true;
};

struct ImportOutcome {
    Document doc;
    bool ok = false;
    std::string error;
    size_t totalWrites = 0;
    size_t structuredWrites = 0;
    size_t registerWritesInFile = 0;  // 文件里 0x54/0x51 命令的条数（独立对照）
};

ImportOutcome BuildDocument(const std::string& vgmPath, const ImportOptions& opt);

// 读整个文件（raw 三段切分与 sha256 用）。
bool ReadWholeFile(const std::string& path, std::vector<uint8_t>* out, std::string* err);

}  // namespace vgmstudio
