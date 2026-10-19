// Pixel tests for the actual native GLES adapter, without linking the engine.
// Build/run instructions and generated material programs: test-gles-renderer.py.
#define GLES_COMPAT_IMPLEMENTATION
#include "gles_compat.h"
#include "gl/shaders/gles_program_cache.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace streamtrace
{
struct Upload { GLenum target,usage; GLsizeiptr bytes; bool data; };
struct Draw { GLint first,stride,buffer; GLsizei count; const void *offset; };
bool active=false;
bool failChmod=false;
bool hideDepthClamp=false;
unsigned subDataCalls=0;
GLuint boundedProgram=0;
GLint boundedFirst=-1,boundedLast=-1;
unsigned boundedUploads=0,boundedMaxCount=0,boundedOverflow=0;
std::vector<Upload> uploads;
std::vector<Draw> draws;
bool attributes=false;
unsigned enables=0,disables=0,pointers=0;
}
// Linker wrappers observe the real adapter's strategy without replacing GL.
extern "C" void __real_glBufferData(GLenum,GLsizeiptr,const void*,GLenum);
extern "C" void __real_glBufferSubData(GLenum,GLintptr,GLsizeiptr,const void*);
extern "C" void __real_glDrawArrays(GLenum,GLint,GLsizei);
extern "C" void __real_glEnableVertexAttribArray(GLuint);
extern "C" void __real_glDisableVertexAttribArray(GLuint);
extern "C" void __real_glVertexAttribPointer(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
extern "C" void __wrap_glEnableVertexAttribArray(GLuint index)
{
	if(streamtrace::attributes)++streamtrace::enables;
	__real_glEnableVertexAttribArray(index);
}
extern "C" void __wrap_glDisableVertexAttribArray(GLuint index)
{
	if(streamtrace::attributes)++streamtrace::disables;
	__real_glDisableVertexAttribArray(index);
}
extern "C" void __wrap_glVertexAttribPointer(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer)
{
	if(streamtrace::attributes)++streamtrace::pointers;
	__real_glVertexAttribPointer(index,size,type,normalized,stride,pointer);
}
extern "C" const GLubyte *__real_glGetString(GLenum);
extern "C" const GLubyte *__wrap_glGetString(GLenum name)
{
	const GLubyte *value=__real_glGetString(name);
	if(name!=GL_EXTENSIONS||!value||!streamtrace::hideDepthClamp)return value;
	static std::string filtered;filtered=reinterpret_cast<const char*>(value);
	std::string token="GL_EXT_depth_clamp";size_t at=filtered.find(token);
	if(at!=std::string::npos)filtered.erase(at,token.size());
	return reinterpret_cast<const GLubyte*>(filtered.c_str());
}
extern "C" void __real_glUniform4fv(GLint,GLsizei,const GLfloat*);
extern "C" void __wrap_glUniform4fv(GLint location,GLsizei count,const GLfloat *value)
{
	if(streamtrace::boundedProgram&&location>=streamtrace::boundedFirst&&location<=streamtrace::boundedLast)
	{
		GLint actual=0;glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
		if(actual==GLint(streamtrace::boundedProgram))
		{
			++streamtrace::boundedUploads;streamtrace::boundedMaxCount=std::max(streamtrace::boundedMaxCount,unsigned(count));
			if(count<0||location+count-1>streamtrace::boundedLast)++streamtrace::boundedOverflow;
		}
	}
	__real_glUniform4fv(location,count,value);
}
extern "C" int __real_chmod(const char*,mode_t);
extern "C" int __wrap_chmod(const char *path,mode_t mode)
{
	if(streamtrace::failChmod) { errno=EPERM;return -1; }
	return __real_chmod(path,mode);
}
extern "C" void __wrap_glBufferData(GLenum target,GLsizeiptr bytes,const void *data,GLenum usage)
{
	if(streamtrace::active) { streamtrace::Upload u={target,usage,bytes,data!=0};streamtrace::uploads.push_back(u); }
	__real_glBufferData(target,bytes,data,usage);
}
extern "C" void __wrap_glBufferSubData(GLenum target,GLintptr offset,GLsizeiptr bytes,const void *data)
{
	if(streamtrace::active) ++streamtrace::subDataCalls;
	__real_glBufferSubData(target,offset,bytes,data);
}
extern "C" void __wrap_glDrawArrays(GLenum mode,GLint first,GLsizei count)
{
	if(streamtrace::active)
	{
		streamtrace::Draw d={first,0,0,count,0};
		glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&d.stride);
		glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&d.buffer);
		glGetVertexAttribPointerv(0,GL_VERTEX_ATTRIB_ARRAY_POINTER,const_cast<void**>(&d.offset));
		streamtrace::draws.push_back(d);
	}
	__real_glDrawArrays(mode,first,count);
}

void I_FatalError(const char *format, ...)
{
	char message[8192]; va_list args; va_start(args,format);
	vsnprintf(message,sizeof message,format,args); va_end(args);
	throw std::runtime_error(message);
}

