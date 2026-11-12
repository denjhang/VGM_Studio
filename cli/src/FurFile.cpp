// FurFile.cpp —— .fur 工程解析（只取乐器表 + tick 速率）。
//
// 代码来源（复制而非自创）：
//   · zlib 解压     furnace/src/engine/fileOps/fileOpsCommon.cpp:49-140
//   · 旧 INFO 头    furnace/src/engine/fileOps/fur.cpp:1035-1345
//   · 新 INF2 头    furnace/src/engine/fileOps/fur.cpp:800-1030
//   · 乐器解码      cli/third_party/furnace/src/engine/instrument.cpp:4316
//                   （readInsData，与读 .fui 同一条路径）
//
// 只做到「读出 insPtr 列表」为止：subsong / pattern / 订单表与音色无关，
// 不解也完全不影响 opm2opll 的行为。
#include "FurFile.h"

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "engine/instrument.h"
#include "engine/safeReader.h"

namespace vgmstudio {
namespace {

// furnace fileOpsCommon.h:50 的 DIV_FUR_MAGIC（16 字节，末尾自带 NUL）
constexpr char kFurMagic[17] = "-Furnace module-";
constexpr int kOldHeaderCut = 240;  // <240 用 INFO 头，否则 INF2

// fileOpsCommon.cpp:49-140 的 zlib 解压（块循环 + Z_SYNC_FLUSH）
bool InflateAll(const std::vector<uint8_t>& in, std::vector<uint8_t>* out) {
    z_stream zl;
    std::memset(&zl, 0, sizeof(zl));
    zl.avail_in = (uInt)in.size();
    zl.next_in = (Bytef*)in.data();
    if (inflateInit(&zl) != Z_OK) return false;

    out->clear();
    std::vector<uint8_t> chunk(65536);
    bool ok = false;
    while (true) {
        zl.next_out = chunk.data();
        zl.avail_out = (uInt)chunk.size();
        const int r = inflate(&zl, Z_SYNC_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END) break;
        const size_t got = chunk.size() - zl.avail_out;
        out->insert(out->end(), chunk.begin(), chunk.begin() + got);
        if (r == Z_STREAM_END) {
            ok = true;
            break;
        }
    }
    inflateEnd(&zl);
    return ok;
}

// 旧 INFO 头：读到 insPtr 为止（fur.cpp:1035-1345）
bool ReadOldInfo(SafeReader& r, int version, FurSong* out, std::vector<int>* insPtr,
                 std::string* err) {
    char magic[5] = {0};
    r.read(magic, 4);
    if (std::memcmp(magic, "INFO", 4) != 0) {
        *err = "fur: 旧头部 magic 不是 INFO";
        return false;
    }
    r.readI();  // info 块长度，忽略

    r.readC();  // oldTimeBase
    r.readC();  // speed[0]
    r.readC();  // speed[1]
    r.readC();  // arpLen
    out->hz = r.readF();

    r.readS();  // patLen
    r.readS();  // ordersLen
    r.readC();  // hilightA
    r.readC();  // hilightB

    const short insLen = r.readS();
    r.readS();  // waveLen
    r.readS();  // sampleLen
    r.readI();  // numberOfPats
    if (insLen < 0 || insLen > 256) {
        *err = "fur: 乐器数越界";
        return false;
    }

    for (int i = 0; i < 32; i++) r.readC();  // system id
    for (int i = 0; i < 32; i++) r.readC();  // system volume
    for (int i = 0; i < 32; i++) r.readC();  // system panning
    for (int i = 0; i < 32; i++) r.readI();  // system flags ptr

    out->name = r.readString();
    out->author = r.readString();

    if (version >= 33) {
        r.readF();  // tuning
    } else {
        r.readI();
    }

    // 兼容标志位：无论版本都是固定 20 字节（各分支读满 20 个字节）
    if (version >= 37) {
        for (int i = 0; i < 20; i++) r.readC();
    } else {
        for (int i = 0; i < 20; i++) r.readC();
    }

    for (int i = 0; i < insLen; i++) insPtr->push_back(r.readI());
    return true;
}

// 新 INF2 头：读到 instrument 元素指针为止（fur.cpp:800-1030）
bool ReadNewInfo(SafeReader& r, FurSong* out, std::vector<int>* insPtr, int* subSongPtr0,
                 std::string* err) {
    char magic[5] = {0};
    r.read(magic, 4);
    if (std::memcmp(magic, "INF2", 4) != 0) {
        *err = "fur: 新头部 magic 不是 INF2";
        return false;
    }
    r.readI();  // info 块长度

    out->name = r.readString();
    out->author = r.readString();
    r.readString();  // systemName
    r.readString();  // category
    r.readString();  // nameJ
    r.readString();  // authorJ
    r.readString();  // systemNameJ
    r.readString();  // categoryJ

    r.readF();  // tuning
    r.readC();  // autoSystem
    r.readF();  // masterVol
    r.readS();  // chans
    const short systemLen = r.readS();
    if (systemLen < 1 || systemLen > 32) {
        *err = "fur: 芯片数越界";
        return false;
    }
    for (int i = 0; i < systemLen; i++) {
        r.readS();  // system id
        r.readS();  // channels
        r.readF();  // volume
        r.readF();  // panning
        r.readF();  // front/rear panning
    }

    // patchbay
    const int conns = r.readI();
    if (conns < 0 || conns > 4096) {
        *err = "fur: patchbay 连接数越界";
        return false;
    }
    for (int i = 0; i < conns; i++) r.readI();
    r.readC();  // patchbayAuto

    *subSongPtr0 = 0;
    bool hasElement = true;
    while (hasElement) {
        const unsigned char elementType = (unsigned char)r.readC();
        switch (elementType) {
            case 0:  // DIV_ELEMENT_END
                hasElement = false;
                break;
            case 1: {  // DIV_ELEMENT_SUBSONG
                const int n = r.readI();
                if (n < 0 || n > 256) {
                    *err = "fur: subsong 数越界";
                    return false;
                }
                for (int i = 0; i < n; i++) {
                    const int p = r.readI();
                    if (i == 0) *subSongPtr0 = p;
                }
                break;
            }
            case 4: {  // DIV_ELEMENT_INSTRUMENT
                const int n = r.readI();
                if (n < 0 || n > 256) {
                    *err = "fur: 乐器数越界";
                    return false;
                }
                for (int i = 0; i < n; i++) insPtr->push_back(r.readI());
                break;
            }
            default: {  // 其余元素：读 count + 指针，跳过
                const int n = r.readI();
                if (n < 0 || n > 1 << 20) {
                    *err = "fur: 元素表长度越界";
                    return false;
                }
                for (int i = 0; i < n; i++) r.readI();
                break;
            }
        }
    }
    return true;
}

}  // namespace

bool LoadFur(const std::string& path, FurSong* out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *err = "fur: 打不开 " + path;
        return false;
    }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (raw.size() < 32) {
        *err = "fur: 文件太小";
        return false;
    }

