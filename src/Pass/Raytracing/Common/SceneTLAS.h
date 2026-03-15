#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"
#include "Types/TopLevelAS.h"

namespace Pass
{
	class SceneTLAS : public RenderPass
	{
		eastl::unique_ptr<RaytracingData> m_RaytracingData;
		nvrhi::BufferHandle m_RaytracingBuffer;

		TopLevelAS m_TopLevelAS;
	public:
		SceneTLAS(Renderer* renderer);

		nvrhi::IBuffer* GetRaytracingBuffer();
		TopLevelAS& GetTopLevelAS();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}