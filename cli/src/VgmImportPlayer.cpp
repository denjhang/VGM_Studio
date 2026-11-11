// VgmImportPlayer.cpp —— 用 libvgm 完整解析 VGM，并把寄存器写接出来。
//
// 解析完全由 libvgm 完成：VGMPlayer::Render() → ParseFile() → _CMD_INFO[0x100]
// → Cmd_*()。我们一行 VGM 命令解析都不写（RULES.md E1/F2）。
//
// 唯一的注入点是"影子设备"：给文件里出现的每个 DEV_ID 注册一个假设备，
// Start/Update 返回静音，write8 把 (port, reg, val) 接住。
// 时间戳取 Tick2Sample(_fileTick)——写入时刻正是当前命令的执行时刻，
// 与 libvgm 自己的时间模型完全一致。
#include "VgmImportPlayer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "emu/EmuStructs.h"
#include "emu/SoundDevs.h"
#include "emu/SoundEmu.h"
#include "player/vgmplayer.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"

namespace vgmstudio {

class ShadowPlayer;  // 前置声明：影子回调要问它"现在第几采样"

namespace {

// 单线程使用，全局指针足够（不支持并发导入同一个库）。
ShadowPlayer* g_player = nullptr;
RegStream* g_sink = nullptr;
uint32_t g_order = 0;
uint8_t g_pendingReg = 0;
bool g_haveReg = false;

struct ShadowCtx {
    uint8_t vgmChipType = 0;
    uint8_t chipId = 0;
};

constexpr size_t kMaxShadowDevs = 16;

const char* ShadowName(const DEV_GEN_CFG*) { return "VGM Studio Shadow"; }
UINT16 ShadowChannels(const DEV_GEN_CFG*) { return 8; }
const char** ShadowChannelsNames(const DEV_GEN_CFG*) { return nullptr; }
const DEVLINK_IDS* ShadowLinkIds(const DEV_GEN_CFG*) { return nullptr; }

uint32_t CurrentSampleOfPlayer();

void ShadowWrite8(void* info, UINT8 addr, UINT8 data) {
    const ShadowCtx* c = static_cast<const ShadowCtx*>(info);
    if (c == nullptr || g_sink == nullptr) return;
    if ((addr & 0x01) == 0x00) {  // 地址口 = 寄存器号
        g_pendingReg = data;
        g_haveReg = true;
        return;
    }
    if (!g_haveReg) return;  // 没有地址就来了数据：忽略
    RegWrite w;
    w.atSample = CurrentSampleOfPlayer();
    w.order = g_order++;
    w.chipType = c->vgmChipType;
    w.chipId = c->chipId;
    w.port = static_cast<uint8_t>(addr >> 1);
    w.reg = g_pendingReg;
    w.val = data;
    g_sink->push_back(w);
    g_haveReg = false;
}

const DEVDEF_RWFUNC g_shadowRwFuncs[] = {
    {static_cast<UINT8>(RWF_REGISTER | RWF_WRITE), DEVRW_A8D8, 0,
     reinterpret_cast<void*>(&ShadowWrite8)},
    {0, 0, 0, nullptr},
};

void ShadowStop(void*) {}
void ShadowReset(void*) {
    g_pendingReg = 0;
    g_haveReg = false;
}
void ShadowUpdate(void*, UINT32 samples, DEV_SMPL** outputs) {
    if (outputs == nullptr) return;
    for (int ch = 0; ch < 2; ch++) {
        if (outputs[ch] != nullptr) std::memset(outputs[ch], 0, samples * sizeof(DEV_SMPL));
    }
}

DEV_DEF g_shadowDefs[kMaxShadowDevs];

ShadowCtx g_shadowCtx[kMaxShadowDevs];

template <size_t I>
ShadowCtx* ShadowCtxOf() {
    return &g_shadowCtx[I];
}

// 每个下标一个 Start，好用不同 ctx。devDef 必须在这里回填：
// libvgm 的 SndEmu_StartCore 不会替设备填它，真实核都靠 INIT_DEVINF 宏设置。
template <size_t I>
UINT8 ShadowStartT(const DEV_GEN_CFG*, DEV_INFO* retDevInf) {
    retDevInf->dataPtr = reinterpret_cast<DEV_DATA*>(ShadowCtxOf<I>());
    retDevInf->devDef = &g_shadowDefs[I];
    retDevInf->sampleRate = 0;  // 0 → Resmpl_Init 走空指针分支
    retDevInf->linkDevCount = 0;
    retDevInf->linkDevs = nullptr;
    return EERR_OK;
}

using ShadowStartFn = UINT8 (*)(const DEV_GEN_CFG*, DEV_INFO*);
const ShadowStartFn kShadowStarts[kMaxShadowDevs] = {
    ShadowStartT<0>,  ShadowStartT<1>,  ShadowStartT<2>,  ShadowStartT<3>,
    ShadowStartT<4>,  ShadowStartT<5>,  ShadowStartT<6>,  ShadowStartT<7>,
    ShadowStartT<8>,  ShadowStartT<9>,  ShadowStartT<10>, ShadowStartT<11>,
    ShadowStartT<12>, ShadowStartT<13>, ShadowStartT<14>, ShadowStartT<15>,
};

// DEV_DECL 末尾是柔性数组 cores[]，C++ 不能把它放在别的结构体中间，
// 所以用对齐的原始缓冲区手工布局：DEV_DECL 在前，cores 两格紧随其后。
struct ShadowDecl {
    alignas(DEV_DECL) unsigned char buf[sizeof(DEV_DECL) + 2 * sizeof(const DEV_DEF*)];
    DEV_DECL* decl() { return reinterpret_cast<DEV_DECL*>(buf); }
    const DEV_DEF** cores() {
        return reinterpret_cast<const DEV_DEF**>(buf + sizeof(DEV_DECL));
    }
};
ShadowDecl g_shadowDecls[kMaxShadowDevs];
const DEV_DECL* g_userDevList[kMaxShadowDevs + 1];
size_t g_shadowCount = 0;

std::string ChipName(uint8_t vgmType);
std::string CommandLabel(UINT8 cmd);

}  // namespace

// ───────────────────── VGMPlayer 子类 ─────────────────────
class ShadowPlayer : public VGMPlayer {
  public:
    ~ShadowPlayer() override { Release(); }

