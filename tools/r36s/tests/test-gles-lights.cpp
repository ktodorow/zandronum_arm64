#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#include "tarray.h"

void *M_Malloc(size_t size) { void *p=std::malloc(size);if(!p&&size)throw std::bad_alloc();return p; }
void *M_Realloc(void *old,size_t size) { void *p=std::realloc(old,size);if(!p&&size)throw std::bad_alloc();return p; }
void M_Free(void *p) { std::free(p); }

// Only external actor/plane queries are fixtures. Collection, categorization,
// color math, record packing, capacity and multipass policy below are engine code.
struct VisibilityTarget
{
	bool visible; mutable int queries;
	bool IsVisibleToPlayer() const { ++queries;return visible; }
};
struct ADynamicLight
{
	int x=0,y=0,z=0,flags4=0;
	float radius=100;
	bool owned=false,subtractive=false;
	VisibilityTarget *target=nullptr;
	int red=255,green=128,blue=64;
	float GetRadius() const { return radius; }
	int GetRed() const { return red; }
	int GetGreen() const { return green; }
	int GetBlue() const { return blue; }
	bool IsSubtractive() const { return subtractive; }
};
struct Plane
{
	float distance=0;bool backside=false;
	float DistToPoint(float,float,float) const { return distance; }
	bool PointOnSide(float,float,float) const { return backside; }
};
struct Vector
{
	float x=0,y=0,z=0;
	void Set(float a,float b,float c) { x=a;y=b;z=c; }
	float Length() const { return std::sqrt(x*x+y*y+z*z); }
};
const int MF4_ADDITIVE=1,ZADF_FORCE_VIDEO_DEFAULTS=2;
int zadmflags=0;
bool gl_lights_additive=false,gl_lights_checkside=true;
float gl_lights_size=1,gl_lights_intensity=1;
#define FIXED2FLOAT(x) (float(x)/65536.0f)
#include "engine-light-code.h"

