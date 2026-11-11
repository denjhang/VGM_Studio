// PreviewVgm.cpp —— 试听用的最小 VGM 写出（仅 YM2413 0x51）。见头文件警告。
#include "PreviewVgm.h"

#include <algorithm>
#include <cstring>

namespace vgmstudio {
namespace {

struct Ev {
    uint32_t at;
    uint8_t reg;
    uint8_t val;
};

void WriteLE32(std::vector<uint8_t>* b, size_t ofs, uint32_t v) {
    if (ofs + 4 > b->size()) return;
    (*b)[ofs + 0] = static_cast<uint8_t>(v);
    (*b)[ofs + 1] = static_cast<uint8_t>(v >> 8);
    (*b)[ofs + 2] = static_cast<uint8_t>(v >> 16);
    (*b)[ofs + 3] = static_cast<uint8_t>(v >> 24);
}

}  // namespace

std::vector<uint8_t> EmitPreviewVgm(const Document& doc, std::string* err) {
    if (doc.chips.empty()) {
        *err = "document has no chip";
        return {};
    }
    if (doc.chips[0].vgmChipType != 0x01) {
        *err = "preview writer only supports YM2413";
        return {};
    }
    const uint32_t clock = doc.chips[0].clock ? doc.chips[0].clock : 3579545;

    // 收集全部写，按 (时间) 排序；同一时刻保持原顺序
    std::vector<Ev> evs;
    for (const Channel& c : doc.channels)
        for (const Note& n : c.notes)
            for (const Segment& s : n.segments)
                for (const RegValue& r : s.regs) evs.push_back(Ev{s.atSample, r.reg, r.val});
    for (const GlobalSegment& g : doc.globals)
        for (const RegValue& r : g.regs) evs.push_back(Ev{g.atSample, r.reg, r.val});
    std::stable_sort(evs.begin(), evs.end(),
                     [](const Ev& a, const Ev& b) { return a.at < b.at; });

    // 数据段
    std::vector<uint8_t> data;
    uint32_t cursor = 0;
    auto waitTo = [&](uint32_t target) {
        while (cursor < target) {
            uint32_t d = target - cursor;
            if (d <= 16) {
                data.push_back(static_cast<uint8_t>(0x70 + (d - 1)));
                cursor = target;
            } else {
                uint32_t step = std::min<uint32_t>(d, 65535);
                data.push_back(0x61);
                data.push_back(static_cast<uint8_t>(step & 0xFF));
                data.push_back(static_cast<uint8_t>((step >> 8) & 0xFF));
                cursor += step;
            }
        }
    };
    for (const Ev& e : evs) {
        waitTo(e.at);
        data.push_back(0x51);
        data.push_back(e.reg);
        data.push_back(e.val);
    }
    waitTo(doc.timeBase.totalSamples);
    data.push_back(0x66);

    // 头部 0x100（VGM 1.51）。必须够长：libvgm 按 _CHIPCLK_OFS 读到 0xFC，
    // 头部短了就会把命令流当成芯片时钟读出来。
    constexpr uint32_t kHdrSize = 0x100;
    std::vector<uint8_t> out(kHdrSize, 0);
    std::memcpy(&out[0x00], "Vgm ", 4);
    WriteLE32(&out, 0x04, static_cast<uint32_t>(kHdrSize + data.size()) - 4);  // EOF
    WriteLE32(&out, 0x08, 0x151);
    WriteLE32(&out, 0x0C, 0);  // SN76489 clock
    WriteLE32(&out, 0x10, clock);  // YM2413 clock
    WriteLE32(&out, 0x18, doc.timeBase.totalSamples);
    WriteLE32(&out, 0x1C, 0);  // loop offset
    WriteLE32(&out, 0x20, 0);  // loop samples
    WriteLE32(&out, 0x24, 60);  // rate
    WriteLE32(&out, 0x34, kHdrSize - 0x34);  // data offset 是相对 0x34 的偏移
    // 其余芯片时钟保持 0（表示未使用）

    out.insert(out.end(), data.begin(), data.end());
    return out;
}

}  // namespace vgmstudio
