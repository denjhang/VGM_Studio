// Document.cpp —— VGSJ JSON 读写（schema 见 docs/DATA-MODEL.md）。
#include "Document.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace vgmstudio {

using nlohmann::json;

// ───────────────────────────── base64 ─────────────────────────────
namespace {
constexpr char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int B64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

std::string Base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kB64Chars[(v >> 18) & 0x3F]);
        out.push_back(kB64Chars[(v >> 12) & 0x3F]);
        out.push_back(kB64Chars[(v >> 6) & 0x3F]);
        out.push_back(kB64Chars[v & 0x3F]);
    }
    if (i < len) {
        uint32_t v = data[i] << 16;
        bool two = (i + 1 < len);
        if (two) v |= data[i + 1] << 8;
        out.push_back(kB64Chars[(v >> 18) & 0x3F]);
        out.push_back(kB64Chars[(v >> 12) & 0x3F]);
        out.push_back(two ? kB64Chars[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

std::vector<uint8_t> Base64Decode(const std::string& text) {
    std::vector<uint8_t> out;
    int buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = B64Value(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// ───────────────────────────── sha256 ─────────────────────────────
// 自带一份最小实现：只用于 meta.sourceSha256，不引第三方依赖。
namespace {
struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    size_t bufLen = 0;

    Sha256() {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::memcpy(h, init, sizeof(h));
    }

    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const uint8_t* data, size_t n) {
        len += n;
        while (n > 0) {
            size_t take = std::min(n, sizeof(buf) - bufLen);
            std::memcpy(buf + bufLen, data, take);
            bufLen += take;
            data += take;
            n -= take;
            if (bufLen == sizeof(buf)) {
                Block(buf);
                bufLen = 0;
            }
        }
    }

    std::string Final() {
        uint64_t bitLen = len * 8;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        uint8_t zero = 0x00;
        while (bufLen != 56) Update(&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; i++) lenBytes[i] = uint8_t(bitLen >> (56 - i * 8));
        Update(lenBytes, 8);
        char out[65];
        for (int i = 0; i < 8; i++) std::snprintf(out + i * 8, 9, "%08x", h[i]);
        out[64] = '\0';
        return out;
    }
};
}  // namespace

std::string Sha256Hex(const uint8_t* data, size_t len) {
    Sha256 s;
    s.Update(data, len);
    return s.Final();
}

// ───────────────────────── JSON 读写 ─────────────────────────
size_t Document::CountStructuredWrites() const {
    size_t n = 0;
    for (const Channel& c : channels) {
        for (const Note& note : c.notes)
            for (const Segment& seg : note.segments) n += seg.regs.size();
        for (const Orphan& o : c.orphans) n += o.regs.size();
    }
    for (const GlobalSegment& g : globals) n += g.regs.size();
    return n;
}

namespace {

json RegsToJson(const std::vector<RegValue>& regs) {
    json a = json::array();
    for (const RegValue& r : regs) a.push_back({{"reg", r.reg}, {"val", r.val}});
    return a;
}

std::vector<RegValue> RegsFromJson(const json& a) {
    std::vector<RegValue> out;
    for (const json& r : a) out.push_back(RegValue{r.at("reg").get<uint8_t>(), r.at("val").get<uint8_t>()});
    return out;
}

}  // namespace

bool Document::SaveJson(const std::string& path, bool pretty, std::string* err) const {
    json j;
    j["format"] = format;
    j["version"] = version;

    j["meta"] = {
        {"sourceFile", meta.sourceFile},
        {"sourceSize", meta.sourceSize},
        {"sourceSha256", meta.sourceSha256},
        {"importedAt", meta.importedAt},
        {"parser", {{"lib", meta.parserLib}, {"regStreamMethod", meta.regStreamMethod}}},
        {"structurer", {{"name", meta.structurer}, {"splitRule", meta.splitRule}}},
        {"unhandled", meta.unhandled},
    };

    j["timeBase"] = {
        {"sampleRate", timeBase.sampleRate},
        {"totalSamples", timeBase.totalSamples},
        {"loopSample", timeBase.loopSample},
        {"vgmRate", timeBase.vgmRate},
    };

    json chipsArr = json::array();
    for (const ChipEntry& c : chips) {
        chipsArr.push_back({
            {"id", c.id},
            {"type", c.type},
            {"vgmChipType", c.vgmChipType},
            {"clock", c.clock},
            {"instances", c.instances},
            {"channelCount", c.channelCount},
            {"portCount", c.portCount},
            {"capabilities",
             {{"operators", c.capabilities.operators},
              {"algorithms", c.capabilities.algorithms},
              {"polyphony", c.capabilities.polyphony}}},
        });
    }
    j["chips"] = chipsArr;

    json globalsArr = json::array();
    for (const GlobalSegment& g : globals) {
        globalsArr.push_back({{"chip", "ym2151.0"}, {"atSample", g.atSample}, {"regs", RegsToJson(g.regs)}});
    }
    j["globals"] = globalsArr;

    json chansArr = json::array();
    for (const Channel& c : channels) {
        json cj;
        cj["chip"] = "ym2151.0";
        cj["index"] = c.index;
        cj["name"] = c.name;
        cj["kind"] = c.kind;

        json notesArr = json::array();
        for (const Note& n : c.notes) {
            json nj;
            nj["id"] = n.id;
            nj["startSample"] = n.startSample;
            nj["endSample"] = n.endSample;
            nj["durationSamples"] = (n.endSample > n.startSample) ? (n.endSample - n.startSample) : 0;
            json segs = json::array();
            for (const Segment& s : n.segments) {
                segs.push_back({{"atSample", s.atSample}, {"regs", RegsToJson(s.regs)}});
            }
            nj["segments"] = segs;
            notesArr.push_back(nj);
        }
        cj["notes"] = notesArr;

        json orphanArr = json::array();
        for (const Orphan& o : c.orphans) {
            orphanArr.push_back({{"atSample", o.atSample}, {"regs", RegsToJson(o.regs)}, {"reason", o.reason}});
        }
        cj["orphans"] = orphanArr;

        json lanesArr = json::array();
        for (const Lane& l : c.lanes) {
            json pts = json::array();
            for (const LanePoint& p : l.points) pts.push_back(json::array({p.atSample, p.value}));
            lanesArr.push_back({{"param", l.param}, {"reg", l.reg}, {"points", pts}});
        }
        cj["lanes"] = lanesArr;
        chansArr.push_back(cj);
    }
    j["channels"] = chansArr;

    j["raw"] = {{"dataOffset", raw.dataOffset},
                {"eofOffset", raw.eofOffset},
                {"header", raw.headerBase64.empty() ? "" : "base64:" + raw.headerBase64},
                {"commands", raw.commandsBase64.empty() ? "" : "base64:" + raw.commandsBase64},
                {"tail", raw.tailBase64.empty() ? "" : "base64:" + raw.tailBase64}};

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        *err = "cannot write " + path;
        return false;
    }
    f << (pretty ? j.dump(2) : j.dump());
    f.put('\n');
    return true;
}

bool Document::LoadJson(const std::string& path, Document* out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *err = "cannot read " + path;
        return false;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        *err = std::string("json parse error: ") + e.what();
        return false;
    }

    out->format = j.value("format", std::string("vgm-studio/song"));
    out->version = j.value("version", 1);

    const json& m = j.at("meta");
    out->meta.sourceFile = m.value("sourceFile", std::string());
    out->meta.sourceSize = m.value("sourceSize", (uint64_t)0);
    out->meta.sourceSha256 = m.value("sourceSha256", std::string());
    out->meta.importedAt = m.value("importedAt", std::string());
    if (m.contains("parser")) {
        out->meta.parserLib = m["parser"].value("lib", std::string("libvgm"));
        out->meta.regStreamMethod = m["parser"].value("regStreamMethod", std::string("shadowdev"));
    }
    if (m.contains("structurer")) {
        out->meta.structurer = m["structurer"].value("name", std::string("ym2151/v1"));
        out->meta.splitRule = m["structurer"].value("splitRule", std::string("modizer-note-merge"));
    }
    out->meta.unhandled = m.value("unhandled", std::vector<std::string>());

    const json& tb = j.at("timeBase");
    out->timeBase.sampleRate = tb.value("sampleRate", 44100);
    out->timeBase.totalSamples = tb.value("totalSamples", 0u);
    out->timeBase.loopSample = tb.value("loopSample", (int64_t)-1);
    out->timeBase.vgmRate = tb.value("vgmRate", 60u);

    out->chips.clear();
    for (const json& c : j.at("chips")) {
        ChipEntry ce;
        ce.id = c.value("id", std::string());
        ce.type = c.value("type", std::string());
        ce.vgmChipType = c.value("vgmChipType", 0);
        ce.clock = c.value("clock", 0u);
        ce.instances = c.value("instances", 1);
        ce.channelCount = c.value("channelCount", 8);
        ce.portCount = c.value("portCount", 2);
        if (c.contains("capabilities")) {
            const json& cap = c["capabilities"];
            ce.capabilities.operators = cap.value("operators", 4);
            ce.capabilities.algorithms = cap.value("algorithms", 8);
            ce.capabilities.polyphony = cap.value("polyphony", 8);
        }
        out->chips.push_back(ce);
    }

    out->globals.clear();
    for (const json& g : j.at("globals")) {
        GlobalSegment gs;
        gs.atSample = g.value("atSample", 0u);
        gs.regs = RegsFromJson(g.at("regs"));
        out->globals.push_back(gs);
    }

    out->channels.clear();
    for (const json& c : j.at("channels")) {
        Channel ch;
        ch.index = c.value("index", 0);
        ch.name = c.value("name", std::string());
        ch.kind = c.value("kind", std::string("FM"));
        for (const json& n : c.at("notes")) {
            Note note;
            note.id = n.value("id", 0);
            note.startSample = n.value("startSample", 0u);
            note.endSample = n.value("endSample", 0u);
            for (const json& s : n.at("segments")) {
                Segment seg;
                seg.atSample = s.value("atSample", 0u);
                seg.regs = RegsFromJson(s.at("regs"));
                note.segments.push_back(seg);
            }
            ch.notes.push_back(note);
        }
        for (const json& o : c.at("orphans")) {
            Orphan orph;
            orph.atSample = o.value("atSample", 0u);
            orph.reason = o.value("reason", std::string());
            orph.regs = RegsFromJson(o.at("regs"));
            ch.orphans.push_back(orph);
        }
        for (const json& l : c.at("lanes")) {
            Lane lane;
            lane.param = l.value("param", std::string());
            lane.reg = l.value("reg", 0);
            for (const json& p : l.at("points")) lane.points.push_back(LanePoint{p[0].get<uint32_t>(), p[1].get<int>()});
            ch.lanes.push_back(lane);
        }
        out->channels.push_back(ch);
    }

    const json& r = j.at("raw");
    out->raw.dataOffset = r.value("dataOffset", 0u);
    out->raw.eofOffset = r.value("eofOffset", 0u);
    auto strip = [](std::string s) {
        if (s.rfind("base64:", 0) == 0) return s.substr(7);
        return s;
    };
    out->raw.headerBase64 = strip(r.value("header", std::string()));
    out->raw.commandsBase64 = strip(r.value("commands", std::string()));
    out->raw.tailBase64 = strip(r.value("tail", std::string()));

    return true;
}

}  // namespace vgmstudio