    // 先试 zlib，失败则按未压缩处理（与 fileOpsCommon.cpp 同策略）
    std::vector<uint8_t> plain;
    if (InflateAll(raw, &plain) && plain.size() >= 32) {
        // 解压成功
    } else {
        plain = raw;
    }
    if (std::memcmp(plain.data(), kFurMagic, 16) != 0) {
        *err = "fur: 不是 Furnace 工程（magic 不符）";
        return false;
    }

    std::vector<uint8_t> buf = plain;  // SafeReader 不持有所有权，拷一份保活
    SafeReader r(buf.data(), buf.size());
    std::vector<int> insPtr;
    try {
        r.seek(16, SEEK_SET);
        out->version = r.readS();
        r.readS();                       // reserved
        const int infoSeek = r.readI();
        if (infoSeek < 32 || (size_t)infoSeek >= buf.size()) {
            *err = "fur: info 指针越界";
            return false;
        }
        if (!r.seek(infoSeek, SEEK_SET)) {
            *err = "fur: 无法 seek 到 info";
            return false;
        }
        int subSongPtr0 = 0;
        const bool ok = (out->version >= kOldHeaderCut)
                            ? ReadNewInfo(r, out, &insPtr, &subSongPtr0, err)
                            : ReadOldInfo(r, out->version, out, &insPtr, err);
        if (!ok) return false;

        // INF2 的 hz 在 subsong 里（SNG2 头），INFO 头里没有
        if (out->version >= kOldHeaderCut && subSongPtr0 > 0 &&
            (size_t)subSongPtr0 + 12 <= buf.size()) {
            SafeReader sr(buf.data(), buf.size());
            sr.seek(subSongPtr0, SEEK_SET);
            char m[5] = {0};
            sr.read(m, 4);
            if (std::memcmp(m, "SNG2", 4) == 0) {
                sr.readI();
                out->hz = sr.readF();
            }
        }

        out->instruments.reserve(insPtr.size());
        for (size_t i = 0; i < insPtr.size(); i++) {
            const int p = insPtr[i];
            if (p < 0 || (size_t)p >= buf.size()) {
                *err = "fur: 乐器指针越界";
                return false;
            }
            FurInstrument fi;
            std::string derr;
            if (!DecodeFuiVersioned(buf.data() + p, buf.size() - p, out->version, &fi.voice,
                                    &derr)) {
                char b[96];
                std::snprintf(b, sizeof(b), "fur: 第 %zu 个乐器解码失败: %s", i, derr.c_str());
                *err = b;
                return false;
            }
            fi.name = fi.voice.name;
            fi.type = fi.voice.type;
            out->instruments.push_back(std::move(fi));
        }
    } catch (EndOfFileException&) {
        *err = "fur: 文件被截断";
        return false;
    }
    return true;
}

const FurInstrument* FindFurInstrument(const FurSong& song, const std::string& name,
                                       int preferType) {
    // 1) 名字完全相同
    // 2) 名字 + 后缀（"ch1ins2-vib" 这种，音色名是用户随手编的）
    // 两步都优先返回 preferType 的乐器：工程里往往同时存着源侧 OPM
    // （type 33）和目标侧 OPLL（type 13），只按名字前缀找会拿到源侧那份。
    const FurInstrument* exact = nullptr;
    const FurInstrument* prefix = nullptr;
    for (const FurInstrument& i : song.instruments) {
        if (preferType >= 0 && i.type != preferType) continue;
        if (i.name == name) {
            if (exact == nullptr) exact = &i;
        } else if (i.name.size() > name.size() &&
                   i.name.compare(0, name.size(), name) == 0) {
            const char c = i.name[name.size()];
            if ((c == '-' || c == '_' || c == ' ') && prefix == nullptr) prefix = &i;
        }
    }
    if (exact != nullptr) return exact;
    if (prefix != nullptr) return prefix;
    // 放宽类型再找一次
    if (preferType >= 0) return FindFurInstrument(song, name, -1);
    return nullptr;
}

}  // namespace vgmstudio
