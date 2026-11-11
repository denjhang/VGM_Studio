// RegStream — 原始寄存器写的扁平序列，是导入链路里唯一的中间真值。
// 五元组来自 RULES.md D3：(chip, port, reg, val, atSample)。
#pragma once

#include <cstdint>
#include <vector>

namespace vgmstudio {

// order 是同一采样点内的写入序号，用于恢复原始顺序（VGM 里一点可以连写多条）。
struct RegWrite {
    uint32_t atSample = 0;
    uint32_t order = 0;
    uint8_t chipType = 0;   // VGM 芯片类型字节（0x03 = YM2151，0x01 = YM2413）
    uint8_t chipId = 0;     // 0 / 1（0xA0 以上的命令是第二实例）
    uint8_t port = 0;       // 端口号；YM2151 恒为 0
    uint8_t reg = 0;
    uint8_t val = 0;
};

using RegStream = std::vector<RegWrite>;

}  // namespace vgmstudio
