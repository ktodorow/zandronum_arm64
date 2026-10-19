// Actual production FRenderState/flat-light loop + GLES readback regression.
// Dependency fixtures deliberately contain no render-state setter/cache logic.
#include "gl/system/gl_system.h"
#include "gl/system/gl_interface.h"
#include "gl/data/gl_data.h"
#include "gl/renderer/gl_renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

FakeGL gl={0}; FakeSettings glset={0};
int gl_fogmode=0;
bool gl_colormap_shader=false,gl_brightmap_shader=false,gl_warp_shader=false,gl_fog_shader=false;
FakeShaderManager manager; FakeRenderer renderer={&manager}; FakeRenderer *GLRenderer=&renderer;
FShader material={}; FShaderContainer container;
FShader *FShaderContainer::Bind(int,bool,float,bool){gles::UseProgram(material.handle);return &material;}
FShaderContainer *FakeShaderManager::Get(int){return &container;}
void gl_SetTextureMode(int mode){gles::SetTextureMode(mode);}
void I_FatalError(const char *format,...)
{
 char message[4096];va_list args;va_start(args,format);vsnprintf(message,sizeof message,format,args);va_end(args);
 throw std::runtime_error(message);
}

unsigned draws=0;
extern "C" void __real_glDrawArrays(GLenum,GLint,GLsizei);
extern "C" void __real_glDrawElements(GLenum,GLsizei,GLenum,const void*);
extern "C" void __wrap_glDrawArrays(GLenum mode,GLint first,GLsizei count){++draws;__real_glDrawArrays(mode,first,count);}
extern "C" void __wrap_glDrawElements(GLenum mode,GLsizei count,GLenum type,const void *p){++draws;__real_glDrawElements(mode,count,type,p);}

// World fixtures for compiling the complete, unmodified production flat loop.
// gl_SetupLight is outside this test's scope; each fixture supplies its result
// and equation. The loop's actual Apply placement and geometry remain intact.
struct Vector {
 float x,y,z;void Set(float a,float b,float c){x=a;y=b;z=c;}
 Vector operator-(const Vector &v)const{return Vector{x-v.x,y-v.y,z-v.z};}
 float Dot(const Vector &v)const{return x*v.x+y*v.y+z*v.z;}
};
struct FixturePlane {float ZatPoint(float,float)const{return 0;}};
struct Plane {void Set(const FixturePlane&) {}};
struct vertex_t {float fx,fy;};
struct seg_t {vertex_t *v1;};
struct ADynamicLight {int flags2;float x,y,z;GLenum equation;float color[3];bool visible;};
struct FLightNode {ADynamicLight *lightsource;FLightNode *nextLight;};
struct subsector_t {FLightNode *lighthead[2];seg_t *firstline;unsigned numlines;};
const int GLPASS_LIGHT_ADDITIVE=1,MF2_DORMANT=1;
typedef float fixed_t;
bool gl_lights_checkside=false;
unsigned iter_dlightf=0,draw_dlightf=0;
struct GLFlat {
 struct {FixturePlane plane;} plane;
 struct {int colormap;} Colormap;
 bool ceiling,foggy;float dz;
 void DrawSubsectorLights(subsector_t*,int);
};
bool gl_SetupLight(Plane&,ADynamicLight *light,Vector &nearPt,Vector &up,Vector &right,float &scale,int,bool,bool)
{
 if(!light->visible)return false;
 nearPt.Set(0,0,0);up.Set(0,0,1);right.Set(1,0,0);scale=1;
 gl_RenderState.BlendEquation(light->equation);
 gles::Color3f(light->color[0],light->color[1],light->color[2]);return true;
}
#include "flat-method.inc"

namespace {
int checks=0,failures=0;
bool useMaterial=false;
std::string shaderDirectory;
EGLDisplay display=EGL_NO_DISPLAY;EGLSurface surface=EGL_NO_SURFACE;EGLContext context=EGL_NO_CONTEXT;EGLConfig config;
void Check(bool value,const std::string &message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message.c_str());}}
void ErrorCheck(const char *where)
{
 GLenum error;while((error=glGetError())!=GL_NO_ERROR){char text[256];snprintf(text,sizeof text,"%s GL error %x",where,error);Check(false,text);}
}
void NewContext()
{
 if(context!=EGL_NO_CONTEXT){gles::Shutdown();eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);}
 const EGLint attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,attrs);
 if(context==EGL_NO_CONTEXT||!eglMakeCurrent(display,surface,surface,context))throw std::runtime_error("GLES context creation failed");
 gles::Initialize();
 // Same raw alpha/blend defaults as OpenGLFrameBuffer::InitializeState.
 gles::Enable(GL_ALPHA_TEST);gles::BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gles::AlphaFunc(GL_GEQUAL,.5f);
