#include "critsec.h"
#include "muslib.h"

#ifdef USE_GLES2
class OPLPreparedSource;
class PreparedSoundSource;
#endif

class OPLmusicBlock : public musicBlock
{
public:
	OPLmusicBlock();
	virtual ~OPLmusicBlock();

	bool ServiceStream(void *buff, int numbytes);
	void ResetChips();

	virtual void Restart();

protected:
	virtual int PlayTick() = 0;
	void OffsetSamples(float *buff, int count);

	double NextTickIn;
	double SamplesPerTick;
	int NumChips;
	bool Looping;
	double LastOffset;
	bool FullPan;

#ifdef USE_GLES2
	OPLPreparedSource *PreparedPCM;
	PreparedSoundSource *CreatePreparedPCM();
	void DestroyPreparedPCM();
	bool RecordPreparedCorrection(float *pcm, int samples);
#endif

	FCriticalSection ChipAccess;
};

class OPLmusicFile : public OPLmusicBlock
{
public:
	OPLmusicFile(FILE *file, BYTE *musiccache, int len);
	OPLmusicFile(const OPLmusicFile *source, const char *filename);
	virtual ~OPLmusicFile();

	bool IsValid() const;
	void SetLooping(bool loop);
	void Restart();
	void Dump();

protected:
	OPLmusicFile() {}
	int PlayTick();

	enum { RDosPlay, IMF, DosBox1, DosBox2 } RawPlayer;
	int ScoreLen;
	int WhichChip;
};
