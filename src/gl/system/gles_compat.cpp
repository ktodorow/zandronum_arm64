// Engine-owned native GLES2 vertex submission and legacy scene-state bridge.
// No desktop GL entry points or external GL translator are used here.
#define GLES_COMPAT_IMPLEMENTATION
#include "gles_compat.h"
#include "i_system.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gles
{
namespace
{
struct Matrix
{
	GLfloat v[16];
	Matrix() { Identity(); }
	void Identity() { std::memset(v, 0, sizeof v); v[0]=v[5]=v[10]=v[15]=1; }
};
Matrix Multiply(const Matrix &a, const Matrix &b)
{
	Matrix c;
	for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
	{
		c.v[col*4+row]=0;
		for (int k=0; k<4; ++k) c.v[col*4+row]+=a.v[k*4+row]*b.v[col*4+k];
	}
	return c;
}
bool Inverse(const Matrix &m, Matrix &result)
{
	double a[4][8];
	for (int r=0; r<4; ++r) for (int c=0; c<8; ++c) a[r][c]=c<4?m.v[c*4+r]:(c-4==r?1:0);
	for (int c=0; c<4; ++c)
	{
		int pivot=c;
		for (int r=c+1; r<4; ++r) if (std::fabs(a[r][c])>std::fabs(a[pivot][c])) pivot=r;
		if (std::fabs(a[pivot][c])<1e-30) return false;
		for (int k=0; k<8; ++k) std::swap(a[pivot][k], a[c][k]);
		double scale=a[c][c];
		for (int k=0; k<8; ++k) a[c][k]/=scale;
		for (int r=0; r<4; ++r) if (r!=c)
		{
			double factor=a[r][c];
			for (int k=0; k<8; ++k) a[r][k]-=factor*a[c][k];
		}
	}
	for (int r=0; r<4; ++r) for (int c=0; c<4; ++c) result.v[c*4+r]=GLfloat(a[r][c+4]);
	return true;
}
struct Vertex
{
	GLfloat position[3], uv[2], color[4], normal[3], glow[2], fog[4], light, uv1[3];
};
const GLint components[8]={3,2,4,3,2,4,1,3};
const size_t offsets[8]={offsetof(Vertex,position),offsetof(Vertex,uv),offsetof(Vertex,color),offsetof(Vertex,normal),offsetof(Vertex,glow),offsetof(Vertex,fog),offsetof(Vertex,light),offsetof(Vertex,uv1)};
struct Array
{
	GLint size;
	GLenum type;
	GLboolean normalized, enabled;
	GLsizei stride;
	const void *pointer;
	GLuint buffer;
	Array():size(4),type(GL_FLOAT),normalized(GL_FALSE),enabled(GL_FALSE),stride(0),pointer(0),buffer(0) {}
};
struct Buffer
{
	std::vector<unsigned char> bytes;
	GLintptr mapOffset;
	GLsizeiptr mapLength;
	bool mapped;
	Buffer():mapOffset(0),mapLength(0),mapped(false) {}
};
struct Program
{
	GLint projection,view,model,texture,normal,alphaTest,alphaThreshold,alphaFunc,clip,clipEnabled,clipActive,depthClamp,textureEnabled,textureMode,burn,lineSmooth,lineHalfWidth,precomputedSphere,depthSplit;
	unsigned long revision;
	Program():revision(0) {}
};
// Material uniforms use logical base locations. Canonical array elements avoid
// stale overlapping [0]/[k] uploads when switching physical programs.
struct MaterialUniform
{
	bool integer;
	unsigned width,matrix;
	std::vector<GLfloat> floats;
	std::vector<GLint> integers;
	std::vector<GLint> locations[2];
	std::vector<unsigned long> version,resident[2];
	const void *Data(size_t element) const {return integer?static_cast<const void*>(&integers[element*width]):static_cast<const void*>(&floats[element*width]);}
	void *Data(size_t element) {return integer?static_cast<void*>(&integers[element*width]):static_cast<void*>(&floats[element*width]);}
};
struct UniformElement {size_t uniform,element;};
struct ShaderVariant
{
	GLuint fast;
	unsigned maxLightVec4;
	int lightRange;
	unsigned long generation;
	std::vector<MaterialUniform> uniforms;
	std::map<GLint,UniformElement> elements;
	ShaderVariant():fast(0),maxLightVec4(0),lightRange(-1),generation(1) {}
};
struct State
{
	Matrix projection,view,texture[8];
	std::vector<Matrix> projectionStack,viewStack,textureStack[8];
	GLenum matrixMode,primitive,batchMode;
	unsigned activeTexture,clientTexture;
	GLuint arrayBuffer,elementBuffer,program,actualProgram,basicProgram,basicSolidProgram,streamBuffer;
	Vertex current;
	Array arrays[8];
	// Physical GLES state differs from caller arrays during stream draws.
	// State replacement on Initialize/Shutdown invalidates both masks.
	Array appliedArrays[8];
	unsigned appliedPointers,appliedEnables;
	std::vector<Vertex> primitiveVertices,batch,depthVertices;
	// Two depth planes can split one triangle into at most four pieces.
	std::vector<Vertex> depthPieces[2][4];
	std::map<GLuint,Buffer> buffers;
	std::map<GLuint,Program> programs;
	std::map<GLuint,ShaderVariant> variants;
	std::map<GLuint,std::map<GLint,std::vector<unsigned char> > > uniforms;
	std::map<unsigned,std::vector<unsigned char> > pipeline;
	std::map<GLenum,bool> enabled;
	GLuint textures[8];
	bool textureEnabled[8],alphaTest,depthClamp,inBegin,initialized,flushActive,uintIndices,batchSmooth,hardwareDepthClamp;
	GLenum alphaFunc;
	GLfloat alphaThreshold,clipPlanes[24],lineWidth,batchLineHalfWidth;
	GLint clipEnabled[6],textureMode;
	unsigned long revision;
	State():matrixMode(GL_MODELVIEW),primitive(0),batchMode(GL_TRIANGLES),activeTexture(0),clientTexture(0),arrayBuffer(0),elementBuffer(0),program(0),actualProgram(0),basicProgram(0),basicSolidProgram(0),streamBuffer(0),appliedPointers(0),appliedEnables(0),alphaTest(false),depthClamp(false),inBegin(false),initialized(false),flushActive(false),uintIndices(false),batchSmooth(false),hardwareDepthClamp(false),alphaFunc(GL_ALWAYS),alphaThreshold(0),lineWidth(1),batchLineHalfWidth(0.5f),textureMode(0),revision(1)
	{
		std::memset(&current,0,sizeof current);
		current.color[0]=current.color[1]=current.color[2]=current.color[3]=1;
		current.normal[2]=1;
		std::memset(textures,0,sizeof textures);
		std::memset(textureEnabled,0,sizeof textureEnabled);
		std::memset(clipPlanes,0,sizeof clipPlanes);
		std::memset(clipEnabled,0,sizeof clipEnabled);
	}
};
State s;
void Change() { Flush(); ++s.revision; }
Matrix &CurrentMatrix() { return s.matrixMode==GL_PROJECTION?s.projection:s.matrixMode==GL_TEXTURE?s.texture[s.activeTexture]:s.view; }
std::vector<Matrix> &CurrentStack() { return s.matrixMode==GL_PROJECTION?s.projectionStack:s.matrixMode==GL_TEXTURE?s.textureStack[s.activeTexture]:s.viewStack; }
void SetMatrix(const Matrix &m)
{
	if (std::memcmp(CurrentMatrix().v,m.v,sizeof m.v)) { Change(); CurrentMatrix()=m; }
}
GLfloat *Attribute(Vertex &v, unsigned index) { return reinterpret_cast<GLfloat*>(reinterpret_cast<unsigned char*>(&v)+offsets[index]); }
const GLfloat *Attribute(const Vertex &v, unsigned index) { return reinterpret_cast<const GLfloat*>(reinterpret_cast<const unsigned char*>(&v)+offsets[index]); }

// Compile/link failure handling adapted from GZDoom gles_shaderprogram.cpp,
// commit c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7. Altered for local APIs.
// Copyright (c) 2016-2020 Magnus Norddahl
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from
// the use of this software. Permission is granted to anyone to use this
// software for any purpose, including commercial applications, and to alter
// it and redistribute it freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software in a
// product, an acknowledgment in the product documentation would be
// appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
GLuint Compile(GLenum type, const char *source)
{
	GLuint shader=glCreateShader(type);
	glShaderSource(shader,1,&source,0);
	glCompileShader(shader);
	GLint status=0;
	glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
	if (!status)
	{
		GLint length=0;
		glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
		std::vector<char> log(std::max(1,length),0);
		glGetShaderInfoLog(shader,GLsizei(log.size()),0,&log[0]);
		I_FatalError("Native GLES basic %s shader failed:\n%s",type==GL_VERTEX_SHADER?"vertex":"fragment",&log[0]);
	}
	return shader;
}
GLuint BasicProgram(bool solid=false)
{
	const char *vp=
		"#version 100\nprecision highp float;\n"
		"attribute vec3 aPosition; attribute vec2 aTexCoord; attribute vec4 aColor; attribute vec3 aTexCoord1; attribute vec3 aLineCoverage;\n"
		"uniform mat4 uProjectionMatrix,uViewMatrix,uTextureMatrix; uniform int uDepthClamp,uGLESDepthSplit;\n"
		"varying vec2 vUV,vUV1; varying vec4 vColor,vEye; varying vec3 vLineCoverage;\n"
		"void main(){gl_PointSize=1.0;vEye=uViewMatrix*vec4(aPosition,1.0); gl_Position=uProjectionMatrix*vEye;"
		"if(uDepthClamp!=0){if(uGLESDepthSplit!=0&&aTexCoord1.z!=0.0)gl_Position.z=aTexCoord1.z*gl_Position.w;"
		"else gl_Position.z=clamp(gl_Position.z,-gl_Position.w,gl_Position.w);}"
		"vUV=(uTextureMatrix*vec4(aTexCoord,0.0,1.0)).xy;vUV1=aTexCoord1.xy;vColor=aColor;vLineCoverage=aLineCoverage;}\n";
	const char *fp=
		"#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
		"uniform sampler2D texture0,texture1; uniform int uTextureEnabled,uTextureMode,uBurn;\n"
		"uniform int uAlphaTest,uAlphaFunc,uClipEnabled[6],uClipActive; uniform float uAlphaThreshold; uniform vec4 uClipPlanes[6];\n"
		"uniform int uLineSmooth; uniform float uLineHalfWidth;\n"
		"varying vec2 vUV,vUV1; varying vec4 vColor,vEye; varying vec3 vLineCoverage;\n"
		"void main(){if(uClipActive!=0){for(int i=0;i<6;i++){if(uClipEnabled[i]!=0&&dot(vEye,uClipPlanes[i])<0.0)discard;}}"
		"vec4 c=vec4(1.0);if(uTextureEnabled!=0)c=texture2D(texture0,vUV);"
		"if(uTextureMode==1)c.rgb=vec3(1.0);if(uTextureMode==2)c.a=1.0;"
		"if(uTextureMode==4||uTextureMode==6)c.rgb=vec3(1.0)-c.rgb; if(uTextureMode==6)c.a=1.0;"
		"if(uBurn!=0)c.rgb=texture2D(texture1,vUV1).rgb;c*=vColor;"
		"if(uAlphaTest!=0){bool pass=true;"
		"if(uAlphaFunc==512)pass=false;else if(uAlphaFunc==513)pass=c.a<uAlphaThreshold;"
		"else if(uAlphaFunc==514)pass=c.a==uAlphaThreshold;else if(uAlphaFunc==515)pass=c.a<=uAlphaThreshold;"
		"else if(uAlphaFunc==516)pass=c.a>uAlphaThreshold;else if(uAlphaFunc==517)pass=c.a!=uAlphaThreshold;"
		"else if(uAlphaFunc==518)pass=c.a>=uAlphaThreshold;if(!pass)discard;}"
		"if(uLineSmooth!=0){float side=clamp(uLineHalfWidth+0.5-abs(vLineCoverage.y),0.0,1.0);"
		"float cap=clamp(0.5+min(vLineCoverage.x,vLineCoverage.z-vLineCoverage.x),0.0,1.0);c.a*=side*cap;}gl_FragColor=c;}\n";
	// Full-screen pickup/damage blends need only interpolated vertex color.
	// Keeping discard/texture branches out of this sibling avoids their cost
	// even when the generic shader's uniforms would disable those operations.
	const char *solidFP=
		"#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
		"varying vec4 vColor;void main(){gl_FragColor=vColor;}\n";
	// Share vertex path, including software depth splitting: engine keeps
	// depth clamp enabled even for full-screen overlays.
	GLuint vertex=Compile(GL_VERTEX_SHADER,vp),fragment=Compile(GL_FRAGMENT_SHADER,solid?solidFP:fp),program=glCreateProgram();
	glAttachShader(program,vertex); glAttachShader(program,fragment);
	glBindAttribLocation(program,0,"aPosition"); glBindAttribLocation(program,1,"aTexCoord");
	glBindAttribLocation(program,2,"aColor"); glBindAttribLocation(program,3,"aLineCoverage"); glBindAttribLocation(program,7,"aTexCoord1");
	glLinkProgram(program);
	GLint status=0; glGetProgramiv(program,GL_LINK_STATUS,&status);
	if (!status)
	{
		GLint length=0; glGetProgramiv(program,GL_INFO_LOG_LENGTH,&length);
		std::vector<char> log(std::max(1,length),0); glGetProgramInfoLog(program,GLsizei(log.size()),0,&log[0]);
		I_FatalError("Native GLES basic shader link failed:\n%s",&log[0]);
	}
	glDeleteShader(vertex); glDeleteShader(fragment);
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program,"texture0"),0); glUniform1i(glGetUniformLocation(program,"texture1"),1);
	return program;
}
Program &Locations(GLuint program)
{
	std::map<GLuint,Program>::iterator found=s.programs.find(program);
	if (found!=s.programs.end()) return found->second;
	Program p;
#define LOCATION(member,name) p.member=glGetUniformLocation(program,name)
	LOCATION(projection,"uProjectionMatrix"); LOCATION(view,"uViewMatrix"); LOCATION(model,"uModelMatrix");
	LOCATION(texture,"uTextureMatrix"); LOCATION(normal,"uNormalMatrix"); LOCATION(alphaTest,"uAlphaTest");
	LOCATION(alphaThreshold,"uAlphaThreshold"); LOCATION(alphaFunc,"uAlphaFunc"); LOCATION(clip,"uClipPlanes[0]");
	LOCATION(clipEnabled,"uClipEnabled[0]"); LOCATION(depthClamp,"uDepthClamp"); LOCATION(textureEnabled,"uTextureEnabled");
	LOCATION(clipActive,"uClipActive");
	LOCATION(textureMode,"uTextureMode"); LOCATION(burn,"uBurn");
	LOCATION(lineSmooth,"uLineSmooth"); LOCATION(lineHalfWidth,"uLineHalfWidth");
	LOCATION(precomputedSphere,"uGLESPrecomputedSphere");
	LOCATION(depthSplit,"uGLESDepthSplit");
#undef LOCATION
	return s.programs.insert(std::make_pair(program,p)).first->second;
}
void ConstantAttributes()
{
	for (unsigned i=0;i<8;++i)
	{
		const GLfloat *v=Attribute(s.current,i);
		if (components[i]==1) glVertexAttrib1f(i,v[0]);
		else if (components[i]==2) glVertexAttrib2fv(i,v);
		else if (components[i]==3) glVertexAttrib3fv(i,v);
		else glVertexAttrib4fv(i,v);
	}
}
void ApplyArrayEnabled(unsigned index,bool enabled)
{
	Array &applied=s.appliedArrays[index];
	if((s.appliedEnables&(1u<<index))&&bool(applied.enabled)==enabled)return;
	if(enabled)glEnableVertexAttribArray(index);else glDisableVertexAttribArray(index);
	applied.enabled=enabled?GL_TRUE:GL_FALSE;s.appliedEnables|=1u<<index;
}
void ApplyArrayPointer(unsigned index,GLuint buffer,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer)
{
	Array &applied=s.appliedArrays[index];
	if((s.appliedPointers&(1u<<index))&&applied.buffer==buffer&&applied.size==size&&applied.type==type&&
		applied.normalized==normalized&&applied.stride==stride&&applied.pointer==pointer)return;
	glVertexAttribPointer(index,size,type,normalized,stride,pointer);
	applied.buffer=buffer;applied.size=size;applied.type=type;applied.normalized=normalized;
	applied.stride=stride;applied.pointer=pointer;s.appliedPointers|=1u<<index;
}
void SetupArrays()
{
	ConstantAttributes();
	for (unsigned i=0;i<8;++i)
	{
		Array &a=s.arrays[i];
		if (a.enabled)
		{
			glBindBuffer(GL_ARRAY_BUFFER,a.buffer);
			ApplyArrayPointer(i,a.buffer,a.size,a.type,a.normalized,a.stride,a.pointer);
			ApplyArrayEnabled(i,true);
		}
		else ApplyArrayEnabled(i,false);
	}
	glBindBuffer(GL_ARRAY_BUFFER,s.arrayBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,s.elementBuffer);
}
size_t TypeSize(GLenum type)
{
	switch(type) { case GL_FLOAT:case GL_UNSIGNED_INT:case GL_INT:return 4; case GL_UNSIGNED_SHORT:case GL_SHORT:return 2; case GL_UNSIGNED_BYTE:case GL_BYTE:return 1; default:I_FatalError("Native GLES: unsupported vertex/index type 0x%x",type); }
	return 0;
}
double Component(const unsigned char *p, GLenum type, bool normalized)
{
	switch(type)
	{
	case GL_FLOAT: { GLfloat v; std::memcpy(&v,p,4); return v; }
	case GL_UNSIGNED_BYTE:return normalized?*p/255.0:*p;
	case GL_BYTE: { signed char v; std::memcpy(&v,p,1); return normalized?std::max(-1.0,v/127.0):v; }
	case GL_UNSIGNED_SHORT: { GLushort v; std::memcpy(&v,p,2); return normalized?v/65535.0:v; }
	case GL_SHORT: { GLshort v; std::memcpy(&v,p,2); return normalized?std::max(-1.0,v/32767.0):v; }
	case GL_UNSIGNED_INT: { GLuint v; std::memcpy(&v,p,4); return normalized?v/4294967295.0:v; }
	case GL_INT: { GLint v; std::memcpy(&v,p,4); return normalized?std::max(-1.0,v/2147483647.0):v; }
	}
	return 0;
}
Vertex FetchVertex(GLuint index)
{
	Vertex result=s.current;
	for (unsigned i=0;i<8;++i) if(s.arrays[i].enabled)
	{
		const Array &a=s.arrays[i]; size_t unit=TypeSize(a.type);
		size_t offset=reinterpret_cast<size_t>(a.pointer)+size_t(index)*(a.stride?a.stride:a.size*unit);
		const unsigned char *p;
		if (a.buffer)
		{
			Buffer &b=s.buffers[a.buffer];
			if (offset+size_t(a.size)*unit>b.bytes.size()) I_FatalError("Native GLES vertex array exceeds buffer");
			p=&b.bytes[offset];
		}
		else p=reinterpret_cast<const unsigned char*>(a.pointer)+size_t(index)*(a.stride?a.stride:a.size*unit);
		GLfloat *out=Attribute(result,i);
		for (int k=0;k<std::min(a.size,components[i]);++k) out[k]=GLfloat(Component(p+k*unit,a.type,a.normalized!=0));
	}
	return result;
}
Matrix DepthTransform()
{
	// Only material programs consume texture matrix 7 as their model transform.
	Matrix view=Locations(s.program).model>=0?Multiply(s.view,s.texture[7]):s.view;
	return Multiply(s.projection,view);
}
GLfloat DepthDistance(const Vertex &vertex,const Matrix &transform,unsigned plane)
{
	GLfloat z=transform.v[14],w=transform.v[15];
	for(unsigned i=0;i<3;++i)
	{
		z+=transform.v[i*4+2]*vertex.position[i];
		w+=transform.v[i*4+3]*vertex.position[i];
	}
	return plane?w-z:w+z;
}
bool NeedsDepthSplit(const Vertex *vertices,size_t count,const Matrix &transform)
{
	for(unsigned plane=0;plane<2;++plane)
	{
		bool positive=false,negative=false;
		for(size_t i=0;i<count;++i)
		{
			GLfloat w=transform.v[15];for(unsigned k=0;k<3;++k)w+=transform.v[k*4+3]*vertices[i].position[k];
			if(w<0)return true;
			GLfloat d=DepthDistance(vertices[i],transform,plane);
			positive=positive||d>0;negative=negative||d<0;
			if(positive&&negative)return true;
		}
	}
	return false;
}
bool NeedsDepthSplit(const std::vector<Vertex> &vertices,const Matrix &transform)
{
	return !vertices.empty()&&NeedsDepthSplit(&vertices[0],vertices.size(),transform);
}
Vertex Interpolate(const Vertex &a,const Vertex &b,GLfloat t)
{
	Vertex result;
	for(unsigned i=0;i<8;++i)
	{
		const GLfloat *av=Attribute(a,i),*bv=Attribute(b,i);
		GLfloat *out=Attribute(result,i);
		for(int j=0;j<components[i];++j)out[j]=av[j]+t*(bv[j]-av[j]);
	}
	return result;
}
void SplitDepthPolygon(const std::vector<Vertex> &input,const Matrix &transform,unsigned plane,
	std::vector<Vertex> &positive,std::vector<Vertex> &negative)
{
	bool hasPositive=false,hasNegative=false;
	for(size_t i=0;i<input.size();++i)
	{
		GLfloat d=DepthDistance(input[i],transform,plane);
		hasPositive=hasPositive||d>0;hasNegative=hasNegative||d<0;
	}
	// A polygon lying on a boundary belongs to one region, avoiding duplicate
	// fragments (visible with blending) when a previous split created that edge.
	if(!hasNegative){positive=input;return;}
	if(!hasPositive){negative=input;return;}
	for(size_t i=0;i<input.size();++i)
	{
		const Vertex &a=input[i],&b=input[(i+1)%input.size()];
		GLfloat da=DepthDistance(a,transform,plane),db=DepthDistance(b,transform,plane);
		if(da>=0)positive.push_back(a);
		if(da<=0)negative.push_back(a);
		if((da<0&&db>0)||(da>0&&db<0))
		{
			Vertex intersection=Interpolate(a,b,da/(da-db));
			positive.push_back(intersection);negative.push_back(intersection);
		}
	}
}
void SphereCoordinates(Vertex &vertex)
{
	// SPHEREMAP computes a nonlinear vertex output. Evaluate it before splitting,
	// then interpolate those UVs rather than recomputing them at new vertices.
	Matrix view=Multiply(s.view,s.texture[7]),inverse;
	Inverse(s.view,inverse);
	GLfloat eye[3],normal[3],lengthEye=0,lengthNormal=0;
	for(unsigned row=0;row<3;++row)
	{
		eye[row]=view.v[12+row];normal[row]=0;
		for(unsigned col=0;col<3;++col)
		{
			eye[row]+=view.v[col*4+row]*vertex.position[col];
			normal[row]+=inverse.v[row*4+col]*vertex.normal[col];
		}
		lengthEye+=eye[row]*eye[row];lengthNormal+=normal[row]*normal[row];
	}
	lengthEye=std::sqrt(lengthEye);lengthNormal=std::sqrt(lengthNormal);
	GLfloat dot=0;
	for(unsigned i=0;i<3;++i){eye[i]/=lengthEye;normal[i]/=lengthNormal;dot+=eye[i]*normal[i];}
	GLfloat reflected[3];for(unsigned i=0;i<3;++i)reflected[i]=eye[i]-2*dot*normal[i];
	GLfloat m=2*std::sqrt(reflected[0]*reflected[0]+reflected[1]*reflected[1]+(reflected[2]+1)*(reflected[2]+1));
	vertex.uv1[0]=reflected[0]/m+0.5f;vertex.uv1[1]=reflected[1]/m+0.5f;
}
void SetDepthRegion(Vertex *vertices,size_t count,const Matrix &transform)
{
	GLfloat nearDistance=0,farDistance=0;
	for(size_t i=0;i<count;++i)
	{
		nearDistance+=DepthDistance(vertices[i],transform,0);
		farDistance+=DepthDistance(vertices[i],transform,1);
	}
	GLfloat region=nearDistance<0?-1:farDistance<0?1:0;
	for(size_t i=0;i<count;++i)vertices[i].uv1[2]=region;
}
bool SplitDepthBatch()
{
	if(!s.depthClamp||s.hardwareDepthClamp||s.batchMode==GL_POINTS)return false;
	Matrix transform=DepthTransform();
	if(!NeedsDepthSplit(s.batch,transform))return false;
	bool sphere=Locations(s.program).precomputedSphere>=0;
	if(sphere)for(size_t i=0;i<s.batch.size();++i)SphereCoordinates(s.batch[i]);
	std::vector<Vertex> &result=s.depthVertices;
	result.clear();
	result.reserve(s.batch.size());
	unsigned count=s.batchMode==GL_LINES?2:3;
	for(size_t i=0;i+count<=s.batch.size();i+=count)
	{
		if(!NeedsDepthSplit(&s.batch[i],count,transform))
		{
			SetDepthRegion(&s.batch[i],count,transform);
			result.insert(result.end(),s.batch.begin()+i,s.batch.begin()+i+count);continue;
		}
		if(count==2)
		{
			GLfloat cuts[4]={0,1,0,0};unsigned n=2;
			for(unsigned plane=0;plane<2;++plane)
			{
				GLfloat a=DepthDistance(s.batch[i],transform,plane),b=DepthDistance(s.batch[i+1],transform,plane);
				if((a<0&&b>0)||(a>0&&b<0))cuts[n++]=a/(a-b);
			}
			std::sort(cuts,cuts+n);
			for(unsigned j=0;j+1<n;++j)
			{
				Vertex segment[2]={Interpolate(s.batch[i],s.batch[i+1],cuts[j]),Interpolate(s.batch[i],s.batch[i+1],cuts[j+1])};
				SetDepthRegion(segment,2,transform);result.insert(result.end(),segment,segment+2);
			}
		}
		else
		{
			s.depthPieces[0][0].assign(s.batch.begin()+i,s.batch.begin()+i+3);
			unsigned pieceCount=1;
			for(unsigned plane=0;plane<2;++plane)
			{
				std::vector<Vertex> *pieces=s.depthPieces[plane&1];
				std::vector<Vertex> *next=s.depthPieces[(plane+1)&1];
				for(unsigned j=0;j<pieceCount;++j)
				{
					next[j*2].clear();next[j*2+1].clear();
					if(pieces[j].size()>=3)
						SplitDepthPolygon(pieces[j],transform,plane,next[j*2],next[j*2+1]);
				}
				pieceCount*=2;
			}
			for(unsigned j=0;j<pieceCount;++j)
			{
				std::vector<Vertex> &piece=s.depthPieces[0][j];
				if(piece.size()<3)continue;
				SetDepthRegion(&piece[0],piece.size(),transform);
				for(size_t k=1;k+1<piece.size();++k)
				{result.push_back(piece[0]);result.push_back(piece[k]);result.push_back(piece[k+1]);}
			}
		}
	}
	s.batch.swap(result);
	return true;
}
void AppendSmoothLine(const Vertex &start, const Vertex &end, const Matrix &transform, const Matrix &inverse,
	const GLint *viewport, GLfloat halfWidth, std::vector<Vertex> &vertices)
{
	GLfloat clip[2][4];
	const Vertex *ends[2]={&start,&end};
	for (int i=0;i<2;++i) for (int row=0;row<4;++row)
	{
		clip[i][row]=transform.v[12+row];
		for (int col=0;col<3;++col) clip[i][row]+=transform.v[col*4+row]*ends[i]->position[col];
	}
	if (clip[0][3]<=0 || clip[1][3]<=0) return;
	GLfloat window[2][2];
	for (int i=0;i<2;++i) for (int j=0;j<2;++j)
		window[i][j]=viewport[j]+(clip[i][j]/clip[i][3]+1)*viewport[j+2]*0.5f;
	GLfloat dx=window[1][0]-window[0][0],dy=window[1][1]-window[0][1];
	GLfloat length=std::sqrt(dx*dx+dy*dy);
	if (length<1e-6f) return;
	dx/=length;dy/=length;
	GLfloat extent=halfWidth+1;
	Vertex quad[4];
	for (int i=0;i<4;++i)
	{
		int endpoint=i/2;
		GLfloat along=endpoint?1.f:-1.f,side=(i&1)?-extent:extent;
		GLfloat x=window[endpoint][0]+along*dx-side*dy;
		GLfloat y=window[endpoint][1]+along*dy+side*dx;
		GLfloat ndc[4]={2*(x-viewport[0])/viewport[2]-1,2*(y-viewport[1])/viewport[3]-1,
			clip[endpoint][2]/clip[endpoint][3],1};
		GLfloat object[4]={0,0,0,0};
		for (int row=0;row<4;++row) for (int col=0;col<4;++col) object[row]+=inverse.v[col*4+row]*ndc[col];
		if (std::fabs(object[3])<1e-30f) return;
		quad[i]=*ends[endpoint];
		for (int j=0;j<3;++j) quad[i].position[j]=object[j]/object[3];
		// Basic 2D rendering has no normal input. Reuse that attribute for
		// distances along/across this segment, measured in viewport pixels.
		quad[i].normal[0]=endpoint?length+1:-1;
		quad[i].normal[1]=side;
		quad[i].normal[2]=length;
	}
	const int indices[6]={0,1,3,0,3,2};
	for (int i=0;i<6;++i) vertices.push_back(quad[indices[i]]);
}
void AppendPrimitive(GLenum mode, const std::vector<Vertex> &vertices)
{
	GLenum output=(mode==GL_POINTS)?GL_POINTS:(mode==GL_LINES||mode==GL_LINE_STRIP||mode==GL_LINE_LOOP)?GL_LINES:GL_TRIANGLES;
	bool smooth=output==GL_LINES && s.program==s.basicProgram && s.enabled[GL_LINE_SMOOTH];
	GLfloat halfWidth=s.lineWidth*0.5f;
	std::vector<Vertex> smoothVertices;
	const size_t n=vertices.size();
	if (smooth)
	{
		GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
		if (viewport[2]<=0 || viewport[3]<=0) return;
		Matrix transform=Multiply(s.projection,s.view),inverse;
		if (!Inverse(transform,inverse)) I_FatalError("Native GLES smooth line has a singular transform");
		smoothVertices.reserve(n*6);
		size_t step=mode==GL_LINES?2:1;
		for (size_t i=0;i+1<n;i+=step)
			AppendSmoothLine(vertices[i],vertices[i+1],transform,inverse,viewport,halfWidth,smoothVertices);
		if (mode==GL_LINE_LOOP && n>1)
			AppendSmoothLine(vertices[n-1],vertices[0],transform,inverse,viewport,halfWidth,smoothVertices);
		if (smoothVertices.empty()) return;
		output=GL_TRIANGLES;
	}
	if (!s.batch.empty() && (s.batchMode!=output || s.batchSmooth!=smooth || (smooth && s.batchLineHalfWidth!=halfWidth))) Flush();
	if (s.batchSmooth!=smooth || (smooth && s.batchLineHalfWidth!=halfWidth))
	{
		s.batchSmooth=smooth;s.batchLineHalfWidth=halfWidth;++s.revision;
	}
	s.batchMode=output;
	if (smooth)
	{
		s.batch.insert(s.batch.end(),smoothVertices.begin(),smoothVertices.end());
		if (s.batch.size()>16384) Flush();
		return;
	}
	switch(mode)
	{
	case GL_QUADS:
		for(size_t i=0;i+3<n;i+=4) { s.batch.push_back(vertices[i]);s.batch.push_back(vertices[i+1]);s.batch.push_back(vertices[i+2]);s.batch.push_back(vertices[i]);s.batch.push_back(vertices[i+2]);s.batch.push_back(vertices[i+3]); } break;
	case GL_TRIANGLE_FAN:case GL_POLYGON:
		for(size_t i=1;i+1<n;++i) { s.batch.push_back(vertices[0]);s.batch.push_back(vertices[i]);s.batch.push_back(vertices[i+1]); } break;
	case GL_TRIANGLE_STRIP:
		for(size_t i=0;i+2<n;++i) {s.batch.push_back(vertices[i+(i&1)]);s.batch.push_back(vertices[i+1-(i&1)]);s.batch.push_back(vertices[i+2]);} break;
	case GL_QUAD_STRIP:
		for(size_t i=0;i+3<n;i+=2) {s.batch.push_back(vertices[i]);s.batch.push_back(vertices[i+1]);s.batch.push_back(vertices[i+3]);s.batch.push_back(vertices[i]);s.batch.push_back(vertices[i+3]);s.batch.push_back(vertices[i+2]);} break;
	case GL_LINE_STRIP:case GL_LINE_LOOP:
		for(size_t i=0;i+1<n;++i) {s.batch.push_back(vertices[i]);s.batch.push_back(vertices[i+1]);}
		if(mode==GL_LINE_LOOP && n>1){s.batch.push_back(vertices[n-1]);s.batch.push_back(vertices[0]);} break;
	case GL_POINTS:case GL_LINES:case GL_TRIANGLES:s.batch.insert(s.batch.end(),vertices.begin(),vertices.end());break;
	default:I_FatalError("Native GLES: unsupported primitive 0x%x",mode);
	}
	if (s.batch.size()>16384) Flush();
}
GLuint BoundBuffer(GLenum target) { return target==GL_ELEMENT_ARRAY_BUFFER?s.elementBuffer:s.arrayBuffer; }
void BindActual(GLuint program)
{
	if(s.actualProgram!=program){glUseProgram(program);s.actualProgram=program;}
}
void UploadMaterial(const MaterialUniform &u,unsigned physical,size_t first,size_t count)
{
	GLint location=u.locations[physical][first];GLsizei n=GLsizei(count);
	if(u.integer)
	{
		const GLint *v=&u.integers[first*u.width];
		switch(u.width){case 1:glUniform1iv(location,n,v);break;case 2:glUniform2iv(location,n,v);break;case 3:glUniform3iv(location,n,v);break;case 4:glUniform4iv(location,n,v);break;}
	}
	else
	{
		const GLfloat *v=&u.floats[first*u.width];
		if(u.matrix){switch(u.matrix){case 2:glUniformMatrix2fv(location,n,GL_FALSE,v);break;case 3:glUniformMatrix3fv(location,n,GL_FALSE,v);break;case 4:glUniformMatrix4fv(location,n,GL_FALSE,v);break;}}
		else switch(u.width){case 1:glUniform1fv(location,n,v);break;case 2:glUniform2fv(location,n,v);break;case 3:glUniform3fv(location,n,v);break;case 4:glUniform4fv(location,n,v);break;}
	}
}
void ReplayMaterial(ShaderVariant &variant)
{
	unsigned physical=s.actualProgram==variant.fast?1:0;
	for(size_t k=0;k<variant.uniforms.size();++k)
	{
		MaterialUniform &u=variant.uniforms[k];
		for(size_t i=0;i<u.version.size();)
		{
			if(u.locations[physical][i]<0||u.resident[physical][i]==u.version[i]){++i;continue;}
			size_t end=i+1;
			while(end<u.version.size()&&u.locations[physical][end]==u.locations[physical][end-1]+1&&u.resident[physical][end]!=u.version[end])++end;
			UploadMaterial(u,physical,i,end-i);
			for(;i<end;++i)u.resident[physical][i]=u.version[i];
		}
	}
}
void PrepareDrawProgram()
{
	if(s.program==s.basicProgram)
	{
		bool solid=!s.textureEnabled[0]&&!s.textureEnabled[1]&&!s.alphaTest&&
			!s.batchSmooth&&!s.enabled[GL_LINE_SMOOTH]&&s.textureMode==0;
		for(unsigned i=0;i<6;++i)if(s.clipEnabled[i])solid=false;
		BindActual(solid?s.basicSolidProgram:s.basicProgram);return;
	}
	std::map<GLuint,ShaderVariant>::iterator found=s.variants.find(s.program);
	if(found==s.variants.end()){BindActual(s.program);return;}
	bool fast=!s.alphaTest||s.alphaFunc==GL_ALWAYS;
	for(unsigned i=0;i<6;++i)if(s.clipEnabled[i])fast=false;
	if(fast&&found->second.maxLightVec4)
	{
		const GLint *range=&found->second.uniforms[found->second.lightRange].integers[0];
		// Canonical base data retains full supported capacity. Only choose the
		// short physical shader when complete category endpoints fit its bound.
		fast=range[0]>=0&&range[0]<=range[1]&&range[1]<=range[2]&&range[2]<=256&&
			!((range[0]|range[1]|range[2])&1)&&unsigned(range[2])<=found->second.maxLightVec4;
	}
	BindActual(fast?found->second.fast:s.program);ReplayMaterial(found->second);
}
void UnregisterVariants(GLuint program)
{
	for(std::map<GLuint,ShaderVariant>::iterator i=s.variants.begin();i!=s.variants.end();)
	{
		if(i->first!=program&&i->second.fast!=program){++i;continue;}
		// A renderer material cache may skip uploads when the base is next used.
		// Preserve its latest canonical values even while another material is bound.
		GLuint previous=s.actualProgram;
		BindActual(i->first);ReplayMaterial(i->second);
		if(s.program!=i->first)BindActual(previous);
		s.uniforms.erase(i->first);s.uniforms.erase(i->second.fast);
		s.variants.erase(i++);
	}
}
bool RouteUniform(GLint location,bool integer,unsigned width,unsigned matrix,GLsizei count,const void *value)
{
	std::map<GLuint,ShaderVariant>::iterator found=s.variants.find(s.program);
	if(found==s.variants.end())return false;
	if(location<0||count==0)return true;
	ShaderVariant &variant=found->second;
	std::map<GLint,UniformElement>::iterator slot=variant.elements.find(location);
	if(slot==variant.elements.end())I_FatalError("Native GLES material upload uses unmapped logical uniform %d",location);
	MaterialUniform &u=variant.uniforms[slot->second.uniform];size_t first=slot->second.element;
	if(count<0||first+size_t(count)>u.version.size()||integer!=u.integer||width!=u.width||matrix!=u.matrix)
		I_FatalError("Native GLES material uniform upload does not match declared type/array");
	size_t bytes=size_t(count)*width*sizeof(GLfloat);
	if(!std::memcmp(u.Data(first),value,bytes))return true;
	Flush(); // old geometry must see old canonical values, including overlaps.
	std::memcpy(u.Data(first),value,bytes);++variant.generation;
	for(size_t i=first;i<first+size_t(count);++i)u.version[i]=variant.generation;
	ReplayMaterial(variant);return true;
}
bool UniformChanged(GLint location, const void *value, size_t bytes)
{
	if(location<0) return false;
	std::vector<unsigned char> &old=s.uniforms[s.program][location];
	if(old.size()==bytes && (!bytes||!std::memcmp(&old[0],value,bytes))) return false;
	Flush(); old.resize(bytes); if(bytes) std::memcpy(&old[0],value,bytes); return true;
}
bool PipelineChanged(unsigned key, const void *value, size_t bytes)
{
	std::vector<unsigned char> &old=s.pipeline[key];
	if(old.size()==bytes && !std::memcmp(&old[0],value,bytes)) return false;
	Flush(); old.resize(bytes); std::memcpy(&old[0],value,bytes); return true;
}
}