namespace
{
int checks=0, failures=0;
void Check(bool ok,const std::string &name)
{
	++checks; if(!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",name.c_str()); }
}
void ErrorCheck(const char *name)
{
	GLenum error; while((error=glGetError())!=GL_NO_ERROR)
	{
		char message[256]; std::snprintf(message,sizeof message,"%s: GL error 0x%x",name,error);
		Check(false,message);
	}
}
struct Pixel { unsigned char r,g,b,a; };
Pixel Read(int x=32,int y=32)
{
	Pixel p; gles::ReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&p); return p;
}
bool Near(int x,int y,int tolerance=2) { return std::abs(x-y)<=tolerance; }
void Expect(const char *name,int r,int g,int b,int a,int x=32,int y=32)
{
	Pixel p=Read(x,y);
	if(!(Near(p.r,r)&&Near(p.g,g)&&Near(p.b,b)&&Near(p.a,a)))
	{
		char message[512]; std::snprintf(message,sizeof message,
			"%s: got (%d,%d,%d,%d), expected (%d,%d,%d,%d)",name,p.r,p.g,p.b,p.a,r,g,b,a);
		Check(false,message);
	}
	else Check(true,name);
}
void Clear()
{
	gles::ClearColor(0,0,0,0); gles::Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
}
void Identity()
{
	gles::ActiveTexture(GL_TEXTURE7); gles::MatrixMode(GL_TEXTURE); gles::LoadIdentity();
	gles::ActiveTexture(GL_TEXTURE0); gles::MatrixMode(GL_TEXTURE); gles::LoadIdentity();
	gles::MatrixMode(GL_PROJECTION); gles::LoadIdentity();
	gles::MatrixMode(GL_MODELVIEW); gles::LoadIdentity();
}
void Reset()
{
	gles::UseProgram(0); Identity(); gles::SetTextureMode(0);
	gles::ActiveTexture(GL_TEXTURE1); gles::Disable(GL_TEXTURE_2D);
	gles::ActiveTexture(GL_TEXTURE0); gles::Disable(GL_TEXTURE_2D);
	gles::Disable(GL_ALPHA_TEST); gles::Disable(GL_DEPTH_CLAMP);
	gles::Disable(GL_DEPTH_TEST); gles::Disable(GL_CULL_FACE); gles::Disable(GL_BLEND);
	gles::Disable(GL_STENCIL_TEST); gles::Disable(GL_SCISSOR_TEST);
	gles::Disable(GL_LINE_SMOOTH); gles::LineWidth(1);
	for(int i=0;i<6;++i) gles::Disable(GL_CLIP_PLANE0+i);
	for(int i=0;i<8;++i) gles::DisableVertexAttribArray(i);
	gles::Color4f(1,1,1,1); gles::TexCoord2f(0.5f,0.5f);
	gles::MultiTexCoord2f(GL_TEXTURE1,0.5f,0.5f);
	gles::Viewport(0,0,64,64); Clear();
}
void Quad(float left=-0.8f,float bottom=-0.8f,float right=0.8f,float top=0.8f,float z=0)
{
	gles::Begin(GL_QUADS); gles::Vertex3f(left,bottom,z); gles::Vertex3f(right,bottom,z);
	gles::Vertex3f(right,top,z); gles::Vertex3f(left,top,z); gles::End();
}
GLuint Texture(const unsigned char *pixels,int width=1,int height=1)
{
	GLuint texture; glGenTextures(1,&texture); gles::BindTexture(GL_TEXTURE_2D,texture);
	gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	gles::TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
	return texture;
}
void TestBasicSolid()
{
	// Alpha ALWAYS forces the generic sibling without changing its pixels.
	Reset();gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_ALWAYS,0);Quad();gles::Flush();
	GLint generic=0;glGetIntegerv(GL_CURRENT_PROGRAM,&generic);
	gles::Disable(GL_ALPHA_TEST);Quad();gles::Flush();
	GLint solid=0,logical=-1;glGetIntegerv(GL_CURRENT_PROGRAM,&solid);gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);
	Check(solid!=generic&&logical==0,"solid sibling selected with logical program0 preserved");
	Check(glGetUniformLocation(solid,"uTextureEnabled")==-1&&glGetUniformLocation(solid,"uClipActive")==-1,
		"solid sibling contains no texture/clip branches");
	GLuint buffers[2];glGenBuffers(2,buffers);
	const GLushort indices[]={0,1,2,0,2,3};
	const GLenum blends[][2]={{GL_ONE,GL_ZERO},{GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA},{GL_DST_COLOR,GL_ZERO},{GL_ONE,GL_ONE}};
	unsigned compared=0;
	for(unsigned path=0;path<3;++path)for(unsigned blend=0;blend<4;++blend)
	for(unsigned transform=0;transform<3;++transform)for(unsigned alpha=0;alpha<4;++alpha)for(unsigned depth=0;depth<2;++depth)
	{
		GLfloat vertices[][7]={{-.8f,-.8f,0,1,.25f,.5f,alpha/3.f},{.8f,-.8f,0,.25f,1,.5f,alpha/3.f},
			{.8f,.8f,0,.5f,.25f,1,alpha/3.f},{-.8f,.8f,0,.75f,.5f,.25f,alpha/3.f}};
		if(depth){vertices[0][2]=-3;vertices[1][2]=-.2f;vertices[2][2]=3;vertices[3][2]=.4f;}
		std::vector<Pixel> images[2];
		for(unsigned reference=0;reference<2;++reference)
		{
			Reset();gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
			gles::ClearColor(.125f,.25f,.375f,.5f);gles::Clear(GL_COLOR_BUFFER_BIT);
			if(reference){gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_ALWAYS,0);}
			if(depth)gles::Enable(GL_DEPTH_CLAMP);
			gles::Enable(GL_BLEND);gles::BlendFunc(blends[blend][0],blends[blend][1]);
			if(transform){gles::MatrixMode(GL_PROJECTION);gles::Ortho(-1.2,1.1,-1.1,1.2,-2,2);
				gles::MatrixMode(GL_MODELVIEW);gles::Translatef(.13f,-.07f,0);gles::Rotatef(17.f*transform,0,0,1);gles::Scalef(.8f,1.1f,1);}
			if(!path)
			{
				gles::Begin(GL_QUADS);
				for(unsigned i=0;i<4;++i){gles::Color4f(vertices[i][3],vertices[i][4],vertices[i][5],vertices[i][6]);gles::Vertex3f(vertices[i][0],vertices[i][1],vertices[i][2]);}
				gles::End();
			}
			else
			{
				if(path==2){gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);gles::BufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
					gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);}
				gles::VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof vertices[0],path==2?0:vertices);
				gles::VertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof vertices[0],path==2?reinterpret_cast<void*>(3*sizeof(GLfloat)):&vertices[0][3]);
				gles::EnableVertexAttribArray(0);gles::EnableVertexAttribArray(2);
				gles::DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,path==2?0:indices);
			}
			gles::Flush();GLint actual=0;glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
			Check(actual==(reference?generic:solid),"solid/generic physical program follows current draw state");
			images[reference].resize(64*64);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&images[reference][0]);
		}
		Check(!std::memcmp(&images[0][0],&images[1][0],images[0].size()*sizeof(Pixel)),"solid overlay exact pixels across blends/alpha/transforms/array paths");++compared;
	}
	// Each disqualifying state must restore the full generic shader.
	for(unsigned state=0;state<15;++state)
	{
		Reset();gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
		if(state==0)gles::Enable(GL_TEXTURE_2D);
		else if(state==1){gles::ActiveTexture(GL_TEXTURE1);gles::Enable(GL_TEXTURE_2D);gles::ActiveTexture(GL_TEXTURE0);}
		else if(state==2){gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_ALWAYS,0);}
		else if(state<9)gles::Enable(GL_CLIP_PLANE0+state-3);
		else if(state==9)gles::Enable(GL_DEPTH_CLAMP);
		else if(state<14){const int modes[]={1,2,4,6};gles::SetTextureMode(modes[state-10]);}
		else gles::Enable(GL_LINE_SMOOTH);
		Quad();gles::Flush();GLint actual=0;glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
		Check(actual==(state==9?solid:generic),"solid sibling gates texture/burn/alpha/clip/mode/line; preserves depth clamp "+std::to_string(state));
	}
	Reset();gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);gles::DeleteBuffers(2,buffers);
	std::printf("Solid overlays: %u exact full-image generic/fast comparisons\n",compared);ErrorCheck("solid overlays");
}
void TestImmediateAndMatrices()
{
	Reset(); gles::Color4ub(255,64,128,255); Quad(); Expect("quad",255,64,128,255);
	Clear(); gles::Begin(GL_TRIANGLE_FAN);
	gles::Vertex2f(-0.8f,-0.8f); gles::Vertex2f(0.8f,-0.8f);
	gles::Vertex2f(0.8f,0.8f); gles::Vertex2f(-0.8f,0.8f); gles::End();
	Expect("triangle fan",255,64,128,255);
	Clear(); gles::Enable(GL_CULL_FACE); gles::FrontFace(GL_CCW);
	gles::Begin(GL_TRIANGLE_STRIP); gles::Vertex2f(-0.8f,-0.8f); gles::Vertex2f(0.8f,-0.8f);
	gles::Vertex2f(-0.8f,0.8f); gles::Vertex2f(0.8f,0.8f); gles::End();
	Expect("triangle strip lower winding",255,64,128,255,40,20);
	Expect("triangle strip upper winding",255,64,128,255,40,45);
	gles::Disable(GL_CULL_FACE);
	Clear(); gles::Begin(GL_QUAD_STRIP); gles::Vertex2f(-0.8f,-0.8f); gles::Vertex2f(-0.8f,0.8f);
	gles::Vertex2f(0.8f,-0.8f); gles::Vertex2f(0.8f,0.8f); gles::End();
	Expect("quad strip",255,64,128,255);
	Clear(); gles::PushMatrix(); gles::Translatef(0.5f,0,0); gles::Scalef(0.25f,0.25f,1);
	Quad(); gles::PopMatrix();
	Expect("translated scaled matrix",255,64,128,255,48,32);
	Expect("translated matrix leaves origin",0,0,0,0,32,32);
	GLfloat matrix[16]; gles::GetFloatv(GL_MODELVIEW_MATRIX,matrix);
	bool identity=true; for(int i=0;i<16;++i) identity=identity&&std::abs(matrix[i]-(i%5==0?1:0))<0.00001f;
	Check(identity,"matrix push/pop identity");
	gles::Rotatef(90,0,0,1); gles::Translatef(0.5f,0,0); gles::GetFloatv(GL_MODELVIEW_MATRIX,matrix);
	Check(std::abs(matrix[12])<0.0001f&&std::abs(matrix[13]-0.5f)<0.0001f,"column-major postmultiply rotation");
	Identity(); Clear();
	// A matrix change must flush earlier vertices using the previous matrix.
	gles::Color4f(1,0,0,1); Quad(-0.9f,-0.4f,-0.5f,0.4f);
	gles::Translatef(1.4f,0,0); gles::Color4f(0,1,0,1); Quad(-0.9f,-0.4f,-0.5f,0.4f);
	Expect("matrix change flush old vertices",255,0,0,255,10,32);
	Expect("matrix change new vertices",0,255,0,255,54,32);
	ErrorCheck("immediate/matrices");
}
void TestAlphaAndClipping()
{
	Reset(); gles::Enable(GL_ALPHA_TEST);
	const GLenum functions[]={GL_NEVER,GL_LESS,GL_EQUAL,GL_LEQUAL,GL_GREATER,GL_NOTEQUAL,GL_GEQUAL,GL_ALWAYS};
	const float alphas[]={0.25f,0.5f,0.75f};
	for(unsigned f=0;f<8;++f) for(unsigned a=0;a<3;++a)
	{
		Clear(); gles::AlphaFunc(functions[f],0.5f); gles::Color4f(1,0,0,alphas[a]); Quad();
		bool pass=f==0?false:f==1?alphas[a]<0.5f:f==2?alphas[a]==0.5f:f==3?alphas[a]<=0.5f:
			f==4?alphas[a]>0.5f:f==5?alphas[a]!=0.5f:f==6?alphas[a]>=0.5f:true;
		char name[128]; std::snprintf(name,sizeof name,"alpha func 0x%x value %.2f",functions[f],alphas[a]);
		Expect(name,pass?255:0,0,0,pass?int(std::lround(alphas[a]*255)):0);
	}
	Reset();
	for(int plane=0;plane<6;++plane)
	{
		Clear(); const GLdouble equation[]={1,0,0,0};
		gles::ClipPlane(GL_CLIP_PLANE0+plane,equation); gles::Enable(GL_CLIP_PLANE0+plane); Quad();
		Expect("clip plane left",0,0,0,0,16,32); Expect("clip plane right",255,255,255,255,48,32);
		gles::Disable(GL_CLIP_PLANE0+plane);
	}
	// glClipPlane captures inverse-transpose of modelview at specification time.
	Clear(); gles::Translatef(0.5f,0,0); const GLdouble translated[]={1,0,0,0};
	gles::ClipPlane(GL_CLIP_PLANE0,translated); gles::LoadIdentity(); gles::Enable(GL_CLIP_PLANE0); Quad();
	Expect("eye-space clip captured transform",0,0,0,0,40,32);
	Expect("eye-space clip visible side",255,255,255,255,52,32);
	Reset(); Quad(-0.8f,-0.8f,0.8f,0.8f,2);
	Expect("depth clipping",0,0,0,0); gles::Enable(GL_DEPTH_CLAMP); Quad(-0.8f,-0.8f,0.8f,0.8f,2);
	Expect("depth clamp",255,255,255,255);
	ErrorCheck("alpha/clipping/depthclamp");
}
void TestTexturesAndBurn()
{
	Reset(); const unsigned char texel[]={64,128,192,96}; GLuint texture=Texture(texel); gles::Enable(GL_TEXTURE_2D);
	const int modes[]={0,1,2,4,6}; const int expected[][4]={{64,128,192,96},{255,255,255,96},{64,128,192,255},{191,127,63,96},{191,127,63,255}};
	for(int i=0;i<5;++i)
	{
		Clear(); gles::SetTextureMode(modes[i]); Quad(); char name[80]; std::snprintf(name,sizeof name,"texture mode %d",modes[i]);
		Expect(name,expected[i][0],expected[i][1],expected[i][2],expected[i][3]);
	}
	gles::SetTextureMode(0); Clear(); gles::Color4f(0.5f,1,0.25f,0.5f); Quad(); Expect("texture vertex modulation",32,128,48,48);
	Clear(); gles::Color4f(1,1,1,1); Quad(-0.9f,-0.4f,-0.1f,0.4f);
	const unsigned char green[]={0,255,0,255}; GLuint other=Texture(green); Quad(0.1f,-0.4f,0.9f,0.4f);
	Expect("texture bind flush previous draw",64,128,192,96,16,32);
	Expect("texture bind new draw",0,255,0,255,48,32);
	Clear(); gles::BindTexture(GL_TEXTURE_2D,texture);
	gles::ActiveTexture(GL_TEXTURE1); gles::BindTexture(GL_TEXTURE_2D,other); gles::Enable(GL_TEXTURE_2D);
	gles::ActiveTexture(GL_TEXTURE0); Quad(); Expect("burn mask alpha/color texture combine",0,255,0,96);
	gles::ActiveTexture(GL_TEXTURE1); gles::Disable(GL_TEXTURE_2D); gles::ActiveTexture(GL_TEXTURE0);
	// A nonidentity texture matrix must affect sampling, independent of view.
	const unsigned char stripe[]={255,0,0,255,0,0,255,255}; GLuint patterned=Texture(stripe,2,1);
	Clear(); gles::TexCoord2f(0.25f,0.5f); Quad(); Expect("texture matrix identity",255,0,0,255);
	gles::MatrixMode(GL_TEXTURE); gles::Translatef(0.5f,0,0); Clear(); Quad(); Expect("texture matrix translation",0,0,255,255);
	gles::MatrixMode(GL_MODELVIEW);
	// Readback converts desktop RGB/BGR/BGRA with pack-aligned row strides.
	unsigned char rgb[8]={0},bgr[8]={0},bgra[8]={0};
	gles::ReadPixels(32,32,1,2,GL_RGB,GL_UNSIGNED_BYTE,rgb);
	gles::ReadPixels(32,32,1,2,GL_BGR,GL_UNSIGNED_BYTE,bgr);
	gles::ReadPixels(32,32,1,2,GL_BGRA,GL_UNSIGNED_BYTE,bgra);
	Check(rgb[2]==255&&rgb[6]==255&&bgr[0]==255&&bgr[4]==255&&bgra[0]==255&&bgra[3]==255,"aligned RGB/BGR/BGRA readback");
	glPixelStorei(GL_PACK_ALIGNMENT,8);unsigned char aligned[16]={0};
	gles::ReadPixels(32,32,1,2,GL_BGR,GL_UNSIGNED_BYTE,aligned);
	Check(aligned[0]==255&&aligned[8]==255,"pack8 BGR readback row stride");
	std::memset(aligned,0,sizeof aligned);gles::ReadPixels(32,32,1,2,GL_BGRA,GL_UNSIGNED_BYTE,aligned);
	Check(aligned[0]==255&&aligned[3]==255&&aligned[8]==255&&aligned[11]==255,"pack8 BGRA readback row stride");
	glPixelStorei(GL_PACK_ALIGNMENT,4);
	gles::DeleteTextures(1,&texture); gles::DeleteTextures(1,&other); gles::DeleteTextures(1,&patterned);
	ErrorCheck("textures/burn/readback");
}
void TestBuffers()
{
	Reset(); const GLfloat vertices[]={-0.8f,-0.8f,0,0.8f,-0.8f,0,0.8f,0.8f,0,-0.8f,0.8f,0};
	GLuint vbo,ebo; glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
	gles::BindBuffer(GL_ARRAY_BUFFER,vbo); gles::BufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
	gles::VertexPointer(3,GL_FLOAT,0,0); gles::EnableClientState(GL_VERTEX_ARRAY);
	gles::Color4f(1,0,0,1); gles::DrawArrays(GL_TRIANGLE_FAN,0,4); Expect("VBO native triangle fan",255,0,0,255);
	Clear(); gles::Color4f(0,1,0,1); gles::DrawArrays(GL_QUADS,0,4); Expect("VBO quad expansion",0,255,0,255);
	const GLushort ushortIndices[]={0,1,2,0,2,3};
	gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo); gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof ushortIndices,ushortIndices,GL_STATIC_DRAW);
	Clear(); gles::Color4f(0,0,1,1); gles::DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0); Expect("EBO native ushort",0,0,255,255);
	const GLuint uintIndices[]={0,1,2,3};
	gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof uintIndices,uintIndices,GL_STATIC_DRAW);
	Clear(); gles::Color4f(1,1,0,1); gles::DrawElements(GL_QUADS,4,GL_UNSIGNED_INT,0); Expect("EBO uint quad expansion",255,255,0,255);
	GLfloat *mapped=static_cast<GLfloat*>(gles::MapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY));
	for(int i=0;i<4;++i) mapped[i*3]+=1.5f;
	Check(gles::UnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE,"VBO shadow mapping unmap");
	Clear(); gles::DrawArrays(GL_TRIANGLE_FAN,0,4); Expect("mapped VBO moved origin",0,0,0,0,32,32);
	Expect("mapped VBO moved target",255,255,0,255,60,32);
	gles::BufferSubData(GL_ARRAY_BUFFER,0,sizeof vertices,vertices);
	Clear(); gles::DrawArrays(GL_TRIANGLE_FAN,0,4); Expect("VBO subdata restores origin",255,255,0,255);
	gles::BindBuffer(GL_ARRAY_BUFFER,0); gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
	gles::VertexPointer(3,GL_FLOAT,0,vertices); Clear(); gles::DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,ushortIndices);
	Expect("client vertex/index arrays",255,255,0,255);
	gles::DisableClientState(GL_VERTEX_ARRAY); gles::DeleteBuffers(1,&vbo); gles::DeleteBuffers(1,&ebo);
	ErrorCheck("buffers");
}
void TestBatchStorage()
{
	Reset();
	// Keep a caller-owned VBO bound across stream uploads. Later draw must
	// still use its original data and the adapter's captured array binding.
	const GLfloat triangle[]={-0.9f,-0.9f,0,-0.5f,-0.9f,0,-0.7f,-0.5f,0};
	GLuint callerVBO;glGenBuffers(1,&callerVBO);gles::BindBuffer(GL_ARRAY_BUFFER,callerVBO);
	gles::BufferData(GL_ARRAY_BUFFER,sizeof triangle,triangle,GL_STATIC_DRAW);
	gles::VertexPointer(3,GL_FLOAT,0,0);gles::EnableClientState(GL_VERTEX_ARRAY);
	streamtrace::uploads.clear();streamtrace::draws.clear();streamtrace::subDataCalls=0;
	streamtrace::uploads.reserve(64);streamtrace::draws.reserve(64);streamtrace::active=true;
	for(int i=0;i<64;++i)
	{
		gles::LoadIdentity();gles::Translatef(-0.875f+(i%8)*0.25f,-0.875f+(i/8)*0.25f,0);
		gles::Color4ub((i%8)*32,(i/8)*32,255-i*3,255);
		// Vary actual batch size, not just contents. No read/finish/swap between
		// uploads: earlier queued draws must survive later storage replacement.
		for(int copy=0;copy<1+i%4;++copy) Quad(-0.09375f,-0.09375f,0.09375f,0.09375f);
		gles::Flush();
	}
	streamtrace::active=false;
	Check(streamtrace::subDataCalls==0,"stream batches never subupdate in-flight storage");
	bool exact=streamtrace::uploads.size()==64&&streamtrace::draws.size()==64;
	for(size_t i=0;exact&&i<64;++i)
	{
		const streamtrace::Upload &u=streamtrace::uploads[i];const streamtrace::Draw &d=streamtrace::draws[i];
		exact=u.target==GL_ARRAY_BUFFER&&u.usage==GL_STREAM_DRAW&&u.data&&d.first==0&&d.offset==0
			&&d.stride>0&&u.bytes==d.count*d.stride&&d.count==6*(1+int(i)%4)
			&&d.buffer!=0&&GLuint(d.buffer)!=callerVBO&&d.buffer==streamtrace::draws[0].buffer;
	}
	Check(exact,"each flush replaces exact batch storage at zero attribute offset");
	GLint bound=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&bound);
	Check(GLuint(bound)==callerVBO,"stream flush restores caller VBO binding");
	// First readback only after all 64 uploads/draws; checks geometry and
	// vertex color + transform snapshots across repeated storage replacement.
	std::vector<Pixel> pixels(64*64);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&pixels[0]);
	for(int i=0;i<64;++i)
	{
		const Pixel &p=pixels[(4+(i/8)*8)*64+4+(i%8)*8];char name[96];
		std::snprintf(name,sizeof name,"queued stream batch %d retains geometry/color/matrix",i);
		Check(Near(p.r,(i%8)*32)&&Near(p.g,(i/8)*32)&&Near(p.b,255-i*3)&&p.a==255,name);
	}
	gles::LoadIdentity();Clear();gles::Color4f(1,0,1,1);gles::DrawArrays(GL_TRIANGLES,0,3);
	Expect("caller VBO survives stream store replacement",255,0,255,255,10,8);
	gles::DisableClientState(GL_VERTEX_ARRAY);gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::DeleteBuffers(1,&callerVBO);
	ErrorCheck("batch storage replacement");
}
void TestAttributeState()
{
	Reset();Quad();gles::Flush();
	streamtrace::enables=streamtrace::disables=streamtrace::pointers=0;streamtrace::attributes=true;
	for(unsigned i=0;i<64;++i)
	{
		gles::Color4f(i&1,0,1,1);Quad();gles::Flush();
	}
	Check(streamtrace::enables==0&&streamtrace::disables==0&&streamtrace::pointers==0,
		"64 stream replacements retain attribute layout without redundant setters");
	Expect("last stream replacement retains new vertex color",255,0,255,255);
	const GLfloat vertices[]={-0.8f,-0.8f,0,0.8f,-0.8f,0,0.8f,0.8f,0,-0.8f,0.8f,0};
	GLfloat shifted[12];std::memcpy(shifted,vertices,sizeof shifted);for(unsigned i=0;i<4;++i)shifted[i*3]+=2;
	GLuint buffers[3];glGenBuffers(3,buffers);
	gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);gles::BufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
	gles::VertexPointer(3,GL_FLOAT,0,0);gles::EnableClientState(GL_VERTEX_ARRAY);
	Clear();gles::Color4f(1,0,0,1);gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("stream to caller array",255,0,0,255);
	unsigned before=streamtrace::pointers;
	Clear();gles::Color4f(0,1,0,1);gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("disabled array constant color updates",0,255,0,255);
	Check(streamtrace::pointers==before,"unchanged caller array retains layout");
	gles::BindBuffer(GL_ARRAY_BUFFER,buffers[1]);gles::BufferData(GL_ARRAY_BUFFER,sizeof shifted,shifted,GL_STATIC_DRAW);
	gles::VertexPointer(3,GL_FLOAT,0,0);Clear();gles::DrawArrays(GL_TRIANGLE_FAN,0,4);
	Expect("same offset on different VBO selects different geometry",0,0,0,0);
	Check(streamtrace::pointers==before+1,"attribute association includes VBO name");
	before=streamtrace::pointers;
	gles::BufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STREAM_DRAW);
	Clear();gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("caller storage replacement retains association",0,255,0,255);
	Check(streamtrace::pointers==before,"storage replacement needs no pointer reset");
	struct Packed {GLfloat position[3];GLubyte colors[8];};
	Packed packed[4];for(unsigned i=0;i<4;++i)
	{
		std::memcpy(packed[i].position,vertices+i*3,3*sizeof(GLfloat));
		const GLubyte colors[]={128,0,0,255,0,128,0,255};std::memcpy(packed[i].colors,colors,8);
	}
	gles::BindBuffer(GL_ARRAY_BUFFER,buffers[2]);gles::BufferData(GL_ARRAY_BUFFER,sizeof packed,packed,GL_STATIC_DRAW);
	gles::VertexPointer(3,GL_FLOAT,sizeof(Packed),0);
	gles::VertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Packed),reinterpret_cast<const void*>(offsetof(Packed,colors)));
	gles::EnableVertexAttribArray(2);gles::Color4f(1,1,1,1);
	Clear();gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("interleaved normalized byte colors",128,0,0,255);
	gles::VertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Packed),reinterpret_cast<const void*>(offsetof(Packed,colors)+4));
	Clear();gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("changed color offset",0,128,0,255);
	gles::VertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,sizeof(Packed),reinterpret_cast<const void*>(offsetof(Packed,colors)+4));
	Clear();gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("changed color normalization",0,255,0,255);
	gles::VertexAttribPointer(2,3,GL_UNSIGNED_BYTE,GL_FALSE,sizeof(Packed),reinterpret_cast<const void*>(offsetof(Packed,colors)+4));
	Clear();gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("changed component count supplies default alpha",0,255,0,255);
	GLint size=0,type=0,stride=0,normalized=0;glGetVertexAttribiv(2,GL_VERTEX_ATTRIB_ARRAY_SIZE,&size);
	Check(size==3,"component count reaches driver");
	gles::VertexAttribPointer(2,3,GL_BYTE,GL_FALSE,sizeof(Packed),reinterpret_cast<const void*>(offsetof(Packed,colors)+4));
	gles::DrawArrays(GL_POINTS,0,1);glGetVertexAttribiv(2,GL_VERTEX_ATTRIB_ARRAY_TYPE,&type);
	Check(type==GL_BYTE,"changed component type reaches driver independently");
	gles::VertexAttribPointer(2,3,GL_BYTE,GL_FALSE,0,reinterpret_cast<const void*>(offsetof(Packed,colors)+4));
	gles::DrawArrays(GL_POINTS,0,1);
	glGetVertexAttribiv(2,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);glGetVertexAttribiv(2,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
	Check(stride==0&&normalized==GL_FALSE,"changed stride reaches driver independently");
	// Stream/caller transitions must restore enables, layout and constants.
	Clear();gles::Color4f(0,0,1,1);Quad();gles::Flush();Expect("caller to stream layout",0,0,255,255);
	gles::DisableVertexAttribArray(2);gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);gles::VertexPointer(3,GL_FLOAT,0,0);
	Clear();gles::Color4f(1,1,0,1);gles::DrawArrays(GL_TRIANGLE_FAN,0,4);Expect("stream to constant caller color",255,255,0,255);
	gles::DisableClientState(GL_VERTEX_ARRAY);gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::DeleteBuffers(3,buffers);
	streamtrace::attributes=false;ErrorCheck("attribute cache transitions");
}
void TestAttributeContexts(EGLDisplay display,EGLConfig config,EGLSurface surface,EGLContext original)
{
	gles::Shutdown();
	const EGLint attributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
	for(unsigned i=0;i<2;++i)
	{
		EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,attributes);
		if(context==EGL_NO_CONTEXT||!eglMakeCurrent(display,surface,surface,context))
			throw std::runtime_error("attribute test context creation failed");
		gles::Initialize();TestBasicSolid();TestAttributeState();gles::Shutdown();
		eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);
	}
	if(!eglMakeCurrent(display,surface,surface,original))throw std::runtime_error("attribute test context restore failed");
	gles::Initialize();TestBasicSolid();TestAttributeState();
}
void TestFramebufferFormats()
{
	// Exercise both camera attachment layouts, including GLES2 core fallback.
	for(int packed=0;packed<2;++packed)
	{
		Reset(); GLuint framebuffer,depth,stencil=0;
		std::vector<unsigned char> pixels(64*64*4,0); GLuint color=Texture(&pixels[0],64,64);
		glGenFramebuffers(1,&framebuffer); gles::BindFramebuffer(GL_FRAMEBUFFER,framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
		glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
		glRenderbufferStorage(GL_RENDERBUFFER,packed?GL_DEPTH24_STENCIL8:GL_DEPTH_COMPONENT16,64,64);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
		if(!packed)
		{
			glGenRenderbuffers(1,&stencil); glBindRenderbuffer(GL_RENDERBUFFER,stencil);
			glRenderbufferStorage(GL_RENDERBUFFER,GL_STENCIL_INDEX8,64,64);
		}
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,packed?depth:stencil);
		GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if(!packed && status!=GL_FRAMEBUFFER_COMPLETE)
		{
			// GLES2 does not guarantee this attachment combination. This negative
			// case prevents treating separate objects as a portable fallback.
			Check(status==GL_FRAMEBUFFER_UNSUPPORTED,"separate D16/S8 unsupported combination is reported");
			std::printf("Separate D16/S8 camera framebuffer unsupported (0x%x); packed format required.\n",status);
			gles::BindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
			glDeleteRenderbuffers(1,&depth); glDeleteRenderbuffers(1,&stencil); gles::DeleteTextures(1,&color);
			ErrorCheck("unsupported separate camera FBO"); continue;
		}
		Check(status==GL_FRAMEBUFFER_COMPLETE,packed?"packed D24S8 camera framebuffer":"supported separate D16/S8 camera framebuffer");
		Clear(); gles::Enable(GL_STENCIL_TEST); gles::StencilFunc(GL_ALWAYS,1,0xff);
		gles::StencilOp(GL_REPLACE,GL_REPLACE,GL_REPLACE); gles::ColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
		Quad(-0.8f,-0.8f,0,0.8f); gles::ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
		gles::StencilFunc(GL_EQUAL,1,0xff); gles::StencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
		gles::Color4f(0,1,0,1); Quad();
		Expect("camera stencil accepted",0,255,0,255,16,32); Expect("camera stencil rejected",0,0,0,0,48,32);
		gles::Disable(GL_STENCIL_TEST); gles::Enable(GL_DEPTH_TEST); gles::DepthFunc(GL_LESS);
		gles::DepthMask(GL_TRUE); Clear();
		gles::Color4f(1,0,0,1); Quad(-0.8f,-0.8f,0.8f,0.8f,0.5f);
		gles::Color4f(0,0,1,1); Quad(-0.8f,-0.8f,0.8f,0.8f,-0.5f);
		gles::Color4f(0,1,0,1); Quad(-0.8f,-0.8f,0.8f,0.8f,0.75f);
		Expect("camera depth ordering",0,0,255,255);
		gles::BindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
		glDeleteRenderbuffers(1,&depth); if(stencil) glDeleteRenderbuffers(1,&stencil); gles::DeleteTextures(1,&color);
		ErrorCheck(packed?"packed camera FBO":"separate camera FBO");
	}
}
void Line(float x0,float y0,float x1,float y1)
{
	gles::Begin(GL_LINES); gles::Vertex2f(x0,y0); gles::Vertex2f(x1,y1); gles::End();
}
void ExpectAlpha(const char *name,int alpha,int x,int y)
{
	Pixel p=Read(x,y);
	if(!Near(p.a,alpha))
	{
		char message[256]; std::snprintf(message,sizeof message,"%s: alpha %d expected %d",name,p.a,alpha);
		Check(false,message);
	}
	else Check(true,name);
}
void TestSmoothLines()
{
	Reset(); Line(-0.5f,0.0078125f,0.5f,0.0078125f);
	ExpectAlpha("aliased line solid pixel",255,32,32); ExpectAlpha("aliased line neighboring pixel",0,32,31);
	Clear(); gles::Enable(GL_LINE_SMOOTH); gles::Enable(GL_ALPHA_TEST); gles::AlphaFunc(GL_GEQUAL,0.5f);
	Line(-0.4921875f,0.0078125f,0.5078125f,0.0078125f);
	ExpectAlpha("smooth horizontal inner coverage",191,32,32);
	ExpectAlpha("smooth coverage survives alpha test",64,32,31);
	ExpectAlpha("smooth endpoint fractional coverage",143,16,32);
	ExpectAlpha("smooth endpoint outer coverage",0,15,32);
	Clear(); Line(-0.4921875f,-0.4921875f,0.5078125f,0.5078125f);
	ExpectAlpha("smooth diagonal center",255,32,32); ExpectAlpha("smooth diagonal edge",75,32,33);
	Clear(); gles::Viewport(8,12,32,24); Line(-0.5f,0.020833333f,0.5f,0.020833333f);
	ExpectAlpha("smooth line changed viewport inner",191,24,24);
	ExpectAlpha("smooth line changed viewport edge",64,24,23);
	gles::Viewport(0,0,64,64); Clear(); gles::LineWidth(3); Line(-0.5f,0.0078125f,0.5f,0.0078125f);
	ExpectAlpha("smooth width3 center",255,32,32); ExpectAlpha("smooth width3 outer edge",64,32,30);
	// Queued AA triangle expansion and an ordinary triangle must have distinct
	// uniform state even though their submitted primitive types match.
	gles::Color4f(0,1,0,1); Quad(-0.4f,0.3f,0.4f,0.7f);
	Expect("ordinary triangle after AA batch",0,255,0,255,32,48);
	Clear(); gles::LineWidth(1); Line(-0.5f,0.0078125f,0.5f,0.0078125f);
	gles::Disable(GL_LINE_SMOOTH); gles::Color4f(1,0,0,1); Quad(-0.4f,0.3f,0.4f,0.7f);
	Expect("ordinary triangle after smooth disable",255,0,0,255,32,48);
	ErrorCheck("smooth lines");
}
std::string Load(const std::string &path)
{
	std::ifstream file(path.c_str()); if(!file) throw std::runtime_error("Cannot open "+path);
	std::ostringstream contents; contents<<file.rdbuf(); return contents.str();
}
GLuint Compile(GLenum type,const std::string &source,const std::string &name)
{
	GLuint shader=glCreateShader(type); const char *text=source.c_str(); glShaderSource(shader,1,&text,0); glCompileShader(shader);
	GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
	if(!ok)
	{
		char log[8192]; glGetShaderInfoLog(shader,sizeof log,0,log);
		throw std::runtime_error(name+": "+log);
	}
	return shader;
}
GLuint Program(const std::string &vertex,const std::string &fragment,const std::string &name,GLESProgramCache *cache=0)
{
	GLuint vs=Compile(GL_VERTEX_SHADER,vertex,name+" vertex"),fs=Compile(GL_FRAGMENT_SHADER,fragment,name+" fragment");
	GLuint program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs);
	if(cache) cache->Prepare(program);
	const char *attributes[]={"aPosition","aTexCoord","aColor","aNormal","glowdistance","fogparams","lightlevel_in","aGLESSplitData"};
	for(unsigned i=0;i<8;++i) glBindAttribLocation(program,i,attributes[i]);
	glLinkProgram(program); GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
	if(!ok) { char log[8192]; glGetProgramInfoLog(program,sizeof log,0,log); throw std::runtime_error(name+" link: "+log); }
	glDeleteShader(vs); glDeleteShader(fs); return program;
}
void *Resolve(const char *name) { return reinterpret_cast<void *>(eglGetProcAddress(name)); }
void TestProgramCache()
{
	char temporary[]="/tmp/zandronum-gles-cache-XXXXXX";char *directory=mkdtemp(temporary);
	if(!directory) throw std::runtime_error("Cannot create temporary cache directory");
	GLESProgramCache cache;cache.Initialize(Resolve,directory);
	Check(cache.Active(),"native program binary cache supported");
	const std::string vertex="#version 100\nattribute vec4 aPosition;void main(){gl_Position=aPosition;}\n";
	const std::string fragment="#version 100\nprecision mediump float;uniform vec4 tint;void main(){gl_FragColor=tint;}\n";
	std::string key=cache.Key(vertex,fragment),path=std::string(directory)+"/"+key+".bin";
	Check(key!=cache.Key(vertex+"\n",fragment)&&key!=cache.Key(vertex,fragment+"\n"),"cache key includes both complete shader sources");
	GLuint source=Program(vertex,fragment,"binary cache source",&cache);
	Check(cache.Store(source,key),"native program binary stored");
	struct stat info;Check(stat(directory,&info)==0&&(info.st_mode&0777)==0700,"binary cache directory private");
	Check(stat(path.c_str(),&info)==0&&(info.st_mode&0777)==0600,"binary cache file private");
	GLuint loaded=glCreateProgram();Check(cache.Load(loaded,key),"native program binary loaded and linked");
	Reset();gles::UseProgram(loaded);gles::Uniform4f(glGetUniformLocation(loaded,"tint"),1,0,0,1);Quad();
	Expect("binary-loaded program draws expected image",255,0,0,255);
	gles::UseProgram(0);gles::DeleteProgram(loaded);
	for(int corruption=0;corruption<4;++corruption)
	{
		Check(cache.Store(source,key),"cache fixture restored");
		FILE *file=std::fopen(path.c_str(),corruption==0?"wb":"r+b");
		if(!file) throw std::runtime_error("Cannot modify test cache fixture");
		if(corruption==0) std::fwrite("truncated",1,9,file);
		else if(corruption==1) {std::fseek(file,-1,SEEK_END);int byte=std::fgetc(file);std::fseek(file,-1,SEEK_END);std::fputc(byte^0xff,file);}
		else {uint32_t value=corruption==2?0:UINT32_MAX;std::fseek(file,corruption==2?8:12,SEEK_SET);std::fwrite(&value,sizeof value,1,file);}
		std::fclose(file);GLuint rejected=glCreateProgram();
		Check(!cache.Load(rejected,key),"truncated/checksum/driver-format/oversized cache entry rejected");
		Check(access(path.c_str(),F_OK)!=0,"bad binary entry removed for source fallback");
		// Match the engine fallback: link original GLSL into the same program
		// object after a rejected binary, preserving all material variants.
		GLuint vs=Compile(GL_VERTEX_SHADER,vertex,"cache fallback"),fs=Compile(GL_FRAGMENT_SHADER,fragment,"cache fallback");
		glAttachShader(rejected,vs);glAttachShader(rejected,fs);glBindAttribLocation(rejected,0,"aPosition");gles::LinkProgram(rejected);
		GLint linked=0;glGetProgramiv(rejected,GL_LINK_STATUS,&linked);Check(linked==GL_TRUE,"source fallback links after rejected binary");
		gles::UseProgram(rejected);gles::Uniform4f(glGetUniformLocation(rejected,"tint"),0,1,0,1);Clear();Quad();
		Expect("source fallback image remains correct",0,255,0,255);
		gles::UseProgram(0);gles::DeleteProgram(rejected);glDeleteShader(vs);glDeleteShader(fs);
	}
	// FAT/exFAT may expose fixed modes and reject chmod. Simulate that exact
	// filesystem operation failure while retaining real binary I/O/linking.
	__real_chmod(directory,0777);streamtrace::failChmod=true;cache.Initialize(Resolve,directory);
	Check(cache.Active()&&cache.Store(source,key),"cache works when filesystem rejects chmod");
	loaded=glCreateProgram();Check(cache.Load(loaded,key),"fixed-permission cache binary loads");gles::DeleteProgram(loaded);
	streamtrace::failChmod=false;__real_chmod(directory,0700);
	gles::DeleteProgram(source);unlink(path.c_str());rmdir(directory);
	cache.Initialize(Resolve,NULL);Check(!cache.Active(),"cache disabled when no path supplied");
	cache.Initialize(NULL,"/tmp");Check(!cache.Active(),"cache disabled when binary functions unavailable");
	ErrorCheck("program binary cache");
}
void TestMaterials(const std::string &directory)
{
	std::istringstream list(Load(directory+"/programs.txt")); std::string name; int programs=0,expectedPrograms=0;
	GLuint normal=0,normalLights=0,fixture=0;
	while(list>>name)
	{
		++expectedPrograms;
		try
		{
			GLuint program=Program(Load(directory+"/"+name+".vert"),Load(directory+"/"+name+".frag"),name);
			++programs; if(name=="normal-0") normal=program; else if(name=="normal-4") normalLights=program; else if(name=="fixture-0") fixture=program; else gles::DeleteProgram(program);
		}
		catch(const std::exception &error) { Check(false,error.what()); }
	}
	Check(expectedPrograms>=218&&programs==expectedPrograms,"all shipped and requested fixture material/effect programs compile and link");
	if(normal)
	{
		Reset(); gles::UseProgram(normal);
		gles::Uniform1i(glGetUniformLocation(normal,"tex"),0);
		gles::Uniform1i(glGetUniformLocation(normal,"texturemode"),0);
		gles::Uniform1i(glGetUniformLocation(normal,"fogenabled"),0);
		gles::Uniform1f(glGetUniformLocation(normal,"desaturation_factor"),1);
		gles::Uniform3f(glGetUniformLocation(normal,"dlightcolor"),0,0,0);
		const unsigned char texel[]={64,128,192,96}; GLuint texture=Texture(texel); gles::Enable(GL_TEXTURE_2D);
		Quad(); Expect("world shader explicit attributes/matrices",64,128,192,96);
		gles::Enable(GL_ALPHA_TEST); gles::AlphaFunc(GL_GREATER,0.5f); Clear(); Quad();
		Expect("world shader alpha discard",0,0,0,0);
		gles::Disable(GL_ALPHA_TEST); const GLdouble plane[]={1,0,0,0}; gles::ClipPlane(GL_CLIP_PLANE5,plane);
		gles::Enable(GL_CLIP_PLANE5); Clear(); Quad();
		Expect("world shader plane 5 reject",0,0,0,0,16,32); Expect("world shader plane 5 accept",64,128,192,96,48,32);
		gles::Disable(GL_CLIP_PLANE5); gles::Disable(GL_TEXTURE_2D); Clear(); Quad();
		Expect("world shader texture disabled",255,255,255,255);
		gles::DeleteTextures(1,&texture); gles::UseProgram(0); gles::DeleteProgram(normal);
	}
	if(normalLights)
	{
		// Actual production dynlight specialization, including largest array slot.
		GLuint lightVariant=Program(Load(directory+"/normal-4-opaque.vert"),Load(directory+"/normal-4-opaque.frag"),"uniform-capacity routing variant");
		gles::RegisterNoClipAlphaVariant(normalLights,lightVariant,16);
		Reset();gles::UseProgram(normalLights);gles::Color4f(0.25f,0.25f,0.25f,1);
		gles::Uniform1i(glGetUniformLocation(normalLights,"tex"),0);
		gles::Uniform1i(glGetUniformLocation(normalLights,"texturemode"),0);
		gles::Uniform1i(glGetUniformLocation(normalLights,"fogenabled"),0);
		const unsigned char texel[]={128,128,128,255};GLuint texture=Texture(texel);gles::Enable(GL_TEXTURE_2D);
		const GLfloat lights[]={0,0,0,100, 0.25f,0,0,0,
			0,0,0,100, 0,0.25f,0,0,
			0,0,0,100, 0,0.125f,0,0,
			0,0,0,100, 0,0,0.125f,0};
		GLint lightLocation=glGetUniformLocation(normalLights,"lights[0]");
		GLint rangeLocation=glGetUniformLocation(normalLights,"lightrange");
		gles::Uniform4fv(lightLocation,8,lights);
		auto drawLights=[&](const char *label,int mod,int sub,int total,int r,int g,int b)
		{
			gles::Uniform3i(rangeLocation,mod,sub,total);Clear();Quad();Expect(label,r,g,b,255);
		};
		// Unused array entries deliberately contain nonzero lights, so these
		// image checks catch crossing any category or active-range boundary.
		drawLights("dynamic lights zero ignores populated array",0,0,0,32,32,32);
		drawLights("dynamic lights one modulated",2,2,2,64,32,32);
		drawLights("dynamic lights multiple modulated",4,4,4,64,64,32);
		drawLights("dynamic lights subtractive only",0,2,2,0,32,32);
		drawLights("dynamic lights additive follows texture modulation",0,0,2,96,32,32);
		drawLights("dynamic lights mixed category boundaries",4,6,8,64,48,64);
		drawLights("dynamic lights zero after populated range",0,0,0,32,32,32);
		GLint count=0,arraySize=0;glGetProgramiv(normalLights,GL_ACTIVE_UNIFORMS,&count);
		for(GLint i=0;i<count;++i)
		{
			char uniform[128];GLsizei length;GLint size;GLenum type;
			glGetActiveUniform(normalLights,i,sizeof uniform,&length,&size,&type,uniform);
			if(std::strcmp(uniform,"lights[0]")==0)arraySize=size;
		}
		Check(arraySize>=8&&arraySize%2==0,"dynamic light uniform array retains supported capacity");
		if(arraySize>=8&&arraySize%2==0)
		{
			std::vector<GLfloat> capacity(size_t(arraySize)*4,0);
			for(int i=0;i<arraySize;i+=2)capacity[size_t(i)*4+3]=100;
			capacity[size_t(arraySize-1)*4]=0.25f;
			gles::Uniform4fv(lightLocation,arraySize,&capacity[0]);
			drawLights("dynamic lights last supported slot contributes",arraySize,arraySize,arraySize,64,32,32);
			gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GEQUAL,0.5f);
			drawLights("full light array replays to generic physical program",arraySize,arraySize,arraySize,64,32,32);
			const GLfloat last[]={0,0.25f,0,0};
			gles::Uniform4fv(glGetUniformLocation(normalLights,("lights["+std::to_string(arraySize-1)+"]").c_str()),1,last);
			gles::Disable(GL_ALPHA_TEST);drawLights("last light array element partial replay to fast",arraySize,arraySize,arraySize,32,64,32);
			gles::Uniform4fv(lightLocation,arraySize,&capacity[0]);gles::Enable(GL_ALPHA_TEST);
			drawLights("full capacity upload supersedes last-slot partial key",arraySize,arraySize,arraySize,64,32,32);
		}
		gles::DeleteTextures(1,&texture);gles::UseProgram(0);gles::DeleteProgram(normalLights);
		gles::DeleteProgram(lightVariant);
	}
	if(fixture)
	{
		Reset();gles::UseProgram(fixture);gles::Uniform1i(glGetUniformLocation(fixture,"tex"),0);
		gles::Uniform1i(glGetUniformLocation(fixture,"fogenabled"),0);gles::Uniform1i(glGetUniformLocation(fixture,"texturemode"),0);
		const unsigned char texel[]={64,128,192,96};GLuint texture=Texture(texel);gles::Enable(GL_TEXTURE_2D);Quad();
		Expect("custom fixture shader red tint",16,128,192,96);
		gles::DeleteTextures(1,&texture);gles::UseProgram(0);gles::DeleteProgram(fixture);
	}
	ErrorCheck("material programs");
	std::printf("Actual driver programs linked: %d\n",programs);
}
void DepthOccluder(bool nearPlane=false)
{
	gles::Color4f(1,0,0,1);gles::Begin(GL_TRIANGLES);
	gles::Vertex3f(-0.9f,-0.9f,0);
	gles::Vertex3f(0.9f,-0.9f,nearPlane?-2:2);
	gles::Vertex3f(0,0.9f,nearPlane?-2:2);gles::End();
}
void TestDepthClamp(const std::string &directory)
{
	const char *realExtensions=reinterpret_cast<const char*>(__real_glGetString(GL_EXTENSIONS));
	bool hardware=realExtensions&&std::strstr(realExtensions,"GL_EXT_depth_clamp");
	for(int fallback=0;fallback<2;++fallback)
	{
		Reset();gles::Shutdown();streamtrace::hideDepthClamp=fallback!=0;gles::Initialize();Reset();
		std::string path=fallback?"CPU depth clamp: ":"available depth clamp: ";
		gles::Enable(GL_DEPTH_TEST);gles::DepthFunc(GL_LEQUAL);gles::Enable(GL_DEPTH_CLAMP);
		if(hardware)Check(bool(glIsEnabled(GL_DEPTH_CLAMP))==!fallback,path+"hardware capability dispatch");
		DepthOccluder();gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,0.8f);
		Expect((path+"far crossing preserves interpolated depth").c_str(),0,0,255,255);
		Expect((path+"far crossing retains in-range portion").c_str(),255,0,0,255,12,8);
		Clear();DepthOccluder(true);gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,-0.8f);
		Expect((path+"near crossing preserves interpolated depth").c_str(),255,0,0,255);
		Expect((path+"near crossing retains in-range portion").c_str(),0,0,255,255,12,8);
		Clear();gles::DepthRange(0.25,0.75);DepthOccluder();gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,0.8f);
		Expect((path+"nondefault depth range").c_str(),0,0,255,255);
		Clear();gles::DepthRange(0.75,0.25);DepthOccluder();gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,0.8f);
		Expect((path+"reversed depth range").c_str(),255,0,0,255);gles::DepthRange(0,1);
		// A triangle entirely on a split boundary must not be duplicated.
		Reset();gles::Enable(GL_DEPTH_CLAMP);gles::Enable(GL_BLEND);gles::BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		gles::Color4f(1,0,0,0.5f);Quad(-0.8f,-0.8f,0.8f,0.8f,1);
		gles::Begin(GL_TRIANGLES);gles::Vertex3f(-1,-1,0);gles::Vertex3f(-1,-0.9f,2);gles::Vertex3f(-0.9f,-1,2);gles::End();
		Expect((path+"coplanar triangles blended once").c_str(),128,0,0,64);
		// Native client arrays and VBO/EBO paths need the same exact fallback.
		GLfloat vertices[]={-0.9f,-0.9f,0, 0.9f,-0.9f,2, 0,0.9f,2};
		GLushort indices[]={0,1,2};GLuint buffers[2];glGenBuffers(2,buffers);
		for(int mode=0;mode<3;++mode)
		{
			Reset();gles::Enable(GL_DEPTH_TEST);gles::DepthFunc(GL_LEQUAL);gles::Enable(GL_DEPTH_CLAMP);
			gles::BindBuffer(GL_ARRAY_BUFFER,mode?buffers[0]:0);
			if(mode)gles::BufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
			gles::VertexPointer(3,GL_FLOAT,0,mode?0:vertices);gles::EnableClientState(GL_VERTEX_ARRAY);gles::Color4f(1,0,0,1);
			if(mode==2)
			{
				gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);
				gles::DrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
			}
			else gles::DrawArrays(GL_TRIANGLES,0,3);
			gles::DisableClientState(GL_VERTEX_ARRAY);gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,0.8f);
			Expect((path+"client/VBO/EBO depth "+std::to_string(mode)).c_str(),0,0,255,255);
		}
		Reset();gles::Enable(GL_DEPTH_CLAMP);gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);
		const GLfloat inRange[]={-0.9f,-0.9f,0,0.9f,-0.9f,0,0,0.9f,0};
		gles::BufferData(GL_ARRAY_BUFFER,sizeof inRange,inRange,GL_STATIC_DRAW);gles::VertexPointer(3,GL_FLOAT,0,0);gles::EnableClientState(GL_VERTEX_ARRAY);
		streamtrace::uploads.clear();streamtrace::draws.clear();streamtrace::active=true;gles::DrawArrays(GL_TRIANGLES,0,3);gles::Flush();streamtrace::active=false;
		Check(streamtrace::uploads.empty()&&streamtrace::draws.size()==1&&streamtrace::draws[0].buffer==GLint(buffers[0]),path+"in-range VBO retains native draw without stream upload");
		gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);gles::DeleteBuffers(2,buffers);
		Reset();gles::Enable(GL_DEPTH_TEST);gles::DepthFunc(GL_LEQUAL);gles::Enable(GL_DEPTH_CLAMP);
		gles::Color4f(1,0,0,1);gles::Begin(GL_LINES);gles::Vertex3f(-0.9f,0.015625f,0);gles::Vertex3f(0.9f,0.015625f,2);gles::End();
		gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,0.8f);
		Expect((path+"line crossing depth").c_str(),0,0,255,255,40,32);
		Expect((path+"line in-range depth").c_str(),255,0,0,255,12,32);
		Clear();gles::Color4f(1,0,0,1);gles::Begin(GL_POINTS);gles::Vertex3f(0.015625f,0.015625f,-2);gles::End();
		gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,-0.8f);
		Expect((path+"point near clamp").c_str(),255,0,0,255);
		// Compare nonlinear sphere-map and ordinary interpolants with an original
		// vertex shader reference that changes only clip Z, avoiding depth clipping.
		for(int projection=0;projection<3;++projection)for(int sphere=0;sphere<2;++sphere)
		{
			std::string name=sphere?"spheremap":"normal-0",vertex=Load(directory+"/"+name+".vert"),fragment=Load(directory+"/"+name+".frag");
			GLuint program=Program(vertex,fragment,name+" depth");
			GLuint fast=0;
			if(!sphere)
			{
				fast=Program(Load(directory+"/normal-opaque.vert"),Load(directory+"/normal-opaque.frag"),"fast material depth");
				gles::RegisterNoClipAlphaVariant(program,fast);
			}
			size_t main=vertex.find("void main()");if(main==std::string::npos)throw std::runtime_error("Missing material vertex main");
			vertex.replace(main,11,"void OriginalMain()");vertex+="\nvoid main(){OriginalMain();gl_Position.z=0.0;}\n";
			GLuint reference=Program(vertex,fragment,name+" reference");
			if(projection==0&&sphere==0)
			{
				Reset();gles::UseProgram(program);gles::Uniform1i(glGetUniformLocation(program,"fogenabled"),0);
				gles::Enable(GL_DEPTH_TEST);gles::DepthFunc(GL_LEQUAL);gles::Enable(GL_DEPTH_CLAMP);
				gles::ActiveTexture(GL_TEXTURE7);gles::MatrixMode(GL_TEXTURE);gles::Translatef(0,0,0.7f);gles::ActiveTexture(GL_TEXTURE0);gles::MatrixMode(GL_MODELVIEW);
				gles::Color4f(1,0,0,1);gles::Begin(GL_TRIANGLES);gles::Vertex3f(-0.9f,-0.9f,-0.7f);gles::Vertex3f(0.9f,-0.9f,1.3f);gles::Vertex3f(0,0.9f,1.3f);gles::End();
				gles::UseProgram(0);gles::Color4f(0,0,1,1);Quad(-0.9f,-0.9f,0.9f,0.9f,0.8f);
				Expect((path+"material model matrix depth classification").c_str(),0,0,255,255);
			}
			std::vector<unsigned char> pattern(32*32*4);
			for(int y=0;y<32;++y)for(int x=0;x<32;++x)
			{size_t at=size_t(y*32+x)*4;pattern[at]=x*8;pattern[at+1]=y*8;pattern[at+2]=(x+y)*4;pattern[at+3]=255;}
			Reset();GLuint texture=Texture(&pattern[0],32,32);
			gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gles::TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
			std::vector<Pixel> images[2];
			for(int pass=0;pass<2;++pass)
			{
				Reset();GLuint active=pass?reference:program;gles::UseProgram(active);
				gles::Uniform1i(glGetUniformLocation(active,"tex"),0);gles::Uniform1i(glGetUniformLocation(active,"texturemode"),0);
				gles::Uniform1i(glGetUniformLocation(active,"fogenabled"),0);gles::Uniform1f(glGetUniformLocation(active,"desaturation_factor"),1);
				gles::BindTexture(GL_TEXTURE_2D,texture);gles::Enable(GL_TEXTURE_2D);if(!pass)gles::Enable(GL_DEPTH_CLAMP);
				gles::Enable(GL_CULL_FACE);gles::FrontFace(GL_CCW);
				gles::ActiveTexture(GL_TEXTURE7);gles::MatrixMode(GL_TEXTURE);gles::Translatef(0.1f,0,0.2f);gles::Rotatef(12,0,1,0);gles::ActiveTexture(GL_TEXTURE0);
				gles::MatrixMode(GL_MODELVIEW);gles::Translatef(-0.1f,0,-0.1f);
				if(projection){gles::MatrixMode(GL_PROJECTION);gles::Perspective(90,1,1,2);gles::MatrixMode(GL_MODELVIEW);}
				gles::Begin(GL_TRIANGLES);
				gles::VertexAttrib3f(3,0.3f,0.2f,1);gles::Color4f(1,0.5f,1,1);gles::TexCoord2f(0.1f,0.1f);gles::Vertex3f(projection?-0.4f:-0.9f,projection?-0.4f:-0.9f,projection==2?0.5f:projection?-0.5f:-2);
				gles::VertexAttrib3f(3,-0.3f,0,1);gles::Color4f(0.5f,1,1,1);gles::TexCoord2f(0.9f,0.1f);gles::Vertex3f(0.9f,-0.9f,projection?-3:2);
				gles::VertexAttrib3f(3,0,0.4f,1);gles::Color4f(1,1,0.5f,1);gles::TexCoord2f(0.5f,0.9f);gles::Vertex3f(0,0.9f,projection?-3:2);gles::End();
				images[pass].resize(64*64);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&images[pass][0]);
			}
			unsigned maskMismatch=0,colorMismatch=0,covered=0;
			for(size_t i=0;i<images[0].size();++i)
			{
				Pixel a=images[0][i],b=images[1][i];if((a.a!=0)!=(b.a!=0))++maskMismatch;
				if(a.a&&b.a){++covered;if(!Near(a.r,b.r,3)||!Near(a.g,b.g,3)||!Near(a.b,b.b,3))++colorMismatch;}
			}
			std::string label=path+name+" projection "+std::to_string(projection);
			Check(covered>100&&maskMismatch<=3,label+" model transform coverage");
			Check(colorMismatch==0,label+" original color/UV interpolation");
			gles::UseProgram(0);gles::DeleteTextures(1,&texture);gles::DeleteProgram(program);gles::DeleteProgram(reference);
			if(fast)gles::DeleteProgram(fast);
		}
		gles::VertexAttrib3f(3,0,0,1);Reset();ErrorCheck(path.c_str());
	}
	streamtrace::hideDepthClamp=false;
}

