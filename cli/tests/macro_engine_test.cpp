// 单元测试：逐条宏验证展开结果
#include "MacroEngine.h"
#include <cstdio>
#include <vector>
using namespace vgmstudio;

static FuiVoice Base(bool opll, int ops) {
  FuiVoice v;
  v.type = opll ? kFuiInsOpll : kFuiInsOpm;
  v.ops = ops;
  v.alg = 2; v.fb = 1; v.fms = 0; v.ams = 0;
  for (int i=0;i<4;i++){ v.op[i].mult=i+1; v.op[i].tl=10*i; v.op[i].ar=20; v.op[i].dr=5; v.op[i].sl=2; v.op[i].rr=9; v.op[i].dt=i; v.op[i].kvs=2; }
  return v;
}
static Macro Seq(std::vector<int> vals,int speed,int loop=0){
  Macro m; m.type=0; m.speed=speed; m.delay=0; m.loop=loop; m.rel=255; m.val=vals; return m;
}
static int fails=0;
static void check(const char* name,bool ok,const char* detail=""){
  std::printf("%-28s %s %s\n",name, ok?"PASS":"FAIL",detail); if(!ok) fails++;
}
int main(){
  MacroExpandInput in; in.ch=0; in.startSample=0; in.endSample=44100; in.sampleRate=44100; in.tickRate=60; in.baseVol=127;

  // 1. 序列宏的步进节奏：speed=2 → 每 1470 采样一步
  {
    FuiVoice v=Base(false,4); v.macros[kMacroVol]=Seq({127,100,70},2);
    in.opll=false; auto o=ExpandMacros(v,in);
    std::vector<uint32_t> at; for(auto&w:o.writes) if(w.reg==0x60) at.push_back(w.atSample);
    check("vol seq timing", at.size()>=3 && at[0]==0 && at[1]==1470 && at[2]==2940);
  }
  // 2. 音量宏真的改 TL（OPM）
  {
    FuiVoice v=Base(false,4); v.macros[kMacroVol]=Seq({127,64},2);
    in.opll=false; auto o=ExpandMacros(v,in);
    std::vector<int> carrierTl, modTl;
    for(auto&w:o.writes) if(w.reg>=0x60 && w.reg<=0x7F){
      int j=(w.reg-0x60)/8;
      if(j==3) carrierTl.push_back(w.val&0x7F); else if(j==0) modTl.push_back(w.val&0x7F);
    }
    check("vol changes carrier TL", carrierTl.size()>=2 && carrierTl[0]==30 && carrierTl[1]==93);
    check("vol leaves mod TL", modTl.size()>=2 && modTl[0]==modTl[1]);
  }
  // 3. pitch 宏产生音高变化点
  {
    FuiVoice v=Base(false,4); v.macros[kMacroPitch]=Seq({0,64,-64},3);
    in.opll=false; auto o=ExpandMacros(v,in);
    check("pitch emits offsets", o.pitches.size()>=2 && o.pitches[1].offset128==64);
  }
  // 4. alg 宏写 0x20
  {
    FuiVoice v=Base(false,4); v.macros[kMacroAlg]=Seq({0,4},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool found=false; for(auto&w:o.writes) if(w.reg==0x20 && (w.val&7)==4) found=true;
    check("alg writes 0x20", found);
  }
  // 5. fb 宏写 0x20 的 bit3-5
  {
    FuiVoice v=Base(false,4); v.macros[kMacroFb]=Seq({0,7},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool found=false; for(auto&w:o.writes) if(w.reg==0x20 && ((w.val>>3)&7)==7) found=true;
    check("fb writes 0x20", found);
  }
  // 6. fms/ams 宏写 0x38
  {
    FuiVoice v=Base(false,4); v.macros[kMacroFms]=Seq({0,5},4); v.macros[kMacroAms]=Seq({0,3},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool found=false; for(auto&w:o.writes) if(w.reg==0x38 && ((w.val>>4)&7)==5) found=true;
    check("fms writes 0x38", found);
  }
  // 7. duty 宏写 0x0F、wave 宏写 0x1B
  {
    FuiVoice v=Base(false,4); v.macros[kMacroDuty]=Seq({2,5},4); v.macros[kMacroWave]=Seq({0,3},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool d=false,w=false; for(auto&x:o.writes){ if(x.reg==0x0F) d=true; if(x.reg==0x1B) w=true; }
    check("duty->0x0F wave->0x1B", d&&w);
  }
  // 8. ex1/ex2/ex3 → 0x19 / 0x18
  {
    FuiVoice v=Base(false,4);
    v.macros[kMacroEx1]=Seq({0x20},4); v.macros[kMacroEx2]=Seq({0x30},4); v.macros[kMacroEx3]=Seq({0xC2},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool a=false,b=false,c=false;
    for(auto&x:o.writes){ if(x.reg==0x19 && x.val==0x20) a=true; if(x.reg==0x19 && x.val==0xB0) b=true; if(x.reg==0x18 && x.val==0xC2) c=true; }
    check("ex1/ex2/ex3 LFO regs", a&&b&&c);
  }
  // 9. 算子 TL 宏（OPM）：KVS 影响最终 TL
  {
    FuiVoice v=Base(false,4); v.opMacros[3][kOpTl]=Seq({0,60},3);
    in.opll=false; auto o=ExpandMacros(v,in);
    int n=0; for(auto&w:o.writes) if(w.reg==0x60+3*8) n++;
    check("op tl macro (opm)", n>=2);
  }
  // 10. 算子 AR/DR 宏（OPM）写 0x80 / 0xA0
  {
    FuiVoice v=Base(false,4);
    v.opMacros[0][kOpAr]=Seq({5,31},3); v.opMacros[1][kOpDr]=Seq({1,15},3);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool ar=false,dr=false;
    for(auto&x:o.writes){ if(x.reg==0x80 && (x.val&0x1F)==31) ar=true; if(x.reg==0xA0+8 && (x.val&0x1F)==15) dr=true; }
    check("op ar/dr macros", ar&&dr);
  }
  // 11. OPLL 音量宏写 0x30（含 inst 高 4 位）
  {
    FuiVoice v=Base(true,2); v.macros[kMacroVol]=Seq({15,8},2); v.opllPreset=14;
    in.opll=true; auto o=ExpandMacros(v,in);
    bool found=false; for(auto&w:o.writes) if(w.reg==0x30 && ((w.val>>4)&15)==14) found=true;
    check("opll vol writes 0x30", found);
  }
  // 12. OPLL wave 宏改的是音色号
  {
    FuiVoice v=Base(true,2); v.macros[kMacroWave]=Seq({3},4); v.opllPreset=2;
    in.opll=true; auto o=ExpandMacros(v,in);
    bool found=false; for(auto&w:o.writes) if(w.reg==0x30 && ((w.val>>4)&15)==3) found=true;
    check("opll wave->preset", found);
  }
  // 13. ADSR 模式跑起来并收敛到 sustain
  {
    FuiVoice v=Base(false,4); Macro m; m.type=1; m.speed=1; m.loop=255; m.rel=255;
    m.val={0,127, 8,0, 4,64, 0,0, 8, 0,0,0,0,0,0,0};
    v.macros[kMacroVol]=m; in.opll=false;
    auto o=ExpandMacros(v,in);
    int n=0; for(auto&w:o.writes) if(w.reg==0x60) n++;
    check("ADSR runs", n>=3);
  }
  // 14. LFO 三角波
  {
    FuiVoice v=Base(false,4); Macro m; m.type=2; m.speed=1; m.loop=255; m.rel=255;
    m.val.resize(16,0);
    m.val[0]=0; m.val[1]=127; m.val[11]=8; m.val[12]=0; m.val[13]=0;
    v.macros[kMacroPitch]=m; in.opll=false;
    auto o=ExpandMacros(v,in);
    check("LFO triangle runs", o.pitches.size()>=5);
  }
  // 15. 无宏时零输出
  {
    FuiVoice v=Base(false,4); in.opll=false;
    auto o=ExpandMacros(v,in);
    check("no macros => no writes", o.writes.empty() && o.pitches.empty());
  }
  // 16. phaseReset 宏产生重触发点
  {
    FuiVoice v=Base(false,4); v.macros[kMacroPhaseReset]=Seq({0,1,0,1},3);
    in.opll=false; auto o=ExpandMacros(v,in);
    check("phaseReset retriggers", o.retriggers.size()>=2);
  }
  // 17. ex4 宏改算子掩码
  {
    FuiVoice v=Base(false,4); v.macros[kMacroEx4]=Seq({0x0F,0x03},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    bool found=false;
    for(auto&r:o.retriggers) if(r.opMask==0x03) found=true;
    check("ex4 changes opMask", found);
  }
  // 18. 算子级 dt/dt2/d2r/rs/sl/am/mult 全部落寄存器
  {
    FuiVoice v=Base(false,4);
    v.opMacros[0][kOpDt]=Seq({7},4);
    v.opMacros[0][kOpDt2]=Seq({3},4);
    v.opMacros[0][kOpD2r]=Seq({31},4);
    v.opMacros[0][kOpRs]=Seq({3},4);
    v.opMacros[0][kOpSl]=Seq({15},4);
    v.opMacros[0][kOpAm]=Seq({1},4);
    v.opMacros[0][kOpMult]=Seq({15},4);
    in.opll=false; auto o=ExpandMacros(v,in);
    std::vector<int> regs;
    for(auto&w:o.writes){ bool s=false; for(int r:regs) if(r==w.reg) s=true; if(!s) regs.push_back(w.reg); }
    bool has40=false,hasC0=false,has80=false,hasE0=false,hasA0=false;
    for(int r:regs){ if(r==0x40)has40=true; if(r==0xC0)hasC0=true; if(r==0x80)has80=true; if(r==0xE0)hasE0=true; if(r==0xA0)hasA0=true; }
    check("op dt/dt2/d2r/rs/sl/am/mult", has40&&hasC0&&has80&&hasE0&&hasA0);
  }
  // 19. OPLL 用户音色的算子宏写 0x00-0x06
  {
    FuiVoice v=Base(true,2); v.opllPreset=0;
    v.opMacros[0][kOpAr]=Seq({31},4);
    v.opMacros[0][kOpDr]=Seq({15},4);
    v.opMacros[0][kOpMult]=Seq({7},4);
    v.opMacros[0][kOpRr]=Seq({12},4);
    v.opMacros[0][kOpTl]=Seq({55},4);
    v.opMacros[0][kOpKsl]=Seq({3},4);
    v.opMacros[0][kOpVib]=Seq({1},4);
    v.opMacros[0][kOpEgt]=Seq({1},4);
    v.opMacros[0][kOpKsr]=Seq({1},4);
    in.opll=true; auto o=ExpandMacros(v,in);
    std::vector<int> regs;
    for(auto&w:o.writes){ bool s=false; for(int r:regs) if(r==w.reg) s=true; if(!s) regs.push_back(w.reg); }
    int n=0; for(int r:regs) if(r<=0x07) n++;
    check("opll op macros 0x00-0x06", n>=5);
  }
  // 20. OPLL 的 alg 宏写 0x20 的 SUS 位
  {
    FuiVoice v=Base(true,2); v.opllPreset=0; v.macros[kMacroAlg]=Seq({0,1},4);
    in.opll=true; auto o=ExpandMacros(v,in);
    bool found=false; for(auto&w:o.writes) if(w.reg==0x20 && (w.val&0x20)) found=true;
    check("opll alg -> sus bit", found);
  }
  std::printf("\n%s (%d fail)\n", fails? "FAILED":"ALL PASS", fails);
  return fails?1:0;
}
