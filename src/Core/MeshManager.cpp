#include "Core/MeshManager.h"

#include "Renderer.h"

MeshManager::MeshManager()
	: 	m_MeshSlots(sizeof(uint32_t), Constants::NUM_MESHES_MAX, 0),
	m_GeometrySlots(sizeof(MeshData), Constants::NUM_MESHES_MAX, 0),
	m_TransformSlots(sizeof(float3x4), Constants::NUM_MESHES_MAX),
	m_PrevTransformSlots(sizeof(float3x4), Constants::NUM_MESHES_MAX),
	m_PropertiesSlots(sizeof(PropertiesData), Constants::NUM_MESHES_MAX, 0),
	m_InstanceTransformSlots(sizeof(TransformData), Constants::NUM_INSTANCE_TRANSFORMS_MAX)
{
	CreateBuffers();
}

void MeshManager::CreateBuffers()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto inputDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(float3x4))
		.setStructStride(sizeof(float3x4))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false);

	m_CurrentBuffer = device->createBuffer(inputDesc.setDebugName("Current Transform Buffer"));
	m_PrevBuffer = device->createBuffer(inputDesc.setDebugName("Prev Transform Buffer"));

	auto outDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(true)
		.setIsAccelStructBuildInput(Renderer::GetSingleton()->IsVulkan())
		.setDebugName("Transform Buffer");

	m_Buffer = device->createBuffer(outDesc);

	auto meshDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(MeshData))
		.setStructStride(sizeof(MeshData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Mesh Buffer");

	m_MeshBuffer = device->createBuffer(meshDesc);

	auto propsDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(PropertiesData))
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Properties Buffer");

	m_PropertiesBuffer = device->createBuffer(propsDesc);

	auto instDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_INSTANCE_TRANSFORMS_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false)
		.setIsAccelStructBuildInput(Renderer::GetSingleton()->IsVulkan())
		.setDebugName("Instance Transform Buffer");

	m_InstanceTransformBuffer = device->createBuffer(instDesc);
}

uint32_t MeshManager::AllocateMeshIndex()
{
	uint64_t offset = m_MeshSlots.Allocate();
	if (offset == ResourceSlotManager::INVALID_OFFSET)
		return UINT32_MAX;
	return m_MeshSlots.GetIndexFromOffset(offset);
}

void MeshManager::ReleaseMeshIndex(uint32_t index)
{
	m_MeshSlots.Release(index * sizeof(uint32_t));
}

uint32_t MeshManager::AllocateGeometryIndex()
{
	uint64_t offset = m_GeometrySlots.Allocate();
	if (offset == ResourceSlotManager::INVALID_OFFSET)
		return UINT32_MAX;
	return m_GeometrySlots.GetIndexFromOffset(offset);
}

void MeshManager::ReleaseGeometryIndex(uint32_t index)
{
	m_GeometrySlots.Release(index * sizeof(MeshData));
}

void MeshManager::WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	m_TransformSlots.Write(index * sizeof(float3x4), &transform, sizeof(float3x4));
	m_PrevTransformSlots.Write(index * sizeof(float3x4), &prevTransform, sizeof(float3x4));
}

void MeshManager::WriteMeshData(uint32_t index, const MeshData& meshData)
{
	m_GeometrySlots.Write(index * sizeof(MeshData), &meshData, sizeof(MeshData));
}

void MeshManager::WritePropertiesData(uint32_t index, const PropertiesData& data)
{
	m_PropertiesSlots.Write(index * sizeof(PropertiesData), &data, sizeof(PropertiesData));
}

uint32_t MeshManager::AllocateInstanceTransforms(uint32_t count)
{
	if (count == 0)
		return UINT32_MAX;

	std::scoped_lock lock(m_InstanceTransformMutex);

	for (auto it = m_FreeInstanceRanges.begin(); it != m_FreeInstanceRanges.end(); ++it) {
		if (it->count >= count) {
			const uint32_t base = it->base;
			if (it->count == count) {
				m_FreeInstanceRanges.erase(it);
			}
			else {
				it->base += count;
				it->count -= count;
			}
			return base;
		}
	}

	if (m_InstanceTransformNext + count > Constants::NUM_INSTANCE_TRANSFORMS_MAX)
		return UINT32_MAX;

	const uint32_t base = m_InstanceTransformNext;
	m_InstanceTransformNext += count;
	return base;
}

void MeshManager::ReleaseInstanceTransforms(uint32_t base, uint32_t count)
{
	if (count == 0 || base == UINT32_MAX)
		return;

	std::scoped_lock lock(m_InstanceTransformMutex);

	// Insert sorted by base and coalesce adjacent ranges.
	InstanceRange merged{ base, count };
	auto it = m_FreeInstanceRanges.begin();
	while (it != m_FreeInstanceRanges.end() && it->base < merged.base)
		++it;

	it = m_FreeInstanceRanges.insert(it, merged);

	if (it != m_FreeInstanceRanges.begin()) {
		auto prev = it - 1;
		if (prev->base + prev->count == it->base) {
			prev->count += it->count;
			it = m_FreeInstanceRanges.erase(it);
			it = prev;
		}
	}

	auto next = it + 1;
	if (next != m_FreeInstanceRanges.end() && it->base + it->count == next->base) {
		it->count += next->count;
		m_FreeInstanceRanges.erase(next);
	}
}

void MeshManager::WriteInstanceTransform(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	TransformData data{ transform, prevTransform };
	m_InstanceTransformSlots.Write(index * sizeof(TransformData), &data, sizeof(TransformData));
}

void MeshManager::Flush(nvrhi::ICommandList* commandList)
{
	// Upload dirty mesh data
	{
		auto dirtyRanges = m_GeometrySlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_GeometrySlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_MeshBuffer, mirror + offset, size, offset);
	}

	// Upload dirty properties
	{
		auto dirtyRanges = m_PropertiesSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_PropertiesSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_PropertiesBuffer, mirror + offset, size, offset);
	}

	// Upload dirty transforms
	{
		auto dirtyRanges = m_TransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_TransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_CurrentBuffer, mirror + offset, size, offset);
	}

	{
		auto dirtyRanges = m_PrevTransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_PrevTransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_PrevBuffer, mirror + offset, size, offset);
	}

	{
		auto dirtyRanges = m_InstanceTransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_InstanceTransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_InstanceTransformBuffer, mirror + offset, size, offset);
	}
}
