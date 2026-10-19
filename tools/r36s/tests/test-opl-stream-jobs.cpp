#include "oplsynth/opl_stream_jobs.h"
#include "oplsynth/opl_sample_offset.h"
#include "opl-offset-reference.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static unsigned Checks, Failures;
static void Check(bool value, const char *message)
{
	++Checks;
	if (!value) { ++Failures; std::fprintf(stderr, "FAIL %s\n", message); }
}

struct Backend : OPLStreamBackend
{
	struct Chip { unsigned phase = 0, state = 1; float left = 1, right = 1; } chips[2];
	ReferenceOPLOffset reference;
	bool oracle = false;
	std::thread::id main = std::this_thread::get_id();
	std::atomic<unsigned> operations{0}, workerOperations{0}, rhythmOnWorker{0};
	std::mutex mutex;
	std::condition_variable changed;
	bool block = false, entered = false;
	void Note() { ++operations; if (std::this_thread::get_id() != main) ++workerOperations; }
	void Reset(unsigned chip) { Note(); chips[chip] = Chip(); }
	void WriteReg(unsigned chip, int reg, int value)
	{
		Note();
		if ((reg & 255) == 0xbd && (value & 0x20) && std::this_thread::get_id() != main) ++rhythmOnWorker;
		chips[chip].state = chips[chip].state * 33 + unsigned(reg * 257 + value);
	}
	void SetPanning(unsigned chip, int channel, float left, float right)
	{
		Note(); chips[chip].left = left; chips[chip].right = right;
		chips[chip].state ^= unsigned(channel);
	}
	void Update(unsigned chip, float *pcm, int frames)
	{
		Note();
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (block)
			{
				entered = true; changed.notify_all();
				changed.wait(lock, [this] { return !block; });
			}
		}
		auto &c = chips[chip];
		for (int i = 0; i < frames; ++i)
		{
			float sample = float(std::sin(double(++c.phase) * .031) * (1 + (c.state & 63) * .04) + .75);
			pcm[i*2] += sample * c.left;
			pcm[i*2+1] += sample * c.right;
		}
	}
	void CorrectOffset(float *pcm, int samples, double &lastOffset)
	{
		Note();
		if (oracle) { reference.Correct(pcm, samples); lastOffset = reference.LastOffset; }
		else OPLCorrectSampleOffset(pcm, samples, lastOffset);
	}
	bool WaitEntered()
	{
		std::unique_lock<std::mutex> lock(mutex);
		return changed.wait_for(lock, std::chrono::seconds(3), [this] { return entered; });
	}
	void Release() { std::lock_guard<std::mutex> lock(mutex); block = false; changed.notify_all(); }
};

static bool Collect(OPLStreamJobs &jobs, unsigned slot, const void *&pcm, bool &valid)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	do
	{
		if (jobs.Collect(slot, pcm, valid)) return true;
		std::this_thread::yield();
	} while (std::chrono::steady_clock::now() < end);
	return false;
}

static void Equivalence()
{
	Backend candidate, reference;
	reference.oracle = true;
	double offset = 0, refOffset = 0;
	OPLStreamJobs jobs(candidate, offset);
	Check(jobs.Start(28408), "start worker");
	for (unsigned block = 0; block < 128; ++block)
	{
		unsigned slot = block % 4;
		float expected[7102] = {};
		float *pcm = static_cast<float *>(jobs.Prepare(slot));
		std::memset(pcm, 0, sizeof expected);
		if (block % 17 == 0)
		{
			jobs.Reset(0); reference.Reset(0);
			jobs.ResetOffset(); reference.reference.LastOffset = refOffset = 0;
		}
		for (unsigned chip = 0; chip < 2; ++chip)
		{
			float left = chip ? -.0f : .37123457f, right = chip ? .8192f : 1.0f;
			jobs.SetPanning(chip, int(block), left, right);
			reference.SetPanning(chip, int(block), left, right);
		}
		int at = 0;
		while (at < 3551)
		{
			int count = std::min(3551-at, 1 + int((block*73 + at*17) % 773));
			for (unsigned chip = 0; chip < 2; ++chip)
			{
				int reg = 0x20 + int((block + at) % 100);
				jobs.WriteReg(chip, reg, int(block + at)); reference.WriteReg(chip, reg, int(block + at));
				jobs.Update(chip, pcm+at*2, count); reference.Update(chip, expected+at*2, count);
			}
			jobs.RecordCorrection(pcm+at*2, count*2); reference.CorrectOffset(expected+at*2, count*2, refOffset);
			at += count;
		}
		bool expectedValid = block % 19 != 0;
		jobs.Submit(slot, expectedValid);
		const void *result = NULL; bool valid = !expectedValid;
		Check(Collect(jobs, slot, result, valid), "completion published");
		Check(valid == expectedValid, "false callback preserved");
		Check(result && std::memcmp(result, expected, sizeof expected) == 0, "exact PCM and ordered DC correction");
	}
	jobs.Stop();
	Check(std::memcmp(&offset, &refOffset, sizeof offset) == 0, "DC state returns to main on stop");
	Check(candidate.operations == reference.operations, "no repeated or omitted operation");
	Check(candidate.workerOperations > 0, "synthesis uses worker");
	Check(jobs.Start(28408), "restart joins previous generation");
	jobs.Stop(); jobs.Stop();
	Check(!jobs.Start(4), "unsupported block size stays synchronous");
}

