// FuiVoice —— Furnace `.fui` 乐器文件的读写 + 宏执行。
//
// 为什么不自己解析 FUI：
//   FUI 是 Furnace 的二进制格式（"FINS" 容器 + 特性块 NA/FM/MA/...），
//   版本号 DIV_ENGINE_VERSION 会随 Furnace 升级变化，自己解析必然跟不上。
//   所以这里**直接链接 Furnace 的 instrument.cpp**（见 third_party/furnace/README.md），
//   用它的 DivInstrument::putInsData2 / readInsData 做全部编解码。
//
// ★ 宏速率不在 .fui 里
//   .fui 只存每个宏自己的 speed / delay（每步间隔多少个 tick），
//   而「一秒有多少 tick」来自 .fur 工程的 divSong::hz（Furnace 里叫 tickRate，默认 60）。
//   Furnace 的宏解释器 DivMacroInt::next() 靠宿主引擎的 tick 推动，
//   乐器文件本身不定义中断速率。因此本模块的 TickRate 必须由调用方显式给出
//   （CLI 的 --tick-rate / --tick-hz），否则宏的绝对时长无从确定。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vgmstudio {

// ── 宏（对应 DivInstrumentMacro，字段布局见 furnace/src/engine/instrument.h:306）──
// mode 字段在 FUI 里叫 open 的 bit1..2，这里拆成 Furnace 的三态：
//   type 0 = 序列, 1 = ADSR, 2 = LFO
struct Macro {
    std::vector<int> val;  // 序列模式的值；ADSR/LFO 模式是参数表（见下）
    int type = 0;
    // Furnace 的 DivInstrumentMacro::mode。pitch 宏：0=绝对值，1=累加增量
    // （arcade.cpp:344-352 / opll.cpp:189-197）。arp 宏在 v112 之后固定为 0。
    int mode = 0;
    int loop = 255;
    int rel = 255;
    int delay = 0;
    int speed = 1;
    bool linger = false;       // 音量宏：结束时停在最后值，不回零
    bool activeRelease = false;
};

// macroType 常量，与 furnace instrument.h:110 一致
enum MacroType {
    kMacroVol = 0,
    kMacroArp = 1,
    kMacroDuty = 2,
    kMacroWave = 3,
    kMacroPitch = 4,
    kMacroEx1 = 5, kMacroEx2 = 6, kMacroEx3 = 7,
    kMacroAlg = 8,
    kMacroFb = 9,
    kMacroFms = 10,
    kMacroAms = 11,
    kMacroPanL = 12,
    kMacroPanR = 13,
    kMacroPhaseReset = 14,
    kMacroEx4 = 15, kMacroEx5 = 16,
    kMacroEx6 = 17, kMacroEx7 = 18,
    kMacroEx8 = 19, kMacroEx9 = 20, kMacroEx10 = 21,
    kMacroCount = 22,
};

// ── 一个运算符（对应 DivInstrumentFM::Operator）──
struct FuiOperator {
    bool enable = true;
    int am = 0, ar = 0, dr = 0, mult = 0, rr = 0, sl = 0, tl = 0;
    int dt2 = 0, rs = 0, dt = 0, d2r = 0, ssgEnv = 0;
    int dam = 0, dvb = 0, egt = 0, ksl = 0, sus = 0, vib = 0, ws = 0, ksr = 0;
    int kvs = 2;
};

// 算子级宏的类型（对应 instrument.h:135 的 DIV_MACRO_OP_*，值 32..51）
// 索引 = macroType - 32
enum OpMacroType {
    kOpAm = 0,
    kOpAr = 1,
    kOpDr = 2,
    kOpMult = 3,
    kOpRr = 4,
    kOpSl = 5,
    kOpTl = 6,
    kOpDt2 = 7,
    kOpRs = 8,
    kOpDt = 9,
    kOpD2r = 10,
    kOpSsg = 11,
    kOpDam = 12,
    kOpDvb = 13,
    kOpEgt = 14,
    kOpKsl = 15,
    kOpSus = 16,
    kOpVib = 17,
    kOpWs = 18,
    kOpKsr = 19,
    kOpMacroCount = 20,
};