#ifdef USE_GLES2
 gl_RenderState.Reset();gl_RenderState.ApplyPipelineState();
#endif
}
std::string Read(const std::string &path){std::ifstream file(path.c_str());if(!file)throw std::runtime_error("Cannot open "+path);std::ostringstream text;text<<file.rdbuf();return text.str();}
GLuint Compile(GLenum type,const std::string &text)
{
 GLuint shader=glCreateShader(type);const char *p=text.c_str();glShaderSource(shader,1,&p,0);glCompileShader(shader);
 GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof log,0,log);throw std::runtime_error(log);}return shader;
}
GLuint Program(const std::string &name)
{
 GLuint v=Compile(GL_VERTEX_SHADER,Read(shaderDirectory+"/"+name+".vert"));
 GLuint f=Compile(GL_FRAGMENT_SHADER,Read(shaderDirectory+"/"+name+".frag"));
 GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
 const char *attributes[]={"aPosition","aTexCoord","aColor","aNormal","glowdistance","fogparams","lightlevel_in","aGLESSplitData"};
 for(unsigned i=0;i<8;++i)glBindAttribLocation(p,i,attributes[i]);
 gles::LinkProgram(p);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[8192];glGetProgramInfoLog(p,sizeof log,0,log);throw std::runtime_error(log);}
 glDeleteShader(v);glDeleteShader(f);return p;
}
GLuint fastProgram=0;
void InitMaterial()
{
 material=FShader();material.handle=Program("normal-0");material.currentfogenabled=material.currenttexturemode=-999;
 material.currentfogcolor=PalEntry(0xdeadbeef);
#define UNIFORM(member,name) material.member=glGetUniformLocation(material.handle,name)
 UNIFORM(fogenabled_index,"fogenabled");UNIFORM(texturemode_index,"texturemode");UNIFORM(camerapos_index,"camerapos");
 UNIFORM(fogcolor_index,"fogcolor");UNIFORM(glowtopcolor_index,"topglowcolor");UNIFORM(glowbottomcolor_index,"bottomglowcolor");
 UNIFORM(lightrange_index,"lightrange");UNIFORM(lights_index,"lights");UNIFORM(dlightcolor_index,"dlightcolor");
#undef UNIFORM
 fastProgram=Program("normal-opaque");gles::RegisterNoClipAlphaVariant(material.handle,fastProgram);
}
void Apply(){gl_RenderState.Apply(!useMaterial);}
void Quad(float l=-.8f,float b=-.8f,float r=.8f,float t=.8f)
{
 gles::Begin(GL_TRIANGLE_STRIP);gles::Vertex3f(l,b,0);gles::Vertex3f(r,b,0);gles::Vertex3f(l,t,0);gles::Vertex3f(r,t,0);gles::End();
}
void TexturedQuad()
{
 gles::Begin(GL_TRIANGLE_STRIP);gles::TexCoord2f(0,0);gles::Vertex3f(-.8f,-.8f,0);gles::TexCoord2f(1,0);gles::Vertex3f(.8f,-.8f,0);
 gles::TexCoord2f(0,1);gles::Vertex3f(-.8f,.8f,0);gles::TexCoord2f(1,1);gles::Vertex3f(.8f,.8f,0);gles::End();
}
void Clear(float r=0,float g=0,float b=0,float a=0){gles::ClearColor(r,g,b,a);gles::Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);}
std::vector<unsigned char> Image(){std::vector<unsigned char> pixels(64*64*4);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&pixels[0]);return pixels;}
int Pixel(const std::vector<unsigned char> &p,int x,int y,int c=0){return p[(y*64+x)*4+c];}
bool Visible(const std::vector<unsigned char> &p){for(size_t i=0;i<p.size();i+=4)if(p[i]||p[i+1]||p[i+2])return true;return false;}
GLuint Texture()
{
 const unsigned char pixels[]={255,255,255,0,255,255,255,64,255,255,255,128,255,255,255,255};GLuint t;glGenTextures(1,&t);
 gles::BindTexture(GL_TEXTURE_2D,t);gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 gles::TexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);return t;
}
GLuint Start(bool direct,bool texture=true)
{
 gles::Flush();gl_direct_state_change=direct;gl_RenderState.Reset();
 gles::Disable(GL_DEPTH_TEST);gles::Disable(GL_DEPTH_CLAMP);gles::Disable(GL_CULL_FACE);gles::Disable(GL_STENCIL_TEST);gles::Disable(GL_SCISSOR_TEST);
 gles::Enable(GL_BLEND);gles::BlendEquation(GL_FUNC_ADD);gles::ColorMask(1,1,1,1);
 // Reset's fixed-function caches assume fresh-context defaults. Restore this
 // native capability explicitly between independent desktop fixture cases.
 gles::ActiveTexture(GL_TEXTURE0);gles::Disable(GL_TEXTURE_2D);gles::MatrixMode(GL_PROJECTION);gles::LoadIdentity();gles::MatrixMode(GL_MODELVIEW);gles::LoadIdentity();
 gles::MatrixMode(GL_TEXTURE);gles::LoadIdentity();gles::MatrixMode(GL_MODELVIEW);gles::Viewport(0,0,64,64);
 gl_RenderState.Set2DMode(!useMaterial);gl_RenderState.EnableTexture(texture);gl_RenderState.SetTextureMode(0);gl_RenderState.EnableFog(false);
 gl_RenderState.SetCameraPos(0,0,0);gl_RenderState.SetLightParms(1,1);gl_RenderState.SetDynLight(0,0,0);gl_RenderState.SetFog(PalEntry(0),0);
 int shaderindex=0,colormap=0;gl_RenderState.SetupShader(false,shaderindex,colormap,0);
 gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl_RenderState.BlendEquation(GL_FUNC_ADD);gl_RenderState.AlphaFunc(GL_GEQUAL,.5f);gl_RenderState.EnableAlphaTest(true);Apply();
 GLuint t=texture?Texture():0;Clear();draws=0;return t;
}
void End(GLuint texture){gles::Flush();if(texture)gles::DeleteTextures(1,&texture);ErrorCheck("test end");}
struct Result{std::vector<unsigned char> image;unsigned draws;};
Result Particles(bool direct)
{
 GLuint t=Start(direct);
 for(unsigned i=0;i<64;++i){
  gl_RenderState.BlendEquation(GL_FUNC_ADD);gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl_RenderState.SetTextureMode(0);gl_RenderState.EnableAlphaTest(false);Apply();
  gles::Color4f((i%3+1)/4.f,(i%5+1)/6.f,(i%7+1)/8.f,(i%11+1)/24.f);TexturedQuad();
  gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl_RenderState.BlendEquation(GL_FUNC_ADD);gl_RenderState.SetTextureMode(0);gl_RenderState.EnableAlphaTest(true);
 }
 Result result={Image(),draws};End(t);return result;
}
Result Additive(bool direct)
{
 GLuint t=Start(direct);
 for(unsigned i=0;i<64;++i){
  gl_RenderState.BlendEquation(GL_FUNC_ADD);gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE);gl_RenderState.AlphaFunc(GL_GEQUAL,.375f);Apply();
  gles::Color4f(.01f,.015f,.02f,.75f);gles::TexCoord2f(.9f,0);Quad();
  gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl_RenderState.BlendEquation(GL_FUNC_ADD);gl_RenderState.AlphaFunc(GL_GEQUAL,.5f);
 }
 Result result={Image(),draws};End(t);return result;
}
Result Mixed(bool direct,bool reverse=false)
{
 GLuint t=Start(direct);const float color[][4]={{.35f,.05f,.1f,.6f},{.2f,.5f,.1f,.8f},{.1f,.15f,.8f,.4f}};
 for(int order=0;order<3;++order){int i=reverse?2-order:order;
  gl_RenderState.BlendFunc(GL_SRC_ALPHA,i==1?GL_ONE_MINUS_SRC_ALPHA:GL_ONE);gl_RenderState.EnableAlphaTest(i==1);gl_RenderState.AlphaFunc(GL_GEQUAL,.5f);Apply();
  gles::Color4fv(color[i]);gles::TexCoord2f(.9f,0);Quad(-.8f+i*.2f,-.8f,.8f,.8f);gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl_RenderState.EnableAlphaTest(true);
 }
 Result result={Image(),draws};End(t);return result;
}
Result FlatLights(bool direct)
{
 GLuint t=Start(direct,false);Clear(.5f,.5f,.5f,1);gl_RenderState.BlendFunc(GL_ONE,GL_ONE);gl_RenderState.EnableAlphaTest(false);
 // Put flat's x/z geometry into XY window plane; culling intentionally off.
 const GLfloat transform[]={1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1};gles::LoadMatrixf(transform);
 vertex_t vertices[]={{-.8f,-.8f},{.8f,-.8f},{.8f,.8f},{-.8f,.8f}};seg_t segments[]={{vertices},{vertices+1},{vertices+2},{vertices+3}};
 ADynamicLight lights[]={
  {0,0,0,0,GL_FUNC_REVERSE_SUBTRACT,{.07f,.03f,.05f},true},
  {MF2_DORMANT,0,0,0,GL_FUNC_ADD,{1,1,1},true},
  {0,0,0,0,GL_FUNC_ADD,{.14f,.03f,.05f},false},
  {0,0,0,0,GL_FUNC_ADD,{.14f,.03f,.05f},true},
  {0,0,0,0,GL_FUNC_REVERSE_SUBTRACT,{.21f,.03f,.05f},true}};
 FLightNode nodes[5];for(int i=0;i<5;++i){nodes[i].lightsource=lights+i;nodes[i].nextLight=i<4?nodes+i+1:0;}
 subsector_t sub={{nodes,nodes},segments,4};GLFlat flat={};iter_dlightf=draw_dlightf=0;flat.DrawSubsectorLights(&sub,0);
 Result result={Image(),draws};Check(iter_dlightf==4&&draw_dlightf==3,"real flat loop skips dormant/rejected lights");End(t);return result;
}
void BatchComparisons()
{
 Result direct=Particles(true),deferred=Particles(false);
 Check(Visible(direct.image),"smooth-particle reference contains visible pixels");
 Check(direct.image==deferred.image,"64 smooth particles complete RGBA image exact");
 Check(direct.draws==64&&deferred.draws==1,"64 smooth particles draw submissions 64 to 1");
 std::printf("%s particles: direct %u draws, deferred %u, full image %s\n",useMaterial?"material":"basic",direct.draws,deferred.draws,direct.image==deferred.image?"exact":"DIFFERENT");
 direct=Additive(true);deferred=Additive(false);
 Check(Visible(direct.image),"additive reference contains visible pixels");Check(direct.image==deferred.image,"64 additive sprites complete RGBA image exact");
 Check(direct.draws==64&&deferred.draws==1,"64 additive sprites draw submissions 64 to 1");
 direct=Mixed(true);deferred=Mixed(false);Result reversed=Mixed(false,true);
 Check(direct.image==deferred.image,"sorted additive/masked/additive complete RGBA image exact");Check(deferred.draws==3,"mixed blend/alpha transitions keep three ordered batches");
 Check(deferred.image!=reversed.image,"negative control: reversing overlapping draws changes image");
}
void Boundaries()
{
 GLuint t=Start(false,false);gl_RenderState.EnableAlphaTest(false);Apply();gles::Color4f(1,0,0,.25f);Quad();gl_RenderState.EnableAlphaTest(true);
 gles::Flush();std::vector<unsigned char> old=Image();Check(Pixel(old,32,32)>0,"explicit Flush keeps applied particle state, not pending restore");
 Apply();gles::Color4f(0,1,0,.25f);Quad();Check(Image()==old,"next Apply alpha test rejects next geometry");
 Clear();Check(!Visible(Image()),"clear boundary clears pending geometry in order");End(t);
 for(int indexed=0;indexed<2;++indexed)for(int apply=0;apply<2;++apply){
  t=Start(false,false);const GLfloat vertices[]={.1f,-.8f,0,.8f,-.8f,0,.8f,.8f,0,.1f,.8f,0};const GLushort indices[]={0,1,2,0,2,3};
  GLuint buffers[2];glGenBuffers(2,buffers);gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);gles::BufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
  gles::VertexPointer(3,GL_FLOAT,0,0);gles::EnableClientState(GL_VERTEX_ARRAY);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);
  gl_RenderState.EnableAlphaTest(false);Apply();gles::Color4f(1,0,0,.25f);Quad(-.8f,-.8f,-.1f,.8f);gl_RenderState.EnableAlphaTest(true);
  if(apply)Apply();gles::Color4f(0,1,0,.25f);
  if(indexed)gles::DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);else gles::DrawArrays(GL_TRIANGLE_FAN,0,4);
  std::vector<unsigned char> image=Image();Check(Pixel(image,16,32)>0,"direct array boundary preserves preceding particle");
  Check((Pixel(image,48,32,1)>0)==!apply,indexed?"EBO draw uses applied state only":"VBO draw uses applied state only");
  gles::DisableClientState(GL_VERTEX_ARRAY);gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);gles::DeleteBuffers(2,buffers);End(t);
 }
 t=Start(false,false);GLuint texture,framebuffer;glGenTextures(1,&texture);gles::BindTexture(GL_TEXTURE_2D,texture);gles::TexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
 glGenFramebuffers(1,&framebuffer);gles::BindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
 Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"boundary fixture FBO complete");Clear();gles::BindFramebuffer(GL_FRAMEBUFFER,0);
 gl_RenderState.EnableAlphaTest(false);Apply();gles::Color4f(1,0,0,.25f);Quad();gl_RenderState.EnableAlphaTest(true);
 gles::BindFramebuffer(GL_FRAMEBUFFER,framebuffer);Check(!Visible(Image()),"FBO switch keeps old geometry off new target");gles::BindFramebuffer(GL_FRAMEBUFFER,0);
 Check(Pixel(Image(),32,32)>0,"FBO switch flushes old target without applying pending restore");
 glDeleteFramebuffers(1,&framebuffer);gles::DeleteTextures(1,&texture);End(t);
 t=Start(false,false);gl_RenderState.EnableAlphaTest(false);Apply();gles::Color4f(1,0,0,.25f);Quad();gl_RenderState.EnableAlphaTest(true);
 Check(Pixel(Image(),32,32)>0,"readback boundary flushes using applied state");
 Clear();gl_RenderState.EnableAlphaTest(false);Apply();gles::Color4f(1,0,0,.25f);Quad();gl_RenderState.EnableAlphaTest(true);
 gles::Flush();Check(eglSwapBuffers(display,surface)==EGL_TRUE,"framebuffer Swap-style Flush then EGL swap succeeds");Check(!gles::IsEnabled(GL_ALPHA_TEST),"swap boundary does not apply pending restore");End(t);
}
void RuntimeToggle()
{
 GLuint t=Start(false,false);gl_RenderState.EnableAlphaTest(false);gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);gl_RenderState.AlphaFunc(GL_GREATER,.75f);gl_RenderState.BlendEquation(GL_FUNC_REVERSE_SUBTRACT);Apply();
 gl_direct_state_change=true;gl_RenderState.EnableAlphaTest(true);gl_RenderState.BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);gl_RenderState.AlphaFunc(GL_GEQUAL,.5f);gl_RenderState.BlendEquation(GL_FUNC_ADD);Apply();
 gl_direct_state_change=false;gl_RenderState.EnableAlphaTest(false);gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);gl_RenderState.AlphaFunc(GL_GREATER,.75f);gl_RenderState.BlendEquation(GL_FUNC_REVERSE_SUBTRACT);Apply();
 GLint source=0,destination=0,equation=0;gles::GetIntegerv(GL_BLEND_SRC_RGB,&source);gles::GetIntegerv(GL_BLEND_DST_RGB,&destination);gles::GetIntegerv(GL_BLEND_EQUATION_RGB,&equation);
 Check(!gles::IsEnabled(GL_ALPHA_TEST)&&source==GL_ONE&&destination==GL_ZERO&&equation==GL_FUNC_REVERSE_SUBTRACT,"runtime 0 to 1 to 0 commits requested alpha/blend/equation");
 gl_RenderState.EnableAlphaTest(true);gl_RenderState.BlendEquation(GL_FUNC_ADD);Apply();gles::Color4f(1,0,0,.6f);Quad();Check(!Visible(Image()),"runtime toggle preserves requested alpha function/threshold");End(t);
