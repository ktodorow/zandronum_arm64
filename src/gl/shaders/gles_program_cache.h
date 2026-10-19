// Optional native GLES program binaries. Independent implementation; cache
// entries are disposable and never replace source compilation on a miss.
#ifndef ZANDRONUM_GLES_PROGRAM_CACHE_H
#define ZANDRONUM_GLES_PROGRAM_CACHE_H

#include <GLES2/gl2.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

class GLESProgramCache
{
public:
	typedef void *(*GetProc)(const char *);
private:
	typedef void (GL_APIENTRYP GetBinaryProc)(GLuint,GLsizei,GLsizei *,GLenum *,void *);
	typedef void (GL_APIENTRYP SetBinaryProc)(GLuint,GLenum,const void *,GLsizei);
	typedef void (GL_APIENTRYP ParameterProc)(GLuint,GLenum,GLint);
	GetBinaryProc getBinary;
	SetBinaryProc setBinary;
	ParameterProc parameter;
	std::string directory;
	enum { MaxBinarySize = 32*1024*1024 };
	struct Header
	{
		char magic[8];
		uint32_t format, length;
		uint64_t checksum;
	};
	static uint64_t Hash(const void *data,size_t length,uint64_t state=UINT64_C(14695981039346656037))
	{
		const unsigned char *bytes=static_cast<const unsigned char *>(data);
		for(size_t i=0;i<length;++i) { state^=bytes[i];state*=UINT64_C(1099511628211); }
		return state;
	}
	static bool CreateDirectory(const std::string &path)
	{
		if(path.empty()) return false;
		for(size_t i=1;i<=path.size();++i) if(i==path.size() || path[i]=='/')
		{
			std::string part=path.substr(0,i);
			if(mkdir(part.c_str(),0700)!=0 && errno!=EEXIST) return false;
			struct stat info;
			if(stat(part.c_str(),&info)!=0 || !S_ISDIR(info.st_mode)) return false;
		}
		// Removable FAT/exFAT storage may expose fixed permissions and reject
		// chmod. Cache contains validated, nonsecret driver binaries; request
		// private permissions where supported, but let actual writes decide.
		chmod(path.c_str(),0700);
		return true;
	}
	std::string Path(const std::string &key) const { return directory+"/"+key+".bin"; }
public:
	GLESProgramCache():getBinary(NULL),setBinary(NULL),parameter(NULL) {}
	void Initialize(GetProc getProc,const char *path)
	{
		directory.clear(); getBinary=NULL;setBinary=NULL;parameter=NULL;
		if(path==NULL || *path=='\0' || getProc==NULL) return;
		// A proc address alone does not establish extension support.
		const char *version=reinterpret_cast<const char *>(glGetString(GL_VERSION));
		const char *extensions=reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
		bool core=version && std::strstr(version,"OpenGL ES 3");
		if(!core && !(extensions && std::strstr(extensions,"GL_OES_get_program_binary"))) return;
		getBinary=reinterpret_cast<GetBinaryProc>(getProc(core?"glGetProgramBinary":"glGetProgramBinaryOES"));
		setBinary=reinterpret_cast<SetBinaryProc>(getProc(core?"glProgramBinary":"glProgramBinaryOES"));
		if(getBinary==NULL || setBinary==NULL) return;
		GLint formats=0;glGetIntegerv(0x87FE /* GL_NUM_PROGRAM_BINARY_FORMATS */, &formats);
		if(formats<=0 || !CreateDirectory(path)) return;
		if(core) parameter=reinterpret_cast<ParameterProc>(getProc("glProgramParameteri"));
		directory=path;
	}
	bool Active() const { return !directory.empty(); }
	std::string Key(const std::string &vertex,const std::string &fragment) const
	{
		// Includes the explicit attribute ABI: binary-linked locations must
		// match both the shader loader and the native vertex submission path.
		const char *abi="Zandronum GLES cache v1;0position;1uv;2color;3normal;4glow;5fog;6light;7uv1-depthregion3;";
		uint64_t a=Hash(abi,std::strlen(abi)),b=Hash(abi,std::strlen(abi),UINT64_C(7809847782465536322));
		const GLenum names[]={GL_VENDOR,GL_RENDERER,GL_VERSION,GL_SHADING_LANGUAGE_VERSION};
		for(unsigned i=0;i<sizeof names/sizeof names[0];++i)
		{
			const char *value=reinterpret_cast<const char *>(glGetString(names[i]));
			if(value) { size_t length=std::strlen(value)+1;a=Hash(value,length,a);b=Hash(value,length,b); }
		}
		const std::string *sources[]={&vertex,&fragment};
		for(unsigned i=0;i<2;++i)
		{
			// Explicit lengths prevent ambiguous vertex/fragment boundaries.
			uint64_t length=sources[i]->size();a=Hash(&length,sizeof length,a);b=Hash(&length,sizeof length,b);
			a=Hash(sources[i]->data(),sources[i]->size(),a);b=Hash(sources[i]->data(),sources[i]->size(),b);
		}
		char key[33];std::snprintf(key,sizeof key,"%016llx%016llx",static_cast<unsigned long long>(a),static_cast<unsigned long long>(b));
		return key;
	}
	void Prepare(GLuint program) const
	{
		if(Active() && parameter) parameter(program,0x8257 /* GL_PROGRAM_BINARY_RETRIEVABLE_HINT */,GL_TRUE);
	}
	bool Load(GLuint program,const std::string &key) const
	{
		if(!Active()) return false;
		std::string path=Path(key); FILE *file=std::fopen(path.c_str(),"rb");if(!file) return false;
		Header header;struct stat info;
		bool valid=fstat(fileno(file),&info)==0 && S_ISREG(info.st_mode)
			&& std::fread(&header,sizeof header,1,file)==1
			&& std::memcmp(header.magic,"ZDGLES1",8)==0 && header.length>0 && header.length<=MaxBinarySize
			&& info.st_size==static_cast<off_t>(sizeof header+header.length);
		std::vector<unsigned char> binary;
		if(valid)
		{
			binary.resize(header.length);
			valid=std::fread(&binary[0],1,header.length,file)==header.length && Hash(&binary[0],binary.size())==header.checksum;
		}
		std::fclose(file);
		if(valid)
		{
			setBinary(program,header.format,&binary[0],static_cast<GLsizei>(binary.size()));
			GLint linked=GL_FALSE;glGetProgramiv(program,GL_LINK_STATUS,&linked);valid=linked==GL_TRUE;
			if(!valid) while(glGetError()!=GL_NO_ERROR) {} // rejected driver data must not poison source fallback
		}
		if(!valid) unlink(path.c_str());
		return valid;
	}
	bool Store(GLuint program,const std::string &key) const
	{
		if(!Active()) return false;
		GLint size=0;glGetProgramiv(program,0x8741 /* GL_PROGRAM_BINARY_LENGTH */, &size);
		if(size<=0 || size>MaxBinarySize) return false;
		std::vector<unsigned char> binary(size);GLsizei length=0;GLenum format=0;
		getBinary(program,size,&length,&format,&binary[0]);
		if(length<=0 || length>size) return false;
		Header header;std::memset(&header,0,sizeof header);std::memcpy(header.magic,"ZDGLES1",8);
		header.format=format;header.length=length;header.checksum=Hash(&binary[0],length);
		std::string pattern=Path(key)+".XXXXXX";std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back('\0');
		int fd=mkstemp(&temporary[0]);if(fd<0) return false;
		FILE *file=fdopen(fd,"wb");if(!file) {close(fd);unlink(&temporary[0]);return false;}
		bool valid=std::fwrite(&header,sizeof header,1,file)==1 && std::fwrite(&binary[0],1,length,file)==static_cast<size_t>(length);
		if(std::fclose(file)!=0) valid=false;
		if(valid) valid=std::rename(&temporary[0],Path(key).c_str())==0;
		if(!valid) unlink(&temporary[0]);
		return valid;
	}
};
#endif
