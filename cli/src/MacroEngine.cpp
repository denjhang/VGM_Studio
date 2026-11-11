// MacroEngine.cpp —— 全部宏的展开（通道级 22 条 + 算子级 4×20 条）。
//
// 逐行对应：
//   OPM : furnace/src/engine/platform/arcade.cpp:292-480  DivPlatformArcade::tick()
//   OPLL: furnace/src/engine/platform/opll.cpp:150-340    DivPlatformOPLL::tick()
//   dt  : furnace/src/engine/platform/fmsharedbase.h:42   dtTable[8]={7,6,5,0,1,2,3,4}
//   音量: furnace/src/engine/dispatch.h:1747-1750        VOL_SCALE_LOG / _LINEAR / _BROKEN
#include "MacroEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace vgmstudio {
namespace {

// fmsharedbase.h:42 —— 内部 dt ⇄ OPM 寄存器 DT1
constexpr int kDtTable[8] = {7, 6, 5, 0, 1, 2, 3, 4};

// fmsharedbase.h:25-35 —— ALG → 哪些算子是载波（bit 顺序 1,3,2,4）
constexpr bool kIsOutput[8][4] = {
    {false, false, false, true},
    {false, false, false, true},
    {false, false, false, true},
    {false, false, false, true},
    {false, false, true,  true},
    {false, true,  true,  true},
    {false, true,  true,  true},
    {true,  true,  true,  true},
};

// dispatch.h:1748-1750。Furnace 的 compatFlags.newVolumeScaling 默认 true（song.cpp:1165），
// 于是 VOL_SCALE_LOG_BROKEN = VOL_SCALE_LOG = CLAMP(x+y-range, 0, range)。
inline int VolScaleLog(int x, int y, int range) {
    const int v = x + y - range;
    return v < 0 ? 0 : (v > range ? range : v);
}

inline int DtToReg(int dt) { return kDtTable[dt & 7]; }

// 单条宏的执行状态（对应 macroInt.h:52-96 的 DivMacroStruct）
struct MacroState {
    int pos = 0, lastPos = 0, delay = 0, val = 0;
    bool began = true, released = false, lfoDir = false, running = false;
    const Macro* m = nullptr;
    int type = 0;

    void Init(const Macro& macro) {
        m = &macro;
        type = macro.type;
        pos = lastPos = delay = 0;
        val = 0;
        began = true;
        released = false;
        lfoDir = false;
        running = !macro.val.empty();
        const int n = static_cast<int>(macro.val.size());
        if (type == 1 && n > 1) {
            // macroInt.cpp:53-58 —— ADSR 的 pos 从 bottom 起，反向时补 0xff
            pos = macro.val[0] << 8;
            if (macro.val[0] > macro.val[1]) pos |= 0xff;
        } else if (type == 2 && n > 13) {
            // macroInt.cpp:60-79 —— LFO 的初始相位
            const int low = macro.val[0], high = macro.val[1];
            const int bottom = low << 8, top = (high << 8) | 0xff;
            const int wave = macro.val[12] & 3;
            const int phase = macro.val[13];
            if (wave == 0) {
                pos = (phase & 512) ? (top + (((bottom - top) * (phase & 511)) >> 9))
                                    : (bottom + (((top - bottom) * phase) >> 9));
                lfoDir = (phase & 512) != 0;
            } else if (wave == 1) {
                pos = bottom + (((top - bottom) * phase) >> 10);
            } else {
                pos = phase << 6;
            }
        }
    }

    void Release() {
        released = true;
        if (type == 1 && lastPos < 3) delay = 0;
        if (type == 0 && m != nullptr && m->rel < static_cast<int>(m->val.size()) &&
            m->activeRelease) {
            delay = 0;
            pos = m->rel;
        }
    }

