#include "InstrumentLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include "FurFile.h"

namespace vgmstudio {
namespace {

// 配对键：名字里的 chNinsM 前缀。
//   源侧 "ch5ins3-sd"        → ch5ins3
//   目标侧 "ch5ins3-SD2-note:B3-..." → ch5ins3
// 不能用完整基名：两端的后缀本来就不同（"pad" vs "light pad sq"），
// 用 "ch5ins3" 这一级才是一一对应的——同一首曲子里 chXinsY 不会重复。
std::string InstrumentSlotKeyImpl(const std::string& name) {
    size_t i = 0;
    if (name.size() < 3 || name[0] != 'c' || name[1] != 'h') return std::string();
    i = 2;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) i++;
    if (i < name.size() + 1 && name.compare(i, 3, "ins") == 0) {
        i += 3;
        const size_t start = i;
        while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) i++;
        if (i > start) return name.substr(0, i);
    }
    return std::string();
}

int SlotChannelOfNameImpl(const std::string& name) {
    const size_t i = name.find("ins");
    if (i < 3 || name[0] != 'c' || name[1] != 'h') return -1;
    int n = 0;
    for (size_t k = 2; k < i; k++) {
        if (!std::isdigit(static_cast<unsigned char>(name[k]))) return -1;
        n = n * 10 + (name[k] - '0');
    }
    return n > 0 ? n - 1 : -1;
}

void OpToJson(const FuiOperator& o, nlohmann::json* j) {
    *j = {{"mult", o.mult}, {"tl", o.tl},  {"ar", o.ar},   {"dr", o.dr},  {"sl", o.sl},
          {"rr", o.rr},     {"dt", o.dt},   {"dt2", o.dt2}, {"ksr", o.ksr}, {"am", o.am},
          {"vib", o.vib},   {"egt", o.egt}, {"ksl", o.ksl}, {"ws", o.ws},  {"sus", o.sus},
          {"rs", o.rs},     {"kvs", o.kvs}};
}

void OpFromJson(const nlohmann::json& j, FuiOperator* o) {
    auto get = [&j](const char* k, int def) { return j.contains(k) ? j[k].get<int>() : def; };
    o->mult = get("mult", 1);
    o->tl = get("tl", 0);
    o->ar = get("ar", 0);
    o->dr = get("dr", 0);
    o->sl = get("sl", 0);
    o->rr = get("rr", 0);
    o->dt = get("dt", 0);
    o->dt2 = get("dt2", 0);
    o->ksr = get("ksr", 0);
    o->am = get("am", 0);
    o->vib = get("vib", 0);
    o->egt = get("egt", 0);
    o->ksl = get("ksl", 0);
    o->ws = get("ws", 0);
    o->sus = get("sus", 0);
    o->rs = get("rs", 0);
    o->kvs = get("kvs", 2);
}

nlohmann::json MacrosToJson(const FuiVoice& v) {
    nlohmann::json arr = nlohmann::json::array();
    for (int m = 0; m < kMacroCount; m++) {
        const Macro& mc = v.macros[m];
        if (mc.val.empty()) continue;
        // ★ 全部字段都要存：只存 values/speed 会丢 type（序列/ADSR/LFO）、
        //   mode（pitch 的绝对值/累加）、linger（vol 宏结束时保持最后值）。
        //   丢了 linger 会让 vol 宏每帧都回零，串出来是一堆多余的 0x30 写。
        arr.push_back({{"macro", m},
                       {"values", mc.val},
                       {"type", mc.type},
                       {"mode", mc.mode},
                       {"speed", mc.speed},
                       {"delay", mc.delay},
                       {"loop", mc.loop},
                       {"rel", mc.rel},
                       {"linger", mc.linger},
                       {"activeRelease", mc.activeRelease}});
    }
    // 算子级宏（opMacros）整体保留
    nlohmann::json opArr = nlohmann::json::array();
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < kOpMacroCount; j++) {
            const Macro& mc = v.opMacros[i][j];
            if (mc.val.empty()) continue;
            opArr.push_back({{"op", i},
                             {"macro", j},
                             {"values", mc.val},
                             {"type", mc.type},
                             {"mode", mc.mode},
                             {"speed", mc.speed},
                             {"delay", mc.delay},
                             {"loop", mc.loop},
                             {"rel", mc.rel},
                             {"linger", mc.linger}});
        }
    arr.push_back({{"opMacros", opArr}});
    return arr;
}

