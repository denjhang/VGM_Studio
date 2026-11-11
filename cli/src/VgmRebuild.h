// VgmRebuild —— JSON → 内存 VGM 字节流。
//
// 只做"原样拼回"：header + raw.commands + tail 三段直接拼接，保证字节级往返
// （DATA-MODEL.md §8）。这是 M1 的门禁，不是输出引擎。
//
// 从结构化数据重排命令（编辑后输出）不做在这里 —— 按 RULES.md F1/E3，
// 输出必须走 furnace 的 dispatch（芯片驱动自己决定怎么写、什么时候写），
// 自己拼 0x54/0x61 字节会漏掉 addr→data 配对、同址多次写、复位静音序列等时序约束。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Document.h"

namespace vgmstudio {

struct RebuildStats {
    size_t headerBytes = 0;
    size_t commandBytes = 0;
    size_t tailBytes = 0;
};

// 未编辑路径：header + raw.commands + tail 原样拼回去。
std::vector<uint8_t> RebuildFromRaw(const Document& doc, RebuildStats* stats, std::string* err);

}  // namespace vgmstudio
