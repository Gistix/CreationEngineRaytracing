#pragma once

struct ReleasedData
{
	uint64_t frameIndex;
	eastl::unique_ptr<Model> model;
};