#pragma once

#include "Constants.h"
#include <nvrhi/nvrhi.h>

class Renderer;

struct RingBuffer
{
	eastl::array<nvrhi::BufferHandle, Constants::MAX_FRAMES_IN_FLIGHT> handles;

	RingBuffer() = default;

	RingBuffer(nvrhi::IDevice* device, const nvrhi::BufferDesc& descTemplate, std::string_view name)
	{
		auto desc = descTemplate;
		for (uint32_t s = 0; s < Constants::MAX_FRAMES_IN_FLIGHT; s++) {
			desc.debugName = std::format("{}[{}]", name, s).c_str();
			handles[s] = device->createBuffer(desc);
		}
	}

	nvrhi::IBuffer* operator[](uint32_t slot) const { return handles[slot]; }
	nvrhi::IBuffer* current() const;
};
