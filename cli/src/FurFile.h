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

// ── 乐器名里的参数标签 ────────────────────────────────────────────────
// 用户在 Furnace 里直接把参数写进乐器名，用 '-' 分隔，例如
//     ch5ins1-BD3-note:B2-vol:15-noteoff:self
//     ch9ins1-SD2-HF-link:ch5ins3-vol:13-noteoff:self
//
// 四个参数都只作用于「用该音色的那些音符」：
//   note:X       音高改由名字指定。X 就是目标芯片上的**最终音高**（科学音高记法，
//                A4=440）：不做源 KC/KF 换算、不做 ML 八度补偿、不叠加 --transpose，
//                也不跟随源里的滑音/颤音——那些由 2413 侧的宏负责。
//   vol:N        基础音量改由名字指定（0..15，OPLL 0x30 低 4 位，0=最响）。
//                跳过载波 TL 换算；宏仍在这个基线上做比例。
//   noteoff:self 不使用源的 key-off，把音符延长到宏完整播完才 off。
//   noteoff:N    不使用源的 key-off，宏只走 N 步就立刻 off（N 是 tick 数，
//                1 步 = speed 个 tick）。用来给鼓这种「宏尾巴到了就必定收声」
//                的音色卡一个精确长度，不必把整条宏都放完。
//   link:X       联动音色：X 是另一个乐器的基名（如 ch5ins3）。本乐器每次击发时，
//                X 也在同一采样点、同样时长地一起击发。
struct InsTags {
    std::string base;      // 参数之前的基名，例如 "ch5ins3"
    std::string full;      // 完整乐器名（含参数）
    bool hasNote = false;
    int noteMidi = -1;     // MIDI 音号（C4=60）
    double noteHz = 0.0;   // 由 noteMidi 换算的频率
    bool hasVol = false;
    int vol = 0;           // 0..15
    bool noteOffSelf = false;
    // noteoff:N 的 N（tick 数）。>0 时优先于 noteOffSelf 的「宏跑完」时长。
    int noteOffTicks = 0;
    std::string linkTo;    // 非空 = 要联动的乐器基名
};

// 从乐器名里解析上述参数；没有标签时返回全默认值。
InsTags ParseInstrumentTags(const std::string& name);

// "B2" / "B-2" / "C#4" / "Bb3" → MIDI 音号（C4=60）。解析失败返回 -1。
// 注意 '-' 在这里是 Furnace 记法的占位符（C-4 即 C4），不是降号。
int ParseNoteName(const std::string& text);

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
