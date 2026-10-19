#include "opl_stream_jobs.h"
#include <cassert>
#include <cstdint>
#include <new>
#include <system_error>

OPLStreamJobs::OPLStreamJobs(OPLStreamBackend &backend, double &mainOffset)
	: Backend(backend), MainOffset(mainOffset), RenderOffset(mainOffset), Head(0),
	  Count(0), Preparing(-1), Working(false), Quit(false), Started(false), Melodic(true)
{
}

OPLStreamJobs::~OPLStreamJobs()
{
	Stop();
}

bool OPLStreamJobs::Start(unsigned bytes)
{
	Stop();
	if (!Melodic || bytes != 28408) return false;
	try
	{
		for (auto &slot : Slots)
		{
			slot.pcm.resize(bytes / sizeof(float));
			slot.commands.reserve(2048);
		}
		RenderOffset = MainOffset;
		Quit = false;
		Worker = std::thread(&OPLStreamJobs::Run, this);
		Started = true;
	}
	catch (const std::system_error &)
	{
		return false;
	}
	catch (const std::bad_alloc &)
	{
		return false;
	}
	return true;
}

void OPLStreamJobs::Drain()
{
	std::unique_lock<std::mutex> lock(Mutex);
	Changed.wait(lock, [this] { return Count == 0 && !Working; });
}

void OPLStreamJobs::Join()
{
	if (!Worker.joinable()) return;
	Drain();
	{
		std::lock_guard<std::mutex> lock(Mutex);
		Quit = true;
	}
	Changed.notify_all();
	Worker.join();
}

void OPLStreamJobs::Stop()
{
	// A callback can unwind into engine error cleanup while preparing a job.
	// Its unpublished commands cannot be observed by worker.
	if (Preparing >= 0)
	{
		Slots[Preparing].state = Free;
		Preparing = -1;
	}
	Join();
	if (Started) MainOffset = RenderOffset;
	Started = false;
	Head = Count = 0;
	for (auto &slot : Slots) slot.state = Free;
}

void *OPLStreamJobs::Prepare(unsigned index)
{
	assert(Started && Preparing < 0 && index < Slots.size());
	std::lock_guard<std::mutex> lock(Mutex);
	Slot &slot = Slots[index];
	assert(slot.state == Free);
	slot.commands.clear();
	slot.melodic = true;
	slot.state = PreparingSlot;
	Preparing = int(index);
	return slot.pcm.data();
}

void OPLStreamJobs::Submit(unsigned index, bool valid)
{
	assert(Preparing == int(index));
	Slot &slot = Slots[index];
	slot.valid = valid;
	Preparing = -1;
	if (!slot.melodic)
	{
		// Unsupported rhythm can reach global RNG. Finish prior jobs and take
		// exclusive chip ownership on main before executing this command.
		Melodic = false;
		Join();
	}
	if (!Worker.joinable())
	{
		Execute(slot);
		slot.state = Ready;
		return;
	}
	{
		std::lock_guard<std::mutex> lock(Mutex);
		assert(Count < Queue.size());
		slot.state = Queued;
		Queue[(Head + Count++) % Queue.size()] = index;
	}
	Changed.notify_all();
}

bool OPLStreamJobs::Collect(unsigned index, const void *&pcm, bool &valid)
{
	assert(index < Slots.size());
	std::lock_guard<std::mutex> lock(Mutex);
	Slot &slot = Slots[index];
	if (slot.state != Ready) return false;
	pcm = slot.pcm.data();
	valid = slot.valid;
	slot.state = Free;
	return true;
}

void OPLStreamJobs::Run()
{
	std::unique_lock<std::mutex> lock(Mutex);
	for (;;)
	{
		Changed.wait(lock, [this] { return Quit || Count != 0; });
		if (Quit) return;
		unsigned index = Queue[Head];
		Head = (Head + 1) % Queue.size();
		--Count;
		Working = true;
		Slots[index].state = Rendering;
		lock.unlock();
		Execute(Slots[index]);
		lock.lock();
		Slots[index].state = Ready;
		Working = false;
		Changed.notify_all();
	}
}

void OPLStreamJobs::Append(const Command &command)
{
	if (Preparing >= 0)
	{
		Slot &slot = Slots[Preparing];
		slot.commands.push_back(command);
		if (command.op == Register && (command.a & 255) == 0xbd && (command.b & 0x20))
			slot.melodic = false;
		return;
	}
	Drain();
	if (command.op == Register && (command.a & 255) == 0xbd && (command.b & 0x20))
	{
		Melodic = false;
		Join();
	}
	Execute(command, NULL);
}

unsigned OPLStreamJobs::Offset(float *pcm, int samples) const
{
	assert(Preparing >= 0 && samples >= 0);
	const auto &buffer = Slots[Preparing].pcm;
	uintptr_t first = reinterpret_cast<uintptr_t>(buffer.data());
	uintptr_t address = reinterpret_cast<uintptr_t>(pcm);
	assert(address >= first && (address - first) % sizeof(float) == 0);
	unsigned offset = unsigned((address - first) / sizeof(float));
	assert(offset <= buffer.size() && unsigned(samples) <= buffer.size() - offset);
	return offset;
}

void OPLStreamJobs::Reset(unsigned chip)
{
	Append({ResetChip, chip, 0, 0, 0, 0, 0});
}

void OPLStreamJobs::WriteReg(unsigned chip, int reg, int value)
{
	Append({Register, chip, 0, reg, value, 0, 0});
}

void OPLStreamJobs::SetPanning(unsigned chip, int channel, float left, float right)
{
	Append({Pan, chip, 0, channel, 0, left, right});
}

void OPLStreamJobs::Update(unsigned chip, float *pcm, int frames)
{
	if (Preparing < 0)
	{
		Drain();
		Backend.Update(chip, pcm, frames);
		return;
	}
	Append({Samples, chip, Offset(pcm, frames * 2), frames, 0, 0, 0});
}

bool OPLStreamJobs::RecordCorrection(float *pcm, int samples)
{
	if (Preparing < 0)
	{
		if (!Started) return false;
		Drain();
		Backend.CorrectOffset(pcm, samples, RenderOffset);
		MainOffset = RenderOffset;
		return true;
	}
	Append({Correction, 0, Offset(pcm, samples), samples, 0, 0, 0});
	return true;
}

void OPLStreamJobs::ResetOffset()
{
	Append({ClearOffset, 0, 0, 0, 0, 0, 0});
}

void OPLStreamJobs::Execute(const Command &command, float *pcm)
{
	switch (command.op)
	{
	case ResetChip: Backend.Reset(command.chip); break;
	case Register: Backend.WriteReg(command.chip, command.a, command.b); break;
	case Pan: Backend.SetPanning(command.chip, command.a, command.left, command.right); break;
	case Samples: Backend.Update(command.chip, pcm + command.offset, command.a); break;
	case Correction: Backend.CorrectOffset(pcm + command.offset, command.a, RenderOffset); break;
	case ClearOffset: RenderOffset = 0; break;
	}
}

void OPLStreamJobs::Execute(Slot &slot)
{
	for (const auto &command : slot.commands) Execute(command, slot.pcm.data());
}
