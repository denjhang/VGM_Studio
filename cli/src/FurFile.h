// FurFile —— 从 Furnace 工程文件（.fur）里取出全部乐器。
//
// 为什么需要它：
//   以前每个音色要单独导出成 chXinsY_ym2413.fui 再逐个改、逐个覆盖，
//   文件号一多就容易搞错。现在用户在 Furnace 里把 19 个音色全部改进
//   一个 2151-2413.fur 里存盘，opm2opll 直接读这个工程。
//
// 解析方式（不自己发明格式）：
//   · 解压与头部解析逐行对应 furnace/src/engine/fileOps/fileOpsCommon.cpp:49-150
//     与 furnace/src/engine/fileOps/fur.cpp:624-1345（旧 INFO 头）/
//     800-1030（新 INF2 头）。
//   · 每个乐器的字节交回给已经 vendored 的 DivInstrument::readInsData
//     （cli/third_party/furnace/src/engine/instrument.cpp:4316），
//     与读 .fui 走的是同一条路径，所以版本兼容性由 Furnace 自己负责。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FuiVoice.h"

namespace vgmstudio {

struct FurInstrument {
    std::string name;   // Furnace 里的乐器名，例如 "ch1ins1"
    int type = 0;       // DivInstrumentType：13=OPLL, 14=OPL, 33=OPM
    FuiVoice voice;     // 已转换好的统一表示
};

struct FurSong {
    int version = 0;      // .fur 里记录的 Furnace 引擎版本
    float hz = 60.0f;     // 工程的 tick 速率（宏的「一秒几个 tick」）
    std::string name;
    std::string author;
    std::vector<FurInstrument> instruments;
};

// 读 .fur（自动处理 zlib 压缩与非压缩两种存法）。
bool LoadFur(const std::string& path, FurSong* out, std::string* err);

// 在工程里按 Furnace 乐器名匹配；找不到返回 nullptr。
// preferType >= 0 时只在该类型里找（13=OPLL）。工程里往往同时存着
// 源侧 OPM 音色和目标侧 OPLL 音色，只按名字找会拿错。
// 注意：**不会**退回「任意类型」搜索——那样在用户重命名后会静默拿到
// 一个 OPM 音色当 OPLL 用。名字找不到请改用 FindFurInstrumentByIndex。
const FurInstrument* FindFurInstrument(const FurSong& song, const std::string& name,
                                      int preferType = -1);

// 按「该类型里的第几个」取乐器。用于用户把乐器改名后仍然能对上：
// opm2opll 导出的 OPLL 音色就是按 ch1ins1, ch1ins2, ch2ins1… 的顺序追加的，
// 所以序号与导出顺序稳定对应。
const FurInstrument* FindFurInstrumentByIndex(const FurSong& song, int type, size_t index);

// 某类型乐器的总数。
size_t CountFurInstruments(const FurSong& song, int type);

}  // namespace vgmstudio