    bool Init(const std::string& path, std::string* err) {
        loader_ = FileLoader_Init(path.c_str());
        if (loader_ == nullptr) {
            *err = "cannot open file";
            return false;
        }
        DataLoader_SetPreloadBytes(loader_, 0x100);
        if (DataLoader_Load(loader_)) {
            *err = "cannot load file";
            return false;
        }
        SetSampleRate(44100);
        if (LoadFile(loader_)) {
            *err = "not a VGM file";
            return false;
        }
        return InstallShadowDevices(err);
    }

    uint32_t CurrentSample() const { return Tick2Sample(_fileTick); }

    // 完整跑一遍：命令解析全部交给 libvgm 的 Render→ParseFile。
    void RenderOnce(RegStream* sink) {
        g_player = this;
        g_sink = sink;
        g_order = 0;
        g_haveReg = false;
        SetEventCallback(&StopOnLoop, this);
        Start();
        std::vector<WAVE_32BS> buf(kRenderChunk);
        uint64_t guard = 0;
        while (!(GetState() & PLAYSTATE_END)) {
            Render(kRenderChunk, buf.data());
            if (++guard > 400000) break;  // 保险丝
        }
        Stop();
        SetEventCallback(nullptr, nullptr);
        g_player = nullptr;
        g_sink = nullptr;
    }

    std::vector<ChipInfo> Chips() const {
        std::vector<ChipInfo> out;
        for (const SONG_DEV_CFG& sd : _devCfgs) {
            ChipInfo ci;
            ci.vgmChipType = sd.vgmChipType;
            ci.clock = GetChipClock(sd.vgmChipType, sd.instance);
            ci.name = ChipName(sd.vgmChipType);
            const DEV_GEN_CFG* cfg = reinterpret_cast<const DEV_GEN_CFG*>(&sd.cfgData[0]);
            const DEV_DECL* decl = SndEmu_GetDevDecl(sd.type, nullptr, 0);
            ci.channelCount = (decl != nullptr) ? decl->channelCount(cfg) : 0;
            out.push_back(ci);
        }
        return out;
    }

    const VGM_HEADER* Header() const { return GetFileHeader(); }

    // 命令统计：遍历时用 libvgm 自己的 _CMD_INFO[] 取长度，不解析命令语义。
    std::vector<CommandStat> CommandStats() const {
        std::vector<CommandStat> stats;
        if (_fileData == nullptr) return stats;
        uint32_t pos = _fileHdr.dataOfs;
        const uint32_t end = _fileHdr.dataEnd;
        while (pos < end) {
            const UINT8 cmd = _fileData[pos];
            AddCount(&stats, cmd);
            uint32_t len = _CMD_INFO[cmd].cmdLen;
            if (cmd == 0x67) len = 0x07 + ReadDblkLen(pos);
            if (cmd == 0x66 || len == 0) break;
            pos += len;
        }
        return stats;
    }

