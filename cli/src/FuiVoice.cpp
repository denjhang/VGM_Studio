// FuiVoice.cpp —— .fui 读写（走 Furnace）+ 宏执行 + 寄存器语义映射。
#include "FuiVoice.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

// Furnace 的原版头文件（vendored，见 third_party/furnace/README.md）
#include "engine/instrument.h"
#include "engine/safeReader.h"
#include "engine/safeWriter.h"

namespace vgmstudio {
namespace {

// Furnace 的宏列表下标 ⇄ 我们的 Macro 下标（两者顺序一致，见 instrument.h:344-370）
DivInstrumentMacro* MacroSlot(DivInstrument* ins, int i) {
    switch (i) {
        case 0: return &ins->std.volMacro;
        case 1: return &ins->std.arpMacro;
        case 2: return &ins->std.dutyMacro;
        case 3: return &ins->std.waveMacro;
        case 4: return &ins->std.pitchMacro;
        case 5: return &ins->std.ex1Macro;
        case 6: return &ins->std.ex2Macro;
        case 7: return &ins->std.ex3Macro;
        case 8: return &ins->std.algMacro;
        case 9: return &ins->std.fbMacro;
        case 10: return &ins->std.fmsMacro;
        case 11: return &ins->std.amsMacro;
        case 12: return &ins->std.panLMacro;
        case 13: return &ins->std.panRMacro;
        case 14: return &ins->std.phaseResetMacro;
        case 15: return &ins->std.ex4Macro;
        case 16: return &ins->std.ex5Macro;
        case 17: return &ins->std.ex6Macro;
        case 18: return &ins->std.ex7Macro;
        case 19: return &ins->std.ex8Macro;
        case 20: return &ins->std.ex9Macro;
        case 21: return &ins->std.ex10Macro;
        default: return nullptr;
    }
}

// 算子级宏的槽位（instrument.h:135 的 DIV_MACRO_OP_*，与 readFeatureOx 的 case 编号一致）
DivInstrumentMacro* OpMacroSlot(DivInstrument* ins, int op, int i) {
    auto& m = ins->std.opMacros[op];
    switch (i) {
        case kOpAm: return &m.amMacro;
        case kOpAr: return &m.arMacro;
        case kOpDr: return &m.drMacro;
        case kOpMult: return &m.multMacro;
        case kOpRr: return &m.rrMacro;
        case kOpSl: return &m.slMacro;
        case kOpTl: return &m.tlMacro;
        case kOpDt2: return &m.dt2Macro;
        case kOpRs: return &m.rsMacro;
        case kOpDt: return &m.dtMacro;
        case kOpD2r: return &m.d2rMacro;
        case kOpSsg: return &m.ssgMacro;
        case kOpDam: return &m.damMacro;
        case kOpDvb: return &m.dvbMacro;
        case kOpEgt: return &m.egtMacro;
        case kOpKsl: return &m.kslMacro;
        case kOpSus: return &m.susMacro;
        case kOpVib: return &m.vibMacro;
        case kOpWs: return &m.wsMacro;
        case kOpKsr: return &m.ksrMacro;
        default: return nullptr;
    }
}

// Furnace 的 dtTable（platform/fmsharedbase.h:42）：内部 dt → OPM 寄存器 DT1
constexpr int kDtTable[8] = {7, 6, 5, 0, 1, 2, 3, 4};

int DtToReg(int dt) {
    return kDtTable[dt & 7];
}

int RegToDt(int regDt1) {
    for (int i = 0; i < 8; i++)
        if (kDtTable[i] == (regDt1 & 7)) return i;
    return 0;
}

}  // namespace

// ─────────────────────────── 编解码（走 Furnace）───────────────────────────

std::vector<uint8_t> EncodeFui(const FuiVoice& v) {
    DivInstrument ins;
    ins.type = (DivInstrumentType)v.type;
    ins.name = v.name;
    ins.fm.alg = v.alg;
    ins.fm.fb = v.fb;
    ins.fm.fms = v.fms;
    ins.fm.ams = v.ams;
    ins.fm.ops = v.ops;
    ins.fm.opllPreset = v.opllPreset;
    ins.fm.block = v.block;
    ins.fm.fmsLFO = v.fmsLFO;
    ins.fm.amsLFO = v.amsLFO;
    ins.fm.tremLFO = v.tremLFO;
    for (int i = 0; i < 4; i++) {
        DivInstrumentFM::Operator& o = ins.fm.op[i];
        const FuiOperator& s = v.op[i];
        o.enable = s.enable;
        o.am = (unsigned char)s.am;
        o.ar = (unsigned char)s.ar;
        o.dr = (unsigned char)s.dr;
        o.mult = (unsigned char)s.mult;
        o.rr = (unsigned char)s.rr;
        o.sl = (unsigned char)s.sl;
        o.tl = (unsigned char)s.tl;
        o.dt2 = (unsigned char)s.dt2;
        o.rs = (unsigned char)s.rs;
        o.dt = (unsigned char)s.dt;
        o.d2r = (unsigned char)s.d2r;
        o.ssgEnv = (unsigned char)s.ssgEnv;
        o.dam = (unsigned char)s.dam;
        o.dvb = (unsigned char)s.dvb;
        o.egt = (unsigned char)s.egt;
        o.ksl = (unsigned char)s.ksl;
        o.sus = (unsigned char)s.sus;
        o.vib = (unsigned char)s.vib;
        o.ws = (unsigned char)s.ws;
        o.ksr = (unsigned char)s.ksr;
        o.kvs = (unsigned char)s.kvs;
    }
    for (int i = 0; i < kMacroCount; i++) {
        DivInstrumentMacro* m = MacroSlot(&ins, i);
        const Macro& s = v.macros[i];
        if (m == nullptr) continue;
        m->len = (unsigned char)(s.val.size() > 255 ? 255 : s.val.size());
        for (int j = 0; j < m->len; j++) m->val[j] = s.val[j];
        m->loop = (unsigned char)s.loop;
        m->rel = (unsigned char)s.rel;
        m->delay = (unsigned char)s.delay;
        m->speed = (unsigned char)s.speed;
        m->mode = (unsigned int)(s.mode & 7);
        m->open = (unsigned char)(((s.type & 3) << 1) | (s.activeRelease ? 8 : 0));
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < kOpMacroCount; j++) {
            DivInstrumentMacro* m = OpMacroSlot(&ins, i, j);
            const Macro& s = v.opMacros[i][j];
            if (m == nullptr) continue;
            m->len = (unsigned char)(s.val.size() > 255 ? 255 : s.val.size());
            for (int k = 0; k < m->len; k++) m->val[k] = s.val[k];
            m->loop = (unsigned char)s.loop;
            m->rel = (unsigned char)s.rel;
            m->delay = (unsigned char)s.delay;
            m->speed = (unsigned char)s.speed;
            m->mode = 0;
            // 算子宏的 open 只有 3 位（readFeatureOx 里 `target->open=wordSize&7`），
            // 所以只保留 type 的 bit1-2，没有 activeRelease。
            m->open = (unsigned char)((s.type & 3) << 1);
        }
    }

