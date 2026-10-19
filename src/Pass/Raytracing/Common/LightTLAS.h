#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

namespace Pass
{
	class LightTLAS : public RenderPass
	{
		eastl::unique_ptr<RaytracingData> m_RaytracingData;
		nvrhi::BufferHandle m_RaytracingBuffer;

		nvrhi::rt::AccelStructHandle m_TopLevelAS;

		eastl::vector<nvrhi::rt::InstanceDesc> m_InstanceDescs;

		uint32_t m_TopLevelInstances = 0;

		nvrhi::BindingLayoutHandle m_BindlessLayout;
		nvrhi::DescriptorTableHandle m_DescriptorTable;
	public:
		LightTLAS(Renderer* renderer);

		inline auto& GetBindlessLayout() const { return m_BindlessLayout; }
		inline auto& GetLightDescriptorTable() const { return m_DescriptorTable; }
		inline uint32_t GetTopLevelInstanceCount() const { return m_TopLevelInstances; }
		inline uint64_t GetTopLevelBufferSize() const { return m_TopLevelAS ? m_TopLevelAS->getBufferSize() : 0; }

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}