namespace
{
int checks=0,failures=0;
void Check(bool ok,const char *name)
{
	++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",name);}
}
void Fill(FDynLightData &data,int category,int count)
{
	for(int i=0;i<count;++i)
	{
		unsigned start=data.arrays[category].Reserve(8);
		for(int j=0;j<8;++j)data.arrays[category][start+j]=float(10000*category+100*i+j);
	}
}
void Packing(int mod,int sub,int add,int capacity)
{
	FDynLightData data;Fill(data,0,mod);Fill(data,1,sub);Fill(data,2,add);
	std::vector<float> reference;
	for(int category=0;category<3;++category)
		for(unsigned i=0;i<data.arrays[category].Size();++i)reference.push_back(data.arrays[category][i]);
	int ranges[3];data.Combine(ranges,capacity);
#ifdef USE_GLES2
	int vecCapacity=2*capacity;
#else
	int vecCapacity=capacity;
#endif
	int expected[3]={2*mod,2*(mod+sub),2*(mod+sub+add)};
	for(int i=0;i<3;++i)
	{
		if(expected[i]>vecCapacity)expected[i]=vecCapacity;
		Check(ranges[i]==expected[i],"engine category range uses correct vec4 capacity");
		Check((ranges[i]&1)==0,"capacity never splits a light record");
	}
	bool contents=true;
	for(int i=0;i<ranges[2]*4;++i)contents&=data.arrays[0][i]==reference[size_t(i)];
	Check(contents,"packed prefix preserves every position/color/category record");
#ifdef USE_GLES2
	Check(data.arrays[0].Size()==unsigned(ranges[2]*4),"native packed storage bounded to uploaded records");
#endif
	data.Clear();Check(data.arrays[0].Size()==0&&data.arrays[1].Size()==0&&data.arrays[2].Size()==0,"collector reuse clears all categories");
	Fill(data,0,1);Fill(data,1,1);Fill(data,2,1);data.Combine(ranges,capacity);
	Check(ranges[2]==(vecCapacity<6?vecCapacity:6),"next surface does not retain previous lights");
}
void TestPacking()
{
#ifdef USE_GLES2
	Packing(0,0,0,128);Packing(128,0,0,128);Packing(129,0,0,128);
	Packing(0,128,0,128);Packing(0,0,128,128);Packing(70,40,40,128);
	Packing(127,1,1,128);Packing(0,127,2,128);Packing(0,0,129,128);
	Packing(1,1,1,1);Packing(2,2,2,3);Packing(100,10,10,113);
	Packing(0,0,0,0);
#else
	// Original desktop Combine has null references on empty arrays; unchanged.
	Packing(1,1,1,128);Packing(70,40,40,128);Packing(1,70,1,64);
#endif
	RenderContext context={};context.maxuniforms=4096;
	Check(context.MaxLights()==128,"actual Mali query retains 128-light native capacity");
#ifdef USE_GLES2
	context.maxuniforms=4*258;
	Check(context.MaxLights()==113,"odd queried light count remains complete two-vec4 records");
	context.maxuniforms=128;Check(context.MaxLights()==1,"small uniform budget has bounded minimum");
#endif
}
void TestVisibility()
{
	Plane plane;ADynamicLight light;VisibilityTarget target={false,0};FDynLightData data;
	light.owned=true;light.target=&target;
	bool accepted=gl_GetLight(plane,&light,0,true,false,data);
#ifdef USE_GLES2
	Check(!accepted&&data.arrays[0].Size()==0&&target.queries==1,"hidden owned actor contributes no shader light");
#else
	Check(accepted&&target.queries==0,"desktop light visibility behavior unchanged");
#endif
	data.Clear();target.visible=true;
	Check(gl_GetLight(plane,&light,0,true,false,data)&&data.arrays[0].Size()==8,"visible owned actor retains light");
	data.Clear();target.visible=false;target.queries=0;light.owned=false;
	Check(gl_GetLight(plane,&light,0,true,false,data)&&target.queries==0,"unowned light does not inherit unrelated target visibility");
	data.Clear();light.owned=true;light.target=nullptr;
	Check(gl_GetLight(plane,&light,0,true,false,data),"owned light with no target keeps existing behavior");
	data.Clear();light.target=&target;light.flags4=MF4_ADDITIVE;
	accepted=gl_GetLight(plane,&light,0,true,false,data);
#ifdef USE_GLES2
	Check(!accepted&&data.arrays[2].Size()==0,"hidden owned additive light cannot leak position");
	light.subtractive=true;
	Check(!gl_GetLight(plane,&light,0,true,false,data)&&data.arrays[1].Size()==0,"hidden owned subtractive light cannot leak position");
#else
	Check(accepted&&data.arrays[2].Size()==8,"desktop additive visibility behavior unchanged");
#endif
}
void TestCollection()
{
	Plane plane;ADynamicLight light;FDynLightData data;
	light.x=65536;light.y=2*65536;light.z=3*65536;
	Check(gl_GetLight(plane,&light,0,true,false,data),"ordinary dynamic light collected");
	Check(data.arrays[0].Size()==8&&data.arrays[0][0]==1&&data.arrays[0][1]==3&&data.arrays[0][2]==2&&data.arrays[0][3]==100,"world axes and radius packed correctly");
	Check(data.arrays[0][4]==1&&std::fabs(data.arrays[0][5]-128.0f/255)<1e-6f,"ordinary light color preserved");
	data.Clear();light.flags4=MF4_ADDITIVE;
	Check(gl_GetLight(plane,&light,0,true,false,data)&&data.arrays[2].Size()==8&&std::fabs(data.arrays[2][4]-.2f)<1e-6f,"additive category and scaling preserved");
	data.Clear();light.flags4=0;light.subtractive=true;
	Check(gl_GetLight(plane,&light,0,true,false,data)&&data.arrays[1].Size()==8,"subtractive category preserved");
	data.Clear();light.subtractive=false;
	Check(gl_GetLight(plane,&light,32,true,false,data)&&data.arrays[0][4]==data.arrays[0][5]&&data.arrays[0][5]==data.arrays[0][6],"full desaturation preserves grayscale lighting");
	data.Clear();light.radius=0;
	Check(!gl_GetLight(plane,&light,0,true,false,data),"inactive zero-radius light rejected");
	light.radius=100;plane.distance=101;
	Check(!gl_GetLight(plane,&light,0,true,false,data),"out-of-radius light rejected");
	plane.distance=0;plane.backside=true;
	Check(!gl_GetLight(plane,&light,0,true,false,data),"back-side light rejected when enabled");
	Check(gl_GetLight(plane,&light,0,false,false,data),"caller can supply prechecked side orientation");
}
void TestMultipass()
{
#ifdef USE_GLES2
	Check(!gl_ForceMultipassLights(false,false),"ordinary multipass remains selected by touching-light list");
	Check(gl_ForceMultipassLights(true,false),"shader0 retains forced multipass");
	Check(!gl_ForceMultipassLights(false,true),"shader1 ordinary surfaces stay in rendered single-pass lists");
	Check(!gl_ForceMultipassLights(true,true),"shader1 ignores force flag instead of hiding world geometry");
#endif
}
}
int main()
{
	TestPacking();TestVisibility();TestCollection();TestMultipass();
#ifdef USE_GLES2
	const char *backend="native";
#else
	const char *backend="desktop";
#endif
	std::printf("Engine light %s: %d checks, %d failures\n",backend,checks,failures);
	return failures?1:0;
}
