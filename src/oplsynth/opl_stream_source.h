#ifndef ZANDRONUM_OPL_STREAM_SOURCE_H
#define ZANDRONUM_OPL_STREAM_SOURCE_H

#include "opl_stream_jobs.h"
#include <memory>

class OPLEmul;
struct OPLio;

class OPLPreparedSource : public PreparedSoundSource, private OPLStreamBackend
{
public:
	OPLPreparedSource(OPLio &io, double &mainOffset);
	~OPLPreparedSource();
	bool Start(unsigned bytes) { return Jobs.Start(bytes); }
	void Stop() { Jobs.Stop(); }
	bool UsesWorker() const { return Jobs.UsesWorker(); }
	void *Prepare(unsigned slot) { return Jobs.Prepare(slot); }
	void Submit(unsigned slot, bool valid) { Jobs.Submit(slot, valid); }
	bool Collect(unsigned slot, const void *&pcm, bool &valid) { return Jobs.Collect(slot, pcm, valid); }
	bool RecordCorrection(float *pcm, int samples) { return Jobs.RecordCorrection(pcm, samples); }
	void ResetOffset() { Jobs.ResetOffset(); }

private:
	OPLio &IO;
	std::vector<OPLEmul *> Chips;
	std::vector<std::unique_ptr<OPLEmul>> Proxies;
	OPLStreamJobs Jobs;
	void Reset(unsigned chip);
	void WriteReg(unsigned chip, int reg, int value);
	void SetPanning(unsigned chip, int channel, float left, float right);
	void Update(unsigned chip, float *pcm, int frames);
	void CorrectOffset(float *pcm, int samples, double &lastOffset);
};

#endif
