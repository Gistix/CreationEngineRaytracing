#pragma once

struct DynamicTrishapeRuntimeData
{
	void*          dynamicData;
	std::uint32_t  dataSize;
	RE::BSSpinLock* lock;
};