#ifdef USE_GLES2
 t=Start(false,false);gl_RenderState.EnableAlphaTest(false);gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);
 Check(gles::IsEnabled(GL_ALPHA_TEST),"deferred setter leaves applied alpha until commit");
 gl_direct_state_change=true;Apply();gles::GetIntegerv(GL_BLEND_SRC_RGB,&source);
 Check(!gles::IsEnabled(GL_ALPHA_TEST)&&source==GL_ONE,"mode toggle then Apply commits pending values without additional setters");
 gl_RenderState.EnableAlphaTest(true);Check(gles::IsEnabled(GL_ALPHA_TEST),"eager setter commits before Apply");End(t);
#endif
}
#ifdef USE_GLES2
void ContextRecreation()
{
 for(int cycle=0;cycle<3;++cycle){
  GLuint t=Start(false,false);gl_RenderState.EnableAlphaTest(false);gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);
  gl_RenderState.AlphaFunc(GL_GREATER,.75f);gl_RenderState.BlendEquation(GL_FUNC_REVERSE_SUBTRACT);Apply();End(t);
  NewContext();GLint source=0,destination=0,equation=0;
  gles::GetIntegerv(GL_BLEND_SRC_RGB,&source);gles::GetIntegerv(GL_BLEND_DST_RGB,&destination);gles::GetIntegerv(GL_BLEND_EQUATION_RGB,&equation);
  Check(gles::IsEnabled(GL_ALPHA_TEST)&&source==GL_SRC_ALPHA&&destination==GL_ONE_MINUS_SRC_ALPHA&&equation==GL_FUNC_ADD,"new context Reset and pipeline commit establish framebuffer defaults");
  // First requested OFF equals Reset's old boolean cache, but differs from
  // native raw startup ON: invalid flag must force complete first commit.
  gl_RenderState.Reset();gl_RenderState.EnableAlphaTest(false);gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);
  gl_RenderState.BlendEquation(GL_FUNC_REVERSE_SUBTRACT);gl_RenderState.ApplyPipelineState();
  gles::GetIntegerv(GL_BLEND_SRC_RGB,&source);gles::GetIntegerv(GL_BLEND_EQUATION_RGB,&equation);
  Check(!gles::IsEnabled(GL_ALPHA_TEST)&&source==GL_ONE&&equation==GL_FUNC_REVERSE_SUBTRACT,"first commit after Reset applies OFF despite same old boolean cache");
  t=Start(false,false);gl_RenderState.EnableAlphaTest(false);Apply();gles::Color4f(1,0,0,.25f);Quad();gl_RenderState.EnableAlphaTest(true);
  unsigned before=draws;gles::Shutdown();Check(draws==before+1,"shutdown flushes queued geometry before deleting adapter state");
  typedef void (*ReadPixelsProc)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
  ReadPixelsProc rawRead=reinterpret_cast<ReadPixelsProc>(eglGetProcAddress("glReadPixels"));
  if(!rawRead)throw std::runtime_error("native glReadPixels unavailable");
  unsigned char pixel[4]={};rawRead(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  Check(pixel[0]>0,"shutdown boundary retains preceding particle pixels without pending restore");
  NewContext();ErrorCheck("context recreation");
 }
}
#endif
}

