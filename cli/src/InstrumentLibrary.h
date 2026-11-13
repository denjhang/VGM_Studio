// InstrumentLibrary —— 跨曲目音色库（YM2151 源音色 → YM2413 目标音色）。
//
// ★ 身份判据：**YM2151 的 FM 参数本身**，不是指纹。
//   指纹（VoiceFingerprint）混进了 type/preset，只适合同一曲目内去重；
//   跨曲目比对必须回到源侧那 4 个算子本身。TL（0x60）是每音符音量、
//   不属于音色身份，比对时一律排除。
//
// 数据来源是 `fuitool lib --json=` 产出的 JSON 的 `pairs` 段，其中每条都同时
// 带着「源侧 OPM 参数」与「目标侧 OPLL 音色 + 名字参数（note/vol/noteoff/link）」。
// 批量转换时按源侧参数查表：完全一致直接用，不完全一致取参数最近的。
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "FuiVoice.h"
#include "FurFile.h"

namespace vgmstudio {

struct FurSong;

// 源音色身份键：alg/fb/fms/ams + 4 个算子的 mult/dt/dt2/ks/ar/am/dr/sr/sl/rr，
// **不含 TL**。同一套音色在任何曲目里算出来都相同。
std::string OpmTimbreKey(const FuiVoice& opmVoice);

// 两个源音色的参数距离（0 = 完全相同）。
// alg 不同直接返回 1000：4 算子连接方式不同就是另一种乐器。
int OpmTimbreDistance(const FuiVoice& a, const FuiVoice& b);

// "ch5ins3" → 4（0 基通道号）。解析失败返回 -1。
int SlotChannelOfName(const std::string& name);

struct LibraryEntry {
    std::string sourceName;   // 源侧标签名，如 ch5ins1-bd
    FuiVoice source;          // 源侧 OPM 音色（只用于比对）
    std::string sourceKey;    // OpmTimbreKey(source)

    std::string targetName;   // 目标乐器名（含 note:/vol:/noteoff:/link: 参数）
    InsTags tags;             // targetName 解析出来的参数
    FuiVoice target;          // 目标音色（preset + 宏；永不落到 0 号槽）
    bool isLink = false;      // link:X 联动音色（没有自己的源音色）
    // isLink 时：被联动的那件**源**乐器的身份键。跨曲目复用时按这个键认人，
    // 而不是按 "ch5ins3" 这种只在本曲有效的通道名。
    std::string linkSourceKey;
};

struct InstrumentLibrary {
    std::vector<LibraryEntry> entries;
    std::string source;

    // 精确匹配 → 参数最近的（距离 ≤ maxDistance）。找不到返回 nullptr。
    // preferChannel：同一套源参数在本工程里可能对应多个不同的目标音色
    // （例如 ch1/ch7/ch8 三个 pad 的 OPM 参数完全一样，但分别配了 Clarinet /
    //  Square Pad / Sine Pad）。跨曲目复用时优先取**同通道号**的那条：
    // 通道号在整套曲目里代表同一种角色，比「fur 里的先后顺序」稳定得多。
    const LibraryEntry* Find(const FuiVoice& opmSource, int maxDistance = 24,
                             int preferChannel = -1) const;

    // 最近的条目（不设阈值），用于报告「差多少」。*dist 返回距离。
    const LibraryEntry* Nearest(const FuiVoice& opmSource, int* dist) const;
    bool Empty() const { return entries.empty(); }
};

// 从 .fur 自动配对：OPLL 乐器按 (通道, 序号) 对上同位置的 OPM 乐器。
// 例：ch5ins3-sd ↔ ch5ins3-SD2-note:B3-noteoff:9-vol:14。
// link:X 联动音色没有自己的源音色，isLink=true 单独入库。
std::vector<LibraryEntry> BuildLibraryPairs(const FurSong& song);

bool LoadInstrumentLibrary(const std::string& path, InstrumentLibrary* out, std::string* err);

// 把配对结果序列化成库 JSON 的 pairs 段（fuitool lib --json= 用）。
nlohmann::json LibraryPairsToJson(const std::vector<LibraryEntry>& pairs);

}  // namespace vgmstudio
