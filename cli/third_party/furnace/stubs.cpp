// Furnace 的 FUI 读写只用到 FM 音色块 + 宏块（NA/FM/MA）。
// instrument.cpp 里采样/波表/日志这些符号属于其它子系统，这里给空实现，
// 不改变 FUI 的字节布局（putInsData2/readInsData 对 FM 的处理路径完全不受影响）。
#include "ta-log.h"
#include "engine/sample.h"
#include "engine/wavetable.h"

#include <cstdio>

int writeLog(int level, const char* msg, fmt::printf_args args) {
  (void)level; (void)msg; (void)args;
  return 0;
}

FILE* ps_fopen(const char* path, const char* mode) { return fopen(path, mode); }

DivSample::~DivSample() {}
void DivSample::putSampleData(SafeWriter*) {}
DivDataErrors DivSample::readSampleData(SafeReader&, short) { return DIV_DATA_INVALID_HEADER; }

void DivWavetable::putWaveData(SafeWriter*) {}
DivDataErrors DivWavetable::readWaveData(SafeReader&, short) { return DIV_DATA_INVALID_HEADER; }
