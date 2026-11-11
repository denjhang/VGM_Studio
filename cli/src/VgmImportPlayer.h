// VgmImportPlayer —— 用 libvgm 完整解析 VGM，并把寄存器写接出来。
//
// 解析完全由 libvgm 的 VGMPlayer::Render() → ParseFile() → _CMD_INFO[0x100]
// → Cmd_*() 完成，我们一行 VGM 命令解析都不写（RULES.md E1/F2）。
// 唯一的注入点是"影子设备"：给每个 DEV_ID 注册一个假设备，
// Start/Update 返回静音，write8 把 (port, reg, val) 接住。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RegStream.h"

namespace vgmstudio {

struct ChipInfo {
    uint8_t vgmChipType = 0;  // VGM 命令里的芯片类型字节（0x03 = YM2151）
    uint32_t clock = 0;
    uint16_t channelCount = 0;
    std::string name;
};

struct CommandStat {
    uint8_t cmd = 0;
    std::string label;
    uint64_t count = 0;
};

struct VgmHeaderInfo {
    uint32_t fileVer = 0;
    uint32_t dataOffset = 0;
    uint32_t eofOffset = 0;
    uint32_t totalSamples = 0;
    uint32_t loopOffset = 0;
    uint32_t loopSamples = 0;
    uint32_t recordHz = 0;
};

struct ImportResult {
    VgmHeaderInfo header;
    std::vector<ChipInfo> chips;
    RegStream writes;
    std::vector<CommandStat> commands;
    std::vector<std::string> unhandled;
    bool ok = false;
    std::string error;
};

// 只取头部 + 命令统计，不跑回放（vgminfo 用）。
ImportResult ProbeVgm(const std::string& path);

// 完整导入：头部 + 命令统计 + 寄存器流（vgm2json 用）。
ImportResult ImportVgm(const std::string& path);

}  // namespace vgmstudio
