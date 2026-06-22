#include "Core/DynamicMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DynamicMesh::DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList) :
	m_BSDynamicTriShape(bsDynamicTriShape), SkinnedMesh(bsDynamicTriShape, commandList)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto& runtimeData = bsDynamicTriShape->GetDynamicTrishapeRuntimeData();

	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(runtimeData.dataSize)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName(std::format("{} - Dynamic", m_Name).c_str());

	m_DynamicBuffer = device->createBuffer(bufferDesc);

	// Initial upload to vertex buffer
	Update(commandList);
}

void DynamicMesh::Update([[maybe_unused]] nvrhi::ICommandList* commandList)
{
	auto& runtimeData = m_BSDynamicTriShape->GetDynamicTrishapeRuntimeData();

	runtimeData.lock.Lock();
	commandList->writeBuffer(m_DynamicBuffer, runtimeData.dynamicData, runtimeData.dataSize);
	runtimeData.lock.Unlock();
}