    SafeWriter w;
    w.init();
    ins.putInsData2(&w, /*fui=*/true, nullptr, /*insName=*/true);
    std::vector<uint8_t> out(w.getFinalBuf(), w.getFinalBuf() + w.size());
    return out;
}

bool DecodeFuiVersioned(const uint8_t* data, size_t len, int version, FuiVoice* out,
                        std::string* err) {
    if (data == nullptr || len < 8) {
        *err = "fui: too short";
        return false;
    }
    std::vector<uint8_t> copy(data, data + len);
    SafeReader r(copy.data(), copy.size());
    DivInstrument ins;
    const DivDataErrors e = ins.readInsData(r, (short)version, nullptr);
    if (e != DIV_DATA_SUCCESS) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "fui: readInsData error %d", (int)e);
        *err = buf;
        return false;
    }

    out->type = (int)ins.type;
    out->name = ins.name;
    out->alg = ins.fm.alg;
    out->fb = ins.fm.fb;
    out->fms = ins.fm.fms;
    out->ams = ins.fm.ams;
    out->ops = ins.fm.ops;
    out->opllPreset = ins.fm.opllPreset;
    out->block = ins.fm.block;
    out->fmsLFO = ins.fm.fmsLFO;
    out->amsLFO = ins.fm.amsLFO;
    out->tremLFO = ins.fm.tremLFO;
    for (int i = 0; i < 4; i++) {
        const DivInstrumentFM::Operator& o = ins.fm.op[i];
        FuiOperator& s = out->op[i];
        s.enable = o.enable;
        s.am = o.am; s.ar = o.ar; s.dr = o.dr; s.mult = o.mult;
        s.rr = o.rr; s.sl = o.sl; s.tl = o.tl;
        s.dt2 = o.dt2; s.rs = o.rs; s.dt = o.dt; s.d2r = o.d2r;
        s.ssgEnv = o.ssgEnv; s.dam = o.dam; s.dvb = o.dvb; s.egt = o.egt;
        s.ksl = o.ksl; s.sus = o.sus; s.vib = o.vib; s.ws = o.ws;
        s.ksr = o.ksr; s.kvs = o.kvs;
    }
    for (int i = 0; i < kMacroCount; i++) {
        DivInstrumentMacro* m = MacroSlot(&ins, i);
        Macro& s = out->macros[i];
        s.val.clear();
        if (m == nullptr) continue;
        s.val.assign(m->val, m->val + m->len);
        s.loop = m->loop;
        s.rel = m->rel;
        s.delay = m->delay;
        s.speed = m->speed;
        s.type = (m->open >> 1) & 3;
        s.mode = (int)m->mode;
        s.activeRelease = (m->open & 8) != 0;
        s.linger = (i == kMacroVol);
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < kOpMacroCount; j++) {
            DivInstrumentMacro* m = OpMacroSlot(&ins, i, j);
            Macro& s = out->opMacros[i][j];
            s.val.clear();
            if (m == nullptr) continue;
            s.val.assign(m->val, m->val + m->len);
            s.loop = m->loop;
            s.rel = m->rel;
            s.delay = m->delay;
            s.speed = m->speed;
            s.type = (m->open >> 1) & 3;
            s.activeRelease = false;
            s.linger = (j == kOpTl && i == 1);
        }
    }
    return true;
}