int main(int argc,char **argv)
{
 try{
  if(argc!=2)throw std::runtime_error("usage: test-renderstate PROGRAM_DIRECTORY");shaderDirectory=argv[1];
#ifdef USE_GLES2
  Check(!gl_direct_state_change,"native default uses deferred state");
#else
  Check(gl_direct_state_change,"desktop default retains direct state");
#endif
  PFNEGLGETPLATFORMDISPLAYEXTPROC platform=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  display=platform?platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,0):eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(display==EGL_NO_DISPLAY||!eglInitialize(display,0,0))throw std::runtime_error("no surfaceless EGL");
  const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
  EGLint n;if(!eglChooseConfig(display,ca,&config,1,&n)||!n)throw std::runtime_error("no RGBA8 D24S8 config");
  const EGLint pa[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};surface=eglCreatePbufferSurface(display,config,pa);eglBindAPI(EGL_OPENGL_ES_API);NewContext();
  std::printf("Actual FRenderState + extracted flat loop + adapter; %s; %s\n",
#ifdef USE_GLES2
  "native branch",
#else
  "desktop branch on fixture transport",
#endif
  glGetString(GL_VERSION));
  BatchComparisons();Boundaries();
#ifdef USE_GLES2
  Result direct=FlatLights(true),deferred=FlatLights(false);Check(direct.image==deferred.image,"real flat-light loop ADD/REVERSE_SUBTRACT complete RGBA exact");Check(direct.draws==3&&deferred.draws==3,"real flat-light equation changes preserve three ordered submissions");
  RuntimeToggle();
  InitMaterial();useMaterial=true;gl.shadermodel=4;BatchComparisons();Boundaries();RuntimeToggle();
  direct=FlatLights(true);deferred=FlatLights(false);Check(direct.image==deferred.image,"real flat-light loop with material shader keeps full image exact");
  useMaterial=false;gl.shadermodel=0;gles::UseProgram(0);gles::DeleteProgram(material.handle);gles::DeleteProgram(fastProgram);
  ContextRecreation();
#endif
  gles::Shutdown();ErrorCheck("shutdown");
 }catch(const std::exception &error){Check(false,error.what());}
 if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);eglTerminate(display);}
 std::printf("Checks: %d; failures: %d\n",checks,failures);return failures?1:0;
}
