#include "opl_stream_source.h"
#include "opl_sample_offset.h"
#include "opl.h"
#include "muslib.h"
#include <cassert>

namespace
{
class RecordingChip : public OPLEmul
{
	OPLStreamJobs &Jobs;
	unsigned Chip;
public:
	RecordingChip(OPLStreamJobs &jobs, unsigned chip) : Jobs(jobs), Chip(chip) {}
	void Reset() { Jobs.Reset(Chip); }
	void WriteReg(int reg, int value) { Jobs.WriteReg(Chip, reg, value); }
	void Update(float *pcm, int frames) { Jobs.Update(Chip, pcm, frames); }
	void SetPanning(int channel, float left, float right) { Jobs.SetPanning(Chip, channel, left, right); }
};
}

OPLPreparedSource::OPLPreparedSource(OPLio &io, double &mainOffset)
	: IO(io), Jobs(*this, mainOffset)
{
	assert(io.SupportsPreparedPCM());
	Chips.reserve(io.NumChips);
	Proxies.reserve(io.NumChips);
	for (unsigned i = 0; i < io.NumChips; ++i)
	{
		assert(io.chips[i] != NULL);
		Chips.push_back(io.chips[i]);
		Proxies.emplace_back(new RecordingChip(Jobs, i));
	}
	// Publish proxies only after every allocation succeeds. Original io remains
	// sole owner of actual chips, restored before deinit/destruction.
	for (unsigned i = 0; i < io.NumChips; ++i) io.chips[i] = Proxies[i].get();
}

OPLPreparedSource::~OPLPreparedSource()
{
	Jobs.Stop();
	for (unsigned i = 0; i < Chips.size(); ++i)
	{
		assert(IO.chips[i] == Proxies[i].get());
		IO.chips[i] = Chips[i];
	}
}

void OPLPreparedSource::Reset(unsigned chip) { Chips[chip]->Reset(); }
void OPLPreparedSource::WriteReg(unsigned chip, int reg, int value) { Chips[chip]->WriteReg(reg, value); }
void OPLPreparedSource::SetPanning(unsigned chip, int channel, float left, float right) { Chips[chip]->SetPanning(channel, left, right); }
void OPLPreparedSource::Update(unsigned chip, float *pcm, int frames) { Chips[chip]->Update(pcm, frames); }
void OPLPreparedSource::CorrectOffset(float *pcm, int samples, double &lastOffset)
{
	OPLCorrectSampleOffset(pcm, samples, lastOffset);
}