bool DecodeFui(const uint8_t* data, size_t len, FuiVoice* out, std::string* err) {
    // .fui 由当前版本的 Furnace 写出，固定用 DIV_ENGINE_VERSION 解码。
    return DecodeFuiVersioned(data, len, 258, out, err);
}

// ── 音色身份（从 FM 参数本身算，不看寄存器影子）─────────────────────
// 影子寄存器里混着演奏期数据（0x60 的 TL 在 OPLL 上甚至有 per-note 音量），
// 同一个音色在不同音符上影子可以不同，所以影子只能用于曲目内去重。
// 判定「是哪个乐器」必须只看定义音色的参数。
//
// 规范化格式（字段顺序固定、全部十进制），既能当 map 键也能直接写进文档：
//   t13;preset5;alg0;fb3;fms0;ams0;ops2;|0:mult1,tl21,...;|1:...
//
// ★ preset 必须算进去：OPLL 上 opllPreset≠0 时芯片用的是 ROM 里那一号音色，
//   fui 里的 FM 参数只是它的副本、并不生效。参数一模一样但 preset 5 / 14
//   的两个乐器，听起来是 Clarinet 和 Acoustic Bass，完全是两个音色。
//   卡在参数上而不看 preset，会把它们误判成同一个。
// block 不算：它只是 2413 侧的八度偏移量，同一个音色换 block 仍是同一音色。
std::string VoiceSignature(const FuiVoice& v) {
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf), "t%d;preset%d;alg%d;fb%d;fms%d;ams%d;ops%d",
                          v.type, v.opllPreset, v.alg & 7, v.fb & 7, v.fms & 7, v.ams & 3,
                          v.ops);
    const int shown = v.ops >= 2 ? v.ops : 2;
    for (int i = 0; i < shown && n > 0 && (size_t)n < sizeof(buf); i++) {
        const FuiOperator& o = v.op[i];
        n += std::snprintf(buf + n, sizeof(buf) - n,
                           "|%d:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", i, o.mult,
                           o.tl, o.ar, o.dr, o.sl, o.rr, o.dt, o.dt2, o.ksr, o.am, o.vib,
                           o.egt, o.ksl, o.ws, o.sus, o.kvs);
    }
    return std::string(buf);
}

