#ifndef ZANDRONUM_OPL_STREAM_JOBS_H
#define ZANDRONUM_OPL_STREAM_JOBS_H

#include "../sound/prepared_stream.h"
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// Only actual melodic DBOPL chips and DC correction may enter this interface.
// Implementations cannot access MIDI parsers, CVars, OpenAL or gameplay RNG.
class OPLStreamBackend
{
public:
	virtual ~OPLStreamBackend() {}
	virtual void Reset(unsigned chip) = 0;
	virtual void WriteReg(unsigned chip, int reg, int value) = 0;
	virtual void SetPanning(unsigned chip, int channel, float left, float right) = 0;
	virtual void Update(unsigned chip, float *pcm, int frames) = 0;
	virtual void CorrectOffset(float *pcm, int samples, double &lastOffset) = 0;
};

class OPLStreamJobs : public PreparedSoundSource
{
public:
	OPLStreamJobs(OPLStreamBackend &backend, double &mainOffset);
	~OPLStreamJobs();
	bool Start(unsigned bytes);
	void Stop();
	bool UsesWorker() const { return Worker.joinable(); }
	void *Prepare(unsigned slot);
	void Submit(unsigned slot, bool valid);
	bool Collect(unsigned slot, const void *&pcm, bool &valid);

	void Reset(unsigned chip);
	void WriteReg(unsigned chip, int reg, int value);
	void SetPanning(unsigned chip, int channel, float left, float right);
	void Update(unsigned chip, float *pcm, int frames);
	bool RecordCorrection(float *pcm, int samples);
	void ResetOffset();
	bool IsPreparing() const { return Preparing >= 0; }

private:
	enum Operation { ResetChip, Register, Pan, Samples, Correction, ClearOffset };
	struct Command
	{
		Operation op;
		unsigned chip, offset;
		int a, b;
		float left, right;
	};
	enum SlotState { Free, PreparingSlot, Queued, Rendering, Ready };
	struct Slot
	{
		std::vector<float> pcm;
		std::vector<Command> commands;
		SlotState state;
		bool valid, melodic;
		Slot() : state(Free), valid(false), melodic(true) {}
	};
	OPLStreamBackend &Backend;
	double &MainOffset;
	double RenderOffset;
	std::array<Slot, 4> Slots;
	std::array<unsigned, 4> Queue;
	unsigned Head, Count;
	int Preparing;
	bool Working, Quit, Started, Melodic;
	std::mutex Mutex;
	std::condition_variable Changed;
	std::thread Worker;

	void Run();
	void Drain();
	void Join();
	void Append(const Command &command);
	void Execute(const Command &command, float *pcm);
	void Execute(Slot &slot);
	unsigned Offset(float *pcm, int samples) const;
	OPLStreamJobs(const OPLStreamJobs &) = delete;
	OPLStreamJobs &operator=(const OPLStreamJobs &) = delete;
};

#endif
