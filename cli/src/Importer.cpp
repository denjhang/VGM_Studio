// Importer.cpp —— VGM → VGSJ Document。
#include "Importer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>

#include "FuiVoice.h"

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
    // ★ VGM 头 0x20 存的是「循环段长度」，不是循环起点位置。
    //   循环一直跑到曲末，所以起点 = 总采样数 − 循环长度。
    //   （此前直接把长度当位置存进来，会得到一个接近曲末的假循环点。）
    if (imported.header.loopOffset != 0 && imported.header.loopSamples > 0 &&
        imported.header.loopSamples <= imported.header.totalSamples) {
        doc.timeBase.loopSample = static_cast<int64_t>(imported.header.totalSamples -
                                                       imported.header.loopSamples);
    } else {
        doc.timeBase.loopSample = -1;
    }
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

    // ── GD3 标签（曲名/作者/日期等）原样取出，供目标 JSON/VGM 透传 ──
    // 头部 0x14 存的是「相对 0x14 的偏移」；0 表示没有标签。
    {
        const uint32_t gd3Rel = (bytes.size() >= 0x18)
                                    ? (static_cast<uint32_t>(bytes[0x14]) |
                                       (static_cast<uint32_t>(bytes[0x15]) << 8) |
                                       (static_cast<uint32_t>(bytes[0x16]) << 16) |
                                       (static_cast<uint32_t>(bytes[0x17]) << 24))
                                    : 0;
        const uint32_t gd3Ofs = gd3Rel ? (0x14 + gd3Rel) : 0;
        if (gd3Ofs != 0 && gd3Ofs + 12 <= bytes.size() &&
            std::memcmp(bytes.data() + gd3Ofs, "Gd3 ", 4) == 0) {
            const uint32_t len =
                static_cast<uint32_t>(bytes[gd3Ofs + 8]) |
                (static_cast<uint32_t>(bytes[gd3Ofs + 9]) << 8) |
                (static_cast<uint32_t>(bytes[gd3Ofs + 10]) << 16) |
                (static_cast<uint32_t>(bytes[gd3Ofs + 11]) << 24);
            const size_t end = static_cast<size_t>(gd3Ofs) + 12 + len;
            if (end <= bytes.size()) {
                doc.tags.gd3Base64 =
                    Base64Encode(bytes.data() + gd3Ofs, end - gd3Ofs);
            } else {
                doc.meta.unhandled.push_back("GD3 标签长度越界，已跳过");
            }
        } else if (gd3Rel != 0) {
            doc.meta.unhandled.push_back("GD3 偏移指向非法位置，已跳过");
        }
    }


    // ── 为每个出现过的 OPM 音色导出 .fui ──
    // 同一通道内，音色参数（0x20..0xE0 的 32 个写）在曲子进行中可能改变；
    // 每次「音色块」变化就当作一个新音色，文件名 chXinsY_ym2151.fui。
    if (opt.exportFui) {
        struct VoiceKey {
            uint64_t hash;
            int ch;
            int ordinal;
        };
        for (size_t ci = 0; ci < doc.channels.size(); ci++) {
            const Channel& chn = doc.channels[ci];
            if (chn.index < 0 || chn.index > 7) continue;

            // 把所有写按时间排好，重放影子寄存器
            struct W { uint32_t at; uint8_t reg; uint8_t val; };
            std::vector<W> stream;
            for (const Orphan& o : chn.orphans)
                for (const RegValue& r : o.regs) stream.push_back(W{o.atSample, r.reg, r.val});
            for (const Note& n : chn.notes)
                for (const Segment& s : n.segments)
                    for (const RegValue& r : s.regs) stream.push_back(W{s.atSample, r.reg, r.val});
            std::stable_sort(stream.begin(), stream.end(),
                             [](const W& a, const W& b) { return a.at < b.at; });

            uint8_t shadow[256] = {0};
            // 按哈希去重：同一通道里不同音符会在几个音色之间来回切换
            // （实测 ch5 只有 4 个音色，但相邻音符交替，逐次比较会产出几百个文件）。
            std::map<uint64_t, int> seen;   // hash → 音色序号（按首次出现编号）
            size_t cur = 0;
            for (const Note& n : chn.notes) {
                while (cur < stream.size() && stream[cur].at <= n.startSample) {
                    shadow[stream[cur].reg] = stream[cur].val;
                    cur++;
                }
                // 音色哈希：只取「定义音色」的寄存器。
                // ★ 排除 0x60 (TL)：TL 是每个音符的音量，不是音色；
                //   算进去会让每个音符都变成一个"新音色"（实测 393 个）。
                uint64_t h = 1469598103934665603ull;
                auto mix = [&h](uint8_t v) { h = (h ^ v) * 1099511628211ull; };
                for (int op = 0; op < 4; op++) {
                    const int base = op * 8 + chn.index;
                    mix(shadow[0x40 + base]);   // DT1/MUL
                    mix(shadow[0x80 + base]);   // KS/AR
                    mix(shadow[0xA0 + base]);   // AM/D1R
                    mix(shadow[0xC0 + base]);   // DT2/D2R
                    mix(shadow[0xE0 + base]);   // D1L/RR
                }
                mix(shadow[0x20 + chn.index] & 0x3F);  // FB + CON
                mix(shadow[0x38 + chn.index]);          // PMS/AMS
                const auto it = seen.find(h);
                if (it != seen.end()) continue;
                const int ordinal = static_cast<int>(seen.size()) + 1;
                seen[h] = ordinal;

                FuiVoice v = OpmShadowToFui(shadow, chn.index);
                char nm[32];
                std::snprintf(nm, sizeof(nm), "ch%dins%d", chn.index + 1, ordinal);
                v.name = nm;

                const std::string dir = opt.fuiDir;  // 由调用方保证已存在
                char fn[64];
                std::snprintf(fn, sizeof(fn), "ch%dins%d_ym2151.fui", chn.index + 1, ordinal);
                std::string path = dir.empty() ? fn : (dir + "/" + fn);
                std::string ferr;
                if (SaveFui(v, path, &ferr)) {
                    out.fuiFiles.push_back(path);
                } else {
                    out.doc.meta.unhandled.push_back("fui 写出失败: " + ferr);
                }
            }
        }
    }

    out.ok = true;
    return out;
}

}  // namespace vgmstudio
