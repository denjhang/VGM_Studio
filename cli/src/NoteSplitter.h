// NoteSplitter —— 把寄存器流切成音符块。
//
// 规则来源：docs/ARCHITECTURE.md §3（modizer 合并判据 + Denjhang 的音高跳变切分），
// 语义表：docs/CHIP-YM2151.md。核心不变量：每一条写都必须落到
// notes[].segments / orphans / globals 之一，不能丢（RULES.md D4）。
#pragma once

#include <cstdint>
#include <vector>

#include "Document.h"
#include "RegStream.h"

namespace vgmstudio {

// 分块只决定"哪些写属于同一个音符块"，不产生任何音高/音量数值。
struct SplitOptions {
    float pitchJumpSemitones = 0.5f;  // KC 跳变超过半音就切新块（docs/CHIP-YM2151.md §2.5）
    bool splitOnLoudnessUp = true;    // 载波 TL 变响视作重触发（modizer 判据）
};

// 单颗芯片的寄存器流 → 通道/音符/孤儿/全局。
// 只处理 YM2151（vgmChipType 0x03）；其他芯片原样进 orphans 并返回 false。
struct SplitResult {
    std::vector<Channel> channels;
    std::vector<GlobalSegment> globals;
    size_t totalWrites = 0;
    size_t structuredWrites = 0;
    bool ok = false;
    std::string error;
};

SplitResult SplitYm2151(const RegStream& writes, const SplitOptions& opt);

}  // namespace vgmstudio