void MacrosFromJson(const nlohmann::json& arr, FuiVoice* v) {
    if (!arr.is_array()) return;
    for (const nlohmann::json& m : arr) {
        if (m.contains("opMacros")) {
            if (!m["opMacros"].is_array()) continue;
            for (const nlohmann::json& om : m["opMacros"]) {
                const int op = om.value("op", -1), idx = om.value("macro", -1);
                if (op < 0 || op >= 4 || idx < 0 || idx >= kOpMacroCount) continue;
                Macro& mc = v->opMacros[op][idx];
                mc.val = om.value("values", std::vector<int>{});
                mc.type = om.value("type", 0);
                mc.mode = om.value("mode", 0);
                mc.speed = om.value("speed", 1);
                mc.delay = om.value("delay", 0);
                mc.loop = om.value("loop", 255);
                mc.rel = om.value("rel", 255);
                mc.linger = om.value("linger", false);
            }
            continue;
        }
        const int idx = m.value("macro", -1);
        if (idx < 0 || idx >= kMacroCount) continue;
        Macro& mc = v->macros[idx];
        mc.val = m.value("values", std::vector<int>{});
        mc.type = m.value("type", 0);
        mc.mode = m.value("mode", 0);
        mc.speed = m.value("speed", 1);
        mc.delay = m.value("delay", 0);
        mc.loop = m.value("loop", 255);
        mc.rel = m.value("rel", 255);
        mc.linger = m.value("linger", false);
        mc.activeRelease = m.value("activeRelease", false);
    }
}

nlohmann::json VoiceToJson(const FuiVoice& v) {
    nlohmann::json ops = nlohmann::json::array();
    const int shown = v.ops >= 2 ? v.ops : 2;
    for (int i = 0; i < shown; i++) {
        nlohmann::json o;
        OpToJson(v.op[i], &o);
        o["index"] = i;
        ops.push_back(o);
    }
    return {{"type", v.type},
            {"opllPreset", v.opllPreset},
            {"alg", v.alg},
            {"fb", v.fb},
            {"fms", v.fms},
            {"ams", v.ams},
            {"ops", v.ops},
            {"operators", ops},
            {"macros", MacrosToJson(v)}};
}

FuiVoice VoiceFromJson(const nlohmann::json& j) {
    FuiVoice v;
    v.type = j.value("type", kFuiInsOpll);
    v.opllPreset = j.value("opllPreset", 0);
    v.alg = j.value("alg", 0);
    v.fb = j.value("fb", 0);
    v.fms = j.value("fms", 0);
    v.ams = j.value("ams", 0);
    v.ops = j.value("ops", 2);
    if (j.contains("operators") && j["operators"].is_array()) {
        for (const nlohmann::json& o : j["operators"]) {
            const int idx = o.value("index", -1);
            if (idx >= 0 && idx < 4) OpFromJson(o, &v.op[idx]);
        }
    }
    if (j.contains("macros")) MacrosFromJson(j["macros"], &v);
    return v;
}

}  // namespace

std::string InstrumentSlotKey(const std::string& name) { return InstrumentSlotKeyImpl(name); }

int SlotChannelOfName(const std::string& name) { return SlotChannelOfNameImpl(name); }

std::string OpmTimbreKey(const FuiVoice& v) {
    char buf[512];
    int n = std::snprintf(buf, sizeof(buf), "opm;alg%d;fb%d;fms%d;ams%d", v.alg & 7, v.fb & 7,
                          v.fms & 7, v.ams & 3);
    for (int i = 0; i < 4 && n > 0 && static_cast<size_t>(n) < sizeof(buf); i++) {
        const FuiOperator& o = v.op[i];
        n += std::snprintf(buf + n, sizeof(buf) - n, "|%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                           o.mult, o.dt, o.dt2, o.ar, o.dr, o.sl, o.rr, o.rs, o.am, o.ksr,
                           o.vib, o.egt);
    }
    return std::string(buf);
}