void TestSpecialization(const std::string &directory)
{
	const std::string vertex=Load(directory+"/normal-0.vert"),fragment=Load(directory+"/normal-0.frag");
	GLuint base=Program(vertex,fragment,"specialization base"),fast=Program(Load(directory+"/normal-opaque.vert"),Load(directory+"/normal-opaque.frag"),"specialization fast");
	GLuint reference=Program(vertex,fragment,"specialization reference");
	gles::RegisterNoClipAlphaVariant(base,fast);
	Check(glGetUniformLocation(fast,"uAlphaTest")==-1&&glGetUniformLocation(fast,"uClipPlanes[0]")==-1&&glGetUniformLocation(fast,"uClipActive")==-1,"fast material removes alpha and clip uniforms");
	Reset();const unsigned char texels[]={64,128,192,96,128,64,32,0,192,128,64,255,32,192,128,160};GLuint texture=Texture(texels,2,2);
	for(int fog=-2;fog<=2;++fog)for(int mode: {0,1,2,4,6})for(int textured=0;textured<2;++textured)
	{
		std::vector<Pixel> images[2];
		for(int pass=0;pass<2;++pass)
		{
			Reset();GLuint program=pass?base:reference;gles::UseProgram(program);
			gles::Uniform1i(glGetUniformLocation(program,"texturemode"),mode);
			gles::Uniform1i(glGetUniformLocation(program,"fogenabled"),fog);
			gles::Uniform4f(glGetUniformLocation(program,"fogcolor"),0.1f,0.25f,0.4f,0);
			gles::Uniform3f(glGetUniformLocation(program,"camerapos"),0.2f,0.3f,0.4f);
			gles::VertexAttrib4f(5,1.5f,4,-0.08f,0);gles::Color4f(0.7f,0.5f,0.2f,0.4f);
			gles::ActiveTexture(GL_TEXTURE7);gles::MatrixMode(GL_TEXTURE);gles::Translatef(0.1f,0,-2);
			gles::ActiveTexture(GL_TEXTURE0);gles::MatrixMode(GL_TEXTURE);gles::Translatef(0.1f,0.05f,0);
			gles::MatrixMode(GL_PROJECTION);gles::Scalef(1,1,0.1f);gles::MatrixMode(GL_MODELVIEW);gles::Translatef(0,0,-0.25f);
			gles::BindTexture(GL_TEXTURE_2D,texture);if(textured)gles::Enable(GL_TEXTURE_2D);
			Quad();images[pass].resize(64*64);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&images[pass][0]);
		}
		Check(!std::memcmp(&images[0][0],&images[1][0],images[0].size()*sizeof(Pixel)),"generic/fast exact pixels fog "+std::to_string(fog)+" texturemode "+std::to_string(mode)+" enabled "+std::to_string(textured));
	}
	Reset();gles::UseProgram(base);gles::Uniform1i(glGetUniformLocation(base,"texturemode"),0);gles::Uniform1i(glGetUniformLocation(base,"fogenabled"),0);
	gles::Enable(GL_TEXTURE_2D);gles::BindTexture(GL_TEXTURE_2D,texture);Quad();gles::Flush();
	GLint logical=0,actual=0;gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
	Check(logical==GLint(base)&&actual==GLint(fast),"logical base identity while physical fast bound");
	// No new material bind between these state changes and draws.
	gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GREATER,1);Clear();Quad();Expect("after-bind alpha rejection uses generic",0,0,0,0);
	glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),"after-bind masked state selects generic");
	gles::AlphaFunc(GL_ALWAYS,1);Clear();Quad();gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(fast),"GL_ALWAYS can use discard-free fast program");
	gles::Disable(GL_ALPHA_TEST);const GLdouble plane[]={1,0,0,0};gles::ClipPlane(GL_CLIP_PLANE5,plane);gles::Enable(GL_CLIP_PLANE5);
	Clear();Quad();Expect("after-bind clip plane preserves rejected half",0,0,0,0,16,32);gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),"after-bind clip state selects generic");
	gles::Disable(GL_CLIP_PLANE5);Clear();Quad();gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(fast),"same logical material returns to fast after clip disabled");
	// Native array/indexed draws select physical state at draw time as well.
	Reset();gles::UseProgram(base);gles::Color4f(1,0,0,0.25f);
	const GLfloat positions[]={-0.8f,-0.8f,0,0.8f,-0.8f,0,0.8f,0.8f,0,-0.8f,0.8f,0};
	const GLushort indices[]={0,1,2,0,2,3};GLuint buffers[2];glGenBuffers(2,buffers);
	gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);gles::BufferData(GL_ARRAY_BUFFER,sizeof positions,positions,GL_STATIC_DRAW);
	gles::VertexPointer(3,GL_FLOAT,0,0);gles::EnableClientState(GL_VERTEX_ARRAY);
	gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);
	for(int indexed=0;indexed<2;++indexed)
	{
		auto draw=[&](){if(indexed)gles::DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);else gles::DrawArrays(GL_TRIANGLE_FAN,0,4);};
		gles::Disable(GL_ALPHA_TEST);Clear();draw();Expect("native draw selects fast without material rebind",255,0,0,64);
		glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(fast),"native physical fast identity");
		gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GEQUAL,0.5f);Clear();draw();Expect("native alpha change selects generic",0,0,0,0);
		glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),"native physical generic identity");
		gles::Disable(GL_ALPHA_TEST);gles::Enable(GL_CLIP_PLANE5);Clear();draw();Expect("native clip change rejects half",0,0,0,0,16,32);Expect("native clip change preserves half",255,0,0,64,48,32);
		gles::Disable(GL_CLIP_PLANE5);
	}
	gles::DisableClientState(GL_VERTEX_ARRAY);gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);gles::DeleteBuffers(2,buffers);
	Reset();gles::UseProgram(base);gles::Color4f(1,0,0,0);gles::Enable(GL_DEPTH_TEST);gles::DepthMask(GL_TRUE);
	Quad(-0.9f,-0.8f,-0.1f,0.8f,-0.5f); // queued opaque-alpha0 geometry
	gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GREATER,0.5f);Quad(0.1f,-0.8f,0.9f,0.8f,-0.5f);
	Expect("queued fast draw retained across alpha change",255,0,0,0,16,32);Expect("next masked batch rejects alpha0",0,0,0,0,48,32);
	gles::Disable(GL_ALPHA_TEST);gles::Color4f(0,0,1,1);Quad(-0.9f,-0.8f,-0.1f,0.8f,0.5f);Expect("alpha-disabled alpha0 still writes depth",255,0,0,0,16,32);
	Reset();gles::UseProgram(0);gles::DeleteTextures(1,&texture);gles::DeleteProgram(base);gles::DeleteProgram(fast);gles::DeleteProgram(reference);ErrorCheck("material specialization");

	// Synthetic material exposes every routed API type and overlapping arrays.
	const std::string arrays="#version 100\nprecision highp float;precision highp int;uniform vec4 values[3];uniform ivec4 integers[2];uniform mat4 matrices[2];uniform vec4 tint;uniform sampler2D tex;void main(){gl_FragColor=(values[0]+values[1]+values[2]+vec4(integers[0]+integers[1])/255.0+matrices[0][0]+matrices[1][0])*tint*texture2D(tex,vec2(0.5));}\n";
	base=Program(vertex,arrays,"uniform routing base");fast=Program(vertex,arrays,"uniform routing fast");Reset();gles::UseProgram(base);
	GLint values=glGetUniformLocation(base,"values[0]"),second=glGetUniformLocation(base,"values[1]"),ints=glGetUniformLocation(base,"integers[0]"),matrix=glGetUniformLocation(base,"matrices[0]"),tint=glGetUniformLocation(base,"tint");
	const GLfloat full[]={0.1f,0,0,0, 0,0.2f,0,0, 0,0,0.3f,0},changed[]={0.2f,0,0,0};
	gles::Uniform4fv(values,3,full);gles::Uniform4fv(second,1,changed);gles::Uniform4f(tint,1,1,1,1);
	gles::ActiveTexture(GL_TEXTURE2);const unsigned char white[]={255,255,255,255};texture=Texture(white);gles::ActiveTexture(GL_TEXTURE0);
	gles::Uniform1i(glGetUniformLocation(base,"tex"),2);gles::RegisterNoClipAlphaVariant(base,fast);
	Quad();Expect("registration captures existing base state and sampler",77,0,77,0);
	gles::Uniform4fv(values,3,full);Clear();Quad();Expect("full array overrides earlier partial write",26,51,77,0);
	gles::Uniform4fv(second,1,changed);Clear();Quad();Expect("partial write overrides full array",77,0,77,0);
	gles::Uniform4fv(values,3,full);Clear();Quad();Expect("repeated full starting-location upload restores overlap",26,51,77,0);
	gles::Uniform4f(second,changed[0],changed[1],changed[2],changed[3]);Clear();Quad();Expect("scalar API and vector API share canonical element",77,0,77,0);
	const GLfloat half[]={0.5f,0.5f,0.5f,1};gles::Uniform4fv(tint,1,half);Clear();Quad();Expect("vector API replaces scalar tint",38,0,38,0);
	gles::Uniform4f(tint,1,1,1,1);Clear();Quad();Expect("repeated scalar tint restores vector overlap",77,0,77,0);
	GLint integerValues[]={1,2,3,4,5,6,7,8};gles::Uniform4iv(ints,2,integerValues);gles::Uniform4i(glGetUniformLocation(base,"integers[1]"),9,10,11,12);
	GLfloat matrices[32]={0};matrices[0]=0.01f;matrices[16]=0.02f;gles::UniformMatrix4fv(matrix,2,GL_FALSE,matrices);
	GLfloat replacement[16]={0};replacement[0]=0.03f;gles::UniformMatrix4fv(glGetUniformLocation(base,"matrices[1]"),1,GL_FALSE,replacement);
	auto verify=[&](GLuint physical,const char *label)
	{
		GLint found[4]={0};GLfloat got[16]={0};glGetUniformiv(physical,glGetUniformLocation(physical,"integers[1]"),found);
		glGetUniformfv(physical,glGetUniformLocation(physical,"matrices[1]"),got);
		Check(found[0]==9&&found[1]==10&&found[2]==11&&found[3]==12&&got[0]==0.03f,label);
	};
	Quad();gles::Flush();verify(fast,"fast integer/matrix array partial state");
	gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_LESS,1);Quad();gles::Flush();verify(base,"generic independent integer/matrix resident state");
	gles::Uniform4iv(ints,2,integerValues);gles::UniformMatrix4fv(matrix,2,GL_FALSE,matrices);gles::Disable(GL_ALPHA_TEST);Quad();gles::Flush();
	GLint gotInts[4]={0};GLfloat gotMatrix[16]={0};glGetUniformiv(fast,glGetUniformLocation(fast,"integers[1]"),gotInts);glGetUniformfv(fast,glGetUniformLocation(fast,"matrices[1]"),gotMatrix);
	Check(gotInts[0]==5&&gotInts[3]==8&&gotMatrix[0]==0.02f,"latest full integer/matrix arrays supersede partial replay keys");
	// Deleting/replacing an inactive family's variant still restores its base.
	GLuint other=Program(vertex,arrays,"unrelated current material");
	gles::Uniform4i(glGetUniformLocation(base,"integers[1]"),19,20,21,22);Quad();gles::Flush();gles::UseProgram(other);
	gles::DeleteProgram(fast);glGetIntegerv(GL_CURRENT_PROGRAM,&actual);gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);
	Check(actual==GLint(other)&&logical==GLint(other),"inactive variant deletion preserves other logical/physical binding");
	glGetUniformiv(base,glGetUniformLocation(base,"integers[1]"),gotInts);Check(gotInts[0]==19&&gotInts[3]==22,"inactive variant deletion restores latest base snapshot");
	fast=Program(vertex,arrays,"inactive replacement old variant");gles::RegisterNoClipAlphaVariant(base,fast);gles::UseProgram(base);Quad();gles::Flush();
	gles::Uniform4i(glGetUniformLocation(base,"integers[1]"),29,30,31,32);gles::UseProgram(other);
	GLuint replaced=fast;fast=Program(vertex,arrays,"inactive replacement new variant");gles::RegisterNoClipAlphaVariant(base,fast);
	glGetIntegerv(GL_CURRENT_PROGRAM,&actual);gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);
	Check(actual==GLint(other)&&logical==GLint(other),"inactive variant replacement preserves unrelated binding");
	glGetUniformiv(base,glGetUniformLocation(base,"integers[1]"),gotInts);Check(gotInts[0]==29,"inactive replacement restores canonical base before recapture");
	gles::DeleteProgram(replaced);gles::UseProgram(base);Quad();gles::Flush();glGetUniformiv(fast,glGetUniformLocation(fast,"integers[1]"),gotInts);
	Check(gotInts[0]==29&&gotInts[3]==32,"replacement fast receives latest inactive canonical state");
	gles::Uniform4iv(ints,2,integerValues);gles::DeleteProgram(other);
	gles::DeleteProgram(fast);glGetIntegerv(GL_CURRENT_PROGRAM,&actual);gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);
	Check(actual==GLint(base)&&logical==GLint(base),"deleting mapped fast restores logical base binding");
	Clear();Quad();gles::Flush();ErrorCheck("deleted variant fallback");
	fast=Program(vertex,arrays,"recreated uniform variant");gles::RegisterNoClipAlphaVariant(base,fast);Quad();gles::Flush();
	glGetUniformiv(fast,glGetUniformLocation(fast,"integers[1]"),gotInts);Check(gotInts[0]==5,"recreated variant captures restored base state");
	gles::LinkProgram(fast);Quad();gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),"relinking variant invalidates physical mapping");
	gles::RegisterNoClipAlphaVariant(base,fast);Quad();gles::Flush();gles::LinkProgram(base);Quad();gles::Flush();
	glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),"relinking base invalidates mapping and old uniform generations");
	Reset();gles::UseProgram(0);gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);Check(logical==0,"logical program0 remains visible for basic shader");
	gles::DeleteProgram(base);gles::DeleteProgram(fast);gles::DeleteTextures(1,&texture);ErrorCheck("typed uniform routing lifecycle");
}