void RegisterNoClipAlphaVariant(GLuint base,GLuint fast,unsigned maxLightVec4)
{
	Flush();UnregisterVariants(base);UnregisterVariants(fast);
	if(!base||!fast||base==fast||maxLightVec4>256||(maxLightVec4&1))I_FatalError("Native GLES invalid shader variant pair");
	ShaderVariant variant;variant.fast=fast;variant.maxLightVec4=maxLightVec4;
	GLint count=0,maximum=0;glGetProgramiv(base,GL_ACTIVE_UNIFORMS,&count);glGetProgramiv(base,GL_ACTIVE_UNIFORM_MAX_LENGTH,&maximum);
	std::vector<char> name(size_t(std::max(1,maximum)));
	for(GLint i=0;i<count;++i)
	{
		GLsizei length=0;GLint size=0;GLenum type=0;glGetActiveUniform(base,i,maximum,&length,&size,&type,&name[0]);
		std::string uniform(&name[0],size_t(length)),stem=uniform;
		size_t array=stem.find("[0]");if(array!=std::string::npos)stem.erase(array,3);
		// These uniforms belong exclusively to ApplyShaderState/depth splitting;
		// material replay must never overwrite their independent revision cache.
		const char *managed[]={"uProjectionMatrix","uViewMatrix","uModelMatrix","uTextureMatrix","uNormalMatrix",
			"uAlphaTest","uAlphaThreshold","uAlphaFunc","uClipPlanes","uClipEnabled","uClipActive","uDepthClamp",
			"uTextureEnabled","uTextureMode","uBurn","uLineSmooth","uLineHalfWidth","uGLESPrecomputedSphere","uGLESDepthSplit"};
		bool skip=false;for(size_t n=0;n<sizeof managed/sizeof *managed;++n)if(stem==managed[n])skip=true;
		if(skip)continue;
		if(maxLightVec4&&stem=="lightrange")
		{
			if(type!=GL_INT_VEC3||size!=1)I_FatalError("Native GLES bounded light range must be one ivec3");
			variant.lightRange=int(variant.uniforms.size());
		}
		MaterialUniform u;u.integer=false;u.matrix=0;
		switch(type)
		{
		case GL_FLOAT:u.width=1;break;case GL_FLOAT_VEC2:u.width=2;break;case GL_FLOAT_VEC3:u.width=3;break;case GL_FLOAT_VEC4:u.width=4;break;
		case GL_FLOAT_MAT2:u.width=4;u.matrix=2;break;case GL_FLOAT_MAT3:u.width=9;u.matrix=3;break;case GL_FLOAT_MAT4:u.width=16;u.matrix=4;break;
		case GL_INT:case GL_BOOL:case GL_SAMPLER_2D:case GL_SAMPLER_CUBE:u.width=1;u.integer=true;break;
		case GL_INT_VEC2:case GL_BOOL_VEC2:u.width=2;u.integer=true;break;
		case GL_INT_VEC3:case GL_BOOL_VEC3:u.width=3;u.integer=true;break;
		case GL_INT_VEC4:case GL_BOOL_VEC4:u.width=4;u.integer=true;break;
		default:I_FatalError("Native GLES unsupported material uniform type 0x%x",type);return;
		}
		if(u.integer)u.integers.resize(size_t(size)*u.width);else u.floats.resize(size_t(size)*u.width);
		u.version.assign(size,1);u.resident[0].assign(size,1);u.resident[1].assign(size,0);
		for(GLint element=0;element<size;++element)
		{
			std::string indexed=uniform;
			if(array!=std::string::npos)indexed.replace(array,3,"["+std::to_string(element)+"]");
			else if(size>1)indexed+="["+std::to_string(element)+"]";
			GLint location=glGetUniformLocation(base,indexed.c_str());
			u.locations[0].push_back(location);u.locations[1].push_back(glGetUniformLocation(fast,indexed.c_str()));
			if(location>=0)
			{
				if(u.integer)glGetUniformiv(base,location,&u.integers[size_t(element)*u.width]);
				else glGetUniformfv(base,location,&u.floats[size_t(element)*u.width]);
				UniformElement slot={variant.uniforms.size(),size_t(element)};variant.elements[location]=slot;
			}
		}
		variant.uniforms.push_back(u);
	}
	if(maxLightVec4&&variant.lightRange<0)I_FatalError("Native GLES bounded shader has no canonical light range");
	s.uniforms.erase(base);s.variants[base]=variant;
}
void Initialize()
{
	if(s.initialized) Shutdown();
	s=State();
	const char *extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
	s.uintIndices=extensions && std::strstr(extensions,"GL_OES_element_index_uint");
	const char *depthExtension=extensions?std::strstr(extensions,"GL_EXT_depth_clamp"):0;
	s.hardwareDepthClamp=depthExtension&&(depthExtension==extensions||depthExtension[-1]==' ')&&
		(depthExtension[18]==0||depthExtension[18]==' ');
	if(s.hardwareDepthClamp)glDisable(GL_DEPTH_CLAMP);
	s.basicProgram=BasicProgram(); s.program=s.basicProgram;s.actualProgram=s.basicProgram;
	if(zandronum_gles_program_label)zandronum_gles_program_label(s.basicProgram,"basic2D","");
	s.basicSolidProgram=BasicProgram(true);
	if(zandronum_gles_program_label)zandronum_gles_program_label(s.basicSolidProgram,"basic2D-solid","");
	glUseProgram(s.basicProgram);
	glGenBuffers(1,&s.streamBuffer);
	s.batch.reserve(16384); s.primitiveVertices.reserve(1024);
	s.initialized=true;
}
void Shutdown()
{
	Flush();
	if(s.streamBuffer)glDeleteBuffers(1,&s.streamBuffer);
	if(s.basicProgram)glDeleteProgram(s.basicProgram);
	if(s.basicSolidProgram)glDeleteProgram(s.basicSolidProgram);
	if(s.hardwareDepthClamp)glDisable(GL_DEPTH_CLAMP);
	s=State();
}
void ApplyShaderState(GLuint program)
{
	Program &p=Locations(program);
	if(p.revision==s.revision) return;
	glUniformMatrix4fv(p.projection,1,GL_FALSE,s.projection.v);
	glUniformMatrix4fv(p.view,1,GL_FALSE,s.view.v);
	glUniformMatrix4fv(p.model,1,GL_FALSE,s.texture[7].v);
	glUniformMatrix4fv(p.texture,1,GL_FALSE,s.texture[0].v);
	Matrix inverse; Inverse(s.view,inverse); GLfloat normal[9];
	for(int col=0;col<3;++col)for(int row=0;row<3;++row)normal[col*3+row]=inverse.v[row*4+col];
	glUniformMatrix3fv(p.normal,1,GL_FALSE,normal);
	glUniform1i(p.alphaTest,s.alphaTest);glUniform1f(p.alphaThreshold,s.alphaThreshold);glUniform1i(p.alphaFunc,s.alphaFunc);
	glUniform4fv(p.clip,6,s.clipPlanes);glUniform1iv(p.clipEnabled,6,s.clipEnabled);
	GLint clipActive=0;for(unsigned i=0;i<6;++i)clipActive|=s.clipEnabled[i];glUniform1i(p.clipActive,clipActive!=0);
	glUniform1i(p.depthClamp,s.depthClamp&&!s.hardwareDepthClamp);glUniform1i(p.textureEnabled,s.textureEnabled[0]);
	glUniform1i(p.textureMode,s.textureMode);glUniform1i(p.burn,s.textureEnabled[1]);
	glUniform1i(p.lineSmooth,s.batchSmooth);glUniform1f(p.lineHalfWidth,s.batchLineHalfWidth);
	p.revision=s.revision;
}
void Flush()
{
	if(s.batch.empty()||s.flushActive)return;
	s.flushActive=true;
	bool split=SplitDepthBatch();
	if(s.batch.empty()){s.flushActive=false;return;}
	PrepareDrawProgram();ApplyShaderState(s.actualProgram);
	if(split){glUniform1i(Locations(s.actualProgram).depthSplit,1);glUniform1i(Locations(s.actualProgram).precomputedSphere,1);}
	size_t bytes=s.batch.size()*sizeof(Vertex);
	glBindBuffer(GL_ARRAY_BUFFER,s.streamBuffer);
	// Replace storage for each batch. Updating disjoint ranges of a live VBO
	// still serializes every draw on the R36S Mali driver; a fresh store lets
	// queued draws retain their data without waiting for the GPU here.
	glBufferData(GL_ARRAY_BUFFER,bytes,&s.batch[0],GL_STREAM_DRAW);
	for(unsigned i=0;i<8;++i)
	{
		ApplyArrayEnabled(i,true);
		ApplyArrayPointer(i,s.streamBuffer,components[i],GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsets[i]));
	}
	glDrawArrays(s.batchMode,0,GLsizei(s.batch.size()));
	if(split){glUniform1i(Locations(s.actualProgram).depthSplit,0);glUniform1i(Locations(s.actualProgram).precomputedSphere,0);}
	s.batch.clear();
	if (s.batchSmooth) {s.batchSmooth=false;++s.revision;}
	glBindBuffer(GL_ARRAY_BUFFER,s.arrayBuffer);
	s.flushActive=false;
}
void SetTextureMode(int mode) {if(s.textureMode!=mode){Change();s.textureMode=mode;}}
void Begin(GLenum mode) { if(s.inBegin)I_FatalError("Native GLES nested glBegin");s.primitive=mode;s.inBegin=true;s.primitiveVertices.clear(); }
void End() {if(!s.inBegin)I_FatalError("Native GLES glEnd without glBegin");s.inBegin=false;AppendPrimitive(s.primitive,s.primitiveVertices);}
void Vertex3f(GLfloat x,GLfloat y,GLfloat z) {s.current.position[0]=x;s.current.position[1]=y;s.current.position[2]=z;if(s.inBegin)s.primitiveVertices.push_back(s.current);}
void Vertex3fv(const GLfloat *v){Vertex3f(v[0],v[1],v[2]);}
void Vertex2f(GLfloat x,GLfloat y){Vertex3f(x,y,0);}
void Vertex2d(GLdouble x,GLdouble y){Vertex3f(GLfloat(x),GLfloat(y),0);}
void Vertex2i(GLint x,GLint y){Vertex3f(GLfloat(x),GLfloat(y),0);}
void Color4f(GLfloat r,GLfloat g,GLfloat b,GLfloat a){s.current.color[0]=r;s.current.color[1]=g;s.current.color[2]=b;s.current.color[3]=a;}
void Color4fv(const GLfloat *v){Color4f(v[0],v[1],v[2],v[3]);}
void Color3f(GLfloat r,GLfloat g,GLfloat b){Color4f(r,g,b,1);}
void Color3ub(GLubyte r,GLubyte g,GLubyte b){Color4f(r/255.f,g/255.f,b/255.f,1);}
void Color4ub(GLubyte r,GLubyte g,GLubyte b,GLubyte a){Color4f(r/255.f,g/255.f,b/255.f,a/255.f);}
void TexCoord2f(GLfloat u,GLfloat v){s.current.uv[0]=u;s.current.uv[1]=v;}
void TexCoord2fv(const GLfloat *v){TexCoord2f(v[0],v[1]);}
void MultiTexCoord2f(GLenum unit,GLfloat u,GLfloat v){if(unit==GL_TEXTURE0)TexCoord2f(u,v);else if(unit==GL_TEXTURE1){s.current.uv1[0]=u;s.current.uv1[1]=v;}}
void MultiTexCoord2fv(GLenum unit,const GLfloat *v){MultiTexCoord2f(unit,v[0],v[1]);}
void Normal3fv(const GLfloat *v){std::memcpy(s.current.normal,v,3*sizeof(GLfloat));}
void VertexAttrib4f(GLuint i,GLfloat x,GLfloat y,GLfloat z,GLfloat w){if(i>=8)I_FatalError("Native GLES attribute %u exceeds scene contract",i);GLfloat v[4]={x,y,z,w};std::memcpy(Attribute(s.current,i),v,components[i]*sizeof(GLfloat));}
void VertexAttrib3f(GLuint i,GLfloat x,GLfloat y,GLfloat z){VertexAttrib4f(i,x,y,z,1);}
void VertexAttrib2f(GLuint i,GLfloat x,GLfloat y){VertexAttrib4f(i,x,y,0,1);}
void VertexAttrib1f(GLuint i,GLfloat x){VertexAttrib4f(i,x,0,0,1);}
void VertexAttrib2fv(GLuint i,const GLfloat *v){VertexAttrib2f(i,v[0],v[1]);}
void VertexAttrib3fv(GLuint i,const GLfloat *v){VertexAttrib3f(i,v[0],v[1],v[2]);}
void VertexAttrib4fv(GLuint i,const GLfloat *v){VertexAttrib4f(i,v[0],v[1],v[2],v[3]);}
void VertexAttrib4ubv(GLuint i,const GLubyte *v){VertexAttrib4f(i,v[0]/255.f,v[1]/255.f,v[2]/255.f,v[3]/255.f);}
void MatrixMode(GLenum mode){s.matrixMode=mode;}
void LoadIdentity(){SetMatrix(Matrix());}
void LoadMatrixf(const GLfloat *value){Matrix m;std::memcpy(m.v,value,sizeof m.v);SetMatrix(m);}
void MultMatrixf(const GLfloat *value){Matrix m;std::memcpy(m.v,value,sizeof m.v);SetMatrix(Multiply(CurrentMatrix(),m));}
void PushMatrix(){CurrentStack().push_back(CurrentMatrix());}
void PopMatrix(){std::vector<Matrix>&stack=CurrentStack();if(stack.empty())I_FatalError("Native GLES matrix stack underflow");SetMatrix(stack.back());stack.pop_back();}
void Translatef(GLfloat x,GLfloat y,GLfloat z){Matrix m;m.v[12]=x;m.v[13]=y;m.v[14]=z;SetMatrix(Multiply(CurrentMatrix(),m));}
void Scalef(GLfloat x,GLfloat y,GLfloat z){Matrix m;m.v[0]=x;m.v[5]=y;m.v[10]=z;SetMatrix(Multiply(CurrentMatrix(),m));}
void Rotatef(GLfloat angle,GLfloat x,GLfloat y,GLfloat z)
{
	GLfloat length=std::sqrt(x*x+y*y+z*z);if(!length)return;x/=length;y/=length;z/=length;
	GLfloat c=std::cos(angle*0.017453292519943295f),sn=std::sin(angle*0.017453292519943295f),t=1-c;Matrix m;
	m.v[0]=x*x*t+c;m.v[4]=x*y*t-z*sn;m.v[8]=x*z*t+y*sn;
	m.v[1]=y*x*t+z*sn;m.v[5]=y*y*t+c;m.v[9]=y*z*t-x*sn;
	m.v[2]=z*x*t-y*sn;m.v[6]=z*y*t+x*sn;m.v[10]=z*z*t+c;
	SetMatrix(Multiply(CurrentMatrix(),m));
}
void Ortho(GLdouble l,GLdouble r,GLdouble b,GLdouble t,GLdouble n,GLdouble f){Matrix m;m.v[0]=2/(r-l);m.v[5]=2/(t-b);m.v[10]=-2/(f-n);m.v[12]=-(r+l)/(r-l);m.v[13]=-(t+b)/(t-b);m.v[14]=-(f+n)/(f-n);SetMatrix(Multiply(CurrentMatrix(),m));}
void Frustum(GLdouble l,GLdouble r,GLdouble b,GLdouble t,GLdouble n,GLdouble f){Matrix m;m.v[0]=2*n/(r-l);m.v[5]=2*n/(t-b);m.v[8]=(r+l)/(r-l);m.v[9]=(t+b)/(t-b);m.v[10]=-(f+n)/(f-n);m.v[11]=-1;m.v[14]=-2*f*n/(f-n);m.v[15]=0;SetMatrix(Multiply(CurrentMatrix(),m));}
void Perspective(GLdouble fov,GLdouble aspect,GLdouble n,GLdouble f){GLdouble top=n*std::tan(fov*0.008726646259971648);Frustum(-top*aspect,top*aspect,-top,top,n,f);}
void GetFloatv(GLenum name,GLfloat *value){if(name==GL_MODELVIEW_MATRIX)std::memcpy(value,s.view.v,sizeof s.view.v);else if(name==GL_PROJECTION_MATRIX)std::memcpy(value,s.projection.v,sizeof s.projection.v);else if(name==GL_TEXTURE_MATRIX)std::memcpy(value,s.texture[s.activeTexture].v,sizeof s.texture[0].v);else if(name==GL_CURRENT_COLOR)std::memcpy(value,s.current.color,sizeof s.current.color);else glGetFloatv(name,value);}
void GetIntegerv(GLenum name,GLint *value){if(name==GL_MATRIX_MODE)*value=s.matrixMode;else if(name==GL_ACTIVE_TEXTURE)*value=GL_TEXTURE0+s.activeTexture;else if(name==GL_CLIENT_ACTIVE_TEXTURE)*value=GL_TEXTURE0+s.clientTexture;else if(name==GL_DRAW_BUFFER||name==GL_READ_BUFFER)*value=GL_BACK;else if(name==GL_ARRAY_BUFFER_BINDING)*value=s.arrayBuffer;else if(name==GL_ELEMENT_ARRAY_BUFFER_BINDING)*value=s.elementBuffer;else if(name==GL_CURRENT_PROGRAM)*value=s.program==s.basicProgram?0:s.program;else glGetIntegerv(name,value);}
void GetBooleanv(GLenum name,GLboolean *value){if(name==GL_STEREO||name==GL_DOUBLEBUFFER)*value=name==GL_DOUBLEBUFFER?GL_TRUE:GL_FALSE;else glGetBooleanv(name,value);}
void Enable(GLenum cap)
{
	if(cap==GL_TEXTURE_2D){if(!s.textureEnabled[s.activeTexture]){Change();s.textureEnabled[s.activeTexture]=true;}return;}
	if(cap==GL_ALPHA_TEST){if(!s.alphaTest){Change();s.alphaTest=true;}return;}
	if(cap>=GL_CLIP_PLANE0&&cap<GL_CLIP_PLANE0+6){int i=cap-GL_CLIP_PLANE0;if(!s.clipEnabled[i]){Change();s.clipEnabled[i]=1;}return;}
	if(cap==GL_DEPTH_CLAMP||cap==GL_DEPTH_CLAMP_NV){if(!s.depthClamp){Change();s.depthClamp=true;if(s.hardwareDepthClamp)glEnable(GL_DEPTH_CLAMP);}return;}
	if(cap==GL_LINE_SMOOTH){if(!s.enabled[cap]){Flush();s.enabled[cap]=true;}return;}
	if(cap==GL_FOG||cap==GL_TEXTURE_GEN_S||cap==GL_TEXTURE_GEN_T||cap==GL_NORMALIZE||cap==GL_COLOR_MATERIAL||cap==GL_LIGHTING||cap==GL_MULTISAMPLE||cap==GL_POLYGON_SMOOTH||cap==GL_POLYGON_OFFSET_LINE||cap==GL_LINE_SMOOTH){s.enabled[cap]=true;return;}
	if(s.enabled.count(cap)&&s.enabled[cap])return;Flush();s.enabled[cap]=true;glEnable(cap);
}
void Disable(GLenum cap)
{
	if(cap==GL_TEXTURE_2D){if(s.textureEnabled[s.activeTexture]){Change();s.textureEnabled[s.activeTexture]=false;}return;}
	if(cap==GL_ALPHA_TEST){if(s.alphaTest){Change();s.alphaTest=false;}return;}
	if(cap>=GL_CLIP_PLANE0&&cap<GL_CLIP_PLANE0+6){int i=cap-GL_CLIP_PLANE0;if(s.clipEnabled[i]){Change();s.clipEnabled[i]=0;}return;}
	if(cap==GL_DEPTH_CLAMP||cap==GL_DEPTH_CLAMP_NV){if(s.depthClamp){Change();s.depthClamp=false;if(s.hardwareDepthClamp)glDisable(GL_DEPTH_CLAMP);}return;}
	if(cap==GL_LINE_SMOOTH){if(s.enabled[cap]){Flush();s.enabled[cap]=false;}return;}
	if(cap==GL_FOG||cap==GL_TEXTURE_GEN_S||cap==GL_TEXTURE_GEN_T||cap==GL_NORMALIZE||cap==GL_COLOR_MATERIAL||cap==GL_LIGHTING||cap==GL_MULTISAMPLE||cap==GL_POLYGON_SMOOTH||cap==GL_POLYGON_OFFSET_LINE||cap==GL_LINE_SMOOTH){s.enabled[cap]=false;return;}
	if(s.enabled.count(cap)&&!s.enabled[cap])return;Flush();s.enabled[cap]=false;glDisable(cap);
}
GLboolean IsEnabled(GLenum cap){if(cap==GL_TEXTURE_2D)return s.textureEnabled[s.activeTexture];if(cap==GL_ALPHA_TEST)return s.alphaTest;if(cap>=GL_CLIP_PLANE0&&cap<GL_CLIP_PLANE0+6)return s.clipEnabled[cap-GL_CLIP_PLANE0];if(cap==GL_DEPTH_CLAMP||cap==GL_DEPTH_CLAMP_NV)return s.depthClamp;if(cap==GL_FOG||cap==GL_TEXTURE_GEN_S||cap==GL_TEXTURE_GEN_T||cap==GL_MULTISAMPLE||cap==GL_POLYGON_SMOOTH||cap==GL_POLYGON_OFFSET_LINE||cap==GL_LINE_SMOOTH||cap==GL_NORMALIZE||cap==GL_COLOR_MATERIAL||cap==GL_LIGHTING)return s.enabled[cap];return glIsEnabled(cap);}
void AlphaFunc(GLenum function,GLclampf reference){reference=std::max(0.f,std::min(1.f,reference));if(function!=s.alphaFunc||reference!=s.alphaThreshold){Change();s.alphaFunc=function;s.alphaThreshold=reference;}}
void ClipPlane(GLenum plane,const GLdouble *equation){int p=plane-GL_CLIP_PLANE0;if(p<0||p>=6)I_FatalError("Native GLES clip plane exceeds six planes");Change();Matrix inverse;if(!Inverse(s.view,inverse))I_FatalError("Native GLES singular clip-plane transform");for(int r=0;r<4;++r){s.clipPlanes[p*4+r]=0;for(int k=0;k<4;++k)s.clipPlanes[p*4+r]+=inverse.v[r*4+k]*equation[k];}}
void Fogf(GLenum,GLfloat){} void Fogi(GLenum,GLint){} void Fogfv(GLenum,const GLfloat*){}
void TexEnvi(GLenum,GLenum,GLint){} void TexGeni(GLenum,GLenum,GLint){} void ShadeModel(GLenum){}
void Hint(GLenum target,GLenum mode){if(target==GL_GENERATE_MIPMAP_HINT)glHint(target,mode);}
void PolygonMode(GLenum,GLenum mode){if(mode!=GL_FILL)I_FatalError("Native GLES does not expose desktop polygon mode");}
void DepthRange(GLclampd n,GLclampd f){Flush();glDepthRangef(GLclampf(n),GLclampf(f));}
void ClearDepth(GLclampd value){Flush();glClearDepthf(GLclampf(value));}
void DrawBuffer(GLenum){} void ReadBuffer(GLenum){}
void ActiveTexture(GLenum unit){unsigned index=unit-GL_TEXTURE0;if(index>=8)I_FatalError("Native GLES texture unit exceeds scene contract");s.activeTexture=index;glActiveTexture(unit);}
void ClientActiveTexture(GLenum unit){s.clientTexture=unit-GL_TEXTURE0;}
void UseProgram(GLuint program){if(!program)program=s.basicProgram;if(s.program==program)return;Flush();s.program=program;BindActual(program);}
void DeleteProgram(GLuint program){Flush();UnregisterVariants(program);s.programs.erase(program);s.uniforms.erase(program);glDeleteProgram(program);}
void LinkProgram(GLuint program){Flush();UnregisterVariants(program);s.programs.erase(program);s.uniforms.erase(program);glLinkProgram(program);}
void GetObjectParameteriv(GLuint object,GLenum name,GLint *value){if(glIsProgram(object))glGetProgramiv(object,name,value);else glGetShaderiv(object,name,value);}
void BindBuffer(GLenum target,GLuint buffer){if(target==GL_ELEMENT_ARRAY_BUFFER)s.elementBuffer=buffer;else s.arrayBuffer=buffer;glBindBuffer(target,buffer);}
void BufferData(GLenum target,GLsizeiptr size,const void *data,GLenum usage){Flush();Buffer &b=s.buffers[BoundBuffer(target)];b.bytes.resize(size);if(data&&size)std::memcpy(&b.bytes[0],data,size);glBufferData(target,size,data,usage);}
void BufferSubData(GLenum target,GLintptr offset,GLsizeiptr size,const void *data){Flush();Buffer &b=s.buffers[BoundBuffer(target)];if(offset<0||size<0||size_t(offset+size)>b.bytes.size())I_FatalError("Native GLES buffer upload out of range");if(size)std::memcpy(&b.bytes[offset],data,size);glBufferSubData(target,offset,size,data);}
// Deleted names may be reused for unrelated storage; recapture associations.
void DeleteBuffers(GLsizei count,const GLuint *buffers){Flush();for(int i=0;i<count;++i)s.buffers.erase(buffers[i]);glDeleteBuffers(count,buffers);s.appliedPointers=0;}
void *MapBufferRange(GLenum target,GLintptr offset,GLsizeiptr length,GLbitfield){Flush();Buffer &b=s.buffers[BoundBuffer(target)];if(b.mapped||offset<0||length<0||size_t(offset+length)>b.bytes.size())I_FatalError("Native GLES buffer mapping out of range");b.mapOffset=offset;b.mapLength=length;b.mapped=true;return length?&b.bytes[offset]:0;}
void *MapBuffer(GLenum target,GLenum){return MapBufferRange(target,0,s.buffers[BoundBuffer(target)].bytes.size(),0);}
GLboolean UnmapBuffer(GLenum target){Buffer &b=s.buffers[BoundBuffer(target)];if(!b.mapped)return GL_FALSE;if(b.mapLength)glBufferSubData(target,b.mapOffset,b.mapLength,&b.bytes[b.mapOffset]);b.mapped=false;return GL_TRUE;}
void FlushMappedBufferRange(GLenum target,GLintptr offset,GLsizeiptr length){Buffer &b=s.buffers[BoundBuffer(target)];if(!b.mapped||offset<0||length<0||offset+length>b.mapLength)I_FatalError("Native GLES mapped upload out of range");if(length)glBufferSubData(target,b.mapOffset+offset,length,&b.bytes[b.mapOffset+offset]);}
void VertexAttribPointer(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer){if(index>=8)I_FatalError("Native GLES attribute array exceeds scene contract");Array &a=s.arrays[index];a.size=size;a.type=type;a.normalized=normalized;a.stride=stride;a.pointer=pointer;a.buffer=s.arrayBuffer;}
void EnableVertexAttribArray(GLuint index){if(index<8)s.arrays[index].enabled=GL_TRUE;}
void DisableVertexAttribArray(GLuint index){if(index<8)s.arrays[index].enabled=GL_FALSE;}
void VertexPointer(GLint size,GLenum type,GLsizei stride,const void *pointer){VertexAttribPointer(0,size,type,GL_FALSE,stride,pointer);}
void TexCoordPointer(GLint size,GLenum type,GLsizei stride,const void *pointer){VertexAttribPointer(s.clientTexture?7:1,size,type,GL_FALSE,stride,pointer);}
void EnableClientState(GLenum array){if(array==GL_VERTEX_ARRAY)EnableVertexAttribArray(0);else if(array==GL_TEXTURE_COORD_ARRAY)EnableVertexAttribArray(s.clientTexture?7:1);}
void DisableClientState(GLenum array){if(array==GL_VERTEX_ARRAY)DisableVertexAttribArray(0);else if(array==GL_TEXTURE_COORD_ARRAY)DisableVertexAttribArray(s.clientTexture?7:1);}
void DrawArrays(GLenum mode,GLint first,GLsizei count)
{
	Flush();if(!count)return;
	bool convert=mode==GL_QUADS||mode==GL_QUAD_STRIP||mode==GL_POLYGON;
	if(convert||(s.depthClamp&&!s.hardwareDepthClamp&&mode!=GL_POINTS))
	{
		std::vector<Vertex> vertices;vertices.reserve(count);
		for(int i=0;i<count;++i)vertices.push_back(FetchVertex(first+i));
		if(convert||NeedsDepthSplit(vertices,DepthTransform())){AppendPrimitive(mode,vertices);return;}
	}
	PrepareDrawProgram();ApplyShaderState(s.actualProgram);SetupArrays();glDrawArrays(mode,first,count);
}
void DrawElements(GLenum mode,GLsizei count,GLenum type,const void *indices)
{
	Flush();if(!count)return;
	bool convert=mode==GL_QUADS||mode==GL_QUAD_STRIP||mode==GL_POLYGON||(type==GL_UNSIGNED_INT&&!s.uintIndices);
	if(!convert&&(!s.depthClamp||s.hardwareDepthClamp||mode==GL_POINTS)){PrepareDrawProgram();ApplyShaderState(s.actualProgram);SetupArrays();glDrawElements(mode,count,type,indices);return;}
	size_t unit=TypeSize(type);const unsigned char *p=static_cast<const unsigned char*>(indices);
	if(s.elementBuffer){Buffer &b=s.buffers[s.elementBuffer];size_t offset=reinterpret_cast<size_t>(indices);if(offset+count*unit>b.bytes.size())I_FatalError("Native GLES index array exceeds buffer");p=&b.bytes[offset];}
	std::vector<Vertex> vertices;vertices.reserve(count);for(int i=0;i<count;++i)vertices.push_back(FetchVertex(GLuint(Component(p+i*unit,type,false))));
	if(convert||NeedsDepthSplit(vertices,DepthTransform())){AppendPrimitive(mode,vertices);return;}
	PrepareDrawProgram();ApplyShaderState(s.actualProgram);SetupArrays();glDrawElements(mode,count,type,indices);
}
void DrawRangeElements(GLenum mode,GLuint,GLuint,GLsizei count,GLenum type,const void *indices){DrawElements(mode,count,type,indices);}
void BindTexture(GLenum target,GLuint texture){if(s.textures[s.activeTexture]==texture)return;Flush();s.textures[s.activeTexture]=texture;glBindTexture(target,texture);}
void DeleteTextures(GLsizei count,const GLuint *textures){Flush();for(int i=0;i<count;++i)for(int j=0;j<8;++j)if(s.textures[j]==textures[i])s.textures[j]=0;glDeleteTextures(count,textures);}
void TexParameteri(GLenum target,GLenum name,GLint value){Flush();if(name==GL_GENERATE_MIPMAP)return;if((name==GL_TEXTURE_WRAP_S||name==GL_TEXTURE_WRAP_T)&&value==GL_CLAMP)value=GL_CLAMP_TO_EDGE;glTexParameteri(target,name,value);}
void TexParameterf(GLenum target,GLenum name,GLfloat value){Flush();glTexParameterf(target,name,value);}
void TexImage2D(GLenum target,GLint level,GLint,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const void *pixels){Flush();glTexImage2D(target,level,format,width,height,border,format,type,pixels);}
void TexSubImage2D(GLenum target,GLint level,GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,const void *pixels){Flush();glTexSubImage2D(target,level,x,y,width,height,format,type,pixels);}
void CopyTexImage2D(GLenum target,GLint level,GLenum format,GLint x,GLint y,GLsizei width,GLsizei height,GLint border){Flush();if(format==GL_RGBA8||format==GL_RGB8)format=format==GL_RGBA8?GL_RGBA:GL_RGB;glCopyTexImage2D(target,level,format,x,y,width,height,border);}
void CopyTexSubImage2D(GLenum target,GLint level,GLint xo,GLint yo,GLint x,GLint y,GLsizei width,GLsizei height){Flush();glCopyTexSubImage2D(target,level,xo,yo,x,y,width,height);}
void BindFramebuffer(GLenum target,GLuint buffer){Flush();glBindFramebuffer(target,buffer);}
void GenQueries(GLsizei count,GLuint *queries){static GLuint next=1;for(int i=0;i<count;++i)queries[i]=next++;}
void DeleteQueries(GLsizei,const GLuint*){} void BeginQuery(GLenum,GLuint){} void EndQuery(GLenum){}
void GetQueryObjectuiv(GLuint,GLenum,GLuint *value){*value=1;}
void TexBuffer(GLenum,GLenum,GLuint){I_FatalError("Native GLES2 texture buffers are unsupported");}