    // 没有处理函数的命令（诊断账本）
    std::vector<std::string> UnhandledCommands() const {
        std::vector<std::string> out;
        if (_fileData == nullptr) return out;
        uint32_t pos = _fileHdr.dataOfs;
        const uint32_t end = _fileHdr.dataEnd;
        while (pos < end) {
            const UINT8 cmd = _fileData[pos];
            if (_CMD_INFO[cmd].func == nullptr) out.push_back(CommandLabel(cmd));
            uint32_t len = _CMD_INFO[cmd].cmdLen;
            if (cmd == 0x67) len = 0x07 + ReadDblkLen(pos);
            if (cmd == 0x66 || len == 0) break;
            pos += len;
        }
        return out;
    }

    void Release() {
        if (loader_ != nullptr) {
            UnloadFile();
            DataLoader_Deinit(loader_);
            loader_ = nullptr;
        }
    }

  private:
    static UINT8 StopOnLoop(PlayerBase* /*player*/, void* /*param*/, UINT8 evtType,
                            void* /*evtParam*/) {
        return (evtType == PLREVT_LOOP) ? 0x01 : 0x00;  // 循环前停下，只跑一遍
    }

    UINT32 ReadDblkLen(uint32_t pos) const {
        UINT32 v = static_cast<UINT32>(_fileData[pos + 3]) |
                   (static_cast<UINT32>(_fileData[pos + 4]) << 8) |
                   (static_cast<UINT32>(_fileData[pos + 5]) << 16) |
                   (static_cast<UINT32>(_fileData[pos + 6]) << 24);
        return v & 0x7FFFFFFFu;
    }

    static void AddCount(std::vector<CommandStat>* stats, UINT8 cmd) {
        for (CommandStat& s : *stats) {
            if (s.cmd == cmd) {
                s.count++;
                return;
            }
        }
        CommandStat s;
        s.cmd = cmd;
        s.label = CommandLabel(cmd);
        s.count = 1;
        stats->push_back(s);
    }

    bool InstallShadowDevices(std::string* err) {
        g_shadowCount = 0;
        for (const SONG_DEV_CFG& sd : _devCfgs) {
            if (g_shadowCount >= kMaxShadowDevs) {
                *err = "too many chips (shadow device pool exhausted)";
                return false;
            }
            const size_t idx = g_shadowCount++;

            // 影子上下文必须带上这张 VGM 芯片类型，否则写出来后无法分辨来自哪颗芯片
            g_shadowCtx[idx].vgmChipType = sd.vgmChipType;
            g_shadowCtx[idx].chipId = sd.instance;

            DEV_DEF& def = g_shadowDefs[idx];
            std::memset(&def, 0, sizeof(def));
            def.name = "VGM Studio Shadow";
            def.author = "VGM Studio";
            def.coreID = 0x56475348u;  // 'VGSH'
            def.Start = kShadowStarts[idx];
            def.Stop = ShadowStop;
            def.Reset = ShadowReset;
            def.Update = ShadowUpdate;
            def.rwFuncs = g_shadowRwFuncs;

            ShadowDecl& st = g_shadowDecls[idx];
            std::memset(&st, 0, sizeof(st));
            DEV_DECL* d = st.decl();
            d->deviceID = sd.type;
            d->name = ShadowName;
            d->channelCount = ShadowChannels;
            d->channelNames = ShadowChannelsNames;
            d->linkDevIDs = ShadowLinkIds;
            const DEV_DEF** cores = st.cores();
            cores[0] = &def;
            cores[1] = nullptr;
            g_userDevList[idx] = d;
        }
        g_userDevList[g_shadowCount] = nullptr;
        SetUserDevices(g_userDevList, 0);
        return true;
    }

