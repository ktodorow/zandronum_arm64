#!/usr/bin/env python3
"""Compile production DBOPL/proxies/service loop; compare frozen synchronous PCM.
Only engine shell services (critical section, RNG registration, OPLio allocation)
are stubbed. RNG trap detects any synthesis access from worker.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

def method(src, signature):
    start = src.index(signature)
    brace = src.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]

PRELUDE = r'''
#include "oplsynth/opl.h"
#include "oplsynth/muslib.h"
#include "oplsynth/opl_stream_source.h"
#include "oplsynth/opl_sample_offset.h"
#include "m_random.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <mutex>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
static unsigned checks, failures;
static FILE *pcmOutput=nullptr;
static void SavePCM(const void *pcm) { if(pcmOutput) std::fwrite(pcm,1,28408,pcmOutput); }
static std::thread::id mainThread = std::this_thread::get_id();
static std::atomic<unsigned> rngCalls(0), rngWorkerCalls(0);
static void Check(bool yes, const char *what) {
 ++checks; if (!yes) { ++failures; std::fprintf(stderr,"FAIL %s\n",what); }
}
// Isolated engine shell: retain production FRandom declaration, trap calls.
FRandom::FRandom() {} FRandom::~FRandom() {}
unsigned FRandom::GenRand32() {
 if (std::this_thread::get_id()!=mainThread) ++rngWorkerCalls;
 return ++rngCalls;
}
OPLio::~OPLio() {}
int OPLio::OPLinit(uint, bool, bool) { return 0; }
void OPLio::OPLdeinit() {}
void OPLio::OPLwriteReg(int, uint, uchar) {}
void OPLio::SetClockRate(double) {}
void OPLio::WriteDelay(int) {}
struct Lock { std::mutex m; void Enter(){m.lock();} void Leave(){m.unlock();} };
struct Fixture {
 OPLio *io; Lock ChipAccess;
 double NextTickIn=0, SamplesPerTick=49716.0/140, LastOffset=0;
 unsigned long MLtime=0; int playingcount=0, tick=0;
 bool FullPan=true, Looping=true;
 Fixture(unsigned count) : io(new OPLio) {
  std::memset(io->chips,0,sizeof io->chips);
  io->NumChips=count; io->IsOPL3=true; io->EmulatorCore=1;
  for(unsigned c=0;c<count;++c) { io->chips[c]=DBOPLCreate(true); Init(c); }
 }
 virtual ~Fixture() { for(unsigned c=0;c<io->NumChips;++c) delete io->chips[c]; delete io; }
 void Init(unsigned c) {
  auto chip=io->chips[c]; chip->WriteReg(0x105,1); chip->WriteReg(0xbd,0);
  const int offsets[]={0,1,2,8,9,10,16,17,18};
  for(int channel=0;channel<18;++channel) {
   int bank=channel<9?0:256, op=offsets[channel%9]+bank;
   chip->WriteReg(0x20+op,0x21); chip->WriteReg(0x23+op,0x21);
   chip->WriteReg(0x40+op,0x17); chip->WriteReg(0x43+op,0);
   chip->WriteReg(0x60+op,0xf2); chip->WriteReg(0x63+op,0xf3);
   chip->WriteReg(0x80+op,0x54); chip->WriteReg(0x83+op,0x45);
   chip->WriteReg(0xe0+op,channel%8); chip->WriteReg(0xe3+op,(channel+3)%8);
   chip->WriteReg(bank+0xc0+channel%9,0x31);
   chip->WriteReg(bank+0xa0+channel%9,0x50+channel*7);
   chip->WriteReg(bank+0xb0+channel%9,0x31);
  }
 }
 int PlayTick() {
  if (++tick==24) { tick=0; return 0; }
  for(unsigned c=0;c<io->NumChips;++c) {
   auto chip=io->chips[c]; int ch=(tick*7+c)%18, bank=ch<9?0:256;
   if(tick==9) { chip->Reset(); Init(c); }
   chip->SetPanning(ch, float((tick%7)+1)/8, float((tick%5)+1)/6);
   chip->WriteReg(bank+0xa0+ch%9,0x30+tick*3+c);
   chip->WriteReg(bank+0xb0+ch%9, tick%4==0?0x11:0x31);
  }
  return tick%5==0?3:1;
 }
 void OPLstopMusic() {
  for(unsigned c=0;c<io->NumChips;++c) for(int ch=0;ch<18;++ch)
   io->chips[c]->WriteReg((ch<9?0:256)+0xb0+ch%9,0);
 }
 void OPLplayMusic(int) { for(unsigned c=0;c<io->NumChips;++c) Init(c); }
 virtual void Restart()=0;
};
struct OPLmusicBlock : Fixture {
 OPLPreparedSource *PreparedPCM=nullptr;
 explicit OPLmusicBlock(unsigned n):Fixture(n){}
 ~OPLmusicBlock() { delete PreparedPCM; }
 bool ServiceStream(void*,int); void OffsetSamples(float*,int); void Restart();
 bool RecordPreparedCorrection(float *p,int n) {return PreparedPCM->RecordCorrection(p,n);}
};
struct ReferenceBlock : Fixture {
 explicit ReferenceBlock(unsigned n):Fixture(n){}
 bool ServiceStream(void*,int); void OffsetSamples(float*,int); void Restart();
};
'''
MAIN = r'''
static bool Wait(PreparedSoundSource &jobs,unsigned slot,const void *&pcm,bool &valid) {
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 do { if(jobs.Collect(slot,pcm,valid)) return true; std::this_thread::yield(); }
 while(std::chrono::steady_clock::now()<end);
 return false;
}
static void Run(unsigned chips, bool looping) {
 OPLmusicBlock candidate(chips); ReferenceBlock reference(chips);
 candidate.Looping=reference.Looping=looping;
 // Same four synchronous prefill callbacks as OpenAL. Wrap before prefill.
 auto original=candidate.io->chips[0];
 candidate.PreparedPCM=new OPLPreparedSource(*candidate.io,candidate.LastOffset);
 Check(candidate.io->chips[0]!=original,"proxy installed");
 float ref[4][7102];
 for(int i=0;i<4;++i) {
  float pcm[7102]; bool a=candidate.ServiceStream(pcm,sizeof pcm);
  bool b=reference.ServiceStream(ref[i],sizeof ref[i]);
  Check(a==b && !std::memcmp(pcm,ref[i],sizeof pcm),"synchronous prefill exact");
  SavePCM(pcm);
 }
 Check(candidate.PreparedPCM->Start(28408),"actual DBOPL worker start");
 bool expectedValid[4];
 for(unsigned group=0;group<32;++group) {
  for(unsigned slot=0;slot<4;++slot) {
   void *pcm=candidate.PreparedPCM->Prepare(slot);
   bool a=candidate.ServiceStream(pcm,28408);
   expectedValid[slot]=reference.ServiceStream(ref[slot],28408);
   candidate.PreparedPCM->Submit(slot,a);
  }
  for(unsigned slot=0;slot<4;++slot) {
   const void *pcm=nullptr; bool valid=false;
   Check(Wait(*candidate.PreparedPCM,slot,pcm,valid),"actual PCM completed");
   Check(valid==expectedValid[slot],"actual EOF/loop result exact");
   Check(pcm && !std::memcmp(pcm,ref[slot],28408),"actual DBOPL stereo PCM bit identical");
   if(pcm) SavePCM(pcm);
  }
 }
 candidate.PreparedPCM->Stop();
 Check(!std::memcmp(&candidate.LastOffset,&reference.LastOffset,sizeof(double)),"actual DC state exact on stop");
 // Unwrap and resume synchronous output from worker chip state.
 delete candidate.PreparedPCM; candidate.PreparedPCM=nullptr;
 Check(candidate.io->chips[0]==original,"actual chips restored before destruction");
 float pcm[7102];
 bool a=candidate.ServiceStream(pcm,sizeof pcm), b=reference.ServiceStream(ref[0],sizeof pcm);
 Check(a==b && !std::memcmp(pcm,ref[0],sizeof pcm),"synchronous resume after stop exact");
}
static void RunRhythm() {
 OPLmusicBlock block(1);
 block.PreparedPCM=new OPLPreparedSource(*block.io,block.LastOffset);
 Check(block.PreparedPCM->Start(28408),"rhythm test starts melodic worker");
 void *p=block.PreparedPCM->Prepare(0);
 block.ServiceStream(p,28408);block.PreparedPCM->Submit(0,true);
 p=block.PreparedPCM->Prepare(1);std::memset(p,0,28408);
 block.io->chips[0]->WriteReg(0xbd,0x3f);
 block.io->chips[0]->Update((float*)p,3551);
 block.PreparedPCM->RecordCorrection((float*)p,7102);
 block.PreparedPCM->Submit(1,true);
 const void *result=nullptr;bool valid=false;
 Check(Wait(*block.PreparedPCM,1,result,valid)&&valid,"unexpected real rhythm completes");
 Check(rngCalls>0&&rngWorkerCalls==0,"real rhythm RNG stays main after draining melodic jobs");
 block.PreparedPCM->Stop();
 Check(!block.PreparedPCM->Start(28408),"real rhythm instance permanently synchronous");
}
int main() {
 const char *path=std::getenv("ZANDRONUM_TEST_PCM_OUT");
 if(path) {pcmOutput=std::fopen(path,"wb"); Check(pcmOutput!=nullptr,"PCM output opened");}
 Run(1,true); Run(2,true); Run(4,true); Run(1,false);
 Check(rngCalls==0 && rngWorkerCalls==0,"melodic DBOPL has no global RNG access");
 if(pcmOutput) std::fclose(pcmOutput);
 RunRhythm();
 std::printf("%u checks, %u failures; actual DBOPL, %u RNG calls\n",checks,failures,unsigned(rngCalls));
 return failures?1:0;
}
'''

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cxx',default='c++')
    p.add_argument('--sanitize',default='')
    p.add_argument('--pcm-output')
    p.add_argument('--baseline-dbopl',action='store_true',help='Compare dumped PCM against pre-worker pinned emulator, requires git history')
    p.add_argument('--negative-pan',action='store_true')
    p.add_argument('--negative-rhythm',action='store_true')
    a=p.parse_args()
    source=(ROOT/'src/oplsynth/opl_mus_player.cpp').read_text()
    funcs='\n'.join(method(source,n) for n in ['void OPLmusicBlock::Restart()', 'bool OPLmusicBlock::ServiceStream ', 'void OPLmusicBlock::OffsetSamples('])
    with tempfile.TemporaryDirectory(prefix='zandronum-opl-service-') as tmp:
        t=Path(tmp); cpp=t/'test.cpp'; binary=t/'test'
        cpp.write_text(PRELUDE+'\n'+(ROOT/'tools/r36s/tests/opl-service-reference.h').read_text()+'\n'+funcs+'\n'+MAIN)
        adapter=ROOT/'src/oplsynth/opl_stream_source.cpp'
        if a.negative_pan:
            mutated=t/'opl_stream_source.cpp'
            mutated.write_text(adapter.read_text().replace('Jobs.SetPanning(Chip, channel, left, right);','Jobs.SetPanning(Chip, channel, right, left);'))
            adapter=mutated
        jobs=ROOT/'src/oplsynth/opl_stream_jobs.cpp'
        if a.negative_rhythm:
            mutant=t/'opl_stream_jobs.cpp'
            mutant.write_text(jobs.read_text().replace('(command.b & 0x20)','(false)'))
            jobs=mutant
        emulator=ROOT/'src/oplsynth/dosbox/opl.cpp'
        if a.baseline_dbopl:
            old=t/'baseline/oplsynth/dosbox'; old.mkdir(parents=True)
            for name in ('opl.cpp','opl.h'):
                text=subprocess.check_output(['git','show','178fbce175adc2cc3fca645d66d4c9bebaf5d8b1:src/oplsynth/dosbox/'+name],cwd=ROOT,text=True)
                (old/name).write_text(text)
            for name in ('opl.h','muslib.h'):
                (old.parent/name).symlink_to(ROOT/'src/oplsynth'/name)
            emulator=old/'opl.cpp'
        cmd=[a.cxx,'-std=c++11','-O1','-g','-pthread','-DUSE_GLES2=1','-Isrc','-Isrc/oplsynth',str(cpp),str(adapter),str(jobs),'src/oplsynth/opl_sample_offset.cpp',str(emulator),'-o',str(binary)]
        if a.sanitize: cmd += ['-fno-omit-frame-pointer','-fsanitize='+a.sanitize]
        subprocess.run(cmd,cwd=ROOT,check=True)
        environment=dict(os.environ)
        if a.pcm_output: environment['ZANDRONUM_TEST_PCM_OUT']=str(Path(a.pcm_output).resolve())
        subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=180,env=environment)

if __name__=='__main__': main()