void BlendFunc(GLenum source,GLenum destination){GLenum v[]={source,destination};if(PipelineChanged(0,v,sizeof v))glBlendFunc(source,destination);}
void BlendEquation(GLenum mode){if(PipelineChanged(1,&mode,sizeof mode))glBlendEquation(mode);}
void DepthFunc(GLenum function){if(PipelineChanged(2,&function,sizeof function))glDepthFunc(function);}
void DepthMask(GLboolean value){if(PipelineChanged(3,&value,sizeof value))glDepthMask(value);}
void StencilMask(GLuint mask){if(PipelineChanged(4,&mask,sizeof mask))glStencilMask(mask);}
void Clear(GLbitfield mask){Flush();glClear(mask);}
void CullFace(GLenum face){if(PipelineChanged(5,&face,sizeof face))glCullFace(face);}
void FrontFace(GLenum face){if(PipelineChanged(6,&face,sizeof face))glFrontFace(face);}
void LineWidth(GLfloat width){if(PipelineChanged(7,&width,sizeof width)){glLineWidth(width);if(width>0)s.lineWidth=width;}}
void PolygonOffset(GLfloat factor,GLfloat units){GLfloat v[]={factor,units};if(PipelineChanged(8,v,sizeof v))glPolygonOffset(factor,units);}
void ColorMask(GLboolean r,GLboolean g,GLboolean b,GLboolean a){GLboolean v[]={r,g,b,a};if(PipelineChanged(9,v,sizeof v))glColorMask(r,g,b,a);}
void StencilFunc(GLenum function,GLint reference,GLuint mask){GLuint v[]={function,GLuint(reference),mask};if(PipelineChanged(10,v,sizeof v))glStencilFunc(function,reference,mask);}
void StencilOp(GLenum fail,GLenum depthFail,GLenum pass){GLenum v[]={fail,depthFail,pass};if(PipelineChanged(11,v,sizeof v))glStencilOp(fail,depthFail,pass);}
void ClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat a){Flush();glClearColor(r,g,b,a);}
void Viewport(GLint x,GLint y,GLsizei width,GLsizei height){Flush();glViewport(x,y,width,height);}
void Scissor(GLint x,GLint y,GLsizei width,GLsizei height){Flush();glScissor(x,y,width,height);}
void Finish(){Flush();glFinish();}
void ReadPixels(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,void *data)
{
	Flush();
	if(width<=0||height<=0)return;
	if((format==GL_RGB||format==GL_BGR||format==GL_BGRA)&&type==GL_UNSIGNED_BYTE)
	{
		GLint alignment=4;glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
		glPixelStorei(GL_PACK_ALIGNMENT,1);
		std::vector<GLubyte> rgba(size_t(width)*height*4);glReadPixels(x,y,width,height,GL_RGBA,GL_UNSIGNED_BYTE,&rgba[0]);
		glPixelStorei(GL_PACK_ALIGNMENT,alignment);size_t channels=format==GL_BGRA?4:3;
		size_t stride=(size_t(width)*channels+alignment-1)/alignment*alignment;GLubyte *out=static_cast<GLubyte*>(data);
		for(int row=0;row<height;++row)for(int col=0;col<width;++col){const GLubyte *p=&rgba[(size_t(row)*width+col)*4];GLubyte *q=out+row*stride+col*channels;q[0]=p[format==GL_RGB?0:2];q[1]=p[1];q[2]=p[format==GL_RGB?2:0];if(channels==4)q[3]=p[3];}
	}
	else glReadPixels(x,y,width,height,format,type,data);
}
#define UNIFORM_SCALAR1(suffix,type) void Uniform1##suffix(GLint l,type a){if(!RouteUniform(l,#suffix[0]=='i',1,0,1,&a)&&UniformChanged(l,&a,sizeof a))glUniform1##suffix(l,a);}
#define UNIFORM_SCALAR2(suffix,type) void Uniform2##suffix(GLint l,type a,type b){type v[]={a,b};if(!RouteUniform(l,#suffix[0]=='i',2,0,1,v)&&UniformChanged(l,v,sizeof v))glUniform2##suffix(l,a,b);}
#define UNIFORM_SCALAR3(suffix,type) void Uniform3##suffix(GLint l,type a,type b,type c){type v[]={a,b,c};if(!RouteUniform(l,#suffix[0]=='i',3,0,1,v)&&UniformChanged(l,v,sizeof v))glUniform3##suffix(l,a,b,c);}
#define UNIFORM_SCALAR4(suffix,type) void Uniform4##suffix(GLint l,type a,type b,type c,type d){type v[]={a,b,c,d};if(!RouteUniform(l,#suffix[0]=='i',4,0,1,v)&&UniformChanged(l,v,sizeof v))glUniform4##suffix(l,a,b,c,d);}
UNIFORM_SCALAR1(f,GLfloat) UNIFORM_SCALAR2(f,GLfloat) UNIFORM_SCALAR3(f,GLfloat) UNIFORM_SCALAR4(f,GLfloat)
UNIFORM_SCALAR1(i,GLint) UNIFORM_SCALAR2(i,GLint) UNIFORM_SCALAR3(i,GLint) UNIFORM_SCALAR4(i,GLint)
#undef UNIFORM_SCALAR1
#undef UNIFORM_SCALAR2
#undef UNIFORM_SCALAR3
#undef UNIFORM_SCALAR4
#define UNIFORM_VECTOR(n,suffix,type) void Uniform##n##suffix(GLint l,GLsizei count,const type *v){if(!RouteUniform(l,#suffix[0]=='i',n,0,count,v)&&UniformChanged(l,v,size_t(count)*n*sizeof(type)))glUniform##n##suffix(l,count,v);}
UNIFORM_VECTOR(1,fv,GLfloat) UNIFORM_VECTOR(2,fv,GLfloat) UNIFORM_VECTOR(3,fv,GLfloat) UNIFORM_VECTOR(4,fv,GLfloat)
UNIFORM_VECTOR(1,iv,GLint) UNIFORM_VECTOR(2,iv,GLint) UNIFORM_VECTOR(3,iv,GLint) UNIFORM_VECTOR(4,iv,GLint)
#undef UNIFORM_VECTOR
#define UNIFORM_MATRIX(n) void UniformMatrix##n##fv(GLint l,GLsizei count,GLboolean transpose,const GLfloat *v){if(transpose)I_FatalError("Native GLES matrix uniforms require column-major input");if(!RouteUniform(l,false,n*n,n,count,v)&&UniformChanged(l,v,size_t(count)*n*n*sizeof(GLfloat)))glUniformMatrix##n##fv(l,count,GL_FALSE,v);}
UNIFORM_MATRIX(2) UNIFORM_MATRIX(3) UNIFORM_MATRIX(4)
#undef UNIFORM_MATRIX
}
