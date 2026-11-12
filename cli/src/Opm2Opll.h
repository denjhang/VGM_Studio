// Opm2Opll —— 源 JSON（YM2151/OPM）→ 目标 JSON（YM2413/OPLL）。
//
// 只做语义层转换（RULES.md E5）：
//   · 时间：startSample / endSample 原样搬运，不量化
//   · 通道：8 → 9 一一对应，第 9 通道空置
//   · 音色：OPM 4-op → OPLL 2-op，走 FmVoice 的 OPM→OPN→OPL→ROM 匹配链
//   · 音高：OPM 的 KC/KF 先还原成 Hz，再换算成 OPLL 的 block/fnum
//   · 音量：载波最小 TL（7bit）→ OPLL 0x30 低 4 位
//
// 输出 JSON 里仍然只有寄存器写（0x10/0x20/0x30 + 用户音色 0x00-0x07），
// 没有任何音高/音量/时序量化结果落盘。
#pragma once

#include <string>
#include <vector>

#include "Document.h"

namespace vgmstudio {

struct ConvertOptions {
    uint32_t targetClock = 3579545;  // YM2413 标准时钟
    std::string bank = "ym2413";     // ym2413 | vrc7 | ymf281b
    bool useUserVoice = false;       // true = 用 0 号用户槽承载 OPM 参数（单音色曲目才可行）
    int volumeOffsetBias = 0;        // 叠加在匹配得到的 volumeOffset 上

    // ── .fui 音色文件（与 vgm2json --fui 的命名一一对应）──
    bool exportFui = false;          // 为每个源音色写出 chXinsY_ym2413.fui
    std::string fuiDir;              // 输出目录（调用方保证存在）
    bool useEditedFui = false;       // 若同名 .fui 存在，用它的参数覆盖自动匹配结果

    // ── .fur 工程（一次性读出全部音色，优先级高于 --use-edited-fui）──
    // 在 Furnace 里把 19 个目标音色都改进一个工程存盘，按乐器名
    // "chXinsY" 精确匹配。工程里的 hz 会覆盖 tickRate（宏速率来自工程）。
    std::string furPath;  // 非空即启用

    // ── 宏执行 ──
    // .fui 里只有 speed/delay（每步几个 tick），「一秒几个 tick」由工程决定，
    // 乐器文件本身没有这个数（Furnace 的 song.hz / tickRate，默认 60）。
    // 这里必须显式给出，否则宏无法折算成采样位置。
    int tickRate = 60;
    bool runMacros = true;           // 目标音色带宏时，把宏展开成寄存器写

    // 目标整体移调（半音，正数升调）。叠加在 ML 八度补偿之上。
    // 默认 -12：实测输出比源整体高一个八度（OPLL 载波 ML 与 OPM 的补偿差），
    // 降一个八度后与源听感对齐。可用 --transpose=N 覆盖。
    int transposeSemitones = -12;
};

// 单音符的转换结果（用于有损报告）
struct NoteConvertReport {
    int sourceChannel = 0;
    int targetChannel = 0;
    int program = 0;
    int volumeOffset = 0;
    int octaveOffset = 0;
    int srcCarrierTl = 0;
    uint8_t kc = 0;
    uint8_t kf = 0;
    double freqHz = 0.0;
};

struct ConvertResult {
    Document doc;
    std::vector<NoteConvertReport> reports;
    size_t notesConverted = 0;
    std::vector<std::string> fuiFiles;   // 已写出的 .fui
    std::vector<std::string> fuiApplied; // 实际用上的用户改过的 .fui
    size_t furInstruments = 0;           // .fur 里读到的乐器总数
    std::vector<std::string> furApplied; // 从 .fur 取到并生效的乐器名
    int tickRateUsed = 60;               // 实际使用的宏 tick 速率（--fur 会用工程的 hz）
    bool ok = false;
    std::string error;
};

// 把源 OPM 音色导出成等价的 .fui，并把目标侧匹配到的 OPLL 音色也导出成 .fui。
// 命名规则与 vgm2json --fui 一致：chXinsY_ym2151.fui / chXinsY_ym2413.fui。
void ExportOpmVoiceFui(const uint8_t shadow[256], int ch, int ordinal, const std::string& dir,
                       std::string* err);

ConvertResult ConvertOpmToOpll(const Document& src, const ConvertOptions& opt);

// 数值换算（也被 report 打印用）
int CarrierTlOfOpmVoice(const uint8_t shadow[256], int ch, int con);

}  // namespace vgmstudio