// ── 一个 FM 音色（对应 DivInstrument + DivInstrumentFM + 22 条通道宏 + 4×20 条算子宏）──
struct FuiVoice {
    int type = 0;              // Furnace DivInstrumentType：13=OPLL, 14=OPL, 33=OPM
    std::string name;
    int alg = 0, fb = 0, fms = 0, ams = 0, ops = 4, opllPreset = 0, block = 0;
    bool fmsLFO = false, amsLFO = false, tremLFO = false;
    FuiOperator op[4];
    Macro macros[kMacroCount];              // MA 块：22 条通道级宏
    Macro opMacros[4][kOpMacroCount];       // O1..O4 块：每算子 20 条
};

// Furnace DIV_INS_* 里我们用到的三个
constexpr int kFuiInsOpll = 13;
constexpr int kFuiInsOpl = 14;
constexpr int kFuiInsOpm = 33;

// ── FUI 文件读写（走 Furnace 的 instrument.cpp）──
bool SaveFui(const FuiVoice& v, const std::string& path, std::string* err);
bool LoadFui(const std::string& path, FuiVoice* out, std::string* err);

// 内存版（不落盘，便于测试）
std::vector<uint8_t> EncodeFui(const FuiVoice& v);
bool DecodeFui(const uint8_t* data, size_t len, FuiVoice* out, std::string* err);

// 同上，但按指定 Furnace 引擎版本解码。读 .fur 时必须用工程里记录的版本，
// 否则 instrument.cpp 里那些 `if (version<245)` 之类的兼容转换会走错分支。
bool DecodeFuiVersioned(const uint8_t* data, size_t len, int version, FuiVoice* out,
                        std::string* err);

// ── 宏执行 ──────────────────────────────────────────────────────────
// 完整实现 Furnace 的三种宏模式（序列 / ADSR / LFO），
// 逐行对应 furnace/src/engine/macroInt.cpp:83-296 的 DivMacroStruct::doMacro。
//
// 速率说明：每执行一次 Tick() 算一个 tick。
//   一个宏每步之间隔 (speed) 个 tick（首个步额外等待 delay 个 tick）。
//   而「一秒有多少 tick」= 调用方给的 tickRate（Furnace 的 song.hz，默认 60）。
//   所以一个宏步的绝对时长 = speed / tickRate 秒。.fui 里没有 tickRate。
class MacroRunner {
  public:
    void Start(const Macro& m, int tickRate);
    bool Running() const { return running_; }
    int CurrentValue() const { return val_; }
    bool Changed() const { return changed_; }   // 本次 Tick 是否产生新值
    void Release();                             // 音符 key-off：跳到 rel 或进入 release
    void Tick();                                // 推进 1 tick
    int TickRate() const { return tickRate_; }
    double TickSeconds() const { return tickRate_ > 0 ? 1.0 / tickRate_ : 1.0 / 60.0; }

  private:
    Macro m_;
    int type_ = 0;
    int pos_ = 0, lastPos_ = 0, delay_ = 0, val_ = 0;
    int tickRate_ = 60;
    bool running_ = false, begun_ = true, released_ = false, lfoDir_ = false;
    bool changed_ = false;
};

// ── 寄存器 ⇄ FuiVoice 的语义映射（不依赖 Furnace 运行时）────────────
// OPM：影子寄存器 0x20/0x38/0x40/0x60/0x80/0xA0/0xC0/0xE0
//   op 索引 i 对应寄存器 0x40 + i*8（i=0 M1, 1 M2, 2 C1, 3 C2）
//   （依据 Furnace arcade.cpp:562-602 commitState 与 opOffs={0,8,16,24}）
FuiVoice OpmShadowToFui(const uint8_t shadow[256], int ch);
void FuiToOpmShadow(const FuiVoice& v, uint8_t shadow[256], int ch);

// OPLL：只有用户音色槽（0x00-0x07）能承载完整参数；preset≠0 时参数来自 ROM。
FuiVoice OpllUserVoiceToFui(const uint8_t shadow[256]);
void FuiToOpllUserVoice(const FuiVoice& v, uint8_t shadow[256]);

}  // namespace vgmstudio
