// DC correction extracted unchanged from opl_mus_player.cpp.
#include "opl_sample_offset.h"
#include <algorithm>
#include <cmath>

void OPLCorrectSampleOffset(float *buff, int count, double &LastOffset)
{
	// Three out of four of the OPL waveforms are non-negative. Depending on
	// timbre selection, this can cause the output waveform to tend toward
	// very large positive values. Heretic's music is particularly bad for
	// this. This function attempts to compensate by offseting the sample
	// data back to around the [-1.0, 1.0] range.

	double max = -1e10, min = 1e10, offset, step;
	int i, ramp, largest_at = 0;

	// Find max and min values for this segment of the waveform.
	for (i = 0; i < count; ++i)
	{
		if (buff[i] > max)
		{
			max = buff[i];
			largest_at = i;
		}
		if (buff[i] < min)
		{
			min = buff[i];
			largest_at = i;
		}
	}
	// Prefer to keep the offset at 0, even if it means a little clipping.
	if (LastOffset == 0 && min >= -1.1 && max <= 1.1)
	{
		offset = 0;
	}
	else
	{
		offset = (max + min) / 2;
		// If the new offset is close to 0, make it 0 to avoid making another
		// full loop through the sample data.
		if (fabs(offset) < 1/256.0)
		{
			offset = 0;
		}
	}
	// Ramp the offset change so there aren't any abrupt clicks in the output.
	// If the ramp is too short, it can sound scratchy. cblood2.mid is
	// particularly unforgiving of short ramps.
	if (count >= 512)
	{
		ramp = 512;
		step = (offset - LastOffset) / 512;
	}
	else
	{
		ramp = std::min(count, std::max(196, largest_at));
		step = (offset - LastOffset) / ramp;
	}
	offset = LastOffset;
	i = 0;
	if (step != 0)
	{
		for (; i < ramp; ++i)
		{
			buff[i] = float(buff[i] - offset);
			offset += step;
		}
	}
	if (offset != 0)
	{
		for (; i < count; ++i)
		{
			buff[i] = float(buff[i] - offset);
		}
	}
	LastOffset = float(offset);
}
