#pragma once

#include <PCH.h>

struct IRenderPass
{
	virtual void Init() = 0;
	virtual void CreatePipeline() = 0;
	virtual void ResolutionChanged(uint2 resolution) = 0;
	virtual void Execute(nvrhi::ICommandList* commandList) = 0;
};