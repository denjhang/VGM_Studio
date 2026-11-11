// PreviewVgm —— 把目标 JSON 写成一段可直接试听的 VGM。
//
// ⚠️ 这是**试听用的临时通路**，不是正式输出引擎。
// RULES.md F1 / E3 规定正式输出必须走 furnace dispatch（芯片驱动自己决定写序列）。
// 这里只做「0x51 寄存器写 + 等待」这种最小编码，用于在接入 furnace 之前先听效果。
// 一旦 FurnaceWriter 就位，本文件应当删除。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Document.h"

namespace vgmstudio {

// 只支持 YM2413（0x51）。输出的头部为 VGM 1.51、dataOffset 0x80。
std::vector<uint8_t> EmitPreviewVgm(const Document& doc, std::string* err);

}  // namespace vgmstudio