void TestLightSpecialization(const std::string &directory)
{
	for(int variant: {4,6})
	{
		const std::string name="normal-"+std::to_string(variant),vertex=Load(directory+"/"+name+".vert"),fragment=Load(directory+"/"+name+".frag");
		GLuint base=Program(vertex,fragment,name+" logical"),fast=Program(Load(directory+"/"+name+"-opaque.vert"),Load(directory+"/"+name+"-opaque.frag"),name+" fast");
		GLuint reference=Program(vertex,fragment,name+" reference");gles::RegisterNoClipAlphaVariant(base,fast,variant==4?16:0);
		Check(glGetUniformLocation(fast,"uAlphaTest")==-1&&glGetUniformLocation(fast,"uClipActive")==-1,name+" omits only inactive alpha/clip tests");
		GLint uniforms=0,arraySize=0;
		if(variant==4)
		{
			GLint active=0,childSize=0;glGetProgramiv(fast,GL_ACTIVE_UNIFORMS,&active);
			for(GLint k=0;k<active;++k){char uniform[128];GLsizei length;GLint size;GLenum type;glGetActiveUniform(fast,k,sizeof uniform,&length,&size,&type,uniform);if(!strcmp(uniform,"lights[0]"))childSize=size;}
			Check(childSize==16,"actual bounded child active array reflection is16vec4");
			Check(glGetUniformLocation(fast,"lights[15]")>=0&&glGetUniformLocation(fast,"lights[16]")==-1,"bounded child exposes exactly16vec4 and no tail");
			streamtrace::boundedProgram=fast;streamtrace::boundedFirst=glGetUniformLocation(fast,"lights[0]");streamtrace::boundedLast=glGetUniformLocation(fast,"lights[15]");
			Reset();gles::UseProgram(base);Quad();gles::Flush();GLint actual=0;glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
			Check(actual==GLint(fast),"bounded default-zero range selects short physical program");
		}
		glGetProgramiv(base,GL_ACTIVE_UNIFORMS,&uniforms);
		for(GLint i=0;i<uniforms;++i)
		{
			char uniform[128];GLsizei length;GLint size;GLenum type;glGetActiveUniform(base,i,sizeof uniform,&length,&size,&type,uniform);
			if(std::strcmp(uniform,"lights[0]")==0)arraySize=size;
		}
		Check(arraySize>=8&&arraySize%2==0,name+" full supported light array reflected");
		if(arraySize<8||arraySize%2)throw std::runtime_error("Invalid dynamic light array capacity");
		std::vector<GLfloat> lights(size_t(arraySize)*4,0);
		for(int i=0;i<arraySize;i+=2)
		{
			size_t p=size_t(i)*4;lights[p]=float(i%7)*0.1f;lights[p+1]=float(i%5)*0.1f;lights[p+2]=-2;lights[p+3]=30;
			lights[p+4]=0.002f*float(1+i%3);lights[p+5]=0.0015f*float(1+i%5);lights[p+6]=0.001f*float(1+i%7);
		}
		// Significant last element catches truncation even when earlier lights
		// would otherwise make the full-capacity comparison visually uniform.
		lights[size_t(arraySize-1)*4]=0.05f;lights[size_t(arraySize-1)*4+1]=0.13f;
		const int modBoundary=(arraySize/3/2)*2,subBoundary=(arraySize*2/3/2)*2;
		const GLint ranges[][3]={{0,0,0},{2,2,2},{0,2,2},{0,0,2},{4,6,8},
			{16,16,16},{0,16,16},{0,0,16},{4,10,16},{18,18,18},{0,18,18},{0,0,18},{4,12,18},
			{arraySize,arraySize,arraySize},{0,arraySize,arraySize},{0,0,arraySize},
			{modBoundary,subBoundary,arraySize},{modBoundary,subBoundary,arraySize}};
		Reset();const unsigned char texels[]={64,128,192,96,128,64,32,0,192,128,64,255,32,192,128,160};GLuint texture=Texture(texels,2,2);
		unsigned compared=0;
		for(int fog=-2;fog<=2;++fog)for(int mode: {0,1,2,4,6})for(int range=0;range<18;++range)
		{
			std::vector<Pixel> images[2];
			for(int pass=0;pass<2;++pass)
			{
				Reset();GLuint program=pass?base:reference;gles::UseProgram(program);
				gles::Uniform1i(glGetUniformLocation(program,"tex"),0);gles::Uniform1i(glGetUniformLocation(program,"texturemode"),mode);
				gles::Uniform1i(glGetUniformLocation(program,"fogenabled"),fog);
				gles::Uniform1f(glGetUniformLocation(program,"desaturation_factor"),fog<0?0.0f:fog>0?0.375f:1.0f);
				gles::Uniform4f(glGetUniformLocation(program,"fogcolor"),0.1f,0.25f,0.4f,0);
				gles::Uniform3f(glGetUniformLocation(program,"camerapos"),0.2f,0.3f,0.4f);
				gles::Uniform4fv(glGetUniformLocation(program,"lights[0]"),arraySize,&lights[0]);
				gles::Uniform3iv(glGetUniformLocation(program,"lightrange"),1,ranges[range]);
				gles::VertexAttrib4f(5,1.5f,4,-0.08f,0);gles::Color4f(0.35f,0.25f,0.15f,0.4f);
				gles::ActiveTexture(GL_TEXTURE7);gles::MatrixMode(GL_TEXTURE);gles::Translatef(0.1f,0,-2);
				gles::ActiveTexture(GL_TEXTURE0);gles::MatrixMode(GL_TEXTURE);gles::Translatef(0.1f,0.05f,0);
				gles::MatrixMode(GL_PROJECTION);gles::Scalef(1,1,0.1f);gles::MatrixMode(GL_MODELVIEW);gles::Translatef(0,0,-0.25f);
				gles::BindTexture(GL_TEXTURE_2D,texture);if(range!=17)gles::Enable(GL_TEXTURE_2D);
				Quad();gles::Flush();
				if(pass&&variant==4)
				{
					GLint physical=0;glGetIntegerv(GL_CURRENT_PROGRAM,&physical);
					Check(physical==GLint(ranges[range][2]<=16?fast:base),name+" draw-time bounded/full selection case "+std::to_string(range));
				}
				images[pass].resize(64*64);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&images[pass][0]);
			}
			Check(!std::memcmp(&images[0][0],&images[1][0],images[0].size()*sizeof(Pixel)),name+" exact generic/fast pixels fog "+std::to_string(fog)+" mode "+std::to_string(mode)+" light case "+std::to_string(range));++compared;
		}
		// Masked and portal states must retain full generic output at every light
		// category/cap boundary, including transitions from a selected short child.
		for(int state=1;state<=3;++state)for(int range=0;range<18;++range)
		{
			std::vector<Pixel> images[2];
			for(int pass=0;pass<2;++pass)
			{
				Reset();GLuint program=pass?base:reference;gles::UseProgram(program);
				gles::Uniform1i(glGetUniformLocation(program,"tex"),0);gles::Uniform1i(glGetUniformLocation(program,"texturemode"),0);
				gles::Uniform1i(glGetUniformLocation(program,"fogenabled"),1);gles::Uniform1f(glGetUniformLocation(program,"desaturation_factor"),0.375f);
				gles::Uniform4f(glGetUniformLocation(program,"fogcolor"),0.1f,0.25f,0.4f,0);gles::Uniform3f(glGetUniformLocation(program,"camerapos"),0.2f,0.3f,0.4f);
				gles::Uniform4fv(glGetUniformLocation(program,"lights[0]"),arraySize,&lights[0]);gles::Uniform3iv(glGetUniformLocation(program,"lightrange"),1,ranges[range]);
				gles::BindTexture(GL_TEXTURE_2D,texture);gles::Enable(GL_TEXTURE_2D);gles::VertexAttrib4f(5,1.5f,4,-0.08f,0);gles::Color4f(0.35f,0.25f,0.15f,1);
				if(state&1){gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GEQUAL,0.5f);}
				if(state&2){const GLdouble plane[]={1,0,0,0};gles::ClipPlane(GL_CLIP_PLANE5,plane);gles::Enable(GL_CLIP_PLANE5);}
				Quad();gles::Flush();if(pass){GLint actual=0;glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),name+" masked/portal light state retains full physical program");}
				images[pass].resize(64*64);gles::ReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,&images[pass][0]);
			}
			Check(!std::memcmp(&images[0][0],&images[1][0],images[0].size()*sizeof(Pixel)),name+" exact masked/portal pixels state "+std::to_string(state)+" light case "+std::to_string(range));++compared;
		}
		std::printf("%s: %u exact full-image generic/fast comparisons; %d supported lights\n",name.c_str(),compared,arraySize/2);
		// Exercise real light-array updates after the fast physical program has
		// already been selected. Both full->partial and partial->full orders matter.
		Reset();gles::UseProgram(base);gles::Color4f(0.25f,0.25f,0.25f,1);gles::Uniform1i(glGetUniformLocation(base,"fogenabled"),0);
		gles::Uniform1i(glGetUniformLocation(base,"texturemode"),0);gles::Uniform1f(glGetUniformLocation(base,"desaturation_factor"),1);
		gles::Uniform3i(glGetUniformLocation(base,"lightrange"),4,4,4);
		std::fill(lights.begin(),lights.end(),0);for(int i=0;i<arraySize;i+=2)lights[size_t(i)*4+3]=100;
		lights[4]=0.25f;lights[13]=0.25f;
		const unsigned char gray[]={128,128,128,255};GLuint grayTexture=Texture(gray);gles::Enable(GL_TEXTURE_2D);
		GLint first=glGetUniformLocation(base,"lights[0]"),second=glGetUniformLocation(base,"lights[3]");
		gles::Uniform4fv(first,arraySize,&lights[0]);Quad();Expect((name+" first fast light array").c_str(),64,64,32,255);
		GLint logical=0,actual=0;gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
		Check(logical==GLint(base)&&actual==GLint(fast),name+" logical identity survives dynamic-light fast selection");
		const GLfloat blue[]={0,0,0.25f,0};gles::Uniform4fv(second,1,blue);Clear();Quad();Expect((name+" partial update on selected fast").c_str(),64,32,64,255);
		gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GEQUAL,0.5f);Clear();Quad();Expect((name+" partial state replays to generic").c_str(),64,32,64,255);
		glGetIntegerv(GL_CURRENT_PROGRAM,&actual);Check(actual==GLint(base),name+" alpha state after bind uses generic");
		gles::Uniform4fv(first,arraySize,&lights[0]);gles::Disable(GL_ALPHA_TEST);Clear();Quad();Expect((name+" full array supersedes partial on fast").c_str(),64,64,32,255);
		gles::Uniform4f(second,0,0,0.25f,0);const GLdouble plane[]={1,0,0,0};gles::ClipPlane(GL_CLIP_PLANE5,plane);gles::Enable(GL_CLIP_PLANE5);
		Clear();Quad();Expect((name+" clip fallback rejects half").c_str(),0,0,0,0,16,32);Expect((name+" clip fallback retains latest lights").c_str(),64,32,64,255,48,32);
		gles::Disable(GL_CLIP_PLANE5);Clear();Quad();Expect((name+" scalar light element returns to fast").c_str(),64,32,64,255);
		if(variant==4)
		{
			GLint range=glGetUniformLocation(base,"lightrange"),tail=glGetUniformLocation(base,"lights[255]");
			Check(arraySize==256,"bounded routing retains full256vec4 canonical capacity");
			GLint physical=0;
			auto identity=[&](bool shortShader,const char *label){gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&physical);Check(physical==GLint(shortShader?fast:base),label);};
			const GLint invalid[][3]={{-2,0,2},{4,2,2},{0,4,2},{1,2,2},{0,0,257}};
			for(unsigned k=0;k<sizeof invalid/sizeof *invalid;++k){gles::Uniform3iv(range,1,invalid[k]);Quad();identity(false,"malformed category endpoints always use full shader");}
			gles::Uniform3i(range,16,16,16);Quad();identity(true,"eight-light inclusive endpoint selects short");
			gles::Uniform3i(range,18,18,18);Quad();identity(false,"nine-light endpoint selects full");
			const GLint ignoredZero[]={0,0,0};gles::Uniform3iv(range,0,ignoredZero);gles::Uniform3iv(-1,1,ignoredZero);Quad();identity(false,"ignored range uploads leave canonical endpoint unchanged");
			const GLint additiveEight[]={0,0,16};gles::Uniform3iv(range,1,additiveEight);Quad();identity(true,"ivec3 array upload selects same bounded shader");
			bool wrongType=false;try{gles::Uniform3f(range,0,0,0);}catch(const std::runtime_error &){wrongType=true;}
			Check(wrongType,"canonical range rejects float API before physical upload");
			gles::Uniform3i(range,4,4,4);gles::Uniform4fv(first,arraySize,&lights[0]);Quad();identity(true,"full256 upload routes only present child elements");
			const GLfloat tailBlue[]={0,0,0.25f,0};gles::Uniform4fv(tail,1,tailBlue);Quad();identity(true,"far-tail update retains short selection and canonical data");
			gles::Uniform3i(range,256,256,256);Quad();identity(false,"full128 lights available after selected short tail update");
			GLfloat gotTail[4];glGetUniformfv(base,tail,gotTail);Check(gotTail[2]==0.25f,"full fallback replays canonical far tail");
			gles::Uniform3i(range,4,4,4);gles::Uniform4fv(first,arraySize,&lights[0]);Quad();identity(true,"full upload restores previous partial tail");
			gles::Uniform3i(range,256,256,256);Quad();glGetUniformfv(base,tail,gotTail);Check(gotTail[2]==lights[size_t(arraySize-1)*4+2],"later full256 supersedes tail element on full fallback");
			gles::Uniform3i(range,4,4,4);gles::Uniform4fv(tail,1,tailBlue);Clear();
			Quad(-0.9f,-0.8f,-0.1f,0.8f);gles::Uniform3i(range,256,256,256);Quad(0.1f,-0.8f,0.9f,0.8f);
			Expect("queued old bounded range stays unchanged",64,64,32,255,16,32);Expect("following full range includes last128th light",64,64,64,255,48,32);
			// Same logical binding must select at actual native array/index draws.
			const GLfloat positions[]={-0.8f,-0.8f,0,0.8f,-0.8f,0,0.8f,0.8f,0,-0.8f,0.8f,0};const GLushort indices[]={0,1,2,0,2,3};GLuint buffers[2];glGenBuffers(2,buffers);
			gles::BindBuffer(GL_ARRAY_BUFFER,buffers[0]);gles::BufferData(GL_ARRAY_BUFFER,sizeof positions,positions,GL_STATIC_DRAW);gles::VertexPointer(3,GL_FLOAT,0,0);gles::EnableClientState(GL_VERTEX_ARRAY);
			gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);gles::BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof indices,indices,GL_STATIC_DRAW);
			for(int indexed=0;indexed<2;++indexed)
			{
				auto draw=[&](){if(indexed)gles::DrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);else gles::DrawArrays(GL_TRIANGLE_FAN,0,4);};
				gles::Uniform3i(range,16,16,16);Clear();draw();identity(true,"native draw selects inclusive short range after setter");
				gles::Uniform3i(range,18,18,18);Clear();draw();identity(false,"native draw selects full above short range after setter");
				gles::Uniform3i(range,4,4,4);gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GEQUAL,0.5f);Clear();draw();identity(false,"native masked bounded material selects generic");
				gles::Disable(GL_ALPHA_TEST);gles::Enable(GL_CLIP_PLANE5);Clear();draw();identity(false,"native clipped bounded material selects generic");
				gles::Disable(GL_CLIP_PLANE5);Clear();draw();identity(true,"native material returns to short without bind");
			}
			gles::DisableClientState(GL_VERTEX_ARRAY);gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);gles::DeleteBuffers(2,buffers);
			gles::Uniform3i(range,4,4,4);
		}
		// With an unrelated material current, deleting/replacing fast must still
		// copy newest full-capacity material state back to its logical base.
		gles::Uniform4fv(first,arraySize,&lights[0]);gles::Uniform4fv(second,1,blue);gles::UseProgram(reference);gles::DeleteProgram(fast);
		gles::GetIntegerv(GL_CURRENT_PROGRAM,&logical);glGetIntegerv(GL_CURRENT_PROGRAM,&actual);
		Check(logical==GLint(reference)&&actual==GLint(reference),name+" inactive deletion keeps unrelated current material");
		gles::UseProgram(base);Clear();Quad();Expect((name+" inactive deletion restores newest light array").c_str(),64,32,64,255);
		fast=Program(Load(directory+"/"+name+"-opaque.vert"),Load(directory+"/"+name+"-opaque.frag"),name+" replacement fast");
		gles::RegisterNoClipAlphaVariant(base,fast,variant==4?16:0);Clear();Quad();Expect((name+" replacement variant recaptures light array").c_str(),64,32,64,255);
		gles::Uniform4fv(first,arraySize,&lights[0]);gles::UseProgram(reference);
		GLuint old=fast;fast=Program(Load(directory+"/"+name+"-opaque.vert"),Load(directory+"/"+name+"-opaque.frag"),name+" second replacement fast");
		gles::RegisterNoClipAlphaVariant(base,fast,variant==4?16:0);gles::DeleteProgram(old);gles::UseProgram(base);Clear();Quad();Expect((name+" inactive replacement preserves full-over-partial array").c_str(),64,64,32,255);
		if(variant==4)
		{
			// New bounded registration must reject unsafe canonical range metadata.
			for(int malformed=0;malformed<2;++malformed)
			{
				std::string fragment=malformed?"#version 100\nprecision highp float;void main(){gl_FragColor=vec4(1.0);}":"#version 100\nprecision highp float;uniform vec3 lightrange;void main(){gl_FragColor=vec4(lightrange,1.0);}";
				GLuint badBase=Program(vertex,fragment,"invalid canonical range"),badFast=Program(vertex,fragment,"invalid canonical fast");
				bool rejected=false;try{gles::RegisterNoClipAlphaVariant(badBase,badFast,16);}catch(const std::runtime_error &){rejected=true;}
				Check(rejected,"bounded registration rejects missing/wrong-type canonical range");gles::DeleteProgram(badBase);gles::DeleteProgram(badFast);
			}
			gles::LinkProgram(fast);Quad();gles::Flush();GLint physical=0;glGetIntegerv(GL_CURRENT_PROGRAM,&physical);Check(physical==GLint(base),"bounded child relink invalidates mapping and restores full base");
			gles::RegisterNoClipAlphaVariant(base,fast,16);Quad();gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&physical);Check(physical==GLint(fast),"relinked bounded child recaptures full canonical state");
			gles::LinkProgram(base);Quad();gles::Flush();glGetIntegerv(GL_CURRENT_PROGRAM,&physical);Check(physical==GLint(base),"bounded base relink invalidates range metadata and old residency");
		}
		if(variant==4)
		{
			Check(streamtrace::boundedUploads>0&&streamtrace::boundedMaxCount<=16&&!streamtrace::boundedOverflow,"real Uniform4fv light uploads never cross child active16 boundary");
			streamtrace::boundedProgram=0;
		}
		Reset();gles::DeleteProgram(base);gles::DeleteProgram(fast);gles::DeleteProgram(reference);gles::DeleteTextures(1,&texture);gles::DeleteTextures(1,&grayTexture);
		ErrorCheck(name.c_str());
	}
}