    // macroInt.cpp:83-296 的 doMacro。返回 true = 本 tick 产生了新值。
    bool Tick() {
        if (!running || m == nullptr) return false;
        if (delay > 0) {
            delay--;
            return false;
        }
        delay = (began && m->delay > 0) ? m->delay : (m->speed > 0 ? m->speed - 1 : 0);
        if (began) began = false;

        const int len = static_cast<int>(m->val.size());
        if (len == 0) {
            running = false;
            return false;
        }

        if (type == 0) {  // 序列
            lastPos = pos;
            val = m->val[pos++];
            if (pos > m->rel && !released) {
                if (m->loop < len && m->loop < m->rel) {
                    pos = m->loop;
                } else {
                    pos--;
                }
            }
            if (pos >= len) {
                if (m->loop < len && (m->loop >= m->rel || m->rel >= len)) {
                    pos = m->loop;
                } else if (m->linger) {
                    pos--;
                } else {
                    running = false;
                }
            }
            return true;
        }

        const int low = m->val[0];
        const int high = len > 1 ? m->val[1] : 0;
        const bool inverted = low > high;
        const int bottom = low << 8;
        const int top = (high << 8) | 0xff;
        const int bottomInv = (low << 8) | 0xff;
        const int topInv = high << 8;
        auto arg = [&](int i) { return i < len ? m->val[i] : 0; };

        if (type == 1) {  // ADSR
            if (released && lastPos < 3) lastPos = 3;
            const int ar = arg(2), ht = arg(3), dr = arg(4), sl = arg(5);
            const int st = arg(6), sr = arg(7), rr = arg(8);
            const int sus = (sl << 8) | 0xff;
            const int susInv = sl << 8;
            if (inverted) {
                switch (lastPos) {
                    case 0: pos -= ar; if (pos <= topInv) { pos = topInv; lastPos = 1; delay = ht; } break;
                    case 1: pos += dr; if (pos >= susInv) { pos = susInv; lastPos = 2; delay = st; } break;
                    case 2: pos += sr; if (pos >= bottomInv) { pos = bottomInv; lastPos = 4; } break;
                    case 3: pos += rr; if (pos >= bottomInv) { pos = bottomInv; lastPos = 4; } break;
                    default: pos = bottomInv; if (!m->linger) running = false; break;
                }
            } else {
                switch (lastPos) {
                    case 0: pos += ar; if (pos >= top) { pos = top; lastPos = 1; delay = ht; } break;
                    case 1: pos -= dr; if (pos <= sus) { pos = sus; lastPos = 2; delay = st; } break;
                    case 2: pos -= sr; if (pos <= bottom) { pos = bottom; lastPos = 4; } break;
                    case 3: pos -= rr; if (pos <= bottom) { pos = bottom; lastPos = 4; } break;
                    default: pos = bottom; if (!m->linger) running = false; break;
                }
            }
            val = pos >> 8;
            return true;
        }

        // type == 2：LFO
        const int speed = arg(11);
        const int wave = arg(12) & 3;
        if (inverted) {
            switch (wave) {
                case 0:
                    if (lfoDir) {
                        pos += speed;
                        if (pos > bottomInv) { lfoDir = false; pos -= speed * 2; }
                    } else {
                        pos -= speed;
                        if (pos < topInv) { lfoDir = true; pos += speed * 2; }
                    }
                    break;
                case 1:
                    pos -= speed;
                    if (pos < topInv) pos += (bottomInv - topInv);
                    break;
                default:
                    pos = (pos + speed) & 65535;
                    val = (pos & 32768) ? high : low;
                    return true;
            }
        } else {
            switch (wave) {
                case 0:
                    if (lfoDir) {
                        pos -= speed;
                        if (pos < bottom) { lfoDir = false; pos += speed * 2; }
                    } else {
                        pos += speed;
                        if (pos > top) { lfoDir = true; pos -= speed * 2; }
                    }
                    break;
                case 1:
                    pos += speed;
                    if (pos > top) pos -= (top - bottom);
                    break;
                default:
                    pos = (pos + speed) & 65535;
                    val = (pos & 32768) ? high : low;
                    return true;
            }
        }
        val = pos >> 8;
        return true;
    }
};

}  // namespace