uint64_t VoiceFingerprint(const FuiVoice& v) {
    const std::string s = VoiceSignature(v);
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

bool SaveFui(const FuiVoice& v, const std::string& path, std::string* err) {
    const std::vector<uint8_t> bytes = EncodeFui(v);
    if (bytes.empty()) {
        *err = "fui: encode produced nothing";
        return false;
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        *err = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

bool LoadFui(const std::string& path, FuiVoice* out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *err = "cannot read " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    return DecodeFui(bytes.data(), bytes.size(), out, err);
}

// ─────────────────────────── 宏执行 ───────────────────────────
// 逐行对应 furnace/src/engine/macroInt.cpp:83-296 的 DivMacroStruct::doMacro。
//
// 中断速率：每个 Tick() = 一个 tick。宏每步之间隔 speed 个 tick
// （首步额外等 delay 个 tick）。一秒有多少 tick 由 tickRate 决定
// （Furnace 的 song.hz / tickRate，默认 60），.fui 里没有这个数。
void MacroRunner::Start(const Macro& m, int tickRate) {
    m_ = m;
    type_ = m.type;
    tickRate_ = tickRate > 0 ? tickRate : 60;
    pos_ = 0;
    lastPos_ = 0;
    delay_ = 0;
    val_ = 0;
    running_ = !m.val.empty();
    begun_ = true;
    released_ = false;
    lfoDir_ = false;
    changed_ = false;
}

void MacroRunner::Release() {
    released_ = true;
    if (type_ == 1 && lastPos_ < 3) delay_ = 0;
    if (type_ == 0 && m_.rel < static_cast<int>(m_.val.size()) && m_.activeRelease) {
        delay_ = 0;
        pos_ = m_.rel;
    }
}

void MacroRunner::Tick() {
    changed_ = false;
    if (!running_) return;

    if (delay_ > 0) {
        delay_--;
        return;
    }
    delay_ = (begun_ && m_.delay > 0) ? m_.delay : (m_.speed > 0 ? m_.speed - 1 : 0);
    if (begun_) begun_ = false;

    const int len = static_cast<int>(m_.val.size());
    if (len == 0) {
        running_ = false;
        return;
    }

    if (type_ == 0) {  // 序列
        lastPos_ = pos_;
        val_ = m_.val[pos_++];
        changed_ = true;
        if (pos_ > m_.rel && !released_) {
            if (m_.loop < len && m_.loop < m_.rel) {
                pos_ = m_.loop;
            } else {
                pos_--;
            }
        }
        if (pos_ >= len) {
            if (m_.loop < len && (m_.loop >= m_.rel || m_.rel >= len)) {
                pos_ = m_.loop;
            } else if (m_.linger) {
                pos_--;
            } else {
                running_ = false;
            }
        }
        return;
    }

    // ADSR / LFO 的参数表布局（与 macroInt.cpp 的 ADSR_* 宏一致）
    const int low = m_.val.size() > 0 ? m_.val[0] : 0;
    const int high = m_.val.size() > 1 ? m_.val[1] : 0;
    const bool inverted = low > high;
    const int bottom = low << 8;
    const int top = (high << 8) | 0xff;
    const int bottomInv = (low << 8) | 0xff;
    const int topInv = high << 8;
    auto arg = [&](int i) { return i < static_cast<int>(m_.val.size()) ? m_.val[i] : 0; };

    if (type_ == 1) {  // ADSR
        if (released_ && lastPos_ < 3) lastPos_ = 3;
        const int ar = arg(2), dr = arg(3), sl = arg(5), sr = arg(7), rr = arg(8);
        const int ht = arg(4), st = arg(6);
        const int sus = (sl << 8) | 0xff;
        const int susInv = sl << 8;
        if (inverted) {
            switch (lastPos_) {
                case 0: pos_ -= ar; if (pos_ <= topInv) { pos_ = topInv; lastPos_ = 1; delay_ = ht; } break;
                case 1: pos_ += dr; if (pos_ >= susInv) { pos_ = susInv; lastPos_ = 2; delay_ = st; } break;
                case 2: pos_ += sr; if (pos_ >= bottomInv) { pos_ = bottomInv; lastPos_ = 4; } break;
                case 3: pos_ += rr; if (pos_ >= bottomInv) { pos_ = bottomInv; lastPos_ = 4; } break;
                default: pos_ = bottomInv; if (!m_.linger) running_ = false; break;
            }
        } else {
            switch (lastPos_) {
                case 0: pos_ += ar; if (pos_ >= top) { pos_ = top; lastPos_ = 1; delay_ = ht; } break;
                case 1: pos_ -= dr; if (pos_ <= sus) { pos_ = sus; lastPos_ = 2; delay_ = st; } break;
                case 2: pos_ -= sr; if (pos_ <= bottom) { pos_ = bottom; lastPos_ = 4; } break;
                case 3: pos_ -= rr; if (pos_ <= bottom) { pos_ = bottom; lastPos_ = 4; } break;
                default: pos_ = bottom; if (!m_.linger) running_ = false; break;
            }
        }
        val_ = pos_ >> 8;
        changed_ = true;
        return;
    }

    // type_ == 2：LFO
    const int speed = arg(11);
    const int wave = arg(12) & 3;
    if (inverted) {
        switch (wave) {
            case 0:
                if (lfoDir_) { pos_ += speed; if (pos_ > bottomInv) { lfoDir_ = false; pos_ -= speed * 2; } }
                else { pos_ -= speed; if (pos_ < topInv) { lfoDir_ = true; pos_ += speed * 2; } }
                break;
            case 1:
                pos_ -= speed;
                if (pos_ < topInv) pos_ += (bottomInv - topInv);
                break;
            default:
                pos_ += speed; pos_ &= 65535;
                val_ = (pos_ & 32768) ? (high << 8) : (low << 8);
                val_ >>= 8; changed_ = true; return;
        }
    } else {
        switch (wave) {
            case 0:
                if (lfoDir_) { pos_ -= speed; if (pos_ < bottom) { lfoDir_ = false; pos_ += speed * 2; } }
                else { pos_ += speed; if (pos_ > top) { lfoDir_ = true; pos_ -= speed * 2; } }
                break;
            case 1:
                pos_ += speed;
                if (pos_ > top) pos_ -= (top - bottom);
                break;
            default:
                pos_ += speed; pos_ &= 65535;
                val_ = (pos_ & 32768) ? (high << 8) : (low << 8);
                val_ >>= 8; changed_ = true; return;
        }
    }
    val_ = pos_ >> 8;
    changed_ = true;
}

// ─────────────────────────── OPM 寄存器 ⇄ FuiVoice ───────────────────────────
// 依据 Furnace platform/arcade.cpp:562-602 的 commitState（唯一权威的反向映射）。
FuiVoice OpmShadowToFui(const uint8_t shadow[256], int ch) {
    FuiVoice v;
    v.type = kFuiInsOpm;
    v.ops = 4;
    char nm[24];
    std::snprintf(nm, sizeof(nm), "FM%d", ch + 1);
    v.name = nm;

    const uint8_t pan_fb_alg = shadow[0x20 + ch];
    v.alg = pan_fb_alg & 7;
    v.fb = (pan_fb_alg >> 3) & 7;
    const uint8_t pms_ams = shadow[0x38 + ch];
    v.fms = (pms_ams >> 4) & 7;
    v.ams = pms_ams & 3;

    for (int i = 0; i < 4; i++) {
        const int base = i * 8 + ch;
        FuiOperator& o = v.op[i];
        const uint8_t dt_mul = shadow[0x40 + base];
        o.mult = dt_mul & 15;
        o.dt = RegToDt((dt_mul >> 4) & 7);
        const uint8_t tl = shadow[0x60 + base];
        o.tl = tl & 127;
        o.ksr = (tl >> 7) & 1;
        const uint8_t ks_ar = shadow[0x80 + base];
        o.ar = ks_ar & 31;
        o.rs = (ks_ar >> 6) & 3;
        const uint8_t am_dr = shadow[0xA0 + base];
        o.dr = am_dr & 31;
        o.am = (am_dr >> 7) & 1;
        const uint8_t dt2_d2r = shadow[0xC0 + base];
        o.d2r = dt2_d2r & 31;
        o.dt2 = (dt2_d2r >> 6) & 3;
        const uint8_t sl_rr = shadow[0xE0 + base];
        o.rr = sl_rr & 15;
        o.sl = (sl_rr >> 4) & 15;
        o.enable = true;
    }
    return v;
}

void FuiToOpmShadow(const FuiVoice& v, uint8_t shadow[256], int ch) {
    const int alg = v.alg & 7;
    const int fb = v.fb & 7;
    // 保留原来的 L/R 位（pan），只改 fb/alg
    shadow[0x20 + ch] = static_cast<uint8_t>((shadow[0x20 + ch] & 0xC0) | (fb << 3) | alg);
    shadow[0x38 + ch] = static_cast<uint8_t>(((v.fms & 7) << 4) | (v.ams & 3));

    for (int i = 0; i < 4; i++) {
        const int base = i * 8 + ch;
        const FuiOperator& o = v.op[i];
        shadow[0x40 + base] = static_cast<uint8_t>((DtToReg(o.dt) << 4) | (o.mult & 15));
        shadow[0x60 + base] = static_cast<uint8_t>((o.ksr ? 0x80 : 0) | (o.tl & 127));
        shadow[0x80 + base] = static_cast<uint8_t>(((o.rs & 3) << 6) | (o.ar & 31));
        shadow[0xA0 + base] = static_cast<uint8_t>(((o.am ? 1 : 0) << 7) | (o.dr & 31));
        shadow[0xC0 + base] = static_cast<uint8_t>(((o.dt2 & 3) << 6) | (o.d2r & 31));
        shadow[0xE0 + base] = static_cast<uint8_t>(((o.sl & 15) << 4) | (o.rr & 15));
    }
}

// ─────────────────────────── OPLL 用户音色 ⇄ FuiVoice ───────────────────────────
// 依据 Furnace platform/opll.cpp:453-466（opllPreset==0 时的 0x00-0x07 打包）。
FuiVoice OpllUserVoiceToFui(const uint8_t shadow[256]) {
    FuiVoice v;
    v.type = kFuiInsOpll;
    v.ops = 2;
    v.name = "user";
    v.opllPreset = 0;

    const uint8_t r0 = shadow[0x00], r1 = shadow[0x01], r2 = shadow[0x02], r3 = shadow[0x03];
    const uint8_t r4 = shadow[0x04], r5 = shadow[0x05], r6 = shadow[0x06], r7 = shadow[0x07];

    v.op[0].am = (r0 >> 7) & 1;
    v.op[0].vib = (r0 >> 6) & 1;
    v.op[0].ssgEnv = ((r0 >> 3) & 1) ? 8 : 0;
    v.op[0].ksr = (r0 >> 4) & 1;
    v.op[0].mult = r0 & 15;
    v.op[0].ksl = (r2 >> 6) & 3;
    v.op[0].tl = r2 & 63;
    v.op[0].ar = (r4 >> 4) & 15;
    v.op[0].dr = r4 & 15;
    v.op[0].sl = (r6 >> 4) & 15;
    v.op[0].rr = r6 & 15;

    v.op[1].am = (r1 >> 7) & 1;
    v.op[1].vib = (r1 >> 6) & 1;
    v.op[1].ssgEnv = ((r1 >> 3) & 1) ? 8 : 0;
    v.op[1].ksr = (r1 >> 4) & 1;
    v.op[1].mult = r1 & 15;
    v.op[1].ksl = (r3 >> 6) & 3;
    v.op[1].tl = 0;  // 载波 TL 在运行时由 0x30 低 4 位给
    v.op[1].ar = (r5 >> 4) & 15;
    v.op[1].dr = r5 & 15;
    v.op[1].sl = (r7 >> 4) & 15;
    v.op[1].rr = r7 & 15;

    v.fms = (r3 >> 4) & 1;
    v.ams = (r3 >> 3) & 1;
    v.fb = r3 & 7;
    v.alg = 0;
    return v;
}

void FuiToOpllUserVoice(const FuiVoice& v, uint8_t shadow[256]) {
    const FuiOperator& m = v.op[0];
    const FuiOperator& c = v.op[1];
    shadow[0x00] = static_cast<uint8_t>((m.am << 7) | (m.vib << 6) |
                                        (((m.ssgEnv & 8) ? 1 : 0) << 3) | ((m.ksr & 1) << 4) |
                                        (m.mult & 15));
    shadow[0x01] = static_cast<uint8_t>((c.am << 7) | (c.vib << 6) |
                                        (((c.ssgEnv & 8) ? 1 : 0) << 3) | ((c.ksr & 1) << 4) |
                                        (c.mult & 15));
    shadow[0x02] = static_cast<uint8_t>(((m.ksl & 3) << 6) | (m.tl & 63));
    shadow[0x03] = static_cast<uint8_t>(((c.ksl & 3) << 6) | ((v.fms & 1) << 4) |
                                        ((v.ams & 1) << 3) | (v.fb & 7));
    shadow[0x04] = static_cast<uint8_t>(((m.ar & 15) << 4) | (m.dr & 15));
    shadow[0x05] = static_cast<uint8_t>(((c.ar & 15) << 4) | (c.dr & 15));
    shadow[0x06] = static_cast<uint8_t>(((m.sl & 15) << 4) | (m.rr & 15));
    shadow[0x07] = static_cast<uint8_t>(((c.sl & 15) << 4) | (c.rr & 15));
}

}  // namespace vgmstudio
