#include "Core/DynamicMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DynamicMesh::DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList) :
	SkinnedMesh(), m_BSDynamicTriShape(bsDynamicTriShape)
{
	m_Name = MakeDebugName(bsDynamicTriShape);

	auto device = Renderer::GetSingleton()->GetDevice();

	auto& runtimeData = bsDynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (runtimeData.dataSize == 0) {
		logger::warn("DynamicMesh::DynamicMesh - No dynamic data for {}", m_Name);
		return;
	}

	// Dynamic positions are float4 per vertex; the BLAS reads them as RGB32_FLOAT with a float4
	// stride so the trailing w component is skipped.
	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(runtimeData.dataSize)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - Dynamic", m_Name).c_str());

	m_DynamicBuffer = device->createBuffer(bufferDesc);

	m_DynamicData.resize(runtimeData.dataSize / sizeof(float4));

	// Upload the initial positions before building the BLAS.
	Update(commandList);

	BuildSkinned(bsDynamicTriShape, commandList, m_DynamicBuffer, static_cast<uint16_t>(sizeof(float4)), false);
}

void DynamicMesh::Update([[maybe_unused]] nvrhi::ICommandList* commandList)
{
	auto& runtimeData = m_BSDynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (!runtimeData.dynamicData)
		return;

	runtimeData.lock.Lock();

	// Has dynamic position changed?
	if (std::memcmp(m_DynamicData.data(), runtimeData.dynamicData, runtimeData.dataSize) == 0) {
		runtimeData.lock.Unlock();
		return;
	}

	commandList->writeBuffer(m_DynamicBuffer, runtimeData.dynamicData, runtimeData.dataSize);
	runtimeData.lock.Unlock();
}
