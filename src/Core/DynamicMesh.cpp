#include "Core/DynamicMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DynamicMesh::DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList) :
	SkinnedMesh()
{
	m_Name = MakeDebugName(bsDynamicTriShape);
	m_BSTriShape = bsDynamicTriShape;

	auto device = Renderer::GetSingleton()->GetDevice();

	auto& runtimeData = bsDynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (runtimeData.dataSize == 0) {
		logger::warn("DynamicMesh::DynamicMesh - No dynamic data for {}", m_Name);
		return;
	}

	// Allocate space for dynamic data
	m_DynamicData.resize(runtimeData.dataSize, 0u);

	// Dynamic positions are float4 per vertex; the BLAS reads them as RGB32_FLOAT with a float4
	// stride so the trailing w component is skipped.
	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(runtimeData.dataSize)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - Dynamic", m_Name).c_str());

	m_DynamicBuffer = device->createBuffer(bufferDesc);

	BuildSkinned(bsDynamicTriShape, m_DynamicBuffer, static_cast<uint16_t>(sizeof(float4)), false);
}

bool DynamicMesh::UpdateData()
{
	auto& runtimeData = m_BSTriShape->AsDynamicTriShape()->GetDynamicTrishapeRuntimeData();

	if (!runtimeData.dynamicData)
		return false;

	if (runtimeData.dataSize == 0)
		return false;

	if (runtimeData.dataSize != m_DynamicData.size())
		return false;

	runtimeData.lock.Lock();
	if (std::memcmp(m_DynamicData.data(), runtimeData.dynamicData, runtimeData.dataSize) == 0) {
		runtimeData.lock.Unlock();
		return false;
	}

	std::memcpy(m_DynamicData.data(), runtimeData.dynamicData, runtimeData.dataSize);
	runtimeData.lock.Unlock();

	m_NeedsUpload = true;

	// Flag a vertex update so the cluster uploads the initial data before its first BLAS build.
	MarkDirty(DirtyFlags::Vertex);

	return true;
}

void DynamicMesh::UploadBuffers(nvrhi::ICommandList* commandList)
{
	if (!m_NeedsUpload || !m_DynamicBuffer)
		return;

	commandList->writeBuffer(m_DynamicBuffer, m_DynamicData.data(), m_DynamicData.size());

	m_NeedsUpload = false;
}
