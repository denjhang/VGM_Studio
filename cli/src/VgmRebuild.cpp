// VgmRebuild.cpp —— 见头文件注释。
#include "VgmRebuild.h"

namespace vgmstudio {

std::vector<uint8_t> RebuildFromRaw(const Document& doc, RebuildStats* stats, std::string* err) {
    std::vector<uint8_t> header = Base64Decode(doc.raw.headerBase64);
    std::vector<uint8_t> cmds = Base64Decode(doc.raw.commandsBase64);
    std::vector<uint8_t> tail = Base64Decode(doc.raw.tailBase64);

    if (header.empty()) {
        *err = "raw.header 缺失：无法做位精确往返（导入时被 --no-raw 关掉？）";
        return {};
    }
    if (cmds.empty()) {
        *err = "raw.commands 缺失";
        return {};
    }

    if (stats != nullptr) {
        stats->headerBytes = header.size();
        stats->commandBytes = cmds.size();
        stats->tailBytes = tail.size();
    }

    std::vector<uint8_t> out;
    out.reserve(header.size() + cmds.size() + tail.size());
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), cmds.begin(), cmds.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

}  // namespace vgmstudio