static void QueueAndStop()
{
	Backend backend;
	double offset = 0;
	OPLStreamJobs jobs(backend, offset);
	Check(jobs.Start(28408), "queue worker start");
	backend.block = true;
	for (unsigned slot = 0; slot < 4; ++slot)
	{
		float *pcm = static_cast<float *>(jobs.Prepare(slot));
		std::memset(pcm, 0, 28408);
		jobs.WriteReg(0, 0x20, int(slot));
		jobs.Update(0, pcm, 3551);
		jobs.Submit(slot, true);
		if (slot == 0) Check(backend.WaitEntered(), "worker rendering barrier");
	}
	const void *pcm = NULL; bool valid = false;
	Check(!jobs.Collect(0, pcm, valid), "poll never exposes unfinished PCM");
	Check(backend.operations == 2, "main submits four slots without rendering or waiting");
	std::thread release([&backend] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); backend.Release(); });
	jobs.Stop(); release.join();
	Check(backend.operations == 8, "stop drains every issued operation exactly once");
	Check(backend.workerOperations == 8, "worker remains exclusive during stop");
	Check(!jobs.Collect(0, pcm, valid), "stop invalidates old completions");
	Check(jobs.Start(28408), "new generation after pending stop");
	jobs.Prepare(0);
	jobs.WriteReg(0, 0x40, 7);
	jobs.Stop();
	Check(backend.operations == 8, "cleanup discards unpublished preparation");
}

static void RhythmFallback()
{
	Backend backend;
	double offset = 0;
	OPLStreamJobs jobs(backend, offset);
	Check(jobs.Start(28408), "rhythm guard start");
	float *pcm = static_cast<float *>(jobs.Prepare(0));
	std::memset(pcm, 0, 28408);
	for (int i = 0; i < 10000; ++i) jobs.WriteReg(0, 0x40, i & 255);
	jobs.WriteReg(0, 0xbd, 0x20);
	jobs.Update(0, pcm, 3551);
	jobs.Submit(0, true);
	const void *result = NULL; bool valid = false;
	Check(jobs.Collect(0, result, valid) && valid, "unsupported trace completes synchronously");
	Check(backend.operations == 10002, "unbounded command vector preserves long trace");
	Check(backend.workerOperations == 0 && backend.rhythmOnWorker == 0, "rhythm never accesses RNG on worker");
	jobs.Stop();
	Check(!jobs.Start(28408), "unsupported instance remains synchronous");
}

static void FailedStartup()
{
	Backend backend;
	double offset = 0;
	OPLStreamJobs jobs(backend, offset);
	Check(!jobs.Start(28408), "pthread failure falls back without throwing");
	float pcm[7102] = {};
	jobs.WriteReg(0, 0x20, 1);
	jobs.Update(0, pcm, 3551);
	Check(!jobs.RecordCorrection(pcm, 7102), "failed worker leaves main correction path");
	OPLCorrectSampleOffset(pcm, 7102, offset);
	Check(backend.operations == 2 && backend.workerOperations == 0, "failed startup retains synchronous chips");
	jobs.Stop();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !std::strcmp(argv[1], "--failed-start"))
	{
		FailedStartup();
		std::printf("%u checks, %u failures; failed pthread startup\n", Checks, Failures);
		return Failures ? 1 : 0;
	}
	Equivalence(); QueueAndStop(); RhythmFallback();
	std::printf("%u checks, %u failures\n", Checks, Failures);
	return Failures ? 1 : 0;
}
