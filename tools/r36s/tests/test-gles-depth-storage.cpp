#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <new>
#include "gles_compat.cpp"
static bool tracking=false;
static size_t allocations=0;
void *operator new(size_t n) {if(tracking)++allocations;void *p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void *p) noexcept {std::free(p);}
void *operator new[](size_t n) {return ::operator new(n);}
void operator delete[](void *p) noexcept {std::free(p);}
void I_FatalError(const char *format,...) {va_list a;va_start(a,format);std::vfprintf(stderr,format,a);va_end(a);std::abort();}

// Frozen pre-storage-change production routine: vertex/order oracle and
// allocation negative control. Geometry math remains shared and unchanged.
namespace gles { namespace {
bool ReferenceSplitDepthBatch()
{
	if(!s.depthClamp||s.hardwareDepthClamp||s.batchMode==GL_POINTS)return false;
	Matrix transform=DepthTransform();
	if(!NeedsDepthSplit(s.batch,transform))return false;
	bool sphere=Locations(s.program).precomputedSphere>=0;
	if(sphere)for(size_t i=0;i<s.batch.size();++i)SphereCoordinates(s.batch[i]);
	std::vector<Vertex> result;
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
			std::vector<std::vector<Vertex> > pieces(1);
			pieces[0].assign(s.batch.begin()+i,s.batch.begin()+i+3);
			for(unsigned plane=0;plane<2;++plane)
			{
				std::vector<std::vector<Vertex> > next;
				for(size_t j=0;j<pieces.size();++j)
				{
					std::vector<Vertex> positive,negative;
					SplitDepthPolygon(pieces[j],transform,plane,positive,negative);
					if(positive.size()>=3)next.push_back(positive);
					if(negative.size()>=3)next.push_back(negative);
				}
				pieces.swap(next);
			}
			for(size_t j=0;j<pieces.size();++j)
			{
				SetDepthRegion(&pieces[j][0],pieces[j].size(),transform);
				for(size_t k=1;k+1<pieces[j].size();++k)
				{result.push_back(pieces[j][0]);result.push_back(pieces[j][k]);result.push_back(pieces[j][k+1]);}
			}
		}
	}
	s.batch.swap(result);
	return true;
}
} }
static unsigned rng=0x1d37a593;
static float randomValue() {rng=rng*1664525u+1013904223u;return (int((rng>>8)%8193)-4096)/1024.f;}
int main(int argc,char **) {
 using namespace gles;
 Program locations;std::memset(&locations,0xff,sizeof locations);s.programs[0]=locations;
 size_t warmAllocations=0,referenceAllocations=0,totalVertices=0,splitCases=0,failures=0;
 bool negative=argc>1;
 for(unsigned test=0;test<512;++test) {
  if(test%128==0){s=State();s.programs[0]=locations;}
  s.depthClamp=test%17!=0;s.hardwareDepthClamp=test%19==0;
  s.batchMode=test%13==0?GL_POINTS:test%3==0?GL_LINES:GL_TRIANGLES;
  s.projection.Identity();s.view.Identity();s.texture[7].Identity();
  s.programs[0].model=test%5==0?0:-1;s.programs[0].precomputedSphere=test%7==0?0:-1;
  s.texture[7].v[12]=.125f;s.texture[7].v[14]=.25f;
  s.view.v[14]=test%2?-.125f:.25f;
  if(test%4!=0) {
   s.projection.v[10]=-3;s.projection.v[11]=-1;s.projection.v[14]=-4;s.projection.v[15]=0;
  }
  std::vector<Vertex> input(3*(1+test%32));
  for(size_t i=0;i<input.size();++i) {
   Vertex &v=input[i];float *values=reinterpret_cast<float*>(&v);
   for(size_t k=0;k<sizeof v/sizeof(float);++k) values[k]=randomValue();
   v.normal[2]+=1;
   if(test%11==0)v.position[2]=float(int(i%3)-1);
   if(test%23==0)v.position[2]=1;
  }
  s.batch.assign(input.begin(),input.end());allocations=0;tracking=true;
  bool expectedSplit=ReferenceSplitDepthBatch();tracking=false;referenceAllocations+=allocations;
  std::vector<Vertex> expected=s.batch;
  for(unsigned repeat=0;repeat<3;++repeat) {
   s.batch.assign(input.begin(),input.end());allocations=0;tracking=true;
   bool split=negative?ReferenceSplitDepthBatch():SplitDepthBatch();tracking=false;
   if(repeat==2)warmAllocations+=allocations;
   size_t n=s.batch.size();
   if(split!=expectedSplit||n!=expected.size()||(n&&std::memcmp(&s.batch[0],&expected[0],n*sizeof(Vertex))))++failures;
   totalVertices+=n;if(split)++splitCases;
  }
 }
 if(warmAllocations!=0||referenceAllocations<=10000)++failures;
 std::printf("Depth storage:1536 exact comparisons; vertices:%zu; split passes:%zu; reference allocations:%zu; warm allocations:%zu; failures:%zu\n",totalVertices,splitCases,referenceAllocations,warmAllocations,failures);
 return failures?1:0;
}