// Optional diagnostic integration: real adapter, real Mesa, real preload and
// chained frame recorder. Python checks CSV state weights against API counts.
void TestStateHistogram(const std::string &directory)
{
	void (*swap)()=reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT,"SDL_GL_SwapBuffers"));
	Check(swap&&zandronum_gles_program_label,"state histogram preload and label hook present");
	if(!swap||!zandronum_gles_program_label)return;
	GLuint material=Program(Load(directory+"/normal-0.vert"),Load(directory+"/normal-0.frag"),"histogram material");
	zandronum_gles_program_label(material,"normal, \"smoke\"","#define FIRST\n#define SECOND\n");
	Reset();const unsigned char white[]={255,255,255,255};GLuint texture=Texture(white);
	std::raise(SIGUSR1);swap();
	for(int frame=0;frame<3;++frame)
	{
		Reset();gles::AlphaFunc(GL_ALWAYS,0);Quad();gles::Flush();
		Reset();gles::UseProgram(material);gles::Enable(GL_TEXTURE_2D);gles::BindTexture(GL_TEXTURE_2D,texture);
		gles::Uniform1i(glGetUniformLocation(material,"tex"),0);
		gles::Uniform1i(glGetUniformLocation(material,"texturemode"),2);
		gles::Uniform1i(glGetUniformLocation(material,"fogenabled"),-1);
		gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_GREATER,0.5f);
		const GLdouble plane[]={1,0,0,0};gles::ClipPlane(GL_CLIP_PLANE5,plane);gles::Enable(GL_CLIP_PLANE5);
		Quad();gles::Flush();
		Reset();gles::Enable(GL_ALPHA_TEST);gles::AlphaFunc(GL_ALWAYS,0.25f);
		gles::Enable(GL_TEXTURE_2D);gles::BindTexture(GL_TEXTURE_2D,texture);gles::SetTextureMode(4);
		gles::ActiveTexture(GL_TEXTURE1);gles::Enable(GL_TEXTURE_2D);gles::BindTexture(GL_TEXTURE_2D,texture);gles::ActiveTexture(GL_TEXTURE0);
		const GLfloat vertices[]={-0.5f,-0.5f,0,0.5f,-0.5f,0,0,0.5f,0};const GLushort indices[]={0,1,2};
		gles::BindBuffer(GL_ARRAY_BUFFER,0);gles::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
		gles::EnableVertexAttribArray(0);gles::VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,vertices);
		gles::DrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,indices);swap();
	}
	// Draw after last accepted frame: stopping at next swap must discard tail.
	Reset();Quad();gles::Flush();std::raise(SIGUSR2);swap();
	Reset();gles::DeleteTextures(1,&texture);gles::DeleteProgram(material);ErrorCheck("state histogram");
}
}
int main(int argc,char **argv)
{
	EGLDisplay display=EGL_NO_DISPLAY; EGLSurface surface=EGL_NO_SURFACE; EGLContext context=EGL_NO_CONTEXT;
	try
	{
		PFNEGLGETPLATFORMDISPLAYEXTPROC platform=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
		display=platform?platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,0):eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if(display==EGL_NO_DISPLAY||!eglInitialize(display,0,0)) throw std::runtime_error("EGL surfaceless display unavailable");
		const EGLint configAttributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
			EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
		EGLConfig config; EGLint count; if(!eglChooseConfig(display,configAttributes,&config,1,&count)||!count) throw std::runtime_error("EGL RGBA8 D24S8 config unavailable");
		const EGLint pbuffer[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE},contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
		eglBindAPI(EGL_OPENGL_ES_API); surface=eglCreatePbufferSurface(display,config,pbuffer); context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
		if(surface==EGL_NO_SURFACE||context==EGL_NO_CONTEXT||!eglMakeCurrent(display,surface,surface,context)) throw std::runtime_error("EGL GLES2 context unavailable");
		std::printf("GL vendor: %s\nGL renderer: %s\nGL version: %s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
		gles::Initialize();
		if(std::getenv("ZANDRONUM_TEST_HISTOGRAM")) {if(argc>1)TestStateHistogram(argv[1]);}
		else
		{
			TestBasicSolid(); TestImmediateAndMatrices(); TestAlphaAndClipping(); TestTexturesAndBurn(); TestBuffers(); TestBatchStorage(); TestAttributeState(); TestFramebufferFormats(); TestSmoothLines(); TestProgramCache();
			if(argc>1) {TestMaterials(argv[1]);TestDepthClamp(argv[1]);TestSpecialization(argv[1]);TestLightSpecialization(argv[1]);}
			TestAttributeContexts(display,config,surface,context);
		}
		gles::Shutdown(); ErrorCheck("shutdown");
	}
	catch(const std::exception &error) { Check(false,error.what()); }
	if(display!=EGL_NO_DISPLAY)
	{
		eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
		if(context!=EGL_NO_CONTEXT) eglDestroyContext(display,context);
		if(surface!=EGL_NO_SURFACE) eglDestroySurface(display,surface);
		eglTerminate(display);
	}
	std::printf("Checks: %d; failures: %d\n",checks,failures); return failures?1:0;
}