int OpmTimbreDistance(const FuiVoice& a, const FuiVoice& b) {
    if ((a.alg & 7) != (b.alg & 7)) return 1000;   // 连接方式不同 = 另一种乐器
    int d = std::abs((a.fb & 7) - (b.fb & 7)) + std::abs((a.fms & 7) - (b.fms & 7)) +
            std::abs((a.ams & 3) - (b.ams & 3));
    for (int i = 0; i < 4; i++) {
        const FuiOperator& x = a.op[i];
        const FuiOperator& y = b.op[i];
        d += std::abs(x.mult - y.mult) * 2;   // 倍频错一档听感差别很大
        d += std::abs(x.dt - y.dt) + std::abs(x.dt2 - y.dt2);
        d += std::abs(x.ar - y.ar) + std::abs(x.dr - y.dr);
        d += std::abs(x.sl - y.sl) + std::abs(x.rr - y.rr);
        d += std::abs(x.rs - y.rs) + std::abs(x.am - y.am) + std::abs(x.ksr - y.ksr);
    }
    return d;
}

const LibraryEntry* InstrumentLibrary::Nearest(const FuiVoice& src, int* dist) const {
    const LibraryEntry* best = nullptr;
    int bestD = 1 << 30;
    for (const LibraryEntry& e : entries) {
        if (e.isLink) continue;
        const int d = OpmTimbreDistance(src, e.source);
        if (d < bestD) {
            bestD = d;
            best = &e;
        }
    }
    if (dist != nullptr) *dist = bestD;
    return best;
}

const LibraryEntry* InstrumentLibrary::Find(const FuiVoice& src, int maxDistance,
                                           int preferChannel) const {
    const std::string key = OpmTimbreKey(src);
    // 参数完全一致 —— 这就是「同一套音色换了一首曲子」。
    // 同键可能有多条（不同通道配了不同目标音色），优先同通道号那条。
    const LibraryEntry* firstHit = nullptr;
    for (const LibraryEntry& e : entries) {
        if (e.isLink || e.sourceKey != key) continue;
        if (preferChannel >= 0 && SlotChannelOfName(e.targetName) == preferChannel) return &e;
        if (firstHit == nullptr) firstHit = &e;
    }
    if (firstHit != nullptr) return firstHit;

    // 不完全一致时取最近的，避免每首曲子都退回 ROM 自动匹配
    const LibraryEntry* best = nullptr;
    int bestD = maxDistance;
    for (const LibraryEntry& e : entries) {
        if (e.isLink) continue;
        const int d = OpmTimbreDistance(src, e.source);
        const bool sameCh = preferChannel >= 0 && SlotChannelOfName(e.targetName) == preferChannel;
        if (d < bestD || (d == bestD && sameCh && best != nullptr &&
                          !(SlotChannelOfName(best->targetName) == preferChannel))) {
            bestD = d;
            best = &e;
        }
    }
    return best;
}

std::vector<LibraryEntry> BuildLibraryPairs(const FurSong& song) {
    std::vector<LibraryEntry> out;

    // 源侧 OPM 乐器：chNinsM 槽位 → 乐器。
    std::map<std::string, const FurInstrument*> opmBySlot;
    for (const FurInstrument& fi : song.instruments) {
        if (fi.type != kFuiInsOpm) continue;
        const std::string k = InstrumentSlotKey(fi.name);
        if (!k.empty()) opmBySlot[k] = &fi;
    }

    for (const FurInstrument& fi : song.instruments) {
        if (fi.type != kFuiInsOpll) continue;
        const InsTags tags = ParseInstrumentTags(fi.name);
        const std::string slot = InstrumentSlotKey(fi.name);

        LibraryEntry e;
        e.targetName = fi.name;
        e.target = fi.voice;
        e.tags = tags;

        // 源侧是这个槽位对应的 OPM 音色（联动音色也要记：它叠在哪件源音色上
        // 是复用它的关键，否则换一首曲子就不知道该怎么装回去）
        auto it = opmBySlot.find(slot);
        if (it != opmBySlot.end()) {
            e.sourceName = it->second->name;
            e.source = it->second->voice;
            e.sourceKey = OpmTimbreKey(e.source);
        } else if (!tags.linkTo.empty()) {
            // 没有同槽位源音色的 link 乐器：源音色取被联动方的
            const std::string linkSlot = InstrumentSlotKey(tags.linkTo);
            auto lit = opmBySlot.find(linkSlot);
            if (lit != opmBySlot.end()) {
                e.sourceName = lit->second->name;
                e.source = lit->second->voice;
                e.sourceKey = OpmTimbreKey(e.source);
                e.linkSourceKey = e.sourceKey;
            }
        }
        if (!tags.linkTo.empty()) {
            e.isLink = true;
            if (!e.linkSourceKey.empty()) e.sourceKey.clear();  // link 条目自身不参与源匹配
        }
        if (e.sourceName.empty() && !e.isLink) continue;   // 无源音色，无法跨曲目复用
        out.push_back(e);
    }
    return out;
}

