#!/usr/bin/env python3
"""Exercise extracted production OpenALSoundStream with deterministic AL boundary.
Fake AL models stopped-source queues becoming processed, delayed completions,
EOF, pause, errors and synchronous startup fallback. No duplicated stream loop.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
PRELUDE=r'''
#include "sound/prepared_stream.h"
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
using ALubyte=unsigned char; using BYTE=unsigned char; using ALuint=unsigned;
using ALsizei=int; using ALint=int; using ALenum=int; using ALfloat=float;
static unsigned checks,failures;
static void Check(bool yes,const char *msg) {
 ++checks; if(!yes) {++failures; std::fprintf(stderr,"FAIL %s\n",msg);}
}
template<class T> struct TArray {
 std::vector<T> v; unsigned Size() const{return unsigned(v.size());}
 void Resize(unsigned n){v.resize(n);} T &operator[](unsigned i){return v[i];}
 void Push(T x){v.push_back(x);} void Pop(T &x){x=v.back();v.pop_back();}
 void Delete(unsigned i){v.erase(v.begin()+i);}
};
template<class T> unsigned FindSoundValue(TArray<T> &a,T x) {
 return unsigned(std::find(a.v.begin(),a.v.end(),x)-a.v.begin());
}
struct FString : std::string {
 using std::string::operator=;
 void AppendFormat(const char *fmt,...) {
  char b[512]; va_list a;va_start(a,fmt);vsnprintf(b,sizeof b,fmt,a);va_end(a);append(b);
 }
};
static void Printf(const char*,...) {}
class SoundStream;
using SoundStreamCallback=bool(*)(SoundStream*,void*,int,void*);
struct SoundStream {
 enum {Bits8=1,Mono=2,Float=4,Bits32=8};
 virtual ~SoundStream(){};
};
struct FileReader {explicit FileReader(const char*){} virtual ~FileReader(){}};
struct MemoryReader:FileReader {MemoryReader(const char*,unsigned):FileReader("") {}};
enum ChannelConfig {ChannelConfig_Mono,ChannelConfig_Stereo};
enum SampleType {SampleType_UInt8,SampleType_Int16};
struct SoundDecoder {
 unsigned read(char*,int){return 0;} bool seek(unsigned){return true;}
 unsigned getSampleOffset(){return 0;} unsigned getSampleLength(){return 0;}
 void getInfo(int *r,ChannelConfig *c,SampleType *t){*r=44100;*c=ChannelConfig_Stereo;*t=SampleType_Int16;}
};
struct FSoundChan{};
class OpenALSoundStream;
struct OpenALSoundRenderer {
 TArray<ALuint> FreeSfx; TArray<OpenALSoundStream*> Streams;
 unsigned EnvSlot=0; float MusicVolume=1;
 OpenALSoundRenderer(){FreeSfx.Push(1);}
 FSoundChan *FindLowestChannel(){return nullptr;} void StopChannel(FSoundChan*){}
 SoundDecoder *CreateDecoder(FileReader*){return nullptr;}
};
'''
FAKE=r'''
struct FakeAL {
 std::vector<unsigned> queue; std::map<unsigned,float> pcm;
 std::vector<float> history;
 int processed=0,state=AL_INITIAL,error=AL_NO_ERROR,plays=0;
 bool failUnqueue=false;
 void Tick(unsigned n) {
  Check(state==AL_PLAYING,"consume only playing source");
  processed=std::min(int(queue.size()),processed+int(n));
  if(processed==int(queue.size())) state=AL_STOPPED;
 }
} al;
static ALenum getALError(){int e=al.error;al.error=AL_NO_ERROR;return e;}
static ALenum alGetError(){return getALError();}
static const char *alGetString(ALenum){return "fake error";}
static void alSource3f(ALuint,ALenum,float,float,float){}
static void alSource3i(ALuint,ALenum,int,int,int){}
static void alSourcef(ALuint,ALenum,float){}
static void alSourcei(ALuint,ALenum p,int value) {
 if(p==AL_BUFFER && value==0) {al.queue.clear();al.processed=0;}
}
static void alGenBuffers(int n,unsigned *b){for(int i=0;i<n;++i)b[i]=10+i;}
static void alDeleteBuffers(int,const unsigned*){Check(al.queue.empty(),"destroy detaches AL buffers");}
static void alSourcePlay(unsigned) {
 ++al.plays;
 if(al.state!=AL_PAUSED && al.state!=AL_PLAYING) al.processed=0;
 al.state=al.queue.empty()?AL_STOPPED:AL_PLAYING;
}
static void alSourcePause(unsigned){if(al.state==AL_PLAYING)al.state=AL_PAUSED;}
static void alSourceStop(unsigned){al.state=AL_STOPPED;al.processed=int(al.queue.size());}
static void alSourceRewind(unsigned){al.state=AL_INITIAL;al.processed=0;}
static bool alIsExtensionPresent(const char*){return true;}
static void alBufferData(unsigned b,int,const void *p,int bytes,int rate) {
 Check(std::find(al.queue.begin(),al.queue.end(),b)==al.queue.end(),"never overwrite queued PCM");
 Check(bytes==28408 && rate==49716,"native PCM format unchanged");
 al.pcm[b]=*(const float*)p;
}
static void alSourceQueueBuffers(unsigned,int n,const unsigned *b) {
 for(int i=0;i<n;++i) {al.queue.push_back(b[i]);al.history.push_back(al.pcm[b[i]]);}
 if(al.state==AL_STOPPED)al.processed=int(al.queue.size());
}
static void alSourceUnqueueBuffers(unsigned,int n,unsigned *b) {
 if(al.failUnqueue){al.error=AL_INVALID_OPERATION;return;}
 for(int i=0;i<n;++i) {
  if(al.processed==0||al.queue.empty()) {al.error=AL_INVALID_OPERATION;return;}
  b[i]=al.queue.front();al.queue.erase(al.queue.begin());--al.processed;
 }
}
static void alGetSourcef(unsigned,int,float *v){*v=1;}
static void alGetSourcei(unsigned,int p,int *v) {
 if(p==AL_SOURCE_STATE)*v=al.state;
 else if(p==AL_BUFFERS_PROCESSED)*v=al.processed;
 else if(p==AL_BUFFERS_QUEUED)*v=int(al.queue.size());
 else *v=0;
}
'''
MAIN=r'''
struct Prepared : PreparedSoundSource {
 float pcm[4][7102]; bool busy[4]={},ready[4]={},valid[4]={};
 bool startOK=true,running=false; unsigned prepares=0,submits=0,collects=0,stops=0;
 bool Start(unsigned bytes){Check(bytes==28408,"start exact block");running=startOK;return startOK;}
 bool UsesWorker() const{return running;}
 void Stop(){running=false;++stops;for(int i=0;i<4;++i)busy[i]=ready[i]=false;}
 void *Prepare(unsigned s){Check(s<4&&!busy[s],"exclusive PCM slot");++prepares;busy[s]=true;ready[s]=false;return pcm[s];}
 void Submit(unsigned s,bool v){Check(busy[s],"submit owned slot");valid[s]=v;++submits;}
 bool Collect(unsigned s,const void *&p,bool &v){
  if(!ready[s])return false;
  Check(busy[s],"collect exactly once");++collects;p=pcm[s];v=valid[s];busy[s]=ready[s]=false;return true;
 }
 void ReadyAll(){for(int i=0;i<4;++i)if(busy[i])ready[i]=true;}
};
struct Input {unsigned next=0,max=100000;};
static bool Callback(SoundStream*,void *p,int n,void *user) {
 auto &input=*(Input*)user;float value=float(input.next++);
 for(int i=0;i<n/4;++i)((float*)p)[i]=value;
 return value<input.max;
}
static void Init(OpenALSoundStream &stream,Input &input,Prepared &prepared) {
 Check(stream.Init(Callback,28408,SoundStream::Float,49716,&input),"stream init");
 Check(stream.SetPreparedSource(&prepared),"prepared source accepted");
 Check(!stream.SetPreparedSource(&prepared),"reject duplicate attachment");
 Check(stream.Play(true,1),"play synchronous prefills");
 Check(input.next==4 && prepared.prepares==0,"four original synchronous prefills");
}
static void OrderedUnderrun() {
 al=FakeAL(); OpenALSoundRenderer renderer; Prepared prepared; Input input;
 OpenALSoundStream stream(&renderer); Init(stream,input,prepared);
 Check(!stream.IsEnded()&&input.next==4,"zero processed produces no speculative callback");
 al.Tick(4);
 Check(!stream.IsEnded()&&al.queue.empty()&&input.next==8,"empty AL queue with pending work remains playing");
 prepared.ready[1]=prepared.ready[2]=prepared.ready[3]=true;
 Check(!stream.IsEnded()&&al.queue.empty(),"out of order completion held behind oldest");
 prepared.ready[0]=true;
 Check(!stream.IsEnded()&&al.queue.size()==4,"completed PCM restarts stopped source");
 Check(al.history==std::vector<float>({0,1,2,3,4,5,6,7}),"exact once FIFO upload");
 Check(al.processed==0&&al.state==AL_PLAYING,"newly queued stopped buffers not discarded");
 Check(stream.GetStats().find("underruns 1")!=std::string::npos,"one starvation counted");
 al.Tick(1);Check(!stream.IsEnded(),"partial queue refill");
 prepared.ReadyAll();Check(!stream.IsEnded(),"partial completion queues");
 Check(al.history.back()==8&&input.next==9,"partial refill exact once");
 stream.Stop();Check(al.queue.empty(),"stop clears queue");
 Check(stream.IsEnded(),"stop marks ended");
 Check(stream.Play(true,1),"restart same instance");
 Check(input.next==13,"restart has no stale completion");
 stream.Stop();
}
static void PausePending() {
 al=FakeAL();OpenALSoundRenderer renderer;Prepared prepared;Input input;
 OpenALSoundStream stream(&renderer);Init(stream,input,prepared);
 al.Tick(4);Check(!stream.IsEnded(),"pause test pending");
 Check(stream.SetPaused(true),"pause no currently queued audio");
 int plays=al.plays;prepared.ReadyAll();Check(!stream.IsEnded(),"paused pending remains live");
 Check(al.plays==plays&&al.state==AL_STOPPED,"logical pause prevents completion autoplay");
 Check(stream.SetPaused(false),"resume queued completed audio");
 Check(al.state==AL_PLAYING&&al.processed==0,"resume starts first unplayed completion");
 stream.SetVolume(.3f);Check(!stream.IsEnded(),"volume during playback");
}
static void EndAndErrors() {
 al=FakeAL();{
  OpenALSoundRenderer renderer;Prepared prepared;Input input;input.max=6;
  OpenALSoundStream stream(&renderer);Init(stream,input,prepared);
  al.Tick(4);Check(!stream.IsEnded(),"EOF pending preserved");
  prepared.ReadyAll();Check(!stream.IsEnded()&&al.queue.size()==2,"false callbacks not uploaded");
  al.Tick(2);Check(!stream.IsEnded(),"EOF command completion retained");
  prepared.ReadyAll();Check(stream.IsEnded(),"EOF after final completed commands");
  Check(al.history==std::vector<float>({0,1,2,3,4,5}),"EOF no silence insertion or duplicate");
 }
 al=FakeAL();{
  OpenALSoundRenderer renderer;Prepared prepared;Input input;
  OpenALSoundStream stream(&renderer);Init(stream,input,prepared);
  al.Tick(1);Check(!stream.IsEnded(),"error test issued work");
  al.failUnqueue=true;al.Tick(1);Check(stream.IsEnded(),"AL failure ends stream");
  unsigned stops=prepared.stops;stream.Stop();
  Check(prepared.stops==stops+1&&!prepared.busy[0],"error cleanup drains even when Playing false");
 }
}
static void StartupFallback() {
 al=FakeAL();OpenALSoundRenderer renderer;Prepared prepared;prepared.startOK=false;Input input;
 OpenALSoundStream stream(&renderer);Init(stream,input,prepared);
 al.Tick(2);Check(!stream.IsEnded(),"thread startup failure keeps synchronous stream");
 Check(input.next==6&&prepared.prepares==0,"fallback renders through original callback");
 Check(al.history==std::vector<float>({0,1,2,3,4,5}),"fallback PCM sequence exact");
 Check(stream.GetStats().find("inactive")!=std::string::npos,"fallback reported");
}
int main(){OrderedUnderrun();PausePending();EndAndErrors();StartupFallback();
 std::printf("%u checks, %u failures; production OpenALSoundStream\n",checks,failures);return failures?1:0;}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--cxx',default='c++');p.add_argument('--sanitize',default='');a=p.parse_args()
 source=(ROOT/'src/sound/oalsound.cpp').read_text()
 start=source.index('class OpenALSoundStream : public SoundStream')
 end=source.index('\n};',start)+3
 code=source[start:end]
 names=sorted(set(re.findall(r'\bAL_[A-Z0-9_]+',code+FAKE)))
 constants='enum {'+','.join(n+'='+str(0 if n in ['AL_NO_ERROR','AL_NONE','AL_FALSE'] else 1 if n=='AL_TRUE' else i+2) for i,n in enumerate(names))+'};\n'
 with tempfile.TemporaryDirectory(prefix='zandronum-opl-openal-') as tmp:
  t=Path(tmp);cpp=t/'test.cpp';binary=t/'test'
  cpp.write_text(PRELUDE+constants+FAKE+code+MAIN)
  cmd=[a.cxx,'-std=c++11','-O1','-g','-DUSE_GLES2=1','-Isrc',str(cpp),'-o',str(binary)]
  if a.sanitize:cmd+=['-fno-omit-frame-pointer','-fsanitize='+a.sanitize]
  subprocess.run(cmd,cwd=ROOT,check=True)
  subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=20)
if __name__=='__main__':main()
