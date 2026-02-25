#pragma once

#include <PCH.h>

#include "RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

namespace Pass
{
	class RaytracingCommon : public RenderPass
	{
		eastl::unique_ptr<RaytracingData> m_RaytracingData;
		nvrhi::BufferHandle m_RaytracingBuffer;

		nvrhi::rt::AccelStructHandle m_TopLevelAS;

		eastl::vector<nvrhi::rt::InstanceDesc> m_InstanceDescs;

		uint32_t m_TopLevelInstances = 0;
	public:
		RaytracingCommon(Renderer* renderer);

		void UpdateAccelStructs(nvrhi::ICommandList* commandList);

		nvrhi::IBuffer* GetRaytracingBuffer();
		nvrhi::rt::IAccelStruct* GetTopLevelAS();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}