bool LoadInstrumentLibrary(const std::string& path, InstrumentLibrary* out, std::string* err) {
    std::ifstream f(path);
    if (!f) {
        *err = "cannot read " + path;
        return false;
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& ex) {
        *err = std::string("library json: ") + ex.what();
        return false;
    }
    out->source = path;
    out->entries.clear();

    const nlohmann::json* pairs = nullptr;
    if (j.contains("pairs") && j["pairs"].is_array()) {
        pairs = &j["pairs"];
    }
    if (pairs == nullptr) {
        *err = "library json: no pairs array (用 fuitool lib --json= 重新导出)";
        return false;
    }
    for (const nlohmann::json& p : *pairs) {
        LibraryEntry e;
        e.sourceName = p.value("sourceName", std::string());
        e.targetName = p.value("targetName", std::string());
        e.isLink = p.value("isLink", false);
        e.tags = ParseInstrumentTags(e.targetName);
        if (p.contains("source")) {
            e.source = VoiceFromJson(p["source"]);
            e.sourceKey = p.value("sourceKey", OpmTimbreKey(e.source));
        }
        e.linkSourceKey = p.value("linkSourceKey", std::string());
        if (p.contains("target")) e.target = VoiceFromJson(p["target"]);
        out->entries.push_back(e);
    }
    return true;
}

// ── 落盘：给 fuitool lib --json= 用 ─────────────────────────────────
nlohmann::json LibraryPairsToJson(const std::vector<LibraryEntry>& pairs) {
    nlohmann::json arr = nlohmann::json::array();
    for (const LibraryEntry& e : pairs) {
        nlohmann::json p = {{"sourceName", e.sourceName},
                            {"targetName", e.targetName},
                            {"isLink", e.isLink},
                            {"tags", {{"note", e.tags.hasNote ? nlohmann::json(e.tags.noteMidi)
                                                              : nlohmann::json(nullptr)},
                                      {"vol", e.tags.hasVol ? nlohmann::json(e.tags.vol)
                                                            : nlohmann::json(nullptr)},
                                      {"noteoff", e.tags.noteOffTicks > 0
                                                      ? nlohmann::json(e.tags.noteOffTicks)
                                                      : (e.tags.noteOffSelf
                                                             ? nlohmann::json("self")
                                                             : nlohmann::json(nullptr))},
                                      {"link", e.tags.linkTo.empty() ? nlohmann::json(nullptr)
                                                                     : nlohmann::json(e.tags.linkTo)}}},
                            {"target", VoiceToJson(e.target)}};
        if (!e.isLink) {
            p["sourceKey"] = e.sourceKey;
            p["source"] = VoiceToJson(e.source);
        } else {
            // link 条目：靠被联动源音色的身份键认人（通道名只在原曲有效）
            p["linkSourceKey"] = e.linkSourceKey;
            if (!e.sourceKey.empty()) {
                p["sourceKey"] = e.sourceKey;
                p["source"] = VoiceToJson(e.source);
            }
        }
        arr.push_back(p);
    }
    return arr;
}

}  // namespace vgmstudio
