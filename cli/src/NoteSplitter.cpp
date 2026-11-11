// NoteSplitter.cpp —— 实现见头文件注释。
#include "NoteSplitter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "AnalyzerYm2151.h"

namespace vgmstudio {
namespace {

constexpr int kChannelCount = 8;

struct ChState {
    uint8_t shadow[256] = {0};
    bool on = false;
    int noteIdx = -1;        // channels.notes 里的下标，-1 = 无活动音符
    int pitchTicks = 0;      // 当前 KC 的整数半音
    bool havePitch = false;
    int minTl = 127;         // 当前载波最响 TL
};

// 写入必须原样保留：同一采样点、同一寄存器可以合法地写多次
// （YM2151 的 0x19 就是 AMD/PMD 各写一次，靠 bit7 区分）。
void AppendReg(std::vector<RegValue>* regs, uint8_t reg, uint8_t val) {
    regs->push_back(RegValue{reg, val});
}

// 把 (reg,val) 追加到"最后一条 atSample 相同"的容器里，否则新开一条。
template <typename List, typename MakeItem>
void AppendAt(List* list, uint32_t atSample, uint8_t reg, uint8_t val, MakeItem makeItem) {
    if (!list->empty() && list->back().atSample == atSample) {
        AppendReg(&list->back().regs, reg, val);
        return;
    }
    list->push_back(makeItem(atSample));
    AppendReg(&list->back().regs, reg, val);
}

void CloseNote(Note* n, uint32_t endSample) {
    if (n != nullptr && n->endSample < endSample) n->endSample = endSample;
}

}  // namespace

SplitResult SplitYm2151(const RegStream& writes, const SplitOptions& opt) {
    SplitResult res;
    res.totalWrites = writes.size();

    std::vector<Channel> channels(kChannelCount);
    for (int ch = 0; ch < kChannelCount; ch++) {
        channels[ch].chipType = 0x03;
        channels[ch].index = ch;
        char name[16];
        std::snprintf(name, sizeof(name), "FM%d", ch + 1);
        channels[ch].name = name;
        channels[ch].kind = "FM";
    }

    ChState st[kChannelCount];
    int nextNoteId = 0;

    for (const RegWrite& w : writes) {
        if (w.chipType != 0x03 || w.chipId != 0) {
            // 非 YM2151 的写：记进 0 号通道的孤儿，reason 说明清楚
            AppendAt(&channels[0].orphans, w.atSample, w.reg, w.val,
                     [](uint32_t at) { return Orphan{at, {}, "非 YM2151 寄存器写"}; });
            res.structuredWrites++;
            continue;
        }

        if (IsGlobalReg(w.reg)) {
            AppendAt(&res.globals, w.atSample, w.reg, w.val,
                     [](uint32_t at) { return GlobalSegment{at, {}}; });
            res.structuredWrites++;
            st[0].shadow[w.reg] = w.val;  // 全局寄存器也进 0 号影子（仅供调试）
            continue;
        }

        if (!IsChannelReg(w.reg)) {
            // 未分类的通道寄存器：不丢，进 0 号通道孤儿
            AppendAt(&channels[0].orphans, w.atSample, w.reg, w.val,
                     [](uint32_t at) { return Orphan{at, {}, "未分类寄存器"}; });
            res.structuredWrites++;
            continue;
        }

        // 0x08 是唯一的例外：寄存器地址恒定，通道号在数据位里（CHIP-YM2151.md §1）
        const int ch = (w.reg == 0x08) ? (w.val & 0x07) : (w.reg & 0x07);
        ChState& cs = st[ch];
        Channel& chn = channels[ch];

        // ── key-on / key-off（0x08）：块的边界，必须按边沿判定 ──
        if (w.reg == 0x08) {
            const bool newOn = (w.val & 0x78) != 0;
            if (newOn && !cs.on) {
                // 上升沿：开新块。这条 key-on 同时是块的第一条写。
                Note n;
                n.id = nextNoteId++;
                n.startSample = w.atSample;
                n.endSample = w.atSample;
                chn.notes.push_back(n);
                cs.noteIdx = static_cast<int>(chn.notes.size()) - 1;
                cs.on = true;

                const int alg = cs.shadow[0x20 + ch] & 0x07;
                cs.minTl = CarrierMinTL(cs.shadow, ch, alg);

                const uint8_t kc = cs.shadow[0x28 + ch];
                cs.pitchTicks = Ym2151Semitone(kc);
                cs.havePitch = true;

                AppendAt(&chn.notes[cs.noteIdx].segments, w.atSample, w.reg, w.val,
                         [](uint32_t at) { return Segment{at, {}}; });
            } else if (!newOn && cs.on) {
                // 下降沿：结束块。key-off 写进块内。
                if (cs.noteIdx >= 0) {
                    AppendAt(&chn.notes[cs.noteIdx].segments, w.atSample, w.reg, w.val,
                             [](uint32_t at) { return Segment{at, {}}; });
                    CloseNote(&chn.notes[cs.noteIdx], w.atSample);
                }
                cs.on = false;
                cs.noteIdx = -1;
            } else {
                // 电平不变的重复写：不新开块，但也不能丢
                if (cs.noteIdx >= 0) {
                    AppendAt(&chn.notes[cs.noteIdx].segments, w.atSample, w.reg, w.val,
                             [](uint32_t at) { return Segment{at, {}}; });
                } else {
                    AppendAt(&chn.orphans, w.atSample, w.reg, w.val,
                             [](uint32_t at) { return Orphan{at, {}, "音符外的 key-on 写"}; });
                }
            }
            cs.shadow[w.reg] = w.val;
            res.structuredWrites++;
            continue;
        }

        // ── 其余通道寄存器：落进活动音符的 segments，否则 orphan ──
        const bool wasOn = cs.on;
        uint32_t splitAt = w.atSample;
        bool needSplit = false;

        if (wasOn && cs.noteIdx >= 0) {
            if (w.reg >= 0x28 && w.reg <= 0x2F) {  // KC：音高跳变切块
                int newTicks = Ym2151Semitone(w.val);
                if (cs.havePitch &&
                    std::abs(newTicks - cs.pitchTicks) > opt.pitchJumpSemitones) {
                    needSplit = true;
                }
            } else if (opt.splitOnLoudnessUp && w.reg >= 0x60 && w.reg <= 0x7F) {
                // TL 变响 = 重触发（modizer 判据）
                uint8_t tmp[256];
                std::memcpy(tmp, cs.shadow, sizeof(tmp));
                tmp[w.reg] = w.val;
                const int alg = cs.shadow[0x20 + ch] & 0x07;
                const int newMin = CarrierMinTL(tmp, ch, alg);
                if (newMin < cs.minTl) needSplit = true;
            }
        }

        if (needSplit && cs.noteIdx >= 0) {
            // 关掉旧块（不写 key-off，legato），原地开新块
            CloseNote(&chn.notes[cs.noteIdx], splitAt);
            Note n;
            n.id = nextNoteId++;
            n.startSample = splitAt;
            n.endSample = splitAt;
            chn.notes.push_back(n);
            cs.noteIdx = static_cast<int>(chn.notes.size()) - 1;
            cs.minTl = CarrierMinTL(cs.shadow, ch, cs.shadow[0x20 + ch] & 0x07);
        }

        if (cs.noteIdx >= 0) {
            AppendAt(&chn.notes[cs.noteIdx].segments, w.atSample, w.reg, w.val,
                     [](uint32_t at) { return Segment{at, {}}; });
        } else {
            AppendAt(&chn.orphans, w.atSample, w.reg, w.val,
                     [](uint32_t at) { return Orphan{at, {}, "音符开始前的参数写"}; });
        }
        cs.shadow[w.reg] = w.val;

        // 更新切块所需的跟踪量
        if (w.reg >= 0x28 && w.reg <= 0x2F) {
            cs.pitchTicks = Ym2151Semitone(w.val);
            cs.havePitch = true;
        } else if (w.reg >= 0x60 && w.reg <= 0x7F) {
            cs.minTl = CarrierMinTL(cs.shadow, ch, cs.shadow[0x20 + ch] & 0x07);
        }

        // ── lanes：pan / pms / ams（视图，与 segments 同源不另计写）──
        if (w.reg >= 0x20 && w.reg <= 0x27) {
            Lane* lane = nullptr;
            for (Lane& l : chn.lanes)
                if (l.param == "pan") lane = &l;
            if (lane == nullptr) {
                chn.lanes.push_back(Lane{"pan", static_cast<uint8_t>(0x20 + ch), {}});
                lane = &chn.lanes.back();
            }
            lane->points.push_back(LanePoint{w.atSample, (w.val >> 6) & 0x03});
        } else if (w.reg >= 0x38 && w.reg <= 0x3F) {
            Lane* pms = nullptr;
            Lane* ams = nullptr;
            for (Lane& l : chn.lanes) {
                if (l.param == "pms") pms = &l;
                if (l.param == "ams") ams = &l;
            }
            if (pms == nullptr) {
                chn.lanes.push_back(Lane{"pms", static_cast<uint8_t>(0x38 + ch), {}});
                pms = &chn.lanes.back();
            }
            if (ams == nullptr) {
                chn.lanes.push_back(Lane{"ams", static_cast<uint8_t>(0x38 + ch), {}});
                ams = &chn.lanes.back();
            }
            pms->points.push_back(LanePoint{w.atSample, (w.val >> 4) & 0x07});
            ams->points.push_back(LanePoint{w.atSample, w.val & 0x03});
        }

        res.structuredWrites++;
    }

    res.channels = std::move(channels);
    res.ok = (res.structuredWrites == res.totalWrites);
    if (!res.ok) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "structured %zu != total %zu", res.structuredWrites,
                      res.totalWrites);
        res.error = buf;
    }
    return res;
}

}  // namespace vgmstudio
