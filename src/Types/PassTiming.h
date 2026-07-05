#pragma once

struct PassTiming
{
	eastl::string name;
	float gpuTiming;  // milliseconds on GPU (timer query)
	float cpuTiming;  // milliseconds on CPU (wall clock)
};