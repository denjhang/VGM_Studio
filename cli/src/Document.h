// Document —— VGSJ 文档模型（schema 见 docs/DATA-MODEL.md）。
// 只有 segments / orphans / globals 里的 (reg, val) 是原值；
// view / lanes 都是可重算的派生视图（RULES.md D3/D5）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RegStream.h"

namespace vgmstudio {

struct RegValue {
    uint8_t reg = 0;
    uint8_t val = 0;
};

struct Segment {
    uint32_t atSample = 0;
    std::vector<RegValue> regs;
};

// 音符 = 一次 key-on 到 key-off 之间的全部寄存器写。里面只有原值，没有音高/音量推测。
struct Note {
    int id = 0;
    uint32_t startSample = 0;
    uint32_t endSample = 0;
    std::vector<Segment> segments;
};

struct Orphan {
    uint32_t atSample = 0;
    std::vector<RegValue> regs;
    std::string reason;
};

// lanes 是派生视图：points 里的值来自 segments/orphans 的原值，不另计一条写。
struct LanePoint {
    uint32_t atSample = 0;
    int value = 0;
};

struct Lane {
    std::string param;  // "pan" / "pms" / "ams"
    uint8_t reg = 0;
    std::vector<LanePoint> points;
};

struct Channel {
    uint8_t chipType = 0;
    int index = 0;
    std::string name;
    std::string kind = "FM";
    std::vector<Note> notes;
    std::vector<Orphan> orphans;
    std::vector<Lane> lanes;
};

struct GlobalSegment {
    uint32_t atSample = 0;
    std::vector<RegValue> regs;
};

struct MetaInfo {
    std::string sourceFile;
    uint64_t sourceSize = 0;
    std::string sourceSha256;
    std::string importedAt;
    std::string parserLib = "libvgm";
    std::string regStreamMethod = "shadowdev";
    std::string structurer = "ym2151/v1";
    std::string splitRule = "modizer-note-merge";
    std::vector<std::string> unhandled;
};

struct TimeBase {
    int sampleRate = 44100;
    uint32_t totalSamples = 0;
    int64_t loopSample = -1;  // 采样位置；-1 = 无循环
    uint32_t vgmRate = 60;
};

struct ChipCapabilities {
    int operators = 4;
    int algorithms = 8;
    int polyphony = 8;
};

struct ChipEntry {
    std::string id;
    std::string type;
    uint8_t vgmChipType = 0;
    uint32_t clock = 0;
    int instances = 1;
    int channelCount = 8;
    int portCount = 2;
    ChipCapabilities capabilities;
};

struct RawData {
    uint32_t dataOffset = 0;
    uint32_t eofOffset = 0;
    // 位精确往返的三段原始字节（DATA-MODEL.md §8）：
    //   header   = [0, dataOffset)
    //   commands = [dataOffset, eofOffset)
    //   tail     = [eofOffset, 文件末尾)   例如 GD3 标签
    std::string headerBase64;
    std::string commandsBase64;
    std::string tailBase64;
};

struct Document {
    std::string format = "vgm-studio/song";
    int version = 1;

    MetaInfo meta;
    TimeBase timeBase;
    std::vector<ChipEntry> chips;
    std::vector<GlobalSegment> globals;
    std::vector<Channel> channels;
    RawData raw;

    // 完整性校验：notes ∪ orphans ∪ globals 的写数。
    size_t CountStructuredWrites() const;
    bool SaveJson(const std::string& path, bool pretty, std::string* err) const;
    static bool LoadJson(const std::string& path, Document* out, std::string* err);
};

// base64 helpers（raw.commands 用）
std::string Base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> Base64Decode(const std::string& text);
std::string Sha256Hex(const uint8_t* data, size_t len);

}  // namespace vgmstudio