    static constexpr UINT32 kRenderChunk = 4096;
    DATA_LOADER* loader_ = nullptr;
};

namespace {

uint32_t CurrentSampleOfPlayer() {
    return (g_player != nullptr) ? g_player->CurrentSample() : 0;
}

std::string ChipName(uint8_t vgmType) {
    switch (vgmType) {
        case 0x00: return "SN76489";
        case 0x01: return "YM2413";
        case 0x02: return "YM2612";
        case 0x03: return "YM2151";
        case 0x04: return "SegaPCM";
        case 0x05: return "RF5C68";
        case 0x06: return "YM2203";
        case 0x07: return "YM2608";
        case 0x08: return "YM2610";
        case 0x09: return "YM3812";
        case 0x0A: return "YM3526";
        case 0x0B: return "Y8950";
        case 0x0C: return "YMF262";
        case 0x0D: return "YMF278B";
        case 0x0E: return "YMF271";
        case 0x0F: return "YMZ280B";
        case 0x10: return "RF5C164";
        case 0x11: return "PWM";
        case 0x12: return "AY8910";
        case 0x13: return "GB_DMG";
        case 0x14: return "NES_APU";
        case 0x15: return "YMW258";
        case 0x16: return "uPD7759";
        case 0x17: return "MSM6258";
        case 0x18: return "MSM6295";
        case 0x19: return "K051649";
        case 0x1A: return "K054539";
        case 0x1B: return "HuC6280";
        case 0x1C: return "C140";
        case 0x1D: return "K053260";
        case 0x1E: return "Pokey";
        case 0x1F: return "QSound";
        case 0x20: return "SCSP";
        case 0x21: return "WonderSwan";
        case 0x22: return "VSU";
        case 0x23: return "SAA1099";
        case 0x24: return "ES5503";
        case 0x25: return "ES5506";
        case 0x26: return "X1-010";
        case 0x27: return "C352";
        case 0x28: return "GA20";
        default: break;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "VGM-chip-0x%02X", vgmType);
    return buf;
}

std::string CommandLabel(UINT8 cmd) {
    char buf[48];
    if (cmd >= 0x70 && cmd <= 0x7F) {
        std::snprintf(buf, sizeof(buf), "0x%02X wait %u", cmd, (cmd & 0x0F) + 1);
        return buf;
    }
    switch (cmd) {
        case 0x50: return "0x50 SN76489 write";
        case 0x51: return "0x51 YM2413 write";
        case 0x52: return "0x52 YM2612 write p0";
        case 0x53: return "0x53 YM2612 write p1";
        case 0x54: return "0x54 YM2151 write";
        case 0x55: return "0x55 YM2203 write";
        case 0x56: return "0x56 YM2608 write p0";
        case 0x57: return "0x57 YM2608 write p1";
        case 0x58: return "0x58 YM2610 write p0";
        case 0x59: return "0x59 YM2610 write p1";
        case 0x5A: return "0x5A YM3812 write";
        case 0x5B: return "0x5B YM3526 write";
        case 0x5C: return "0x5C Y8950 write";
        case 0x5D: return "0x5D YMZ280B write";
        case 0x5E: return "0x5E YMF262 write p0";
        case 0x5F: return "0x5F YMF262 write p1";
        case 0x61: return "0x61 wait N samples";
        case 0x62: return "0x62 wait 735";
        case 0x63: return "0x63 wait 882";
        case 0x66: return "0x66 end";
        case 0x67: return "0x67 data block";
        case 0x68: return "0x68 PCM RAM write";
        case 0x90: return "0x90 DAC stream setup";
        case 0x91: return "0x91 DAC stream data";
        case 0x92: return "0x92 DAC stream freq";
        case 0x93: return "0x93 DAC stream play";
        case 0x94: return "0x94 DAC stream stop";
        case 0x95: return "0x95 DAC stream play(id)";
        case 0xA0: return "0xA0 AY8910 write";
        default: break;
    }
    std::snprintf(buf, sizeof(buf), "0x%02X", cmd);
    return buf;
}

void FillResult(ShadowPlayer& p, ImportResult& res, bool wantWrites) {
    const VGM_HEADER* h = p.Header();
    res.header.fileVer = h->fileVer;
    res.header.dataOffset = h->dataOfs;
    res.header.eofOffset = h->eofOfs;
    res.header.totalSamples = h->numTicks;
    res.header.loopOffset = h->loopOfs;
    res.header.loopSamples = h->loopTicks;
    res.header.recordHz = h->recordHz;
    res.chips = p.Chips();
    res.commands = p.CommandStats();
    res.unhandled = p.UnhandledCommands();
    if (wantWrites) p.RenderOnce(&res.writes);
    res.ok = true;
}

}  // namespace

ImportResult ProbeVgm(const std::string& path) {
    ImportResult res;
    ShadowPlayer player;
    if (!player.Init(path, &res.error)) return res;
    FillResult(player, res, false);
    player.Release();
    return res;
}

ImportResult ImportVgm(const std::string& path) {
    ImportResult res;
    ShadowPlayer player;
    if (!player.Init(path, &res.error)) return res;
    FillResult(player, res, true);
    player.Release();
    return res;
}

}  // namespace vgmstudio
