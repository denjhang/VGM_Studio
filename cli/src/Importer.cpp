// Importer.cpp —— VGM → VGSJ Document。
#include "Importer.h"

#include <cstdio>
#include <ctime>
#include <fstream>

#include "VgmImportPlayer.h"

namespace vgmstudio {
namespace {

std::string NowIso8601() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

std::string BaseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

}  // namespace

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>* out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *err = "cannot open " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0) {
        *err = "empty file";
        return false;
    }
    out->resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out->data()), size);
    if (!f) {
        *err = "short read";
        return false;
    }
    return true;
}

ImportOutcome BuildDocument(const std::string& vgmPath, const ImportOptions& opt) {
    ImportOutcome out;

    std::vector<uint8_t> bytes;
    std::string err;
    if (!ReadWholeFile(vgmPath, &bytes, &err)) {
        out.error = err;
        return out;
    }

    ImportResult imported = ImportVgm(vgmPath);
    if (!imported.ok) {
        out.error = imported.error;
        return out;
    }

    out.registerWritesInFile = 0;
    for (const CommandStat& s : imported.commands) {
        if (s.cmd == 0x54 || s.cmd == 0xA4 || s.cmd == 0x51 || s.cmd == 0xA1) {
            out.registerWritesInFile += s.count;
        }
    }

    SplitResult split = SplitYm2151(imported.writes, opt.split);
    out.totalWrites = split.totalWrites;
    out.structuredWrites = split.structuredWrites;

    Document& doc = out.doc;
    doc.format = "vgm-studio/song";
    doc.version = 1;

    doc.meta.sourceFile = BaseName(vgmPath);
    doc.meta.sourceSize = bytes.size();
    doc.meta.sourceSha256 = Sha256Hex(bytes.data(), bytes.size());
    doc.meta.importedAt = NowIso8601();
    doc.meta.parserLib = "libvgm";
    doc.meta.regStreamMethod = "shadowdev";
    doc.meta.structurer = "ym2151/v1";
    doc.meta.splitRule = "modizer-note-merge";
    doc.meta.unhandled = imported.unhandled;

    doc.timeBase.sampleRate = 44100;
    doc.timeBase.totalSamples = imported.header.totalSamples;
    doc.timeBase.loopSample =
        imported.header.loopOffset ? static_cast<int64_t>(imported.header.loopSamples) : -1;
    doc.timeBase.vgmRate = imported.header.recordHz ? imported.header.recordHz : 60;

    int chipIdx = 0;
    for (const ChipInfo& ci : imported.chips) {
        ChipEntry ce;
        char id[32];
        std::snprintf(id, sizeof(id), "%s.%d", ci.name.c_str(), chipIdx++);
        ce.id = id;
        ce.type = ci.name;
        ce.vgmChipType = ci.vgmChipType;
        ce.clock = ci.clock;
        ce.instances = 1;
        ce.channelCount = ci.channelCount;
        ce.portCount = 2;
        ce.capabilities.operators = 4;
        ce.capabilities.algorithms = 8;
        ce.capabilities.polyphony = ci.channelCount;
        doc.chips.push_back(ce);
    }

    doc.globals = split.globals;
    doc.channels = split.channels;

    if (opt.includeRaw) {
        const uint32_t dataOfs = imported.header.dataOffset;
        const uint32_t eofOfs = imported.header.eofOffset;
        if (dataOfs <= eofOfs && eofOfs <= bytes.size()) {
            doc.raw.dataOffset = dataOfs;
            doc.raw.eofOffset = eofOfs;
            doc.raw.headerBase64 = Base64Encode(bytes.data(), dataOfs);
            doc.raw.commandsBase64 = Base64Encode(bytes.data() + dataOfs, eofOfs - dataOfs);
            doc.raw.tailBase64 = Base64Encode(bytes.data() + eofOfs, bytes.size() - eofOfs);
        } else {
            doc.meta.unhandled.push_back("raw 分段失败：header 偏移越界");
        }
    }

    out.ok = true;
    return out;
}

}  // namespace vgmstudio