MacroExpandOutput ExpandMacros(const FuiVoice& voice, const MacroExpandInput& in) {
    MacroExpandOutput out;
    const int ch = in.ch;
    const uint32_t dur = in.endSample > in.startSample ? in.endSample - in.startSample : 1;
    const double samplesPerTick =
        static_cast<double>(in.sampleRate) / (in.tickRate > 0 ? in.tickRate : 60);
    const int ticks = static_cast<int>(dur / samplesPerTick) + 1;

    // 通道状态（对应 Furnace 的 chan[i].state / chVolL / chVolR）
    int curAlg = in.alg & 7, curFb = in.fb & 7, curFms = in.fms & 7, curAms = in.ams & 3;
    int curOpMask = 0x0F;
    // 初始 patch：调用方给的 in.opllPreset 优先，否则用音色文件自带的
    int curPreset = in.opllPreset != 0 ? in.opllPreset : (in.opll ? voice.opllPreset : 0);   // ★ patch 切换会改它（opll.cpp:182-186）
    int outVol = in.opll ? 15 : 127;
    const int opCount = in.opll ? 2 : 4;

    int stateTl[4], stateAr[4], stateDr[4], stateMult[4], stateRr[4], stateSl[4];
    int stateDt[4], stateDt2[4], stateAm[4], stateRs[4], stateD2r[4];
    int stateKsr[4], stateKsl[4], stateVib[4], stateEgt[4];
    for (int i = 0; i < 4; i++) {
        const FuiOperator& o = voice.op[i];
        stateTl[i] = o.tl;
        stateAr[i] = o.ar; stateDr[i] = o.dr; stateMult[i] = o.mult;
        stateRr[i] = o.rr; stateSl[i] = o.sl;
        stateDt[i] = o.dt; stateDt2[i] = o.dt2; stateAm[i] = o.am;
        stateRs[i] = o.rs; stateD2r[i] = o.d2r;
        stateKsr[i] = o.ksr; stateKsl[i] = o.ksl; stateVib[i] = o.vib;
        stateEgt[i] = (o.ssgEnv & 8) ? 1 : 0;
    }

    MacroState st[kMacroCount];
    for (int i = 0; i < kMacroCount; i++) st[i].Init(voice.macros[i]);
    MacroState opSt[4][kOpMacroCount];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < kOpMacroCount; j++) opSt[i][j].Init(voice.opMacros[i][j]);

    const int baseVol = in.baseVol;
    int panL = in.chVolL, panR = in.chVolR;

    auto emit = [&](uint32_t at, int reg, int val) {
        out.writes.push_back(MacroWrite{at, static_cast<uint8_t>(reg & 0xFF),
                                        static_cast<uint8_t>(val & 0xFF)});
    };
    // OPM 的 0x20：pan(bit6/7) + fb(bit3-5) + alg(bit0-2)
    auto emitPanFbAlg = [&](uint32_t at) {
        emit(at, 0x20 + ch, (curFb << 3) | curAlg | ((panL & 1) << 6) |
                                ((panR & 1) << 7));
    };
    auto emitFmsAms = [&](uint32_t at) {
        emit(at, 0x38 + ch, ((curFms & 7) << 4) | (curAms & 3));
    };
    // OPLL 的 0x30：inst(高 4) + vol(低 4)
    auto emitOpllInstVol = [&](uint32_t at) {
        const int tl = VolScaleLog(outVol, 15 - (stateTl[1] & 15), 15);
        emit(at, 0x30 + ch, ((curPreset & 15) << 4) | ((15 - tl) & 15));
    };
    // OPLL 的 0x00/0x01：AM/VIB/EGT/KSR/MULT
    auto emitOpllOp01 = [&](uint32_t at, int j) {
        emit(at, 0x00 + j, ((stateAm[j] & 1) << 7) | ((stateVib[j] & 1) << 6) |
                               ((stateEgt[j] ? 8 : 0) << 2) | ((stateKsr[j] & 1) << 4) |
                               (stateMult[j] & 15));
    };
    // OPLL 的 0x03：KSL(载波) + FMS + AMS + FB
    auto emitOpll03 = [&](uint32_t at) {
        emit(at, 0x03, ((stateKsl[1] & 3) << 6) | ((curFms & 1) << 4) |
                           ((curAms & 1) << 3) | (curFb & 7));
    };

    for (int t = 0; t < ticks; t++) {
        const uint32_t at = in.startSample + static_cast<uint32_t>(t * samplesPerTick + 0.5);
        if (at >= in.endSample) break;
        const bool lastTick = (t == ticks - 1);

        // ── 22 条通道级宏 ────────────────────────────────────────
        for (int mi = 0; mi < kMacroCount; mi++) {
            if (lastTick) st[mi].Release();
            if (!st[mi].Tick()) continue;
            const int v = st[mi].val;

            switch (mi) {
                case kMacroVol:
                    if (in.opll) {
                        outVol = VolScaleLog(baseVol, std::min(15, v), 15);
                        emitOpllInstVol(at);
                    } else {
                        outVol = VolScaleLog(baseVol, std::min(127, v), 127);
                        for (int j = 0; j < 4; j++) {
                            const bool kvs = (voice.op[j].kvs == 2 &&
                                              kIsOutput[curAlg & 7][j]) ||
                                             voice.op[j].kvs == 1;
                            int tl;
                            if (!voice.op[j].enable) {
                                tl = 127;
                            } else if (kvs) {
                                tl = 127 - VolScaleLog(127 - stateTl[j], outVol & 0x7F, 127);
                            } else {
                                tl = stateTl[j];
                            }
                            emit(at, 0x60 + j * 8 + ch, tl | (stateKsr[j] ? 0x80 : 0));
                        }
                    }
                    break;

                case kMacroArp:
                case kMacroPitch: {
                    // 两者都进 pitch2（Furnace: arcade.cpp:344-353 / opll.cpp:194-203）。
                    // 累加模式由 macro.mode 决定；这里统一按「本次值即当前偏移」处理，
                    // 因为单位是 1/128 半音、且 Furnece 的 mode!=0 时是累加增量。
                    int offset = 0;
                    for (int k = 0; k < 2; k++) {
                        const int idx = (k == 0) ? kMacroArp : kMacroPitch;
                        if (voice.macros[idx].val.empty()) continue;
                        if (st[idx].val == 0) continue;
                        offset += st[idx].val;
                    }
                    out.pitches.push_back(MacroPitch{at, offset});
                    break;
                }

                case kMacroDuty:
                    if (!in.opll) emit(at, 0x0F, v > 0 ? (0x80 | ((v - 1) & 0x1F)) : 0);
                    break;

                case kMacroWave:
                    if (!in.opll) {
                        emit(at, 0x1B, v & 3);
                    } else if (curPreset != 16) {
                        // opll.cpp:182-186 —— OPLL 的 wave 宏就是 patch 切换：
                        // 换掉 chirp 状态里的 opllPreset，再重写 0x30 的高 4 位。
                        // ★ 必须同时更新 curPreset，否则后面所有算子宏仍按 preset==0 分支。
                        curPreset = v & 15;
                        emitOpllInstVol(at);
                    }
                    break;

                case kMacroAlg:
                    curAlg = v & 7;
                    if (in.opll) {
                        // opll.cpp:205-210：OPLL 的 alg 退化成 0x20 的 SUS 位
                        if (curPreset == 0) {
                            emit(at, 0x20 + ch, curAlg ? 0x20 : 0x00);
                        }
                    } else {
                        emitPanFbAlg(at);
                    }
                    break;

                case kMacroFb:
                    curFb = v & 7;
                    if (in.opll) {
                        if (curPreset == 0) emitOpll03(at);
                    } else {
                        emitPanFbAlg(at);
                    }
                    break;

                case kMacroFms:
                    curFms = v & 7;
                    if (in.opll) {
                        if (curPreset == 0) emitOpll03(at);
                    } else {
                        emitFmsAms(at);
                    }
                    break;

                case kMacroAms:
                    curAms = v & 3;
                    if (in.opll) {
                        if (curPreset == 0) emitOpll03(at);
                    } else {
                        emitFmsAms(at);
                    }
                    break;

                case kMacroPanL:
                    panL = (v & 2) >> 1;
                    panR = v & 1;
                    if (!in.opll) emitPanFbAlg(at);
                    break;

                case kMacroPanR:
                    if (!in.opll) {
                        panL = (v & 2) >> 1;
                        panR = v & 1;
                        emitPanFbAlg(at);
                    }
                    break;

                case kMacroEx1:  // PMD → 0x19 低 7 位
                    if (!in.opll) emit(at, 0x19, v & 0x7F);
                    break;
                case kMacroEx2:  // AMD → 0x19 bit7
                    if (!in.opll) emit(at, 0x19, 0x80 | (v & 0x7F));
                    break;
                case kMacroEx3:  // LFO 频率 → 0x18
                    if (!in.opll) emit(at, 0x18, v & 0xFF);
                    break;
                case kMacroPhaseReset:
                    // arcade.cpp:355-359 / opll.cpp:196-199：值 ==1 且通道在用 → 重触发
                    if (v == 1) {
                        out.retriggers.push_back(MacroRetrigger{at, curOpMask});
                    }
                    break;
                case kMacroEx4:
                    // arcade.cpp:432-435：改算子掩码并置 opMaskChanged（下一次 key 写生效）
                    curOpMask = v & 15;
                    out.retriggers.push_back(MacroRetrigger{at, curOpMask});
                    break;
                default:
                    break;  // ex5..ex10：两个平台都不消费
            }
        }

        // ── 算子级宏（4×20 / OPLL 只跑前 2 个算子）──────────────
        for (int j = 0; j < opCount; j++) {
            const int base = j * 8 + ch;
            const bool kvs = (voice.op[j].kvs == 2 && kIsOutput[curAlg & 7][j]) ||
                             voice.op[j].kvs == 1;
            for (int mi = 0; mi < kOpMacroCount; mi++) {
                if (lastTick) opSt[j][mi].Release();
                if (!opSt[j][mi].Tick()) continue;
                const int v = opSt[j][mi].val;

                switch (mi) {
                    case kOpAm:
                        stateAm[j] = v & 1;
                        if (in.opll && curPreset == 0) {
                            emitOpllOp01(at, j);
                        } else if (!in.opll) {
                            emit(at, 0xA0 + base, (stateDr[j] & 31) | ((v & 1) << 7));
                        }
                        break;
                    case kOpAr:
                        stateAr[j] = v & 31;
                        if (in.opll && curPreset == 0) {
                            emit(at, 0x04 + j, ((v & 15) << 4) | (stateDr[j] & 15));
                        } else if (!in.opll) {
                            emit(at, 0x80 + base, (v & 31) | (stateRs[j] << 6));
                        }
                        break;
                    case kOpDr:
                        stateDr[j] = v & 31;
                        if (in.opll && curPreset == 0) {
                            emit(at, 0x04 + j, ((stateAr[j] & 15) << 4) | (v & 15));
                        } else if (!in.opll) {
                            emit(at, 0xA0 + base, (v & 31) | (stateAm[j] << 7));
                        }
                        break;
                    case kOpMult:
                        stateMult[j] = v & 15;
                        if (in.opll && curPreset == 0) {
                            emitOpllOp01(at, j);
                        } else if (!in.opll) {
                            emit(at, 0x40 + base, (v & 15) | (DtToReg(stateDt[j]) << 4));
                        }
                        break;
                    case kOpRr:
                        stateRr[j] = v & 15;
                        if (in.opll && curPreset == 0) {
                            emit(at, 0x06 + j, ((stateSl[j] & 15) << 4) | (v & 15));
                        } else if (!in.opll) {
                            emit(at, 0xE0 + base, (v & 15) | (stateSl[j] << 4));
                        }
                        break;
                    case kOpSl:
                        stateSl[j] = v & 15;
                        if (in.opll && curPreset == 0) {
                            emit(at, 0x06 + j, ((v & 15) << 4) | (stateRr[j] & 15));
                        } else if (!in.opll) {
                            emit(at, 0xE0 + base, (stateRr[j] & 15) | (v << 4));
                        }
                        break;
                    case kOpTl:
                        stateTl[j] = v;
                        if (in.opll) {
                            // opll.cpp:290-305：载波 TL 走 0x30，调制器走 0x02
                            if (j == 1) {
                                emitOpllInstVol(at);
                            } else if (curPreset == 0) {
                                emit(at, 0x02, ((stateKsl[0] & 3) << 6) | (v & 63));
                            }
                        } else {
                            int tl;
                            if (!voice.op[j].enable) {
                                tl = 127;
                            } else if (kvs) {
                                tl = 127 - VolScaleLog(127 - (v & 127), outVol & 0x7F, 127);
                            } else {
                                tl = v & 127;
                            }
                            emit(at, 0x60 + base, tl | (stateKsr[j] ? 0x80 : 0));
                        }
                        break;
                    case kOpDt:
                        stateDt[j] = v & 7;
                        if (!in.opll) {
                            emit(at, 0x40 + base, (stateMult[j] & 15) | (DtToReg(v) << 4));
                        }
                        break;
                    case kOpDt2:
                        stateDt2[j] = v & 3;
                        if (!in.opll) {
                            emit(at, 0xC0 + base, (stateD2r[j] & 31) | ((v & 3) << 6));
                        }
                        break;
                    case kOpD2r:
                        stateD2r[j] = v & 31;
                        if (!in.opll) {
                            emit(at, 0xC0 + base, (v & 31) | ((stateDt2[j] & 3) << 6));
                        }
                        break;
                    case kOpRs:
                        stateRs[j] = v & 3;
                        if (!in.opll) {
                            emit(at, 0x80 + base, (stateAr[j] & 31) | ((v & 3) << 6));
                        }
                        break;
                    case kOpKsr:
                        stateKsr[j] = v & 1;
                        if (in.opll && curPreset == 0) {
                            emitOpllOp01(at, j);
                        } else if (!in.opll) {
                            emit(at, 0x60 + base, (stateTl[j] & 127) | ((v & 1) << 7));
                        }
                        break;
                    case kOpKsl:
                        stateKsl[j] = v & 3;
                        if (in.opll && curPreset == 0) {
                            // opll.cpp:313-324：j==1（载波）→ 0x02；j==0（调制器）→ 0x03
                            if (j == 1) {
                                emit(at, 0x02, ((stateKsl[0] & 3) << 6) | (stateTl[0] & 63));
                            } else {
                                emitOpll03(at);
                            }
                        }
                        break;
                    case kOpVib:
                        stateVib[j] = v & 1;
                        if (in.opll && curPreset == 0) emitOpllOp01(at, j);
                        break;
                    case kOpEgt:
                        stateEgt[j] = v & 1;
                        if (in.opll && curPreset == 0) emitOpllOp01(at, j);
                        break;
                    case kOpSus:
                        // OPM 的 SUS 无对应寄存器；OPLL 的 0x20 bit5 由 key 段负责
                        break;
                    default:
                        break;  // ssg/dam/dvb/ws：OPM/OPLL 不支持
                }
            }
        }
    }

    std::stable_sort(out.writes.begin(), out.writes.end(),
                     [](const MacroWrite& a, const MacroWrite& b) {
                         return a.atSample < b.atSample;
                     });
    std::stable_sort(out.retriggers.begin(), out.retriggers.end(),
                     [](const MacroRetrigger& a, const MacroRetrigger& b) {
                         return a.atSample < b.atSample;
                     });
    std::stable_sort(out.pitches.begin(), out.pitches.end(),
                     [](const MacroPitch& a, const MacroPitch& b) {
                         return a.atSample < b.atSample;
                     });
    return out;
}

}  // namespace vgmstudio
